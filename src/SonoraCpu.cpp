// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "SonoraCpu.h"
#include <cstdlib>

SonoraCpu::SonoraCpu(SonoraMemory& mem, const jit::ResolvedConfig& jitConfig,
                     const pom68k::CoreCpuConfig& cpuConfig,
                     bool withFpu)
    : MoiraCpu(mem, jit::kGuest68030, jitConfig) {
    setModel(moira::Model::M68030);
    setFPUModel(withFpu ? moira::FPUModel::M68882 : moira::FPUModel::NONE);
    if (cpuConfig.cacheBoost) cacheBoost_ = *cpuConfig.cacheBoost;
    if (cpuConfig.icacheMiss) icacheMiss_ = *cpuConfig.icacheMiss;
    armIcacheOverlay(icacheMiss_);
}

void SonoraCpu::hardReset() {
    mem_.reset();
    lastPeriphClock_ = getClock();
    pomIcache.reset();
    jit_.flushAll();
    reset();                       // SSP/PC from $0 (ROM via overlay)
}

void SonoraCpu::didChangeCACR(moira::u32 value) {
    pomInvalidateIcache030(value);         // CI whole / CEI selected longword
    jit_.flushAll();                       // SMC hint, as on every wrapper
}

void SonoraCpu::runCycles(moira::i64 n) {
    // The Egret/Cuda firmware asked for a host reset (RESET_SYSTEM $11, the
    // Finder's "Restart"). Apply it HERE, at a run boundary, never from
    // inside the memory callback that raised it — same contract as the
    // Duo's PMU wake (MscCpu::runCycles). The machine has already re-armed
    // its ROM overlay, so this fetch takes the reset vectors out of ROM.
    if (mem_.consumeRestart()) reset();

    const moira::i64 target = getClock() + n * cacheBoost_;
    if (jit_.enabled()) jit_.executeUntil(target); else executeUntil(target);
    flushTicks();
}

void SonoraCpu::stall(int cycles) {
    if (cycles <= 0) return;
    clock += moira::i64(cycles) * cacheBoost_;   // machine cycles → core clock
    catchUp();
}

void SonoraCpu::catchUp() {
    if (clock < periphDeadline_) return;
    flushTicks();
}

void SonoraCpu::flushTicks() {
    moira::i64 d = clock - lastPeriphClock_;
    if (d <= 0) return;
    lastPeriphClock_ = clock;
    periphAccum_ += d;
    int m = int(periphAccum_ / cacheBoost_);
    periphAccum_ -= moira::i64(m) * cacheBoost_;
    if (m) mem_.tick(m);
    schedulePeriphDeadline();
}

void SonoraCpu::schedulePeriphDeadline() {
    // min(next observable machine-cycle bound, the historical batch): the
    // cap keeps every transport without a deadline API (ADB PIC, IOPs) at
    // exactly its former cadence, so exactness can only refine, never
    // coarsen (TODO § 4, extension inventory 2026-08-04).
    moira::i64 machine = mem_.cyclesToNextEvent();
    if (machine < 1) machine = 1;
    moira::i64 d = machine * cacheBoost_ - periphAccum_;
    if (d > kPeriphBatch) d = kPeriphBatch;
    if (d < 1) d = 1;
    periphDeadline_ = clock + d;
}
