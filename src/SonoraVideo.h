// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Sonora video (LC III family) ──
// Framebuffer at VRAM+0, rows packed at hres × bpp / 8 (no V8-style
// fixed pitch — MAME mv_sonora.cpp screen_update reads the scanlines
// back-to-back). Depth = video control reg 1 (0..4 = 1/2/4/8/16 bpp),
// pixels MSB-first, palette indices padded with low 1s, CLUT from the
// Sonora DAC. Resolution from the ACTIVE MODELINE (vctrl reg 0), not
// the monitor sense — the sense only tells the ROM what to program.
// Raster decode like V8Video: each row is rendered once, when the beam
// scans it (LLE_VS_HLE §1.1, VideoBeam.h). `decode()` keeps the
// whole-frame path for stills.
// Gate: tests/lc3_boot_etalon.cpp.

#pragma once
#include "SonoraMemory.h"
#include "VideoBeam.h"
#include <algorithm>
#include <cstdint>
#include <vector>

class SonoraVideo {
public:
    explicit SonoraVideo(SonoraMemory& mem) : mem_(mem) {}

    static void resolution(uint8_t montype, int& hres, int& vres) {
        switch (montype & 0x7F) {
        case 1:  hres = 640; vres = 870; break;   // 15" portrait
        case 2:  hres = 512; vres = 384; break;   // 12" RGB
        case 6:
        default: hres = 640; vres = 480; break;   // 13" RGB
        }
    }

    // Current display size: the active modeline, else the monitor's
    // native mode (blank frame until the ROM programs vctrl).
    void size(int& hres, int& vres) const {
        if (const SonoraMemory::Modeline* m = mem_.currentModeline()) {
            hres = m->hres(); vres = m->vres();
        } else {
            resolution(mem_.monitorSense(), hres, vres);
        }
    }

    // Decode the current frame to packed 00RRGGBB (row-major).
    void decode(std::vector<uint32_t>& out) const {
        int hres, vres;
        size(hres, vres);
        out.assign(size_t(hres) * vres, 0);
        decodeRows(out, 0, vres);
    }

    // Render visible rows [y0, y1) into an existing hres×vres surface.
    // Rows are packed back-to-back, so the row's VRAM offset is just
    // y × (hres × bpp / 8) — there is no V8-style fixed pitch here.
    void decodeRows(std::vector<uint32_t>& out, int y0, int y1) const {
        int hres, vres;
        size(hres, vres);
        if (out.size() < size_t(hres) * vres) return;
        y0 = std::max(y0, 0);
        y1 = std::min(y1, vres);
        if (y0 >= y1) return;

        const SonoraMemory::Modeline* m = mem_.currentModeline();
        const int depth = mem_.videoDepth();
        if (!m || (mem_.videoMode() & 0x80) || depth > 4 ||
            (depth == 4 && !m->supports16bpp)) {   // blanked → black
            std::fill(out.begin() + size_t(y0) * hres,
                      out.begin() + size_t(y1) * hres, 0u);
            return;
        }

        static const int kBpp[5] = {1, 2, 4, 8, 16};
        const size_t rowBytes = size_t(hres) * kBpp[depth] / 8;

        for (int y = y0; y < y1; y++) {
            const uint8_t* vram = mem_.vram() + size_t(y) * rowBytes;
            uint32_t* dst = out.data() + size_t(y) * hres;
            switch (depth) {
            case 0:                                // 1 bpp
                for (int x = 0; x < hres; x += 8) {
                    uint8_t px = *vram++;
                    for (int b = 0; b < 8; b++)
                        *dst++ = mem_.pen(0x7F | ((px << b) & 0x80));
                }
                break;
            case 1:                                // 2 bpp
                for (int x = 0; x < hres; x += 4) {
                    uint8_t px = *vram++;
                    for (int b = 0; b < 8; b += 2)
                        *dst++ = mem_.pen(0x3F | ((px << b) & 0xC0));
                }
                break;
            case 2:                                // 4 bpp
                for (int x = 0; x < hres; x += 2) {
                    uint8_t px = *vram++;
                    *dst++ = mem_.pen(0x0F | (px & 0xF0));
                    *dst++ = mem_.pen(0x0F | ((px << 4) & 0xF0));
                }
                break;
            case 3:                                // 8 bpp
                for (int x = 0; x < hres; x++)
                    *dst++ = mem_.pen(*vram++);
                break;
            case 4:                                // 16 bpp x1r5g5b5
                for (int x = 0; x < hres; x++) {
                    uint16_t px = uint16_t(vram[0] << 8 | vram[1]);
                    vram += 2;
                    *dst++ = uint32_t(((px >> 10) & 0x1F) << 19
                                    | ((px >> 5) & 0x1F) << 11
                                    | (px & 0x1F) << 3);
                }
                break;
            }
        }
    }

    // Advance the beam off the machine's frame accumulator and decode the
    // rows it has crossed (see V8Video::raster).
    // `full` = the once-per-publish call. It is what allows the
    // no-CRTC-yet fallback below to render a whole frame WITHOUT doing so
    // on every wire slice — on Sonora that window is the whole POST, and a
    // per-slice full decode there is 64 × 640 × 480 pixels a frame for
    // nothing. (Same trap, same fix, as DafbMachine::rasterBeam.)
    void raster(std::vector<uint32_t>& out, bool full = false) {
        int hres, vres;
        size(hres, vres);
        const size_t need = size_t(hres) * vres;
        if (out.size() != need) {
            out.assign(need, 0u);
            beam_.restartFrame();
        }
        beam_.setGeometry(mem_.frameCycles(), mem_.frameActiveCycles(),
                          mem_.frameTotalLines(), vres);
        if (!beam_.valid()) {                      // no modeline yet
            if (full) decodeRows(out, 0, vres);
            return;
        }
        beam_.setPos(mem_.framePos(), mem_.frameCount());
        beam_.pumpRows([&](int a, int b) { decodeRows(out, a, b); });
    }

    const VideoBeam& beam() const { return beam_; }

private:
    SonoraMemory& mem_;
    VideoBeam beam_;
};
