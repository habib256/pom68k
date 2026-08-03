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

    // `onSlice` runs at each subdivision of the DISPLAY portion, so a
    // raster decoder can catch the beam up mid-frame (VideoBeam.h).
    //
    // Subdividing is safe by construction: `runUntil(t)` is "execute while
    // clock < t", so a chain of increasing targets ending at the same value
    // executes exactly the instructions one call would, and the last target
    // IS `frameBase + kVblankStart` — the vblank edge cannot move. The
    // cycle-exact contention model reads the ABSOLUTE clock
    // (Cpu68k::contentionDelay), not a slice-relative one, so it is
    // unaffected too. This machine's timing is the reference every accuracy
    // claim rests on; nothing here is allowed to perturb it.
    template <class F>
    void runFrame(Cpu68k& cpu, MacMemory& mem, F&& onSlice) {
        constexpr int kSlices = 16;
        for (int i = 1; i <= kSlices; i++) {
            cpu.runUntil(frameBase + kVblankStart * i / kSlices);
            onSlice();
        }
        mem.via().raiseCa1();                        // vblank
        mem.updateIrq();
        cpu.runUntil(frameBase + kCyclesPerFrame);
        onSlice();                                   // the blanking tail
        frameBase += kCyclesPerFrame;
        // The one-second RTC tick lives in MacMemory::tick() on a CPU-cycle
        // accumulator; frameNo stays for VBL phase only.
        ++frameNo;
    }

    // Headless tools and gates that have no decoder to feed.
    void runFrame(Cpu68k& cpu, MacMemory& mem) {
        runFrame(cpu, mem, [] {});
    }
};
