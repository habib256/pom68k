// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Side-effect-free logical→physical translation on the 68030 PMMU ──
//
// WHY THIS EXISTS. On the RAM-based-video machines (IIsi, IIci) physical low
// RAM IS the framebuffer and the ROM enables the PMMU (IIsi TC = $80F84500)
// to relocate the System's logical low memory elsewhere. `peek8()` is
// physical, so reading "the Time global at $20C" through it returns desktop
// PIXELS — the trap that cost three rounds of a nonexistent "IIsi has no
// ADB" bug (`LLE_VS_HLE.md` § 3) and that kept the RBV machines out of the
// beyond-boot gates (`TODO.md` § 2: "needs a LOGICAL-address read of the
// Time global first"). This header is that read.
//
// WHY A TEST-SIDE WALK and not a call into Moira. The one walk in the tree,
// `Moira::mmuWalkTables`, reads descriptors through the LIVE bus
// (`mmuRead32` → `read16`) and its non-ptest mode writes the descriptor
// U/M bits — a probe built on it would be an instrument that answers the
// question by changing the answer (the Easy Access lesson, `TODO.md` § 1).
// This walk mirrors that code's structure descriptor for descriptor — the
// short/long cases, the early-termination page descriptor, the indirection
// rule (POM68K D3: a level is last when the NEXT index field is zero) —
// but reads descriptors through the caller's `peek8`, which is physical and
// side-effect-free by contract. Table addresses held in descriptors ARE
// physical, so `peek8` is exactly the right instrument for them.
//
// Deliberately NOT a full PMMU model: no protection checks (WP/S — a probe
// wants the mapping, not an access verdict), no U/M updates (the point), no
// transparent-translation windows (the Mac ROMs map I/O through the tables
// or run TT-less; a gate reading LOW MEMORY never lands in a TT window).
// If a probe address ever needs TT handling, extend this header — do not
// switch to the live walk.

#pragma once
#include <cstdint>

namespace mmu030peek {

// One translation attempt. `peek8(phys)` must be side-effect-free and
// physical. `fc` is the function code the walk would use (5 = supervisor
// data — what the System's own accesses to its globals are). Returns true
// and writes `*phys` on success; false on an invalid descriptor or a
// too-deep table.
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

    const uint32_t bits = tc & 0xffff;               // TIA..TID
    const int pagesize = int(tc >> 20) & 0xf;        // PS
    const int is = int(tc >> 16) & 0xf;              // IS
    int bitpos = (tc & 0x01000000) ? 16 : 12;        // FCL
    int pageshift = is;

    const uint64_t rp = ((tc & 0x02000000) && (fc & 4)) ? srp : crp;
    uint32_t table = uint32_t(rp) & 0xfffffff0;
    int type = int(rp >> 32) & 3;

    uint32_t addrIn = laddr << is;
    int level = 0;

    while (level < 8) {
        const int indexbits = int(bits >> bitpos) & 0xf;
        // Same shift quirk as the oracle: indexbits == 0 → `& 31` keeps
        // the u32 unchanged, as x86 does.
        const uint32_t tableIndex = (bitpos == 16)
            ? uint32_t(fc) : addrIn >> ((32 - indexbits) & 31);
        bitpos -= 4;
        const bool indirect =
            ((!bitpos || !((bits >> bitpos) & 0xf)) && indexbits);

        switch (type) {
        case 0:                                      // invalid descriptor
            return false;
        case 1: {                                    // page / early term.
            const uint32_t base = table & (~uint32_t(0) << pagesize);
            *phys = base + (addrIn >> pageshift);
            return true;
        }
        case 2: {                                    // short descriptors
            level++;
            uint32_t at = table + (tableIndex << 2);
            uint32_t entry = peek32(at);
            type = int(entry) & 3;
            if (indirect && (type == 2 || type == 3)) {
                level++;
                at = entry & 0xfffffffc;
                entry = peek32(at);
                type = int(entry) & 3;
            }
            table = entry & 0xfffffff0;
            break;
        }
        case 3: {                                    // long descriptors
            level++;
            uint32_t at = table + (tableIndex << 3);
            uint32_t entry = peek32(at);
            uint32_t entry2 = peek32(at + 4);
            type = int(entry) & 3;
            if (indirect && (type == 2 || type == 3)) {
                level++;
                at = entry2 & 0xfffffffc;
                entry = peek32(at);
                entry2 = peek32(at + 4);
                type = int(entry) & 3;
            }
            table = entry2 & 0xfffffff0;
            break;
        }
        }

        addrIn <<= indexbits;
        pageshift += indexbits;
    }
    return false;                                    // table deeper than 8
}

} // namespace mmu030peek
