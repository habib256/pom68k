// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── JIT write guard (the only JIT header a memory map includes) ──
// Deliberately dependency-free: Q605Memory, CentrisMemory, Q630Memory and
// Q700Memory include this and nothing else of the JIT, so the memory maps
// stay ignorant of Moira, of backends and of the engine.
//
// What it protects is the *recorded* form of translated code — the block
// IR today, generated host code once the x86-64 backend lands. It does NOT
// protect the instruction-fetch window: that one points straight into the
// guest RAM/ROM buffer, so an ordinary guest write is visible to it
// immediately and self-modifying code stays correct with no invalidation.
//
// The one thing a write cannot express is a change of the address MAP
// itself — the boot overlay flipping a whole gigabyte from "ROM mirrored
// modulo 1 MB" to RAM does not write a single byte. That is what
// invalidate() is for, and it is called from the overlay sites inside
// read8/read16.
//
// Cost when the JIT is off: the memory map holds a null pointer and every
// write path pays one always-predicted branch.

#pragma once
#include <cstdint>

namespace jit {

struct CodeGuard {
    // GRANULARITY. One byte per 256 bytes of PHYSICAL RAM, owned by
    // jit::Engine: non-zero means some cached block has code in that slice.
    //
    // It was one byte per 4 KB until 2026-07-28, and that page was far too
    // coarse to live with: 68k code and the data it works on share pages
    // constantly, so an ordinary write tripped the guard, and a trip drops
    // the WHOLE cache. A single Mac OS 8.1 boot phase took 5 313 such
    // flushes — the engine spent its time re-recording and re-translating
    // code it had already translated. At 256 bytes a write has to land in
    // the same cache-line neighbourhood as translated code to matter.
    //
    // RAM is not mirrored on any of the four 68040 machines (an address
    // above the bank reads open bus, it does not alias), so one physical
    // address maps to exactly one slot and there is no alias fan-out.
    static constexpr uint32_t kShift = 8;
    static constexpr uint32_t kUnit = 1u << kShift;

    // One byte per slice, and since 2026-08-22 the byte is a MASK: bit i
    // set means some cached block has bytes in 32-byte sub-slice i. Every
    // reader that only asks "does this slice hold code?" keeps testing
    // non-zero; note() tests the bits the write actually covers, so a
    // store that lands beside a block — the idle Finder's 21 M near-misses
    // per 30 000 frames — never trips the guard at all.
    static constexpr uint32_t kSubShift = 5;
    static uint8_t subMask(uint32_t lo, uint32_t hi) {   // same slice
        const uint32_t a = (lo & (kUnit - 1)) >> kSubShift;
        const uint32_t b = (hi & (kUnit - 1)) >> kSubShift;
        return uint8_t((0xFFu >> (7 - b)) & (0xFFu << a));
    }
    const uint8_t* pageMap = nullptr;
    uint32_t       pages   = 0;         // slices, not pages

    // WHERE it was hit, not just that it was. A trip used to drop the whole
    // block cache, and on a Mac OS 8.1 boot that happened 5 313 times in one
    // phase — the engine spent its life re-translating code it had already
    // translated. Recording the slices lets the engine evict just the blocks
    // that actually overlap them. The list is small on purpose: past
    // kMaxHits distinct slices, dropping everything really is cheaper than
    // the bookkeeping.
    // Each hit carries the slice AND the union of the physical byte ranges
    // written into it since the last clear (2026-08-22): the engine evicts
    // only the blocks whose own bytes the range touches. A write into the
    // 256-byte neighbourhood of a block changes nothing the block was
    // translated from, so keeping it is bit-exact — and on the idle
    // System 7 Finder those near-misses were 21 M trips per 30 000
    // frames, every one re-recording every block in the slice.
    struct Hit { uint32_t slice, lo, hi; };
    static constexpr int kMaxHits = 8;
    Hit      hits[kMaxHits] = {};
    int      nHits = 0;
    bool     hitOverflow = false;   // too many slices: fall back to a flush

    bool hit = false;          // a write landed in a slice holding code
    bool mapChanged = false;   // the address map itself moved

    // Hot path. `n` is the store width in bytes (a word store can straddle
    // a page boundary, so both ends are tested).
    void note(uint32_t phys, uint32_t n) {
        if (!pageMap) return;
        const uint32_t p0 = phys >> kShift;
        const uint32_t p1 = (phys + n - 1) >> kShift;
        const uint32_t hi = phys + n - 1;
        if (p0 < pages && pageMap[p0]) {
            const uint32_t h0 = p1 == p0 ? hi : (p0 << kShift) + kUnit - 1;
            if (pageMap[p0] & subMask(phys, h0)) record(p0, phys, hi);
        }
        if (p1 != p0 && p1 < pages && pageMap[p1] &&
            (pageMap[p1] & subMask(p1 << kShift, hi))) record(p1, phys, hi);
    }

    void record(uint32_t slice, uint32_t lo, uint32_t hi) {
        hit = true;
        for (int i = 0; i < nHits; i++)
            if (hits[i].slice == slice) {
                if (lo < hits[i].lo) hits[i].lo = lo;
                if (hi > hits[i].hi) hits[i].hi = hi;
                return;
            }
        if (nHits >= kMaxHits) { hitOverflow = true; return; }
        hits[nHits++] = { slice, lo, hi };
    }

    // Cold path: overlay flip, ROM reload, anything that remaps.
    void invalidate() { mapChanged = true; }

    bool tripped() const { return hit || mapChanged; }
    // True when the engine has no choice but to drop everything: the map
    // itself moved, or too many distinct slices were written to track.
    bool mustFlush() const { return mapChanged || hitOverflow; }
    void clear() { hit = false; mapChanged = false; hitOverflow = false; nHits = 0; }
};

}  // namespace jit
