// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Video ──
// Decodes the Mac Plus 512×342 1-bpp framebuffer (MSB = leftmost pixel,
// 1 = black) from RAM into an RGBA texture. Beam timing / RAM contention is
// M4; this is a per-frame whole-buffer decode (POMIIGS VGC pattern).
// Gate: tests/demo_screenshot.cpp.

#pragma once
#include "MacMemory.h"
#include <cstdint>
#include <vector>

class MacVideo {
public:
    static constexpr int kWidth = 512;
    static constexpr int kHeight = 342;

    const uint32_t* render(const MacMemory& mem) {
        fb_.resize(size_t(kWidth) * kHeight);
        const uint8_t* ram = mem.ram();
        uint32_t base = mem.screenBase();   // VIA PA6 selects main/alternate
        for (int y = 0; y < kHeight; y++) {
            for (int xb = 0; xb < kWidth / 8; xb++) {
                uint8_t bits = ram[base + uint32_t(y) * (kWidth / 8) + uint32_t(xb)];
                for (int b = 0; b < 8; b++) {
                    bool black = (bits >> (7 - b)) & 1;
                    fb_[size_t(y) * kWidth + size_t(xb) * 8 + size_t(b)] =
                        black ? 0xFF000000u : 0xFFFFFFFFu;   // ABGR little-endian → RGBA
                }
            }
        }
        return fb_.data();
    }

    int width() const { return kWidth; }
    int height() const { return kHeight; }

private:
    std::vector<uint32_t> fb_;
};
