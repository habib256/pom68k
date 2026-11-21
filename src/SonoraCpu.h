// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── 68030 CPU (Moira wrapper, Mac LC III) ──
// The Cpu030 (LC II) pattern on the Sonora bus at 25 MHz: functional
// accuracy, PMMU for the 24-bit mode, external /BERR through
// Moira::extBusError (the SCSI pseudo-DMA timeout). Keeps the Cpu030
// i-cache throughput overlay (cacheBoost ceiling + per-miss charge,
// rationale in Cpu030.h) — the real LC III leans on its 030 caches just
// as hard. The 68882 socket is empty on a stock LC III (maclc3.cpp:338
// set_fpu_enable(false)); pass withFpu to populate it, as the target
// System 7.5 images expect (the LC II precedent).
// Shared wrapper plumbing: MoiraCpu.h; the pacing, CACR hint and
// save-state chunk shared with RbvCpu and VaspCpu: Moira030Cpu.h, without
// its floppy boost gate (FloppyGate = false).
// Gate: tests/lc3_boot_etalon.cpp.

#pragma once
#include "CoreConfig.h"
#include "Moira030Cpu.h"
#include "SonoraMemory.h"

class SonoraCpu : public pom68k::Moira030Cpu<SonoraCpu, SonoraMemory, false> {
public:
    explicit SonoraCpu(SonoraMemory& mem, const jit::ResolvedConfig& jitConfig,
                       const pom68k::CoreCpuConfig& cpuConfig,
                       bool withFpu = false);

private:
    friend pom68k::MoiraCpu<SonoraCpu, SonoraMemory>;
    friend pom68k::Moira030Cpu<SonoraCpu, SonoraMemory, false>;
};
