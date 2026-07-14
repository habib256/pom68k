// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── 68000 CPU (Moira wrapper) ──
// Integrates Moira (vendored from NeoST, see extern/moira/POM68K_VENDOR.md)
// as the Mac Plus 68000 @ 7.8336 MHz, cycle-exact (MOIRA_PRECISE_TIMING).
// Bus callbacks route through MacMemory; Mac interrupts are all autovectored
// (VIA = level 1, SCC = level 2, programmer's switch = level 4..7).
// Integration pattern: NeoST src/core/Cpu68k.cpp (NeostMoira).
// Gate: tests/cpu_smoke.cpp.

#pragma once
#include "Moira.h"

class MacMemory;

class Cpu68k : public moira::Moira {
public:
    explicit Cpu68k(MacMemory& mem);

    void hardReset();                       // memory overlay + CPU reset
    void runCycles(moira::i64 n);           // execute ≥ n CPU cycles
    void runUntil(moira::i64 clockTarget);  // execute until clock ≥ target
    void updateIpl();                       // recompute IPL from VIA/SCC lines

    // RAM/video bus contention (DEV.md § Timing): during the 512 visible
    // dots of each of the 342 display lines, video owns alternate 4-cycle
    // slots — a CPU RAM access started in a video slot waits for its own.
    // The sound/PWM word fetch steals the last 4 cycles of every line's
    // hblank (all 370 lines). ROM and I/O are never contended.
    // Frame = 370 lines × 352 cycles = 130 240; visible = pos 0-255.
    static int contentionDelay(moira::i64 clock) {
        moira::i64 t = clock;
        for (;;) {                          // a wait can land in the next busy slot
            int f = int(t % 130240);
            int line = f / 352, pos = f % 352;
            int d = 0;
            if (line < 342 && pos < 256) {
                int ph = pos & 7;
                if (ph < 4) d = 4 - ph;
            } else if (pos >= 348) {
                d = 352 - pos;              // sound/PWM fetch slot
            }
            if (!d) return int(t - clock);
            t += d;
        }
    }

private:
    moira::u8  read8(moira::u32 addr) const override;
    moira::u16 read16(moira::u32 addr) const override;
    void write8(moira::u32 addr, moira::u8 v) const override;
    void write16(moira::u32 addr, moira::u16 v) const override;
    void sync(int cycles) override;

    void applyContention(moira::u32 addr) const;
    void catchUp();                         // feed elapsed cycles to peripherals
    void willInterrupt(moira::u8 level) override { irqServed[level & 7]++; }

public:
    long irqServed[8] = {};                 // debug: interrupts taken per level

private:

    MacMemory& mem_;
    moira::i64 lastPeriphClock_ = 0;
};
