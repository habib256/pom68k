// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Moira M68020 @ 15.6672 MHz on the Mac II GLUE map (functional accuracy).

#pragma once
#include "MoiraSnapshot.h"
#include <cstdint>
#include <string>

class MacIIMemory;

class Cpu020 : public MoiraSnapshot {
public:
    // is030 = the IIx/IIcx/SE-30 variant: a 68030 (built-in PMMU + 68882) on
    // the same Mac II GLUE board, sharing the mac2fdhd ROM. The GLUE still
    // does the 24-bit remap (MacIIMemory::physAddr via VIA2 PB3); the 030's
    // own PMMU stays transparent until the ROM enables it.
    explicit Cpu020(MacIIMemory& mem, bool withFpu = true, bool is030 = false);

    void hardReset();
    void runCycles(moira::i64 n);
    void runUntil(moira::i64 clockTarget);
    void updateIpl();
    void stall(int cycles);
    void flushTicks();
    // No i-cache throughput overlay on this core, so the core clock IS
    // machine time — the accessor exists so bus/wire models (E-clock, the
    // PIC1654S co-step) read the same idiom on every machine (SonoraCpu.h).
    moira::i64 machineClock() const { return clock; }

    // ── Save states (chunk "CPU ") — the Cpu030 wrapper pattern ─────────
    template <class Ar> void visit(Ar& ar) {
        visitCpuCommon(ar);
        ar(lastPeriphClock_);
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
    moira::u16 read16OnReset(moira::u32 addr) const override;
    void write8(moira::u32 addr, moira::u8 v) const override;
    void write16(moira::u32 addr, moira::u16 v) const override;
    void sync(int cycles) override;
    void catchUp();

    MacIIMemory& mem_;
    moira::i64 lastPeriphClock_ = 0;
    static constexpr int kPeriphBatch = 64;
};
