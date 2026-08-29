// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── 68040 CPU (Moira wrapper, Macintosh Quadra 630 / LC 580) ──
// The Cpu040 pattern on the F108 bus. The Quadra 630 is a FULL 68040 @
// 33 MHz, so the default is M68040 + its integrated FPU; the LC/Performa
// 630 and 580 siblings ship a 68LC040 — POM68K_Q630_LC040=1 selects it,
// POM68K_Q630_BAREFPU=1 a bare FPUModel::NONE with it.
// Shared wrapper plumbing (bus forwarders, hooks, sync): MoiraCpu.h.
// Gate: tests/q630_boot_etalon.cpp.

#pragma once
#include "CoreConfig.h"
#include "MoiraCpu.h"
#include "Q630Memory.h"
#include <cstdint>

class Q630Cpu : public pom68k::MoiraCpu<Q630Cpu, Q630Memory> {
public:
    explicit Q630Cpu(Q630Memory& mem,
                     const jit::ResolvedConfig& jitConfig,
                     const pom68k::CoreCpuConfig& cpuConfig);

    void hardReset();
    void runCycles(moira::i64 n);
    // ── JIT engine (src/jit/POM68K_JIT.md) ─────────────────────────────
    // The second execution engine, ON by default for this validated 68040.
    // setEngine() is the only
    // switch; the GUI routes it through the machine thread's command queue
    // so it always lands between two instructions.
    void setEngine(int e);

    void stall(int cycles);
    void flushTicks();
    int cacheBoost() const { return cacheBoost_; }
    // Bus/wire time (E-clock, wait states, the PIC1654S co-step) must be
    // measured here, not on the boosted core clock — see SonoraCpu.h.
    moira::i64 machineClock() const { return clock / cacheBoost_; }

    // ── Save states (chunk "CPU ") — the Cpu030 wrapper pattern ─────────
    // cacheBoost_/icacheMiss_ are environment tuning, not guest state.
    template <class Ar> void visit(Ar& ar) {
        visitCpuCommon(ar);
        ar(lastPeriphClock_, periphAccum_, periphDeadline_);
    }

private:
    friend pom68k::MoiraCpu<Q630Cpu, Q630Memory>;
    void didChangeCACR(moira::u32 value) override;
    void catchUp();
    void schedulePeriphDeadline();

    // Same ceiling as Cpu040 (see the note there): the old boost-1 pin was
    // a stale Q605 SCSI symptom, lifted 2026-07-25.
    int cacheBoost_ = 4;
    int icacheMiss_ = 0;
    moira::i64 periphAccum_ = 0;
    moira::i64 periphDeadline_ = 0;
};
