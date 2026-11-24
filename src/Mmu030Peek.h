// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Side-effect-free logical→physical translation on the 68030 PMMU ──
//
// RAM-based-video machines (IIsi, IIci) use physical low RAM as the
// framebuffer and relocate the System's logical low memory with the PMMU.
// A physical read of a logical global can therefore return desktop pixels.
// This adapter resolves the mapping before the caller reads physical RAM.
//
// Mmu030Walk.h owns descriptor geometry and bounds.
// Inspection selects its read-only mode and supplies physical peek8 reads;
// CPU accesses supply live bus reads and descriptor-history writes. No
// inspection access reaches the CPU's ATC or the live memory bus.
// WP/S protection is reported separately from invalid mappings and is
// deliberately ignored here: the debugger wants the mapped bytes.
// TT matching remains the caller's decision (transparentRead below).

#pragma once
#include <cstdint>
#include "Mmu030Walk.h"

namespace mmu030peek {

// One translation attempt. `peek8(phys)` must be side-effect-free and
// physical. `fc` is the function code the walk would use (5 = supervisor
// data — what the System's own accesses to its globals are). Returns true
// and writes `*phys` on success; false on an invalid descriptor or a
// malformed table.
template <class PeekFn>
inline bool translate(uint32_t tc, uint64_t crp, uint64_t srp,
                      uint32_t laddr, int fc, PeekFn&& peek8,
                      uint32_t* phys) {
    if (!(tc & 0x80000000)) {                // TC.E clear: translation off
        *phys = laddr;                       // identity IS the mapping then
        return true;
    }

    auto peek32 = [&](uint32_t a) -> uint32_t {
        return uint32_t(peek8(a)) << 24 | uint32_t(peek8(a + 1)) << 16
             | uint32_t(peek8(a + 2)) << 8 | uint32_t(peek8(a + 3));
    };
    uint32_t page = 0, descriptor = 0;
    uint16_t status = 0;
    bool ci = false;
    int count = 0;
    pom68k::mmu030::tableWalk<false>(tc, crp, srp, laddr, uint8_t(fc), false, 0,
        peek32, [](uint32_t, uint32_t) {}, page, ci, status, descriptor, count);
    if (status & 0xC400) return false;              // B | L | I, not WP/S
    const uint32_t pageMask = (uint32_t(1) << ((tc >> 20) & 15)) - 1;
    *phys = (page & ~pageMask) | (laddr & pageMask);
    return true;
}

// Transparent translation (MC68030UM § 9.7.3) for a READ at `laddr` with
// function code `fc`: TT0/TT1 base (31-24), mask (23-16), E (15), R/W (9),
// RWM (8), FC base (6-4) and FC mask (2-0). A match means the address goes
// out untranslated. `translate()` above does not look at TT registers; a
// caller that needs the CPU's full read decision (the debugger's logical
// memory view) checks this first.
inline bool transparentRead(uint32_t tt, uint32_t laddr, int fc) {
    if (!(tt & 0x8000)) return false;
    const uint32_t base = tt & 0xFF000000u;
    const uint32_t ignore = (tt << 8) & 0xFF000000u;
    if (((laddr ^ base) & ~ignore) & 0xFF000000u) return false;
    const uint32_t fcBase = (tt >> 4) & 7, fcMask = tt & 7;
    if ((uint32_t(fc) ^ fcBase) & ~fcMask & 7) return false;
    return (tt & 0x100) || (tt & 0x200);
}

} // namespace mmu030peek
