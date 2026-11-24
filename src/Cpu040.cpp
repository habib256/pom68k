// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "Cpu040.h"
#include <cstdio>

Cpu040::Cpu040(Q605Memory& mem, const jit::ResolvedConfig& jitConfig,
               const pom68k::CoreCpuConfig& cpuConfig,
               const pom68k::CoreDiagnosticConfig& diagnostics)
    : Moira040Cpu(mem, jitConfig) {
    periphStatsOn_ = diagnostics.peripheralStats;

    // Q6.6: model the full 68040 with its integrated FPU, matching the MAME golden
    // oracle `macqd605` (macquadra605.cpp:158 `M68040(...)`; only its
    // lc475/lc575 variants use M68LC040). In Moira M68040 and M68LC040 are
    // identical except the FPU-availability bit, so this only turns the FPU
    // on. With it present, Mac OS 8.1 runs the ROM's FPU init
    // (`$408E9AC0 fmove.l fpcr,D0`) and boots.
    //
    // POM68K_Q605_NOFPU=1 selects the LC 475 CPU identity (M68LC040) with
    // Moira's external-68882 semantics attached; POM68K_Q605_NOFPU=2 is
    // the silicon: bare FPUModel::NONE, which boots to the Finder
    // (q605_barefpu_boot_etalon; the earlier SysError 90 is gone). The ROM's
    // own FPU probe must then conclude "absent" and select the no-FPU
    // UniversalInfo, like a real LC 475.
    selectIdentity(cpuConfig.q605Fpu != pom68k::Q605FpuMode::Integrated,
                   cpuConfig.q605Fpu == pom68k::Q605FpuMode::None,
                   cpuConfig.mmu040Walk);
    setCacheBoost(cpuConfig.q605CacheBoost);
}

void Cpu040::updateIpl() {
    setIPL(moira::u8(mem_.iplLevel()));
    if (onLockstepEvent) onLockstepEvent("ipl", lockstepDebug());
}

Cpu040::LockstepDebug Cpu040::lockstepDebug() const {
    LockstepDebug d;
    d.clock = clock;
    d.lastPeriphClock = lastPeriphClock_;
    d.periphAccum = periphAccum_;
    d.periphDeadline = periphDeadline_;
    // Dirk's core samples the IPL pins without the fork's deferred-IPL
    // history (NeoST's setIplDelay); those fields stay zero.
    d.iplChangeClock = 0;
    d.iplChangeClockPrev = 0;
    d.flags = flags;
    d.iplDeferred = 0;
    d.irqDelay = irqDelay;
    d.iplPin = ipl;
    d.iplSampled = reg.ipl;
    d.iplPrev = 0;
    return d;
}

Cpu040::~Cpu040() {
    if (!periphStatsOn_) return;
    std::fprintf(stderr,
        "[periph] catchUp=%lld flushTicks=%lld mem.tick=%lld "
        "machine-cycles=%lld (%.2f cycles per tick call)\n",
        periphCatchUps_, periphFlushes_, periphTicks_, periphCycles_,
        periphTicks_ ? double(periphCycles_) / double(periphTicks_) : 0.0);
}

// The per-charge tap MoiraCpu::sync() dispatches through, fired before
// catchUp() exactly as the pre-CRTP sync() did.
void Cpu040::onSyncCharge(int /*cycles*/) {
    if (onLockstepEvent) onLockstepEvent("sync", lockstepDebug());
}

void Cpu040::onPeriphFlushed(int machineCycles) {
    if (periphStatsOn_) {
        periphFlushes_++;
        if (machineCycles) { periphTicks_++; periphCycles_ += machineCycles; }
    }
    if (onLockstepEvent) onLockstepEvent("flush", lockstepDebug());
}

void Cpu040::onJitDeadline() {
    if (onLockstepEvent) onLockstepEvent("sync", lockstepDebug());
    flushTicks();
}
