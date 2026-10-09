// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Side-effect-free logical→physical translation on the 68040 MMU ──
// The 68040 counterpart of Mmu030Peek.h, for the same reason: the debugger
// must answer "what is at this LOGICAL address" without the live walk's
// U/M descriptor write-back or a fault. It mirrors Moira::mmu040PeekWalk
// (MoiraExecMMU_cpp.h) and Moira::mmu040MatchTTR for the data side, but
// reads descriptors through the caller's physical, side-effect-free peek
// instead of mmuRead32 — a table in I/O space is reported invalid rather
// than read through a device.
//
// Not consulted: the ATC. It is a cache of these same tables; a walk that
// disagrees with it is a guest that changed a descriptor without PFLUSH,
// and the table is what the next miss would use. MC68040UM § 3.

#pragma once
#include <cstdint>

namespace mmu040peek {

struct Registers {
    uint32_t tc = 0, urp = 0, srp = 0, dtt0 = 0, dtt1 = 0;
};

// DTT0/DTT1 match for a data access (MC68040UM § 3.2.2): E (15), S-field
// (14-13: 1x = both modes, 00 user, 01 supervisor), base (31-24), mask
// (23-16). Same decision as Moira::mmu040MatchTTR(…, data = true).
inline bool transparent(const Registers& r, uint32_t laddr, bool super) {
    for (uint32_t ttr : {r.dtt0, r.dtt1}) {
        if (!(ttr & 0x8000)) continue;
        const uint32_t msb = (laddr ^ ttr) >> 24 & 0xFF;
        const uint32_t mask = ttr >> 16 & 0xFF;
        if (msb & ~mask) continue;
        if (!(ttr & 0x4000) && (((ttr >> 13) & 1) != 0) != super) continue;
        return true;
    }
    return false;
}

// Returns true and writes `*phys` when `laddr` is translated (or the MMU
// is off, or a TT register matches). False: an invalid descriptor at some
// level — the access would fault.
template <class Peek8>
inline bool translate(const Registers& r, uint32_t laddr, bool super,
                      Peek8&& peek8, uint32_t* phys) {
    if (transparent(r, laddr, super) || !(r.tc & 0x8000)) {
        *phys = laddr;
        return true;
    }
    auto peek32 = [&](uint32_t a) -> uint32_t {
        return uint32_t(peek8(a)) << 24 | uint32_t(peek8(a + 1)) << 16
             | uint32_t(peek8(a + 2)) << 8 | uint32_t(peek8(a + 3));
    };
    uint32_t desc = super ? r.srp : r.urp;
    desc = peek32((desc & 0xFFFFFE00) | ((laddr >> 23) & 0x1FC));
    if (!(desc & 2)) return false;                       // invalid root
    desc = peek32((desc & 0xFFFFFE00) | ((laddr >> 16) & 0x1FC));
    if (!(desc & 2)) return false;                       // invalid pointer
    const bool page8k = (r.tc & 0x4000) != 0;
    const uint32_t at = page8k ? (desc & 0xFFFFFF80) + ((laddr >> 11) & 0x7C)
                               : (desc & 0xFFFFFF00) + ((laddr >> 10) & 0xFC);
    desc = peek32(at);
    if ((desc & 3) == 2) desc = peek32(desc & 0xFFFFFFFC);   // indirect, once
    if (!(desc & 1)) return false;                       // invalid page
    const uint32_t mask = page8k ? 0xFFFFE000u : 0xFFFFF000u;
    *phys = (desc & mask) | (laddr & ~mask);
    return true;
}

} // namespace mmu040peek
