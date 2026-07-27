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
    // One byte per 4 KB of PHYSICAL RAM, owned by jit::Engine: non-zero
    // means some cached block has code in that page. RAM is not mirrored on
    // any of the four 68040 machines (an address above the bank reads open
    // bus, it does not alias), so one physical address maps to exactly one
    // slot and there is no alias fan-out to chase.
    const uint8_t* pageMap = nullptr;
    uint32_t       pages   = 0;

    bool hit = false;          // a write landed in a page holding code
    bool mapChanged = false;   // the address map itself moved

    // Hot path. `n` is the store width in bytes (a word store can straddle
    // a page boundary, so both ends are tested).
    void note(uint32_t phys, uint32_t n) {
        if (!pageMap) return;
        const uint32_t p0 = phys >> 12;
        const uint32_t p1 = (phys + n - 1) >> 12;
        if ((p0 < pages && pageMap[p0]) || (p1 < pages && pageMap[p1])) hit = true;
    }

    // Cold path: overlay flip, ROM reload, anything that remaps.
    void invalidate() { mapChanged = true; }

    bool tripped() const { return hit || mapChanged; }
    void clear() { hit = false; mapChanged = false; }
};

}  // namespace jit
