// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "CentrisCpu.h"

CentrisCpu::CentrisCpu(CentrisMemory& mem, const jit::ResolvedConfig& jitConfig,
                       const pom68k::CoreCpuConfig& cpuConfig)
    : Moira040Cpu(mem, jitConfig) {
    // Centris 610/650 = 68LC040. Default to the LC040 identity + Moira's
    // soft 68882 (Finder-usable, the Q605 no-FPU precedent);
    // POM68K_CENTRIS_FPU=1 the full 68040 (the Quadra 610/650 identity).
    selectIdentity(!cpuConfig.centrisFull040, cpuConfig.centrisBareFpu,
                   cpuConfig.mmu040Walk);
    setCacheBoost(cpuConfig.centrisCacheBoost);
}
