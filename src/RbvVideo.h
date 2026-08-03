// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── RBV video: whole-frame decode (Mac IIsi) ──
// RAM-Based Video: the framebuffer is the START of system RAM (MAME
// rbv.cpp set_ram_info + screen_update read m_ram_ptr), rows packed at
// hres × bpp / 8 — no fixed pitch, unlike the V8's 1024. Depth = RBV
// video config bits 0-2 (0=1bpp … 3=8bpp, no 16bpp), bit 6 = video off.
// Palette indices pad with HIGH 1s from the top of the Bt478 CLUT
// (1 bpp = pens $FE/$FF, 2 bpp = $FC-$FF, 4 bpp = $F0-$FF — rbv.cpp
// update_screen; the V8 line pads the LOW bits instead). Monitor type
// 1 = 640×870 portrait (mono — blue gun), 2 = 512×384, 6 = 640×480.
// Raster decode like V8Video: each row is rendered once, when the beam
// scans it (LLE_VS_HLE §1.1, VideoBeam.h). `decode()` keeps the
// whole-frame path for stills.
// Gate: tests/iisi_boot_etalon.cpp.

#pragma once
#include "RbvMemory.h"
#include "VideoBeam.h"
#include <algorithm>
#include <cstdint>
#include <vector>

class RbvVideo {
public:
    explicit RbvVideo(RbvMemory& mem) : mem_(mem) {}

    static void resolution(uint8_t montype, int& hres, int& vres) {
        switch (montype & 7) {
        case 1:  hres = 640; vres = 870; break;   // 15" portrait (mono)
        case 2:  hres = 512; vres = 384; break;   // 12" RGB
        case 6:
        default: hres = 640; vres = 480; break;   // 13" RGB
        }
    }

    void size(int& hres, int& vres) const {
        resolution(mem_.monitorSense(), hres, vres);
    }

    // Decode the current frame to packed 00RRGGBB (row-major).
    void decode(std::vector<uint32_t>& out) const {
        int hres, vres;
        size(hres, vres);
        out.assign(size_t(hres) * vres, 0);
        decodeRows(out, 0, vres);
    }

    // Render visible rows [y0, y1) into an existing hres×vres surface.
    // Rows are packed back-to-back at hres × bpp / 8.
    void decodeRows(std::vector<uint32_t>& out, int y0, int y1) const {
        int hres, vres;
        size(hres, vres);
        if (out.size() < size_t(hres) * vres) return;
        y0 = std::max(y0, 0);
        y1 = std::min(y1, vres);
        if (y0 >= y1) return;

        const int cfg = mem_.videoConfig() & 7;
        if (mem_.videoConfig() & 0x40) {           // video disabled → black
            std::fill(out.begin() + size_t(y0) * hres,
                      out.begin() + size_t(y1) * hres, 0u);
            return;
        }

        const bool mono = (mem_.monitorSense() & 7) == 1;
        const Ariel& pal = const_cast<RbvMemory&>(mem_).dac();
        // Portrait display drives the blue gun only (rbv.cpp:212-218).
        auto pen = [&](int n) {
            uint32_t p = pal.pen(n);
            if (!mono) return p;
            uint32_t b = p & 0xFF;
            return b << 16 | b << 8 | b;
        };
        // Config 0-3 = 1/2/4/8 bpp; 4-7 are undefined on the RBV (there is
        // no 16 bpp here) and the decode below folds them into the 8 bpp
        // `default`, so the row pitch has to fold with them — indexing a
        // 4-entry table by the raw config would give a 1 bpp pitch and
        // shear the picture on a config the guest is not supposed to write.
        static const int kBpp[4] = {1, 2, 4, 8};
        const size_t rowBytes = size_t(hres) * (cfg < 3 ? kBpp[cfg] : 8) / 8;

        for (int y = y0; y < y1; y++) {
            const uint8_t* fb = mem_.framebuffer() + size_t(y) * rowBytes;
            uint32_t* dst = out.data() + size_t(y) * hres;
            switch (cfg) {
            case 0:                                // 1 bpp: pens $FE/$FF
                for (int x = 0; x < hres; x += 8) {
                    uint8_t px = *fb++;
                    for (int b = 7; b >= 0; b--)
                        *dst++ = pen(0xFE | ((px >> b) & 1));
                }
                break;
            case 1:                                // 2 bpp: pens $FC-$FF
                for (int x = 0; x < hres; x += 4) {
                    uint8_t px = *fb++;
                    for (int b = 6; b >= 0; b -= 2)
                        *dst++ = pen(0xFC | ((px >> b) & 3));
                }
                break;
            case 2:                                // 4 bpp: pens $F0-$FF
                for (int x = 0; x < hres; x += 2) {
                    uint8_t px = *fb++;
                    *dst++ = pen(0xF0 | (px >> 4));
                    *dst++ = pen(0xF0 | (px & 0x0F));
                }
                break;
            case 3:                                // 8 bpp
            default:
                for (int x = 0; x < hres; x++)
                    *dst++ = pen(*fb++);
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
        if (!beam_.valid()) { if (full) decodeRows(out, 0, vres); return; }
        beam_.setPos(mem_.framePos(), mem_.frameCount());
        beam_.pumpRows([&](int a, int b) { decodeRows(out, a, b); });
    }

    const VideoBeam& beam() const { return beam_; }

private:
    RbvMemory& mem_;
    VideoBeam beam_;
};
