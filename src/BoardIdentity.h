// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The core-level identity a catalogue profile carries beyond its platform.
// MEMCjr (MAME macquadra605.cpp) and F108 (macquadra630.cpp) boards tell
// their marketing models apart by the board-ID longword at $5FFFFFFC alone;
// on MEMCjr the profile also decides the FPU (the Quadra 605's 68040 has it
// on die, the LC 475/575's 68LC040 does not). The product's profile
// application and the machine gates share this one mapping, so a gate cannot
// boot an identity the catalogue does not declare (CHANGELOG 2026-10-02
// (eighth), 2026-10-13).
//
// The MSC Duos (MAME macpwrbkmsc.cpp:598-619) share the ECFA989B ROM and
// differ by the CPU ID longword at $5FFFFFFC and by their crystal: the ROM
// decodes $A55A1004/5/6 as boxPowerBookDuo210/230/250 (Apple's own table,
// mac-rom OS/UniversalTables.a:459-461 — ID 6 "DBLite", renamed the Duo 250
// at :26 — and Internal/Asm/InternalOnlyEqu.a:1043-1052) and the 210 runs at 25 MHz where the 230/250 run at 33 MHz
// (macpwrbkmsc.cpp:767,870). The 250's active-matrix panel is invisible to
// the ROM (MAME gives every GSC Duo panel id 6). These go to the MscMemory
// constructor rather than CoreConfig because they are construction
// arguments of the board, not startup options (CHANGELOG 2026-10-14
// (fifth)).

#pragma once

#include "CoreConfig.h"
#include "MachineCatalog.h"

#include <cstdint>

namespace pom68k {

// Writes the board identity of `profile` into `core`; profiles whose board
// has no such register leave `core` untouched.
inline void applyBoardIdentity(CoreConfig& core, SnapMachine profile) {
    switch (profile) {
    case SnapMachine::Lc475:
        core.bus.q605MachineId = 0xA55A2221u;
        core.cpu.q605Fpu = Q605FpuMode::Soft68882;
        break;
    case SnapMachine::Lc575:
        core.bus.q605MachineId = 0xA55A222Eu;
        core.cpu.q605Fpu = Q605FpuMode::Soft68882;
        break;
    case SnapMachine::Q605:
        core.bus.q605MachineId = 0xA55A2225u;
        core.cpu.q605Fpu = Q605FpuMode::Integrated;
        break;
    case SnapMachine::Q630:
        core.bus.q630MachineId = 0xA55A2252u;
        break;
    case SnapMachine::Lc580:
        core.bus.q630MachineId = 0xA55A225Au;
        break;
    default:
        break;
    }
}

struct MscBoardIdentity {
    std::int64_t cpuHz;
    std::uint32_t boxId;
};

constexpr MscBoardIdentity mscBoardIdentity(SnapMachine profile) {
    switch (profile) {
    case SnapMachine::Duo210: return {25000000, 0xA55A1004u};
    case SnapMachine::Duo250: return {33000000, 0xA55A1006u};
    case SnapMachine::Duo270c: return {33000000, 0xA55A1002u};
    default:                  return {33000000, 0xA55A1005u};   // Duo 230
    }
}

// `base` with the identity of `profile` applied: what a fixture passes to
// the board constructor instead of the identity-less defaultCoreConfig().
inline CoreConfig profileCoreConfig(SnapMachine profile,
                                    CoreConfig base = defaultCoreConfig()) {
    applyBoardIdentity(base, profile);
    return base;
}

} // namespace pom68k
