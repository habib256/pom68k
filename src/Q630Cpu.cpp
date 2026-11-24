// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "Q630Cpu.h"

Q630Cpu::Q630Cpu(Q630Memory& mem, const jit::ResolvedConfig& jitConfig,
                 const pom68k::CoreCpuConfig& cpuConfig)
    : Moira040Cpu(mem, jitConfig) {
    // The Quadra 630 ships a full 68040 with its integrated FPU
    // (macquadra630.cpp M68040 @ 33 MHz);
    // POM68K_Q630_LC040 forces the 68LC040 of the LC/Performa 630 and 580.
    selectIdentity(cpuConfig.q630Lc040, cpuConfig.q630BareFpu);
    setCacheBoost(cpuConfig.q630CacheBoost);
}
