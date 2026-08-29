// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "Q630Cpu.h"
#include "LleSession.h"
#include <cstdlib>

Q630Cpu::Q630Cpu(Q630Memory& mem, const jit::ResolvedConfig& jitConfig,
                 const pom68k::CoreCpuConfig& cpuConfig)
    : MoiraCpu(mem, jit::kGuest68040, jitConfig) {
    // The JIT's generated code makes the peripheral catch-up test inline
    // rather than calling sync() on every instruction.
    jit_.setPeriphDeadline(&periphDeadline_, [](moira::Moira* cpu) {
        static_cast<Q630Cpu*>(cpu)->flushTicks();
    });

    // The Quadra 630 ships a full 68040 with its integrated FPU
    // (macquadra630.cpp M68040 @ 33 MHz);
    // POM68K_Q630_LC040 forces the 68LC040 of the LC/Performa 630 and 580.
    if (cpuConfig.q630Lc040) {
        setModel(moira::Model::M68LC040);
        setFPUModel(cpuConfig.q630BareFpu ? moira::FPUModel::NONE
                                          : moira::FPUModel::M68882);
    } else {
        setModel(moira::Model::M68040);
        setFPUModel(moira::FPUModel::M68040);
    }
    if (cpuConfig.mmu040Walk) setMmu040AtcArmed(false);
    if (cpuConfig.q630CacheBoost)
        cacheBoost_ = *cpuConfig.q630CacheBoost;
    armIcacheOverlay(icacheMiss_);
}

void Q630Cpu::hardReset() {
    mem_.reset();
    lastPeriphClock_ = getClock();
    periphAccum_ = 0;
    pomIcache.reset();
    jit_.flushAll();
    reset();
}

void Q630Cpu::runCycles(moira::i64 n) {
    // The Egret/Cuda firmware asked for a host reset (RESET_SYSTEM $11, the
    // Finder's "Restart"). Apply it HERE, at a run boundary, never from
    // inside the memory callback that raised it — same contract as the
    // Duo's PMU wake (MscCpu::runCycles). The machine has already re-armed
    // its ROM overlay, so this fetch takes the reset vectors out of ROM.
    if (mem_.consumeRestart()) reset();

    // The one and only switch point between the two engines.
    const moira::i64 target = getClock() + n * cacheBoost_;
    if (jit_.enabled()) jit_.executeUntil(target); else executeUntil(target);
    flushTicks();
}

void Q630Cpu::stall(int cycles) {
    if (cycles <= 0) return;
    clock += moira::i64(cycles) * cacheBoost_;
    catchUp();
}

void Q630Cpu::didChangeCACR(moira::u32 value) {
    if (value & 0x0800) pomIcache.reset();
    // CINV/CPUSH is the guest announcing that it just wrote code.
    jit_.flushAll();
}

void Q630Cpu::catchUp() {
    if (clock < periphDeadline_) return;
    flushTicks();
}

void Q630Cpu::flushTicks() {
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

void Q630Cpu::schedulePeriphDeadline() {
    moira::i64 machine = mem_.cyclesToNextEvent();
    if (machine < 1) machine = 1;
    moira::i64 d = machine * cacheBoost_ - periphAccum_;
    if (d < 1) d = 1;
    periphDeadline_ = clock + d;
}

void Q630Cpu::setEngine(int e) {
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
