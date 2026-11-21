// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── 68030 CPU (Moira wrapper, Mac IIvx / IIvi) ──
// The SonoraCpu pattern on the VASP bus: functional accuracy, PMMU,
// external /BERR through Moira::extBusError (the SCSI pseudo-DMA
// timeout), the Cpu030 i-cache throughput overlay. The IIvx runs at
// 31.3344 MHz (C32M), the IIvi at 15.6672 MHz (C15M); both ship with the
// 68882 socket populated on the vx (maciivx.cpp FPU config switch) —
// pass withFpu accordingly.
// Shared wrapper plumbing: MoiraCpu.h; the pacing, floppy boost gate and
// save-state chunk shared with RbvCpu: MoiraGated030Cpu.h.
// Gate: tests/iivx_boot_etalon.cpp.

#pragma once
#include "CoreConfig.h"
#include "MoiraGated030Cpu.h"
#include "VaspMemory.h"

class VaspCpu : public pom68k::MoiraGated030Cpu<VaspCpu, VaspMemory> {
public:
    explicit VaspCpu(VaspMemory& mem, const jit::ResolvedConfig& jitConfig,
                     const pom68k::CoreCpuConfig& cpuConfig,
                     bool withFpu = false);

private:
    friend pom68k::MoiraCpu<VaspCpu, VaspMemory>;
    friend pom68k::MoiraGated030Cpu<VaspCpu, VaspMemory>;
};
