// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── 68040 / 68LC040 CPU (Moira wrapper, Mac LC 475 / Quadra 605) ──
// Moira at 25 MHz on the MEMCjr/PrimeTime bus (Q5). The Q2-Q4 core provides
// the integer ISA, the 040 MMU (TTR + URP/SRP walks, format $7 faults) and
// the no-FPU format $4 F-line; external /BERR raises through
// Moira::extBusError040. Q8 adds a separate I/D ATC (Moira) and a
// throughput/i-cache overlay transposed from the 030 Cpu030 model —
// architectural copyback/snooping stays out of scope.
// Timing adjustments: VIA E-clock sync and TurboSCSI wait states via
// Q605Memory::stall().

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
    void didChangeCACR(moira::u32 value) override;
    void catchUp();

    Q605Memory& mem_;
    moira::i64 lastPeriphClock_ = 0;

    // Throughput ceiling (Cpu030 pattern). Default 1 keeps the Q6.6/Q8
    // boot bit-identical; raise via POM68K_Q605_CACHE_BOOST only after
    // measuring against q605_boot_etalon (boost 4 broke SCSI bring-up).
    // flushTicks() scales Moira cycles back down by cacheBoost_.
    int cacheBoost_ = 1;
    int icacheMiss_ = 0;                    // POM68K_Q605_ICACHE_MISS
    moira::i64 periphAccum_ = 0;
};
