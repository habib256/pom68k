// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── 68030 CPU (Moira wrapper, Mac IIsi / IIci) ──
// The SonoraCpu pattern on the RBV bus at 20 MHz: functional accuracy,
// PMMU for the 24-bit mode, external /BERR through Moira::extBusError
// (the SCSI pseudo-DMA timeout), the Cpu030 i-cache throughput overlay.
// The IIsi socket takes an optional 68882 on the adapter card (MAME
// maciici.cpp:514 defaults to "No FPU"); pass withFpu to populate it,
// as the target System 7.5 images expect (the LC II precedent).
// Shared wrapper plumbing: MoiraCpu.h; the pacing, floppy boost gate and
// save-state chunk shared with VaspCpu: MoiraGated030Cpu.h.
// Gate: tests/iisi_boot_etalon.cpp.

#pragma once
#include "CoreConfig.h"
#include "MoiraGated030Cpu.h"
#include "RbvMemory.h"

class RbvCpu : public pom68k::MoiraGated030Cpu<RbvCpu, RbvMemory> {
public:
    explicit RbvCpu(RbvMemory& mem, const jit::ResolvedConfig& jitConfig,
                    const pom68k::CoreCpuConfig& cpuConfig,
                    bool withFpu = false);

private:
    friend pom68k::MoiraCpu<RbvCpu, RbvMemory>;
    friend pom68k::MoiraGated030Cpu<RbvCpu, RbvMemory>;
};
