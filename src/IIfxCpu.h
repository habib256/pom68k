// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Moira M68030 @ 40 MHz on the Mac IIfx map (functional accuracy) — the
// Cpu020 wrapper pattern on platform #12 (docs/IOP_BRINGUP.md M3). No
// i-cache throughput overlay: the core clock IS machine time.

#pragma once
#include "MoiraSnapshot.h"
#include <cstdint>

class IIfxMemory;

class IIfxCpu : public MoiraSnapshot {
public:
    explicit IIfxCpu(IIfxMemory& mem, bool withFpu = true);

    void hardReset();
    void runCycles(moira::i64 n);
    void runUntil(moira::i64 clockTarget);
    void updateIpl();
    void stall(int cycles);
    void flushTicks();
    moira::i64 machineClock() const { return clock; }

    // ── Save states (chunk "CPU ") — the Cpu020 wrapper pattern ─────────
    template <class Ar> void visit(Ar& ar) {
        visitCpuCommon(ar);
        ar(lastPeriphClock_);
    }

private:
    moira::u8  read8(moira::u32 addr) const override;
    moira::u16 read16(moira::u32 addr) const override;
    // Side-effect-free disassembly reads (the Cpu020 lesson: the live bus
    // mutates SCC/SWIM latches and bus-errors on unmapped I/O).
    moira::u16 read16Dasm(moira::u32 addr) const override;
    moira::u16 read16OnReset(moira::u32 addr) const override;
    void write8(moira::u32 addr, moira::u8 v) const override;
    void write16(moira::u32 addr, moira::u16 v) const override;
    void sync(int cycles) override;
    void catchUp();

    IIfxMemory& mem_;
    moira::i64 lastPeriphClock_ = 0;
    static constexpr int kPeriphBatch = 64;
};
