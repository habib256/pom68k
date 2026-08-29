// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "Q700Cpu.h"
#include "LleSession.h"
#include <cstdlib>

Q700Cpu::Q700Cpu(Q700Memory& mem, const jit::ResolvedConfig& jitConfig,
                 const pom68k::CoreCpuConfig& cpuConfig)
    : MoiraCpu(mem, jit::kGuest68040, jitConfig) {
    // The JIT's generated code makes the peripheral catch-up test inline
    // rather than calling sync() on every instruction.
    jit_.setPeriphDeadline(&periphDeadline_, [](moira::Moira* cpu) {
        static_cast<Q700Cpu*>(cpu)->flushTicks();
    });

    // The Quadra 700 ships a full 68040 (macquadra700.cpp M68040 @ 50/2 MHz),
    // so unlike the Centris the default is the 040 identity + integrated FPU.
    // POM68K_Q700_LC040 forces the LC040 (no hardware FPU) for experiments.
    if (cpuConfig.q700Lc040) {
        setModel(moira::Model::M68LC040);
        setFPUModel(cpuConfig.q700BareFpu ? moira::FPUModel::NONE
                                          : moira::FPUModel::M68882);
    } else {
        setModel(moira::Model::M68040);
        setFPUModel(moira::FPUModel::M68040);
    }
    if (cpuConfig.mmu040Walk) setMmu040AtcArmed(false);
    if (cpuConfig.q700CacheBoost)
        cacheBoost_ = *cpuConfig.q700CacheBoost;
    armIcacheOverlay(icacheMiss_);
}

void Q700Cpu::hardReset() {
    mem_.reset();
    lastPeriphClock_ = getClock();
    periphAccum_ = 0;
    pomIcache.reset();
    jit_.flushAll();
    reset();
}

void Q700Cpu::runCycles(moira::i64 n) {
    // The Eclipse's Egret firmware asked for a host reset (RESET_SYSTEM $11,
    // the Finder's "Restart"). Apply it HERE, at a run boundary, never from
    // inside the memory callback that raised it — the Cpu040 contract
    // (Cpu040::runCycles). The machine has already re-armed its ROM overlay,
    // so this fetch takes the reset vectors out of ROM. The Spike has no such
    // MCU, and its `restartPending_` is never set.
    if (mem_.consumeRestart()) reset();

    // The one and only switch point between the two engines.
    const moira::i64 target = getClock() + n * cacheBoost_;
    if (jit_.enabled()) jit_.executeUntil(target); else executeUntil(target);
    flushTicks();
}

void Q700Cpu::stall(int cycles) {
    if (cycles <= 0) return;
    clock += moira::i64(cycles) * cacheBoost_;
    catchUp();
}

void Q700Cpu::didChangeCACR(moira::u32 value) {
    if (value & 0x0800) pomIcache.reset();
    // CINV/CPUSH is the guest announcing that it just wrote code.
    jit_.flushAll();
}

void Q700Cpu::catchUp() {
    if (clock < periphDeadline_) return;
    flushTicks();
}

void Q700Cpu::flushTicks() {
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

void Q700Cpu::schedulePeriphDeadline() {
    moira::i64 machine = mem_.cyclesToNextEvent();
    if (machine < 1) machine = 1;
    moira::i64 d = machine * cacheBoost_ - periphAccum_;
    if (d < 1) d = 1;
    periphDeadline_ = clock + d;
}

void Q700Cpu::setEngine(int e) {
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
