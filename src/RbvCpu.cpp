// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "RbvCpu.h"

RbvCpu::RbvCpu(RbvMemory& mem, const jit::ResolvedConfig& jitConfig,
               const pom68k::CoreCpuConfig& cpuConfig, bool withFpu)
    : Moira030Cpu(mem, jitConfig, cpuConfig, withFpu) {
    // History (2026-07-25): this machine shipped with cacheBoost_ = 1 because
    // the IIsi ROM's Egret transport is a tight HOST-paced bit-bang (it acks
    // each byte with a back-to-back bclr/bset of VIA1 PB4 / via_full) and the
    // boost wedged it after the first byte. The real cause was not the boost
    // itself but `viaSync`/`stall` computing E-clock alignment in the BOOSTED
    // clock domain — every VIA-paced pulse came out cacheBoost_× too short.
    // With bus time charged in machine cycles (RbvMemory::viaSync,
    // Moira030Cpu::stall) the transport is timed correctly and the IIsi
    // boots at the shared default boost. See CHANGELOG 2026-07-25.
}
