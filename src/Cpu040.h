// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── 68040 / 68LC040 CPU (Moira wrapper, Mac LC 475 / Quadra 605) ──
// Moira at 25 MHz on the MEMCjr/PrimeTime bus (Q5). The Q2-Q4 core provides
// the integer ISA, the 040 MMU (TTR + URP/SRP walks, format $7 faults) and
// the no-FPU format $4 F-line; external /BERR raises through
// Moira::extBusError040. Q8 adds a separate I/D ATC (Moira) and a
// throughput overlay transposed from the 030 Cpu030 model, plus Moira's
// data-bearing 4 KB I/D caches, copyback, snooping and bus-beat timing.
// Timing adjustments: VIA E-clock sync and TurboSCSI wait states via
// Q605Memory::stall().
// Shared wrapper plumbing (bus forwarders, hooks, sync): MoiraCpu.h.

#pragma once
#include "CoreConfig.h"
#include "MoiraCpu.h"
#include "Q605Memory.h"
#include <cstdint>
#include <functional>

class Cpu040 : public pom68k::MoiraCpu<Cpu040, Q605Memory> {
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

    void hardReset();                       // overlay + CPU reset
    void runCycles(moira::i64 n);
    // ── JIT engine (src/jit/POM68K_JIT.md) ─────────────────────────────
    // The second execution engine, ON by default for this validated 68040.
    // setEngine() is the only
    // switch; the GUI routes it through the machine thread's command queue
    // so it always lands between two instructions.
    void setEngine(int e);

    void updateIpl();                       // from the PrimeTime resolver
    void stall(int cycles);                 // `cycles` are machine cycles
    void flushTicks();                      // run peripherals up to `clock`
    int cacheBoost() const { return cacheBoost_; }
    // Bus/wire time (E-clock, wait states) must be measured here, not on the
    // boosted core clock — see SonoraCpu.h.
    moira::i64 machineClock() const { return clock / cacheBoost_; }
    LockstepDebug lockstepDebug() const;
    // Optional lockstep-only event tap. It observes boundaries but does not
    // alter peripheral scheduling or force an extra flush.
    std::function<void(const char*, const LockstepDebug&)> onLockstepEvent;

    // ── Save states (chunk "CPU ") — the Cpu030 wrapper pattern ─────────
    // cacheBoost_/icacheMiss_ are environment tuning, not guest state.
    template <class Ar> void visit(Ar& ar) {
        visitCpuCommon(ar);
        ar(lastPeriphClock_, periphAccum_, periphDeadline_);
    }

private:
    friend pom68k::MoiraCpu<Cpu040, Q605Memory>;
    bool periphStatsOn_ = false;
    long long periphCatchUps_ = 0;
    long long periphFlushes_ = 0;
    long long periphTicks_ = 0;
    long long periphCycles_ = 0;
    void didChangeCACR(moira::u32 value) override;
    void onSyncCharge(int cycles);          // lockstep tap (MoiraCpu::sync)
    void catchUp();
    void schedulePeriphDeadline();

    // Throughput ceiling (Cpu030 pattern). Was pinned at 1 for a long time
    // because "boost 2+ → SCSI=0" on q605_boot_etalon; re-measured
    // 2026-07-25 and that ceiling is GONE — every 040 machine etalon
    // (Q605/LC 475/LC 575/Centris/Quadra, incl. the Cuda-LLE and floppy
    // ones) boots at 2, 4 and 8. Default is now 4, matching the 030 family.
    // Stall / viaSync / syncSwimFromCpu are boost-invariant; tune with
    // POM68K_Q605_CACHE_BOOST. flushTicks() scales Moira cycles back down.
    int cacheBoost_ = 4;
    int icacheMiss_ = 0;                    // POM68K_Q605_ICACHE_MISS
    moira::i64 periphAccum_ = 0;
    moira::i64 periphDeadline_ = 0;
};
