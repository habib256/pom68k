// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "MscCpu.h"
#include <cstdlib>

MscCpu::MscCpu(MscMemory& mem, const jit::ResolvedConfig& jitConfig,
               const pom68k::CoreCpuConfig& cpuConfig,
               bool withFpu)
    : MoiraCpu(mem, mem.is040() ? jit::kGuest68040 : jit::kGuest68030, jitConfig) {
    setModel(mem.is040() ? moira::Model::M68LC040 : moira::Model::M68030);
    setFPUModel(withFpu && !mem.is040() ? moira::FPUModel::M68882 : moira::FPUModel::NONE);
    if (mem.is040()) {
        icacheMiss_ = 0;
        if (cpuConfig.mmu040Walk) setMmu040AtcArmed(false);
    }
    if (cpuConfig.cacheBoost) cacheBoost_ = *cpuConfig.cacheBoost;
    boost_ = cacheBoost_;
    floppyGate_ = cpuConfig.floppyBoostGate.value_or(true);
    if (cpuConfig.icacheMiss) icacheMiss_ = *cpuConfig.icacheMiss;
    eventDriven_ = cpuConfig.duoEventDriven;
    if (cpuConfig.cacr030Flush)
        cacrFlushPolicy_ = *cpuConfig.cacr030Flush ? 1 : 0;
    armIcacheOverlay(icacheMiss_);
}

void MscCpu::hardReset() {
    mem_.reset();
    pollFloppyGate();
    lastPeriphClock_ = getClock();
    pomIcache.reset();
    jit_.flushAll();
    reset();                       // SSP/PC from $0 (ROM via overlay)
}

void MscCpu::didChangeCACR(moira::u32 value) {
    if (mem_.is040()) {
        if (value & 0x0800) pomIcache.reset();
        jit_.flushAll();
        return;
    }
    pomInvalidateIcache030(value);
    // The CACR SMC hint, retired behind this board's own audited store
    // inventory (MscMemory::kJitStoreInventoryComplete, pinned by
    // store_inventory_test): every RAM store already crosses
    // CodeGuard::note(). POM68K_JIT_030_CACR_FLUSH keeps its three
    // values as on V8/VASP/RBV.
    const bool flush = cacrFlushPolicy_ < 0
        ? !MscMemory::kJitStoreInventoryComplete : cacrFlushPolicy_ != 0;
    if ((value & 0x0C) && flush) jit_.flushAll();
}

void MscCpu::runCycles(moira::i64 n) {
    // PMU wake (port G bit 5 falling) requested a CPU reset — apply it at
    // this safe boundary, never from inside a memory callback.
    if (mem_.consumeWakeReset()) reset();
    if (mem_.isPb150()) {
        // Like Cpu030: deliver machine time across a floppy gate transition,
        // rather than applying one old throughput ratio to the whole frame.
        const moira::i64 end = machineClock() + n;
        while (machineClock() < end && !isHalted()) {
            const moira::i64 chunk = std::min<moira::i64>(end - machineClock(), 4096);
            const moira::i64 target = getClock() + chunk * boost_;
            if (jit_.enabled()) jit_.executeUntil(target); else executeUntil(target);
            flushTicks();
        }
    } else {
        const moira::i64 target = getClock() + n * boost_;
        if (jit_.enabled()) jit_.executeUntil(target); else executeUntil(target);
    }
    flushTicks();
}

void MscCpu::stall(int cycles) {
    if (cycles <= 0) return;
    clock += moira::i64(cycles) * boost_;
    catchUp();
}

void MscCpu::catchUp() {
    if (clock < periphDeadline_) return;
    flushTicks();
}

// min(next observable device bound, the historical batch), in CORE cycles:
// the bound is machine cycles, and this wrapper runs cacheBoost_× faster, so
// it has to be scaled up — with the fractional remainder already banked in
// periphAccum_ subtracted, exactly as Cpu030/Cpu040 do. Bus time stays on
// the machine counter (pom68k-rbv-iisi-icache-egret); this only decides WHEN
// to enter the fan-out, never how much time the devices are given.
void MscCpu::schedulePeriphDeadline() {
    // OPT-IN for the same measured reason as the Mac II twin (see the long
    // note in Cpu020.cpp): duo230_boot_etalon 109.49 s with the deadline
    // against 100.50 s on the historical batch, **+9.0 %**, same Finder.
    // The PG&E's 68HC05 binds at ~16 machine cycles against a batch of 128,
    // so this board pays 8× more fan-out entries for exactness no gate can
    // currently see — and the Duo's product contract (§ 0·A) is "usable on
    // a Pi 400", which is the tier least able to afford it.
    if (!eventDriven_) { periphDeadline_ = clock + kPeriphBatch; return; }

    moira::i64 machine = mem_.cyclesToNextEvent();
    if (machine < 1) machine = 1;
    moira::i64 d = machine * boost_ - periphAccum_;
    if (d < 1) d = 1;
    if (d > kPeriphBatch) d = kPeriphBatch;
    periphDeadline_ = clock + d;
}

void MscCpu::flushTicks() {
    moira::i64 d = clock - lastPeriphClock_;
    if (d <= 0) return;
    lastPeriphClock_ = clock;
    periphAccum_ += d;
    int m = int(periphAccum_ / boost_);
    periphAccum_ -= moira::i64(m) * boost_;
    if (m) mem_.tick(m);
    pollFloppyGate();
    schedulePeriphDeadline();
}

void MscCpu::pollFloppyGate() {
    if (!mem_.isPb150()) return;
    const int want = floppyGate_ && mem_.internalDrive().motorOn() ? 1 : cacheBoost_;
    if (want == boost_) return;
    machineBase_ += (clock - clockBase_) / boost_;
    clockBase_ = clock;
    periphAccum_ = periphAccum_ * want / boost_;
    boost_ = want;
}
