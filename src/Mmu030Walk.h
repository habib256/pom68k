// POM68K — Macintosh 68k emulator
// Walker body: from the POM68K Moira fork (MIT, see LICENSE-MIT), itself
// written on Dirk W. Hoffmann's Moira (MIT).
//
// ── 68030 table search over caller-supplied descriptor I/O ──
// Dirk's MMU030 (extern/moira) walks through the live CPU bus (moira.read32,
// U/M history writes). Inspection (debugger, low-memory globals on the
// IIsi/IIci) needs the same search over side-effect-free physical peeks, so
// POM68K keeps this algorithm on top of the core instead of in it.
// moira_rules_test holds it against Dirk's PTEST on the same tables.
#pragma once
#include <cstdint>

namespace pom68k::mmu030 {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

// [B] MC68030UM-1989 §§9.5.2–9.5.5,9.7.1,9.8: table searches.
// POM68K: mapping rules for inspection and the PTEST cross-check.
// The caller chooses descriptor I/O; this algorithm owns no CPU or board.
// MaintainHistory permits U/M writes for a bus search (limit zero).
// PTEST supplies a stopping level; inspection runs to the leaf without
// history. Neither read-only path inserts an ATC entry. Protection is
// reported in status separately from invalid mappings (I/L/B).
// A descriptor I/O exception propagates with the previous complete address
// and the attempted descriptor count; the CPU supplies /BERR handling.
template<bool MaintainHistory, class Read32, class Write32>
inline void tableWalk(u32 tc, u64 crp, u64 srp, u32 addr, u8 fc,
                      bool write, int limit, Read32&& read32, Write32&& write32,
                      u32& pageAddr, bool& ci, u16& status,
                      u32& descrAddr, int& descrNum)
{

    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // Table index masks and shifts (WinUAE mmu030_decode_tc: TI fields
    // are consumed up to the first zero field)
    u32 tmask[4] = {}; u8 tshift[4] = {}; int lastTable = 0;
    {
        int shift = 32 - (tc >> 16 & 15);
        for (int i = 0; i < 4; i++) {
            int ti = tc >> (12 - 4 * i) & 15;
            if (!ti) break;
            shift -= ti;
            tshift[i] = u8(shift & 31);
            tmask[i] = ((u32(1) << ti) - 1) << (shift & 31);
            lastTable = i;
        }
    }

    const bool super = (fc & 4) != 0;
    bool superViolation = false, writeProtected = false;
    bool earlyTermination = false;
    int t = 0;
    // [B] MC68030UM-1989 §9.7.1: root pointer.
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // Root pointer (SRP when SRE and supervisor FC, § 9.7.1); reserved
    // bits of the upper long are masked (WinUAE RP_ZERO_BITS)
    u64 rp = ((tc & 0x02000000) && super) ? srp : crp;
    u32 descr0 = u32(rp >> 32) & ~u32(0x0000FFFC);
    u32 descr1 = u32(rp);
    int descrSize = 8;
    int nextSize = 0;
    int type = int(descr0) & 3;
    // Commit the address only after the whole descriptor was read: a
    // PTEST /BERR returns the preceding completed descriptor and count.
    auto fetch = [&](u32 address, int size) {
        descrNum++;
        const u32 first = read32(address);
        const u32 second = size == 8 ? read32(address + 4) : 0;
        descrAddr = address;
        descr0 = first;
        if (size == 8) descr1 = second;
        descrSize = size;
        type = int(first) & 3;
    };

    switch (type) {

        case 0: status |= 0x0400; goto stopSearch;          // invalid
        case 1: earlyTermination = true; goto pageDescriptor;
        case 2: nextSize = 4; break;
        case 3: nextSize = 8; break;
    }

    // [B] MC68030UM-1989 §9.5.5.1: function-code lookup.
    // Function code lookup (§ 9.5.5.1)
    if (tc & 0x01000000) {

        u32 tableAddr = (descrSize == 4 ? descr0 : descr1) & 0xFFFFFFF0;
        fetch(tableAddr + u32(fc) * nextSize, nextSize);

        switch (type) {

            case 0: status |= 0x0400; goto stopSearch;
            case 1: earlyTermination = true; goto pageDescriptor;
            case 2: nextSize = 4; break;
            case 3: nextSize = 8; break;
        }
    }

    // Upper level tables
    do {

        if (descrNum) {

            if (descrSize == 8 && (descr0 & 0x100) && !super) superViolation = true;
            if (descr0 & 0x04) writeProtected = true;

            if (MaintainHistory && !limit && !(descr0 & 0x08) && !superViolation) {
                descr0 |= 0x08;                             // U (history)
                write32(descrAddr, descr0);
            }
            status |= superViolation ? 0x2000 : 0;
            status |= writeProtected ? 0x0800 : 0;
            if (limit && descrNum == limit) goto stopSearch;
        }

        {
            u32 tableAddr = (descrSize == 4 ? descr0 : descr1) & 0xFFFFFFF0;
            u32 tableIndex = (addr & tmask[t]) >> tshift[t];
            t++;

            // [B] MC68030UM-1989 §9.5.1.1: limit fields.
            // Limit check on long descriptors (§ 9.5.1.1)
            if (descrSize == 8) {
                u32 limit = (descr0 & 0x7FFF0000) >> 16;
                if ((descr0 & 0x80000000) && tableIndex < limit) { status |= 0x4400; goto stopSearch; }
                if (!(descr0 & 0x80000000) && tableIndex > limit) { status |= 0x4400; goto stopSearch; }
            }

            fetch(tableAddr + tableIndex * nextSize, nextSize);
        }

        switch (type) {

            case 0: status |= 0x0400; goto stopSearch;
            case 1:
                if (t <= lastTable) earlyTermination = true;
                goto pageDescriptor;
            case 2: nextSize = 4; break;
            case 3: nextSize = 8; break;
        }

    } while (t <= lastTable);

    // [B] MC68030UM-1989 §9.5.3.2: indirection.
    // Indirect descriptor on the last populated level (§ 9.5.3.2)
    {
        if (limit && descrNum == limit) goto stopSearch;
        u32 indirect = (descrSize == 4 ? descr0 : descr1) & 0xFFFFFFFC;
        fetch(indirect, nextSize);

        if (type != 1) { status |= 0x0400; goto stopSearch; }
    }

pageDescriptor:

    // [B] MC68030UM-1989 §§9.4,9.7.1: a valid write marks the ATC
    // modified even when its direct-root mapping has no memory M field.
    // PTEST only examines descriptors; it must not synthesize that history.
    if (MaintainHistory && !descrNum && !limit && write) status |= 0x0200;

    if (descrNum) {

        if (descrSize == 8 && (descr0 & 0x100) && !super) superViolation = true;
        if (descr0 & 0x04) writeProtected = true;

        if (MaintainHistory && !limit && !superViolation) {

            bool dirty = false;
            if (!(descr0 & 0x10) && write && !writeProtected) { descr0 |= 0x10; dirty = true; }   // M
            if (!(descr0 & 0x08)) { descr0 |= 0x08; dirty = true; }                               // U
            if (dirty) write32(descrAddr, descr0);
        }
        status |= superViolation ? 0x2000 : 0;
        status |= writeProtected ? 0x0800 : 0;
        ci = (descr0 & 0x40) != 0;
        status |= (descr0 & 0x10) ? 0x0200 : 0;
    }

    if (earlyTermination) {

        // [B] MC68030UM-1989 §9.7.1: a DT=1 root still checks its
        // limit with FCL enabled; no function-code table is consulted.
        if (descrSize == 8) {
            u32 tableIndex = (addr & tmask[t]) >> tshift[t];
            u32 limit = (descr0 & 0x7FFF0000) >> 16;
            if ((descr0 & 0x80000000) && tableIndex < limit) { status |= 0x4400; goto stopSearch; }
            if (!(descr0 & 0x80000000) && tableIndex > limit) { status |= 0x4400; goto stopSearch; }
        }
        // [B] MC68030UM-1989 §9.5.3.1: early-termination address bits.
        // Unused index bits are added to the page address (§ 9.5.3.1)
        u32 unused = 0;
        for (int i = t; i <= lastTable; i++) unused |= tmask[i];
        pageAddr = addr & unused;
    }

    pageAddr += (descrSize == 4 ? descr0 : descr1) & 0xFFFFFF00;

stopSearch:

    return;
}

} // namespace pom68k::mmu030
