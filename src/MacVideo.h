// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Video ──
// Decodes the Mac Plus 512×342 1-bpp framebuffer (MSB = leftmost pixel,
// 1 = black) from RAM into an RGBA texture.
//
// Raster decode (LLE_VS_HLE §1.1): each row is rendered once, when the beam
// scans it. The Plus is the machine where that costs nothing extra to get
// right — its beam is already modelled, because the VIA PB6 "beam in
// display portion" bit reads it (MacMemory::readB), so `raster()` reuses
// exactly that position rather than inventing a second one. `render()`
// keeps the whole-frame path for stills and tests.
// Gates: tests/demo_screenshot.cpp, tests/video_beam_test.cpp.

#pragma once
#include "MacMemory.h"
#include "VideoBeam.h"
#include <cstdint>
#include <vector>

class MacVideo {
public:
    static constexpr int kWidth = 512;
    static constexpr int kHeight = 342;

    // Whole frame, against the framebuffer as it stands now.
    const uint32_t* render(const MacMemory& mem) {
        fb_.resize(size_t(kWidth) * kHeight);
        decodeRows(mem, 0, kHeight);
        return fb_.data();
    }

    // Advance the beam off the machine's own clock-derived position and
    // decode the rows it has crossed. Call it as often as convenient: more
    // often is finer, never wrong.
    const uint32_t* raster(const MacMemory& mem) {
        if (fb_.size() != size_t(kWidth) * kHeight) {
            fb_.assign(size_t(kWidth) * kHeight, 0xFFFFFFFFu);
            beam_.restartFrame();
        }
        beam_.setGeometry(MacMemory::frameCycles(),
                          MacMemory::frameActiveCycles(),
                          MacMemory::frameTotalLines(), kHeight);
        beam_.setPos(mem.framePos(), mem.frameCount());
        beam_.pumpRows([&](int a, int b) { decodeRows(mem, a, b); });
        return fb_.data();
    }

    // Render rows [y0, y1) of the 1-bpp framebuffer into the surface.
    void decodeRows(const MacMemory& mem, int y0, int y1) {
        if (fb_.size() < size_t(kWidth) * kHeight) return;
        y0 = y0 < 0 ? 0 : y0;
        y1 = y1 > kHeight ? kHeight : y1;
        const uint8_t* ram = mem.ram();
        uint32_t base = mem.screenBase();   // VIA PA6 selects main/alternate
        for (int y = y0; y < y1; y++) {
            for (int xb = 0; xb < kWidth / 8; xb++) {
                uint8_t bits = ram[base + uint32_t(y) * (kWidth / 8) + uint32_t(xb)];
                for (int b = 0; b < 8; b++) {
                    bool black = (bits >> (7 - b)) & 1;
                    fb_[size_t(y) * kWidth + size_t(xb) * 8 + size_t(b)] =
                        black ? 0xFF000000u : 0xFFFFFFFFu;   // ABGR little-endian → RGBA
                }
            }
        }
    }

    const VideoBeam& beam() const { return beam_; }

    int width() const { return kWidth; }
    int height() const { return kHeight; }

private:
    std::vector<uint32_t> fb_;
    VideoBeam beam_;             // not serialized: pure cache
};
