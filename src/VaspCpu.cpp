// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "VaspCpu.h"
#include <algorithm>
#include <cstdlib>

VaspCpu::VaspCpu(VaspMemory& mem, const jit::ResolvedConfig& jitConfig,
                 const pom68k::CoreCpuConfig& cpuConfig,
                 bool withFpu)
    : MoiraCpu(mem, jit::kGuest68030, jitConfig) {
    setModel(moira::Model::M68030);
    setFPUModel(withFpu ? moira::FPUModel::M68882 : moira::FPUModel::NONE);
    if (cpuConfig.cacheBoost) cacheBoost_ = *cpuConfig.cacheBoost;
    if (cpuConfig.icacheMiss) icacheMiss_ = *cpuConfig.icacheMiss;
    boost_ = cacheBoost_;
    if (cpuConfig.floppyBoostGate)
        floppyGate_ = *cpuConfig.floppyBoostGate;
    if (cpuConfig.cacr030Flush)
        cacrFlushPolicy_ = *cpuConfig.cacr030Flush ? 1 : 0;
    armIcacheOverlay(icacheMiss_);
}

void VaspCpu::hardReset() {
    mem_.reset();
    lastPeriphClock_ = getClock();
    pomIcache.reset();
    jit_.flushAll();
    reset();                       // SSP/PC from $0 (ROM via overlay)
}

void VaspCpu::didChangeCACR(moira::u32 value) {
    pomInvalidateIcache030(value);         // CI whole / CEI selected longword
    // The CACR SMC hint, retired on this board the way the V8 retired it:
    // VaspMemory::kJitStoreInventoryComplete documents (and
    // store_inventory_test pins) that every store into RAM passes
    // CodeGuard::note(), so the drop-all-generated-code flush on the CI/CEI
    // strobes protected nothing CodeGuard does not already catch precisely.
    // POM68K_JIT_030_CACR_FLUSH keeps its three values here as on the V8:
    // unset = the board's own answer, 1 = force the hint back on (the
    // measurement instrument), 0 = force it off.
    const bool flush = cacrFlushPolicy_ < 0
        ? !VaspMemory::kJitStoreInventoryComplete : cacrFlushPolicy_ != 0;
    if ((value & 0x0C) && flush) jit_.flushAll();
}

void VaspCpu::runCycles(moira::i64 n) {
    // The Egret/Cuda firmware asked for a host reset (RESET_SYSTEM $11, the
    // Finder's "Restart"). Apply it HERE, at a run boundary, never from
    // inside the memory callback that raised it — same contract as the
    // Duo's PMU wake (MscCpu::runCycles). The machine has already re-armed
    // its ROM overlay, so this fetch takes the reset vectors out of ROM.
    if (mem_.consumeRestart()) reset();

    // Deliver the machine-cycle budget in bounded chunks: the floppy gate
    // can flip boost_ mid-slice, and a single core-clock target computed
    // with the old ratio would mis-deliver machine time (Cpu030 pattern).
    const moira::i64 end = machineClock() + n;
    while (machineClock() < end && !isHalted()) {
        const moira::i64 chunk =
            std::min<moira::i64>(end - machineClock(), 4096);
        const moira::i64 target = getClock() + chunk * boost_;
        if (jit_.enabled()) jit_.executeUntil(target); else executeUntil(target);
        flushTicks();
    }
    flushTicks();
}

void VaspCpu::stall(int cycles) {
    if (cycles <= 0) return;
    clock += moira::i64(cycles) * boost_;        // machine cycles → core clock
    catchUp();
}

void VaspCpu::catchUp() {
    if (clock < periphDeadline_) return;
    flushTicks();
}

void VaspCpu::flushTicks() {
    moira::i64 d = clock - lastPeriphClock_;
    if (d <= 0) return;
    lastPeriphClock_ = clock;
    periphAccum_ += d;
    int m = int(periphAccum_ / boost_);
    periphAccum_ -= moira::i64(m) * boost_;
    if (m) mem_.tick(m);
    pollBoostGate();
    schedulePeriphDeadline();
}

// The Cpu030 floppy boost gate: re-evaluated at a settled point, rebased
// so machineClock() never jumps (rationale in Cpu030.h/.cpp).
void VaspCpu::pollBoostGate() {
    const int want =
        (floppyGate_ && mem_.floppyStreaming()) ? 1 : cacheBoost_;
    if (want == boost_) return;
    machineBase_ += (clock - clockBase_) / boost_;
    clockBase_ = clock;
    periphAccum_ = periphAccum_ * want / boost_;
    boost_ = want;
}

void VaspCpu::schedulePeriphDeadline() {
    // min(next observable machine-cycle bound, the historical batch): the
    // cap keeps every transport without a deadline API (ADB PIC, IOPs) at
    // exactly its former cadence, so exactness can only refine, never
    // coarsen (TODO § 4, extension inventory 2026-08-04).
    moira::i64 machine = mem_.cyclesToNextEvent();
    if (machine < 1) machine = 1;
    moira::i64 d = machine * boost_ - periphAccum_;
    if (d > kPeriphBatch) d = kPeriphBatch;
    if (d < 1) d = 1;
    periphDeadline_ = clock + d;
}
