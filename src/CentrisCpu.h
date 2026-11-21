// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── 68LC040 CPU (Moira wrapper, Mac Centris 610/650) ──
// The Cpu040 pattern on the djMEMC/IOSB bus: the Q2-Q4 040 core (integer
// ISA, 040 MMU, ATC overlay) with external /BERR through
// Moira::extBusError040. The Centris 610/650 ship a 68LC040 (no hardware
// FPU) at 20 / 25 MHz; Moira's soft 68882 makes them Finder-usable (the
// Q605 no-FPU precedent). POM68K_CENTRIS_BAREFPU=1 selects true
// FPUModel::NONE (architectural F-line), POM68K_CENTRIS_FPU=1 the full
// 68040 (for the Quadra 610/650 identity).
// Shared wrapper plumbing: MoiraCpu.h; the 68040 pacing (boost, peripheral
// deadline, stall, engine lock, save-state chunk): Moira040Cpu.h.
// Gate: tests/centris650_boot_etalon.cpp.

#pragma once
#include "CentrisMemory.h"
#include "CoreConfig.h"
#include "Moira040Cpu.h"

class CentrisCpu : public pom68k::Moira040Cpu<CentrisCpu, CentrisMemory> {
public:
    explicit CentrisCpu(CentrisMemory& mem,
                        const jit::ResolvedConfig& jitConfig,
                        const pom68k::CoreCpuConfig& cpuConfig);

private:
    friend pom68k::MoiraCpu<CentrisCpu, CentrisMemory>;
    friend pom68k::Moira040Cpu<CentrisCpu, CentrisMemory>;
};
