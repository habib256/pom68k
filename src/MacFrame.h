// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Frame clock ──
// Shared per-frame driver: frame = 370 lines × 352 cycles = 130 240 CPU
// cycles @ 7.8336 MHz = 60.1474 Hz. Vblank (VIA CA1) rises at line 342
// (cycle 120 384), the RTC one-second tick every 60 frames. Used by the GUI
// and every headless tool so all of them agree on interrupt phase.

#pragma once
#include "Cpu68k.h"
#include "MacMemory.h"

inline constexpr long kCyclesPerFrame = 130240;
inline constexpr long kVblankStart    = 342 * 352;   // line 342, cycle 120384

struct MacFrameClock {
    long frameNo = 0;
    moira::i64 frameBase = 0;

    void resync(Cpu68k& cpu) {
        frameBase = cpu.getClock() - (cpu.getClock() % kCyclesPerFrame);
    }

    void runFrame(Cpu68k& cpu, MacMemory& mem) {
        cpu.runUntil(frameBase + kVblankStart);
        mem.via().raiseCa1();                        // vblank
        mem.updateIrq();
        cpu.runUntil(frameBase + kCyclesPerFrame);
        frameBase += kCyclesPerFrame;
        if (++frameNo % 60 == 0) mem.tickOneSecond();
    }
};
