// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── 68LC040 CPU (Moira wrapper, Mac Centris 610/650) ──
// The Cpu040 pattern on the djMEMC/IOSB bus: the Q2-Q4 040 core (integer
// ISA, 040 MMU, ATC overlay) with external /BERR through
// Moira::extBusError040. The Centris 610/650 ship a 68LC040 (no hardware
// FPU) at 20 / 25 MHz; Moira's soft 68882 makes them Finder-usable (the
// Q605 no-FPU precedent). POM68K_CENTRIS_BAREFPU=1 selects true
// FPUModel::NONE (architectural F-line), POM68K_CENTRIS_FPU=1 the full
// 68040 (for the Quadra 610/650 identity).
// Shared wrapper plumbing (bus forwarders, hooks, sync): MoiraCpu.h.
// Gate: tests/centris650_boot_etalon.cpp.

#pragma once
#include "CentrisMemory.h"
#include "CoreConfig.h"
#include "MoiraCpu.h"
#include <cstdint>

class CentrisCpu : public pom68k::MoiraCpu<CentrisCpu, CentrisMemory> {
public:
    explicit CentrisCpu(CentrisMemory& mem,
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
    friend pom68k::MoiraCpu<CentrisCpu, CentrisMemory>;
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
