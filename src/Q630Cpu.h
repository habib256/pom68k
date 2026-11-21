// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── 68040 CPU (Moira wrapper, Macintosh Quadra 630 / LC 580) ──
// The Cpu040 pattern on the F108 bus. The Quadra 630 is a FULL 68040 @
// 33 MHz, so the default is M68040 + its integrated FPU; the LC/Performa
// 630 and 580 siblings ship a 68LC040 — POM68K_Q630_LC040=1 selects it,
// POM68K_Q630_BAREFPU=1 a bare FPUModel::NONE with it.
// Shared wrapper plumbing: MoiraCpu.h; the 68040 pacing (boost, peripheral
// deadline, stall, engine lock, save-state chunk): Moira040Cpu.h.
// Gate: tests/q630_boot_etalon.cpp.

#pragma once
#include "Q630Memory.h"
#include "CoreConfig.h"
#include "Moira040Cpu.h"

class Q630Cpu : public pom68k::Moira040Cpu<Q630Cpu, Q630Memory> {
public:
    explicit Q630Cpu(Q630Memory& mem,
                     const jit::ResolvedConfig& jitConfig,
                     const pom68k::CoreCpuConfig& cpuConfig);

private:
    friend pom68k::MoiraCpu<Q630Cpu, Q630Memory>;
    friend pom68k::Moira040Cpu<Q630Cpu, Q630Memory>;
};
