// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Header-only abstract interface so SonyDrive / ScsiDisk can notify a
// mechanical-sound consumer without dragging the miniaudio-based
// FloppySound TU into every test/headless build. Concrete
// implementation: FloppySound. Pattern: POM2 FloppySoundSink.h (itself
// after MAME floppy.cpp floppy_sound_device).

#pragma once
#include <cstdint>

class FloppySoundSink {
public:
    // step() stamps carry **emulated** microseconds so cadence
    // classification survives turbo (POM2 lesson: wall-clock audio
    // frames see gap=0 for every step of a turbo'd seek sweep).
    // kNoStamp asks the consumer to fall back to wall-clock cadence —
    // used by callers with no emulated clock in reach (ScsiDisk).
    static constexpr uint64_t kNoStamp = ~0ull;

    virtual ~FloppySoundSink() = default;
    /// Motor state changed. `withDisk` picks the loaded vs empty spin
    /// sample pair.
    virtual void motor(bool on, bool withDisk) = 0;
    /// Head moved one step. `newTrack` is informational (MAME parity);
    /// `emuMicros` is the emulated-time stamp (or kNoStamp).
    virtual void step(int newTrack, uint64_t emuMicros) = 0;
    /// One-shot mechanical click — disk insert / eject.
    virtual void click() = 0;
};
