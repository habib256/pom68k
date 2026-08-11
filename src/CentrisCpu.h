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
// Gate: tests/centris650_boot_etalon.cpp.

#pragma once
#include "MoiraSnapshot.h"
#include "jit/JitEngine.h"
#include <cstdint>

class CentrisMemory;

class CentrisCpu : public MoiraSnapshot {
public:
    explicit CentrisCpu(CentrisMemory& mem);

    void hardReset();
    void runCycles(moira::i64 n);
    // ── JIT engine (src/jit/POM68K_JIT.md) ─────────────────────────────
    // The second execution engine, ON by default for this validated 68040.
    // setEngine() is the only
    // switch; the GUI routes it through the machine thread's command queue
    // so it always lands between two instructions.
    jit::Engine& jit() { return jit_; }
    const jit::Engine& jit() const { return jit_; }
    int  engine() const { return jit_.enabled() ? 1 : 0; }
    void setEngine(int e);

    void updateIpl();
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
    moira::u8  read8(moira::u32 addr) const override;
    moira::u16 read16(moira::u32 addr) const override;
    // Moira's disassembler falls back to read16() unless this is overridden,
    // which sent every disassembly read through the LIVE bus: device registers
    // with read side effects (SCC status latches, IWM state lines) and, on
    // unmapped I/O, a busError() that mutates An/MMU fault state and throws.
    // peek8() is the side-effect-free path the tracers already use.
    moira::u16 read16Dasm(moira::u32 addr) const override;
    void write8(moira::u32 addr, moira::u8 v) const override;
    void write16(moira::u32 addr, moira::u16 v) const override;
    void sync(int cycles) override;
    void didChangeCACR(moira::u32 value) override;
    void catchUp();
    void schedulePeriphDeadline();

    CentrisMemory& mem_;
    jit::Engine jit_;
    moira::i64 lastPeriphClock_ = 0;
    // Same ceiling as Cpu040 (see the note there): the old boost-1 pin was
    // a stale Q605 SCSI symptom, lifted 2026-07-25.
    int cacheBoost_ = 4;
    int icacheMiss_ = 0;
    moira::i64 periphAccum_ = 0;
    moira::i64 periphDeadline_ = 0;
};
