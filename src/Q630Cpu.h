// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── 68040 CPU (Moira wrapper, Macintosh Quadra 630 / LC 580) ──
// The Cpu040 pattern on the F108 bus. The Quadra 630 is a FULL 68040 @
// 33 MHz, so the default is M68040 + Moira's soft 68882; the LC/Performa
// 630 and 580 siblings ship a 68LC040 — POM68K_Q630_LC040=1 selects it,
// POM68K_Q630_BAREFPU=1 a bare FPUModel::NONE with it.
// Gate: tests/q630_boot_etalon.cpp.

#pragma once
#include "MoiraSnapshot.h"
#include "jit/JitEngine.h"
#include <cstdint>

class Q630Memory;

class Q630Cpu : public MoiraSnapshot {
public:
    explicit Q630Cpu(Q630Memory& mem);

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

    Q630Memory& mem_;
    jit::Engine jit_;
    moira::i64 lastPeriphClock_ = 0;
    // Same ceiling as Cpu040 (see the note there): the old boost-1 pin was
    // a stale Q605 SCSI symptom, lifted 2026-07-25.
    int cacheBoost_ = 4;
    int icacheMiss_ = 0;
    moira::i64 periphAccum_ = 0;
    moira::i64 periphDeadline_ = 0;
};
