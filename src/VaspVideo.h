// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── VASP video: whole-frame decode (Mac IIvx / IIvi) ──
// The V8 framebuffer model with a 2048-byte row pitch (vasp.cpp
// screen_update :440-560 — the V8 uses 1024): framebuffer at VRAM+0,
// depth = pseudo-VIA video-config bits 0-2 (0=1bpp … 4=16bpp), pixels
// MSB-first, palette indices padded with low 1s (1 bpp uses Ariel pens
// $7F/$FF), 16 bpp packed x1r5g5b5 at hres×2. Resolution from the
// monitor sense: 1 = 640×870 portrait, 2 = 512×384 12", 6 = 640×480 13".
// Raster decode like V8Video: each row is rendered once, when the beam
// scans it (LLE_VS_HLE §1.1, VideoBeam.h). `decode()` keeps the
// whole-frame path for stills.
// Gate: tests/iivx_boot_etalon.cpp.

#pragma once
#include "VaspMemory.h"
#include "VideoBeam.h"
#include <algorithm>
#include <cstdint>
#include <vector>

class VaspVideo {
public:
    explicit VaspVideo(VaspMemory& mem) : mem_(mem) {}

    static void resolution(uint8_t montype, int& hres, int& vres) {
        switch (montype) {
        case 1:  hres = 640; vres = 870; break;   // 15" portrait
        case 2:  hres = 512; vres = 384; break;   // 12" RGB
        case 6:
        default: hres = 640; vres = 480; break;   // 13" RGB
        }
    }
    void size(int& w, int& h) const { resolution(mem_.monitorSense(), w, h); }

    // Decode the current frame to packed 00RRGGBB (row-major, hres×vres).
    void decode(std::vector<uint32_t>& out) const {
        int hres, vres;
        resolution(mem_.monitorSense(), hres, vres);
        out.assign(size_t(hres) * vres, 0u);
        decodeRows(out, 0, vres);
    }

    // Render visible rows [y0, y1) into an existing hres×vres surface.
    void decodeRows(std::vector<uint32_t>& out, int y0, int y1) const {
        int hres, vres;
        resolution(mem_.monitorSense(), hres, vres);
        if (out.size() < size_t(hres) * vres) return;
        y0 = std::max(y0, 0);
        y1 = std::min(y1, vres);
        if (y0 >= y1) return;

        // setMonitorSense() applies no clamp, and sense 1 selects 640x870 —
        // at a 2048-byte pitch that indexes ~730 KB past the 1 MB VRAM vector.
        // Only the 16 bpp branch below bounded its reads.
        if (size_t(vres) * 2048 > VaspMemory::kVramSize) {
            std::fill(out.begin() + size_t(y0) * hres,
                      out.begin() + size_t(y1) * hres, 0u);
            return;
        }

        const uint8_t* vram = mem_.vram();
        const Ariel& pal = const_cast<VaspMemory&>(mem_).ariel();
        uint32_t* dst = out.data() + size_t(y0) * hres;

        switch (mem_.videoConfig() & 7) {
        case 0:                                    // 1 bpp: pens $7F/$FF
            for (int y = y0; y < y1; y++)
                for (int x = 0; x < hres; x += 8) {
                    uint8_t px = vram[y * 2048 + x / 8];
                    for (int b = 0; b < 8; b++)
                        *dst++ = pal.pen(0x7F | ((px << b) & 0x80));
                }
            break;
        case 1:                                    // 2 bpp
            for (int y = y0; y < y1; y++)
                for (int x = 0; x < hres / 4; x++) {
                    uint8_t px = vram[y * 2048 + x];
                    for (int b = 0; b < 8; b += 2)
                        *dst++ = pal.pen(0x3F | ((px << b) & 0xC0));
                }
            break;
        case 2:                                    // 4 bpp
            for (int y = y0; y < y1; y++)
                for (int x = 0; x < hres / 2; x++) {
                    uint8_t px = vram[y * 2048 + x];
                    *dst++ = pal.pen(0x0F | (px & 0xF0));
                    *dst++ = pal.pen(0x0F | ((px << 4) & 0xF0));
                }
            break;
        case 3:                                    // 8 bpp
            for (int y = y0; y < y1; y++)
                for (int x = 0; x < hres; x++)
                    *dst++ = pal.pen(vram[y * 2048 + x]);
            break;
        case 4:                                    // 16 bpp x1r5g5b5, 1024-pel pitch
        default:
            for (int y = y0; y < y1; y++)
                for (int x = 0; x < hres; x++) {
                    size_t off = (size_t(y) * 1024 + x) * 2;
                    uint16_t px = (off + 1 < VaspMemory::kVramSize)
                        ? uint16_t(vram[off] << 8 | vram[off + 1]) : 0;
                    *dst++ = uint32_t(((px >> 10) & 0x1F) << 19
                                    | ((px >> 5) & 0x1F) << 11
                                    | (px & 0x1F) << 3);
                }
            break;
        }
    }

    // Advance the beam off the machine's frame accumulator and decode the
    // rows it has crossed (see V8Video::raster).
    // `full` marks the once-per-publish call. These two always have a valid
    // frame length (V8 sets it at reset, VASP's is a fixed 60 Hz), so they
    // have no no-CRTC-yet fallback to gate — the parameter exists so every
    // decoder presents the same signature to the machine loops.
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
        (void)full;
        beam_.setPos(mem_.framePos(), mem_.frameCount());
        beam_.pumpRows([&](int a, int b) { decodeRows(out, a, b); });
    }

    const VideoBeam& beam() const { return beam_; }

private:
    VaspMemory& mem_;
    VideoBeam beam_;
};
