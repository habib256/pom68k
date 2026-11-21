// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── 68040 / 68LC040 CPU (Moira wrapper, Mac LC 475 / Quadra 605) ──
// Moira at 25 MHz on the MEMCjr/PrimeTime bus (Q5). The Q2-Q4 core provides
// the integer ISA, the 040 MMU (TTR + URP/SRP walks, format $7 faults) and
// the no-FPU format $4 F-line; external /BERR raises through
// Moira::extBusError040. Q8 adds a separate I/D ATC (Moira) and the
// `cacheBoost_` throughput scaler shared with the 030 wrappers, plus Moira's
// data-bearing 4 KB I/D caches, copyback, snooping and bus-beat timing.
// Timing adjustments: VIA E-clock sync and TurboSCSI wait states via
// Q605Memory::stall().
// Shared wrapper plumbing (bus forwarders, hooks, sync): MoiraCpu.h; the
// 68040 pacing (boost, peripheral deadline, stall, engine lock, save-state
// chunk) shared with the three other 040 boards: Moira040Cpu.h. What stays
// here is the Q605 identity knobs and the lockstep / peripheral-stats taps.

#pragma once
#include "CoreConfig.h"
#include "Moira040Cpu.h"
#include "Q605Memory.h"
#include <cstdint>
#include <functional>

class Cpu040 : public pom68k::Moira040Cpu<Cpu040, Q605Memory> {
public:
    // Diagnostic state deliberately kept separate from save states.  The
    // lockstep gate uses it to catch an IRQ/pacing divergence before it is
    // reflected in architectural registers or RAM.
    struct LockstepDebug {
        moira::i64 clock = 0;
        moira::i64 lastPeriphClock = 0;
        moira::i64 periphAccum = 0;
        moira::i64 periphDeadline = 0;
        moira::i64 iplChangeClock = 0;
        moira::i64 iplChangeClockPrev = 0;
        int flags = 0;
        int iplDeferred = 0;
        int irqDelay = 0;
        moira::u8 iplPin = 0;
        moira::u8 iplSampled = 0;
        moira::u8 iplPrev = 0;
    };

    explicit Cpu040(Q605Memory& mem,
                    const jit::ResolvedConfig& jitConfig,
                    const pom68k::CoreCpuConfig& cpuConfig,
                    const pom68k::CoreDiagnosticConfig& diagnostics);
    ~Cpu040();

    void updateIpl();                       // from the PrimeTime resolver
    LockstepDebug lockstepDebug() const;
    // Optional lockstep-only event tap. It observes boundaries but does not
    // alter peripheral scheduling or force an extra flush.
    std::function<void(const char*, const LockstepDebug&)> onLockstepEvent;

private:
    friend pom68k::MoiraCpu<Cpu040, Q605Memory>;
    friend pom68k::Moira040Cpu<Cpu040, Q605Memory>;
    bool periphStatsOn_ = false;
    long long periphCatchUps_ = 0;
    long long periphFlushes_ = 0;
    long long periphTicks_ = 0;
    long long periphCycles_ = 0;
    void onSyncCharge(int cycles);          // lockstep tap (MoiraCpu::sync)
    void onPeriphCatchUp() { if (periphStatsOn_) periphCatchUps_++; }
    void onPeriphFlushed(int machineCycles);
    void onJitDeadline();
};
