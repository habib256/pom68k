// POM68K M2 (docs/CACHE_040.md § M2) — 68040 cache tags + line data.
//
// MC68040UM § 4: each on-chip cache (instruction and data) is 4 KB,
// 4-way set-associative, 64 sets of 16-byte lines, physically indexed
// and physically tagged — set index = PA[9:4], tag = PA[31:10]. Data
// cache lines carry four dirty bits, one per longword (UM Fig. 4-4);
// instruction lines have none. Replacement uses a 2-bit counter shared
// by the whole cache, invalid ways first (UM § 4.1).
//
// The struct owns replacement, tags, data and dirty state. It deliberately
// owns neither a bus nor a clock: Moira supplies line-fill/writeback callbacks
// so bus errors, MMU restart state and machine timing remain in one place.

#pragma once

#include "MoiraTypes.h"

namespace moira {

struct Cache040 {

    static constexpr int SETS = 64;
    static constexpr int WAYS = 4;

    // CM field encoding (descriptor/TTR bits 6-5, M68040UM Table 3-3)
    static constexpr int CM_WRITETHROUGH = 0;
    static constexpr int CM_COPYBACK     = 1;
    static constexpr int CM_SERIAL_NC    = 2;
    static constexpr int CM_NC           = 3;

    struct Line {
        u32 tag = 0;            // PA[31:10]
        bool valid = false;
        u8 dirty = 0;           // bit n = longword n modified (D-cache)
        u8 data[16] = {};       // big-endian bytes, line base + [0..15]
    };

    struct Evicted {
        bool valid = false;
        u32 base = 0;
        u8 dirty = 0;
        u8 data[16] = {};
    };
    Line sets[SETS][WAYS];
    u8 rot = 0;                 // 2-bit replacement counter (UM § 4.1)

    // Gate observables — counters, not architecture
    u64 fills = 0, hits = 0, pushes = 0;

    static int setOf(u32 pa) { return int((pa >> 4) & 63); }
    static u32 tagOf(u32 pa) { return pa >> 10; }

    Line* lookup(u32 pa) {
        Line* s = sets[setOf(pa)];
        for (int w = 0; w < WAYS; w++)
            if (s[w].valid && s[w].tag == tagOf(pa)) return &s[w];
        return nullptr;
    }

    const Line* lookup(u32 pa) const {
        const Line* s = sets[setOf(pa)];
        for (int w = 0; w < WAYS; w++)
            if (s[w].valid && s[w].tag == tagOf(pa)) return &s[w];
        return nullptr;
    }

    // Installs a new line and returns the displaced contents to the caller,
    // which must push dirty longwords before filling `data`. A hit is returned
    // unchanged. The replacement counter is global, as on the 68040.
    Line* allocate(u32 pa, Evicted *evicted = nullptr) {
        if (Line* l = lookup(pa)) { hits++; return l; }

        const int set = setOf(pa);
        Line* s = sets[set];
        int w;
        for (w = 0; w < WAYS; w++) if (!s[w].valid) break;
        if (w == WAYS) { w = rot & 3; rot = u8((rot + 1) & 3); }

        if (evicted) {
            evicted->valid = s[w].valid;
            evicted->base = (s[w].tag << 10) | (u32(set) << 4);
            evicted->dirty = s[w].dirty;
            for (int i = 0; i < 16; i++) evicted->data[i] = s[w].data[i];
        }
        s[w].tag = tagOf(pa);
        s[w].valid = true;
        s[w].dirty = 0;
        for (u8 &b : s[w].data) b = 0;
        fills++;
        return &s[w];
    }

    // One CPU access over [pa, pa+bytes). Caller has already checked the
    // cache-enable bit; cm is the page/TTR cache mode. Both non-cacheable
    // modes bypass the cache entirely (UM § 4.2: the cache is not
    // searched). MOVE16 never allocates; its line WRITE invalidates a
    // matching line instead of updating it (UM § 4.2, MOVE16 note).
    void touch(u32 pa, bool write, int cm, int bytes, bool move16 = false) {

        if (cm >= CM_SERIAL_NC) return;

        if (move16) {
            if (write) invalidateLine(pa);
            return;
        }

        // Segment by line, then mark the longwords the span covers. The
        // walk runs in u64 with an EXCLUSIVE end: at the top of the
        // address space (pa+bytes-1 == 0xFFFFFFFF) a u32 `a <= hi` loop
        // is a tautology and never terminates (bughunt 2026-08-05).
        const u64 end = u64(pa) + u64(bytes);
        for (u64 la = pa & ~u32(15); la < end; la += 16) {
            u64 lo = la < pa ? pa : la;
            u64 hi = (la + 16 < end ? la + 16 : end) - 1;
            u8 lw = 0;
            for (u64 a = lo & ~u64(3); a <= hi; a += 4)
                lw |= u8(1 << ((a >> 2) & 3));
            touchLine(u32(la), write, cm, lw);
        }
    }

    // CINV scopes (UM § 4.5) — discard, dirty state included
    void invalidateLine(u32 pa) {
        if (Line* l = lookup(pa)) { l->valid = false; l->dirty = 0; }
    }
    void invalidatePage(u32 pa, u32 pageMask) {
        forEachInPage(pa, pageMask,
                      [](Line& l) { l.valid = false; l.dirty = 0; });
    }
    void invalidateAll() {
        for (auto& s : sets) for (auto& l : s) { l.valid = false; l.dirty = 0; }
    }

    // Tag-only CPUSH scope helpers retained for the struct-level policy
    // tests. The live CPU uses drain* below to write dirty bytes before
    // invalidating; clean lines are invalidated too (no 68060 DPI bit).
    int pushLine(u32 pa) {
        int n = 0;
        if (Line* l = lookup(pa)) {
            if (l->dirty) { pushes++; n = 1; }
            l->valid = false; l->dirty = 0;
        }
        return n;
    }
    int pushPage(u32 pa, u32 pageMask) {
        int n = 0;
        forEachInPage(pa, pageMask, [&](Line& l) {
            if (l.dirty) { pushes++; n++; }
            l.valid = false; l.dirty = 0;
        });
        return n;
    }
    int pushAll() {
        int n = 0;
        for (auto& s : sets) for (auto& l : s) {
            if (!l.valid) continue;
            if (l.dirty) { pushes++; n++; }
            l.valid = false; l.dirty = 0;
        }
        return n;
    }

    int validCount() const {
        int n = 0;
        for (auto& s : sets) for (auto& l : s) if (l.valid) n++;
        return n;
    }

    // Data-bearing CPUSH helpers. `sink(base,line)` writes the dirty
    // longwords to memory. The line is invalidated only after the callback
    // returns, so a bus error leaves recoverable cache state behind.
    template <class F> int drainLine(u32 pa, F sink) {
        int n = 0;
        if (Line* l = lookup(pa)) {
            if (l->dirty) { sink(pa & ~u32(15), *l); pushes++; n = 1; }
            l->valid = false; l->dirty = 0;
        }
        return n;
    }
    template <class F> int drainPage(u32 pa, u32 pageMask, F sink) {
        int n = 0;
        for (int set = 0; set < SETS; set++) for (Line &l : sets[set]) {
            const u32 base = (l.tag << 10) | (u32(set) << 4);
            if (!l.valid || (base & pageMask) != (pa & pageMask)) continue;
            if (l.dirty) { sink(base, l); pushes++; n++; }
            l.valid = false; l.dirty = 0;
        }
        return n;
    }
    template <class F> int drainAll(F sink) {
        int n = 0;
        for (int set = 0; set < SETS; set++) for (Line &l : sets[set]) {
            if (!l.valid) continue;
            const u32 base = (l.tag << 10) | (u32(set) << 4);
            if (l.dirty) { sink(base, l); pushes++; n++; }
            l.valid = false; l.dirty = 0;
        }
        return n;
    }

private:

    void touchLine(u32 pa, bool write, int cm, u8 lw) {

        if (Line* l = lookup(pa)) {
            hits++;
            if (write && cm == CM_COPYBACK) l->dirty |= lw;
            return;
        }

        // Miss: reads allocate in both cachable modes; a copyback write
        // miss line-fills then modifies; a writethrough write miss goes
        // to memory without allocating (no write-allocate, UM § 4.2)
        if (write && cm == CM_WRITETHROUGH) return;

        Line* l = allocate(pa);
        l->dirty = (write && cm == CM_COPYBACK) ? lw : u8(0);
    }

    template <class F> void forEachInPage(u32 pa, u32 pageMask, F f) {
        for (auto& s : sets) for (auto& l : s)
            if (l.valid && ((l.tag << 10) & pageMask) == (pa & pageMask))
                f(l);
    }
};

}
