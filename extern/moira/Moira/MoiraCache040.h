// [B] MC68040UM-1993 §4.1 and Figure 4-2: cache organization.
// POM68K M2 (docs/CACHE_040.md § M2) — 68040 cache tags + line data.
//
// MC68040UM-1993 § 4: each on-chip cache (instruction and data) is 4 KB,
// 4-way set-associative, 64 sets of 16-byte lines, physically indexed
// and physically tagged — set index = PA[9:4], tag = PA[31:10]. Data
// cache lines carry four dirty bits, one per longword (UM Figure 4-2);
// instruction lines have none. Replacement uses a 2-bit counter shared
// by the whole cache, invalid ways first (UM § 4.1).
//
// The struct owns replacement, tags, data and dirty state. It deliberately
// owns neither a bus nor a clock: Moira supplies line-fill/writeback callbacks
// so bus errors, MMU restart state and machine timing remain in one place.
// [B] MC68040UM-1993 §4.1: each cache's two-bit counter "is incremented for
// each access to the cache" — per half-line accessed in the instruction
// cache; in the data cache per half-line accessed during reads, per full
// line accessed during copyback writes and per bus transfer of a
// write-through write — and a miss with four valid lines replaces the line
// it points to. The callers count (count()); allocate() only reads rot.
// [F] Whether the missing access is counted before or after it selects its
// victim (here: after), and the fork's instruction-fetch granularity (one
// half-line access per fetched word). Settled by: a hardware replacement
// trace (TODO.md § Moteur › Justesse 68040).

#pragma once

#include "MoiraTypes.h"

namespace moira {

struct Cache040 {

    static constexpr int SETS = 64;
    static constexpr int WAYS = 4;

    // [B] MC68040UM-1993 §3.1.3: CM encoding.
    // CM field encoding (descriptor/TTR bits 6-5, MC68040UM-1993 §3.1.3)
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
    u8 rot = 0;                 // [B] MC68040UM-1993 §4.1: two-bit replacement counter.

    // Gate observables — counters, not architecture
    u64 fills = 0, hits = 0, pushes = 0;

    // POM68K: data-bearing caches are not reconstructible from RAM. Keep
    // the replacement phase and every valid line, including clean stale
    // instruction bytes and retained lines while the cache is disabled.
    // Invalid tags/data and diagnostic counters have no architectural use.
    template<class Ar> void visit(Ar& ar) {
        ar(rot);
        for (auto& set : sets) for (auto& line : set) {
            ar(line.valid);
            if (line.valid) ar(line.tag, line.dirty, line.data);
            else if constexpr (Ar::loading) line = {};
        }
    }

    // [B] MC68040UM-1993 §4.1: n accesses advance the replacement counter.
    void count(unsigned n) { rot = u8((rot + n) & 3); }

    // Half-lines (8 bytes) touched by an access of `bytes` starting at
    // line offset `off`; the access does not cross the line.
    static unsigned halfLines(u32 off, int bytes) {
        return unsigned(((off + u32(bytes) - 1) >> 3) - (off >> 3) + 1);
    }

    // [B] MC68040UM-1993 §7.3: a misaligned noncached operand becomes a
    // sequence of aligned transfers; a write-through write counts each one.
    // [F] The greedy long/word/byte split (a long at +1: byte, word, byte).
    // Settled by: a bus trace of a misaligned write-through store.
    static unsigned transfers(u32 pa, int bytes) {
        unsigned n = 0;
        while (bytes > 0) {
            const int size = (pa & 3) == 0 && bytes >= 4 ? 4
                           : (pa & 1) == 0 && bytes >= 2 ? 2 : 1;
            pa += u32(size); bytes -= size; n++;
        }
        return n;
    }

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
        if (w == WAYS) w = rot & 3;

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

    // [B] MC68040UM-1993 §4.3.2 and §4.3.3: NC and MOVE16 accesses.
    // The manual defines NC handling (§4.3.2) and MOVE16 no-allocation/
    // write-hit invalidation (§4.3.3). Caller has checked the cache enable bit.
    // [F] This policy helper bypasses NC without resolving a resident line;
    // that is not the complete push/invalidate protocol of §4.3.2. Settled by:
    // a resident dirty-line NC-transition gate with visible writeback cycles.
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
            // [B] MC68040UM-1993 §4.1: counted as pomCache040ReadValue and
            // pomCache040WriteValue count.
            if (!write) count(halfLines(u32(lo & 15), int(hi - lo + 1)));
            else if (cm == CM_COPYBACK) count(1);
        }
        if (write && cm == CM_WRITETHROUGH) count(transfers(pa, bytes));
    }

    // [B] M68000PRM-1992 CINV: invalidate selected physical lines/pages.
    // CINV scopes (M68000PRM-1992 CINV) — discard, dirty state included
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
    // image to memory. Invalidate before calling the sink, keeping a local
    // image: a CPUSH bus error exports PD rather than retaining the line.
    // [B] MC68040UM-1993 §7.6.1: CPUSH invalidates even when the push faults.
    template <class F> int drainLine(u32 pa, F sink) {
        int n = 0;
        if (Line* l = lookup(pa)) {
            const Line pending = *l;
            l->valid = false; l->dirty = 0;
            if (pending.dirty) { sink(pa & ~u32(15), pending); pushes++; n = 1; }
        }
        return n;
    }
    template <class F> int drainPage(u32 pa, u32 pageMask, F sink) {
        int n = 0;
        for (int set = 0; set < SETS; set++) for (Line &l : sets[set]) {
            const u32 base = (l.tag << 10) | (u32(set) << 4);
            if (!l.valid || (base & pageMask) != (pa & pageMask)) continue;
            const Line pending = l;
            l.valid = false; l.dirty = 0;
            if (pending.dirty) { sink(base, pending); pushes++; n++; }
        }
        return n;
    }
    template <class F> int drainAll(F sink) {
        int n = 0;
        for (int set = 0; set < SETS; set++) for (Line &l : sets[set]) {
            if (!l.valid) continue;
            const u32 base = (l.tag << 10) | (u32(set) << 4);
            const Line pending = l;
            l.valid = false; l.dirty = 0;
            if (pending.dirty) { sink(base, pending); pushes++; n++; }
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

        // [B] MC68040UM-1993 §4.3.1.1 and §4.3.1.2: allocation policy.
        // Miss: reads allocate in both cachable modes; a copyback write
        // miss line-fills then modifies; a writethrough write miss goes
        // to memory without allocating (no write-allocate, UM § 4.3.1.1)
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
