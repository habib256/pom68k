// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── 68040 CPU (Moira wrapper, Macintosh Quadra 700) ──
// The Cpu040 pattern on the Quadra 700's discrete bus: the Q2-Q4 040 core
// (integer ISA, 040 MMU, ATC overlay) with external /BERR through
// Moira::extBusError040. The Q700 is a FULL 68040 @ 25 MHz (50 MHz XTAL / 2),
// so the default is M68040 + Moira's soft 68882; POM68K_Q700_LC040=1 selects
// the LC040 and POM68K_Q700_BAREFPU=1 a bare FPUModel::NONE with it.
// Gate: tests/q700_boot_etalon.cpp.

#pragma once
#include "Moira.h"
#include "jit/JitEngine.h"
#include <cstdint>

class Q700Memory;

class Q700Cpu : public moira::Moira {
public:
    explicit Q700Cpu(Q700Memory& mem);

    void hardReset();
    void runCycles(moira::i64 n);
    // ── JIT engine (src/jit/POM68K_JIT.md) ─────────────────────────────
    // The second execution engine, OFF by default. setEngine() is the only
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

private:
    moira::u8  read8(moira::u32 addr) const override;
    moira::u16 read16(moira::u32 addr) const override;
    void write8(moira::u32 addr, moira::u8 v) const override;
    void write16(moira::u32 addr, moira::u16 v) const override;
    void sync(int cycles) override;
    void didChangeCACR(moira::u32 value) override;
    void catchUp();

    Q700Memory& mem_;
    jit::Engine jit_;
    moira::i64 lastPeriphClock_ = 0;
    // Same ceiling as Cpu040 (see the note there): the old boost-1 pin was
    // a stale Q605 SCSI symptom, lifted 2026-07-25.
    int cacheBoost_ = 4;
    int icacheMiss_ = 0;
    moira::i64 periphAccum_ = 0;
};
