// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── 68LC040 CPU (Moira wrapper, Mac LC 475 / Quadra 605) ──
// Moira Model::M68LC040 at 25 MHz on the MEMCjr/PrimeTime bus (Q5).
// The Q2-Q4 core provides the integer ISA, the 040 MMU (TTR + URP/SRP
// walks, format $7 faults) and the no-FPU format $4 F-line; external
// /BERR (unmapped I/O) raises through Moira::extBusError040. Timing is
// functional: the only adjustments are the VIA E-clock sync and the
// TurboSCSI wait states, applied by Q605Memory through stall(). No
// cache model yet (the 030's PomIcache overlay can be transposed for
// perf later — TODO § Q8).

#pragma once
#include "Moira.h"
#include <cstdint>

class Q605Memory;

class Cpu040 : public moira::Moira {
public:
    explicit Cpu040(Q605Memory& mem);

    void hardReset();                       // overlay + CPU reset
    void runCycles(moira::i64 n);
    void updateIpl();                       // from the PrimeTime resolver
    void stall(int cycles);
    void flushTicks();                      // run peripherals up to `clock`

private:
    moira::u8  read8(moira::u32 addr) const override;
    moira::u16 read16(moira::u32 addr) const override;
    void write8(moira::u32 addr, moira::u8 v) const override;
    void write16(moira::u32 addr, moira::u16 v) const override;
    void sync(int cycles) override;
    void catchUp();

    Q605Memory& mem_;
    moira::i64 lastPeriphClock_ = 0;
};
