// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "Cpu040.h"
#include "LleSession.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

Cpu040::Cpu040(Q605Memory& mem, const jit::ResolvedConfig& jitConfig,
               const pom68k::CoreCpuConfig& cpuConfig,
               const pom68k::CoreDiagnosticConfig& diagnostics)
    : MoiraCpu(mem, jit::kGuest68040, jitConfig) {
    periphStatsOn_ = diagnostics.peripheralStats;
    // The JIT's generated code makes the peripheral catch-up test inline
    // rather than calling sync() on every instruction.
    jit_.setPeriphDeadline(&periphDeadline_, [](moira::Moira* cpu) {
        auto& c = *static_cast<Cpu040*>(cpu);
        if (c.onLockstepEvent) c.onLockstepEvent("sync", c.lockstepDebug());
        c.flushTicks();
    });

    // Q6.6: model the full 68040 with its integrated FPU, matching the MAME golden
    // oracle `macqd605` (macquadra605.cpp:158 `M68040(...)`; only its
    // lc475/lc575 variants use M68LC040). In Moira M68040 and M68LC040 are
    // identical except the FPU-availability bit, so this only turns the FPU
    // on. With it present, Mac OS 8.1 runs the ROM's FPU init
    // (`$408E9AC0 fmove.l fpcr,D0`) and boots.
    //
    // POM68K_Q605_NOFPU selects the LC 475 CPU identity (M68LC040) but keeps
    // Moira's external-68882 semantics as a SoftwareFPU-equivalent. Bare
    // FPUModel::NONE still
    // reaches SysError 90 (dsNoFPU): Mac OS installs PACK 4's F-line glue,
    // which does not replace FPSP for raw 040 FPU opcodes. True NONE + FPSP
    // remains a follow-up; the soft-FPU path is what makes LC040 Finder-usable.
    if (cpuConfig.q605Fpu != pom68k::Q605FpuMode::Integrated) {
        setModel(moira::Model::M68LC040);
        if (cpuConfig.q605Fpu == pom68k::Q605FpuMode::None) {
            // LLE step 5: BARE 68LC040 — no FPU at all. F2xx opcodes take
            // the architectural vector-11 format-$4 frame; the ROM's own
            // FPU probe must conclude "absent" and select the no-FPU
            // UniversalInfo, like a real LC 475.
            setFPUModel(moira::FPUModel::NONE);
        } else {
            setFPUModel(moira::FPUModel::M68882);
        }
    } else {
        setModel(moira::Model::M68040);
        setFPUModel(moira::FPUModel::M68040);
    }

    // Q8: walk-per-access comparison mode (disables the I/D ATC overlay).
    if (cpuConfig.mmu040Walk) setMmu040AtcArmed(false);

    if (cpuConfig.q605CacheBoost)
        cacheBoost_ = *cpuConfig.q605CacheBoost;
    if (cpuConfig.q605IcacheMiss)
        icacheMiss_ = *cpuConfig.q605IcacheMiss;
    // This remains only the legacy throughput scaler. Moira's distinct
    // PomCache040 model owns architectural I/D contents and coherency.
    armIcacheOverlay(icacheMiss_);
}

void Cpu040::hardReset() {
    mem_.reset();
    lastPeriphClock_ = getClock();
    periphAccum_ = 0;
    schedulePeriphDeadline();
    pomIcache.reset();
    jit_.flushAll();
    reset();                                 // SSP/PC from $0 (ROM overlay)
}

void Cpu040::runCycles(moira::i64 n) {
    // The Egret/Cuda firmware asked for a host reset (RESET_SYSTEM $11, the
    // Finder's "Restart"). Apply it HERE, at a run boundary, never from
    // inside the memory callback that raised it — same contract as the
    // Duo's PMU wake (MscCpu::runCycles). The machine has already re-armed
    // its ROM overlay, so this fetch takes the reset vectors out of ROM.
    if (mem_.consumeRestart()) reset();

    // n is a peripheral (machine) cycle budget; run cacheBoost_× more Moira
    // cycles so hot i-cache-resident code keeps up with a real 040 without
    // derailing ASC/VIA pacing (same contract as Cpu030).
    // The one and only switch point between the two engines.
    const moira::i64 target = getClock() + n * cacheBoost_;
    if (jit_.enabled()) jit_.executeUntil(target); else executeUntil(target);
    flushTicks();
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
    d.iplChangeClock = iplChangeClock;
    d.iplChangeClockPrev = iplChangeClockPrev;
    d.flags = flags;
    d.iplDeferred = iplDeferred;
    d.irqDelay = irqDelay;
    d.iplPin = ipl;
    d.iplSampled = reg.ipl;
    d.iplPrev = iplPrev;
    return d;
}

void Cpu040::stall(int cycles) {
    // Wait states are specified in machine cycles (VIA E-clock, SWIM +5).
    // Scale into Moira time so flushTicks() still yields `cycles` of
    // peripheral time under cacheBoost_ > 1.
    if (cycles <= 0) return;
    clock += moira::i64(cycles) * cacheBoost_;
    catchUp();
}

void Cpu040::didChangeCACR(moira::u32 value) {
    // 040 CACR: bit 15 = enable i-cache, bit 11 = clear i-cache (approx.
    // the strobes System uses). Flush the throughput model conservatively.
    if (value & 0x0800) pomIcache.reset();
    // CINV/CPUSH is the guest announcing that it just wrote code.
    jit_.flushAll();
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

void Cpu040::catchUp() {
    if (periphStatsOn_) periphCatchUps_++;
    if (clock < periphDeadline_) return;
    flushTicks();
}

void Cpu040::schedulePeriphDeadline() {
    const moira::i64 machine = std::max(1, mem_.cyclesToNextEvent());
    moira::i64 d = machine * cacheBoost_ - periphAccum_;
    if (d < 1) d = 1;
    periphDeadline_ = clock + d;
}

void Cpu040::flushTicks() {
    moira::i64 d = clock - lastPeriphClock_;
    if (d > 0) {
        lastPeriphClock_ = clock;
        // Scale Moira cycles down to machine cycles (Cpu030::flushTicks).
        periphAccum_ += d;
        int m = int(periphAccum_ / cacheBoost_);
        periphAccum_ -= moira::i64(m) * cacheBoost_;
        if (periphStatsOn_) {
            periphFlushes_++;
            if (m) { periphTicks_++; periphCycles_ += m; }
        }
        if (m) mem_.tick(m);
        schedulePeriphDeadline();
        if (onLockstepEvent) onLockstepEvent("flush", lockstepDebug());
    }
}

void Cpu040::setEngine(int e) {
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
