// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── 68040 CPU (Moira wrapper, Macintosh Quadra 700) ──
// The Cpu040 pattern on the Quadra 700's discrete bus: the Q2-Q4 040 core
// (integer ISA, 040 MMU, ATC overlay) with external /BERR through
// Moira::extBusError040. The Q700 is a FULL 68040 @ 25 MHz (50 MHz XTAL / 2),
// so the default is M68040 + its integrated FPU; POM68K_Q700_LC040=1 selects
// the LC040 and POM68K_Q700_BAREFPU=1 a bare FPUModel::NONE with it.
// Shared wrapper plumbing: MoiraCpu.h; the 68040 pacing (boost, peripheral
// deadline, stall, engine lock, save-state chunk): Moira040Cpu.h.
// Gate: tests/q700_boot_etalon.cpp.

#pragma once
#include "Q700Memory.h"
#include "CoreConfig.h"
#include "Moira040Cpu.h"

class Q700Cpu : public pom68k::Moira040Cpu<Q700Cpu, Q700Memory> {
public:
    explicit Q700Cpu(Q700Memory& mem,
                     const jit::ResolvedConfig& jitConfig,
                     const pom68k::CoreCpuConfig& cpuConfig);

private:
    friend pom68k::MoiraCpu<Q700Cpu, Q700Memory>;
    friend pom68k::Moira040Cpu<Q700Cpu, Q700Memory>;
};
