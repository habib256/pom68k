// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "Q700Cpu.h"

Q700Cpu::Q700Cpu(Q700Memory& mem, const jit::ResolvedConfig& jitConfig,
                 const pom68k::CoreCpuConfig& cpuConfig)
    : Moira040Cpu(mem, jitConfig) {
    // The Quadra 700 ships a full 68040 (macquadra700.cpp M68040 @ 50/2 MHz),
    // so unlike the Centris the default is the 040 identity + integrated FPU.
    // POM68K_Q700_LC040 forces the LC040 (no hardware FPU) for experiments.
    selectIdentity(cpuConfig.q700Lc040, cpuConfig.q700BareFpu);
    setCacheBoost(cpuConfig.q700CacheBoost);
}
