// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Sound (PWM sample buffer) ──
// The Mac Plus fetches one sound word per scan line (370/frame incl.
// vblank) from a buffer near the top of RAM: the even byte is an 8-bit
// sample, the odd byte the disk-speed PWM value (ignored on 800K drives).
// One word/line at 15.6672 MHz / 704 ⇒ 22 254.55 Hz. Output is really
// 1-bit PWM into an integrator; we take the byte as unsigned linear PCM
// (the standard emulator approximation). VIA PA3 selects main/alt buffer,
// PA2-0 = volume (0-7), PB7 = sound enable (0 = enabled) — all three read
// by the beam at each line, which MacMemory::tick latches (soundLine);
// this class only renders them.
// Source of truth: GttMFH; MAME mac128.cpp; DEV.md § Sound.
// Gate: tests/sound_test.cpp (the startup chime is a real decaying tone,
// and a mid-frame buffer write is heard only by the lines still to come).

#pragma once
#include "MacMemory.h"
#include <cstdint>
#include <vector>

class MacAudio {
public:
    static constexpr int kSamplesPerFrame = 370;
    static constexpr double kSampleRate = 22254.545;

    // Append this frame's 370 samples (float, -1..1): each line's latched
    // byte at that line's volume and enable. Call once per frame, at the
    // frame boundary, after the lines were fetched.
    void renderFrame(const MacMemory& mem, std::vector<float>& out) {
        for (int i = 0; i < MacMemory::kSoundLines; i++) {
            const MacMemory::SoundLine& line = mem.soundLine(i);
            const bool enabled = (line.via & 0x80) == 0;    // PB7: 0 = sound on
            const float gain = enabled ? (line.via & 0x07) / 7.0f : 0.0f;
            out.push_back((int(line.sample) - 128) / 128.0f * gain);
        }
    }
};
