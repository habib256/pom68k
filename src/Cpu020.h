// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Moira M68020 @ 15.6672 MHz on the Mac II GLUE map (functional accuracy).

#pragma once
#include "Moira.h"
#include <cstdint>
#include <string>

class MacIIMemory;

class Cpu020 : public moira::Moira {
public:
    explicit Cpu020(MacIIMemory& mem, bool withFpu = true);

    void hardReset();
    void runCycles(moira::i64 n);
    void runUntil(moira::i64 clockTarget);
    void updateIpl();
    void stall(int cycles);
    void flushTicks();

private:
    moira::u8  read8(moira::u32 addr) const override;
    moira::u16 read16(moira::u32 addr) const override;
    moira::u16 read16OnReset(moira::u32 addr) const override;
    void write8(moira::u32 addr, moira::u8 v) const override;
    void write16(moira::u32 addr, moira::u16 v) const override;
    void sync(int cycles) override;
    void catchUp();

    MacIIMemory& mem_;
    moira::i64 lastPeriphClock_ = 0;
    static constexpr int kPeriphBatch = 64;
};
