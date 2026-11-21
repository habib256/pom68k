// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "SonoraCpu.h"

SonoraCpu::SonoraCpu(SonoraMemory& mem, const jit::ResolvedConfig& jitConfig,
                     const pom68k::CoreCpuConfig& cpuConfig, bool withFpu)
    : Moira030Cpu(mem, jitConfig, cpuConfig, withFpu) {
}
