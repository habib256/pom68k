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
#include "Moira.h"
#include <cstdint>

class CentrisMemory;

class CentrisCpu : public moira::Moira {
public:
    explicit CentrisCpu(CentrisMemory& mem);

    void hardReset();
    void runCycles(moira::i64 n);
    void updateIpl();
    void stall(int cycles);
    void flushTicks();
    int cacheBoost() const { return cacheBoost_; }

private:
    moira::u8  read8(moira::u32 addr) const override;
    moira::u16 read16(moira::u32 addr) const override;
    void write8(moira::u32 addr, moira::u8 v) const override;
    void write16(moira::u32 addr, moira::u16 v) const override;
    void sync(int cycles) override;
    void didChangeCACR(moira::u32 value) override;
    void catchUp();

    CentrisMemory& mem_;
    moira::i64 lastPeriphClock_ = 0;
    int cacheBoost_ = 1;
    int icacheMiss_ = 0;
    moira::i64 periphAccum_ = 0;
};
