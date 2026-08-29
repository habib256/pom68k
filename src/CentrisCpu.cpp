// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "CentrisCpu.h"
#include "LleSession.h"
#include <cstdlib>

CentrisCpu::CentrisCpu(CentrisMemory& mem,
                       const jit::ResolvedConfig& jitConfig,
                       const pom68k::CoreCpuConfig& cpuConfig)
    : MoiraCpu(mem, jit::kGuest68040, jitConfig) {
    // The JIT's generated code makes the peripheral catch-up test inline
    // rather than calling sync() on every instruction.
    jit_.setPeriphDeadline(&periphDeadline_, [](moira::Moira* cpu) {
        static_cast<CentrisCpu*>(cpu)->flushTicks();
    });

    // Centris 610/650 = 68LC040. Default to the LC040 identity + Moira's
    // soft 68882 (Finder-usable, the Q605 no-FPU precedent).
    if (cpuConfig.centrisFull040) {
        setModel(moira::Model::M68040);
        setFPUModel(moira::FPUModel::M68040);
    } else {
        setModel(moira::Model::M68LC040);
        setFPUModel(cpuConfig.centrisBareFpu ? moira::FPUModel::NONE
                                             : moira::FPUModel::M68882);
    }
    if (cpuConfig.mmu040Walk) setMmu040AtcArmed(false);
    if (cpuConfig.centrisCacheBoost)
        cacheBoost_ = *cpuConfig.centrisCacheBoost;
    armIcacheOverlay(icacheMiss_);
}

void CentrisCpu::hardReset() {
    mem_.reset();
    lastPeriphClock_ = getClock();
    periphAccum_ = 0;
    pomIcache.reset();
    jit_.flushAll();
    reset();
}

void CentrisCpu::runCycles(moira::i64 n) {
    // The one and only switch point between the two engines.
    const moira::i64 target = getClock() + n * cacheBoost_;
    if (jit_.enabled()) jit_.executeUntil(target); else executeUntil(target);
    flushTicks();
}

void CentrisCpu::stall(int cycles) {
    if (cycles <= 0) return;
    clock += moira::i64(cycles) * cacheBoost_;
    catchUp();
}

void CentrisCpu::didChangeCACR(moira::u32 value) {
    if (value & 0x0800) pomIcache.reset();
    // CINV/CPUSH is the guest announcing that it just wrote code.
    jit_.flushAll();
}

void CentrisCpu::catchUp() {
    if (clock < periphDeadline_) return;
    flushTicks();
}

void CentrisCpu::flushTicks() {
    moira::i64 d = clock - lastPeriphClock_;
    if (d > 0) {
        lastPeriphClock_ = clock;
        periphAccum_ += d;
        int m = int(periphAccum_ / cacheBoost_);
        periphAccum_ -= moira::i64(m) * cacheBoost_;
        if (m) mem_.tick(m);
        schedulePeriphDeadline();
    }
}

void CentrisCpu::schedulePeriphDeadline() {
    moira::i64 machine = mem_.cyclesToNextEvent();
    if (machine < 1) machine = 1;
    moira::i64 d = machine * cacheBoost_ - periphAccum_;
    if (d < 1) d = 1;
    periphDeadline_ = clock + d;
}

void CentrisCpu::setEngine(int e) {
    // This machine's registry, not the process's: the engine lock is
    // part of the promise made by THIS session (2026-08-27).
    if (!pom68k::lle::engineChangeAllowed(mem_.lleRegistry(), e))
        return;
    // setEnabled() already flushes everything; the explicit disarm is belt
    // and braces — a code window left armed while the INTERPRETER runs would
    // have it fetching from a host pointer nobody maintains any more.
    jit_.setEnabled(e != 0);
    pomJitDisarm();
}
