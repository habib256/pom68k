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

#pragma once

#include "CoreConfig.h"
#include "MachineCatalog.h"

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

// `base` with the identity of `profile` applied: what a fixture passes to
// the board constructor instead of the identity-less defaultCoreConfig().
inline CoreConfig profileCoreConfig(SnapMachine profile,
                                    CoreConfig base = defaultCoreConfig()) {
    applyBoardIdentity(base, profile);
    return base;
}

} // namespace pom68k
