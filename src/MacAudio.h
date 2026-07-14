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
// PA2-0 = volume (0-7), PB7 = sound enable (0 = enabled).
// Source of truth: GttMFH; MAME mac128.cpp; DEV.md § Sound.
// Gate: tests/sound_test.cpp (the startup chime is a real decaying tone).

#pragma once
#include "MacMemory.h"
#include <cstdint>
#include <vector>

class MacAudio {
public:
    static constexpr int kSamplesPerFrame = 370;
    static constexpr double kSampleRate = 22254.545;

    // Append this frame's 370 samples (float, -1..1) to `out`, honouring
    // the VIA buffer-select / volume / enable bits.
    void renderFrame(MacMemory& mem, std::vector<float>& out) {
        uint8_t pa = mem.via().portA();
        uint8_t pb = mem.via().portB();
        bool enabled = (pb & 0x80) == 0;           // PB7: 0 = sound on
        int volume = pa & 0x07;                     // PA2-0: 0..7
        // Sound buffers sit at ramTop-$300 (main) / ramTop-$5F00 (alt);
        // PA3 = 1 selects main (GttMFH).
        uint32_t base = (pa & 0x08) ? (MacMemory::kRamSize - 0x0300)
                                    : (MacMemory::kRamSize - 0x5F00);
        const uint8_t* ram = mem.ram();
        float gain = enabled ? (volume / 7.0f) : 0.0f;
        for (int i = 0; i < kSamplesPerFrame; i++) {
            uint8_t s = ram[base + uint32_t(i) * 2];   // even byte = sample
            out.push_back((int(s) - 128) / 128.0f * gain);
        }
    }
};
