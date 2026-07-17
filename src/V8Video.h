// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── V8 video: whole-frame decode (LC II) ──
// Framebuffer at VRAM+0, row pitch FIXED at 1024 bytes for 1/2/4/8 bpp
// (16 bpp is packed at hres×2). Depth = pseudo-VIA reg $10 bits 0-2
// (0=1bpp … 4=16bpp); pixels MSB-first; palette indices padded with low
// 1s (1 bpp uses Ariel pens $7F/$FF). Resolution from the monitor sense:
// 1 = 640×870 portrait, 2 = 512×384 12" RGB, 6 = 640×480 13" RGB.
// Whole-frame decode at frame end, like the Plus MacVideo (no beam).
// Source of truth: MAME v8.cpp screen_update :495-619 (master
// 2026-07-15); pinned in docs/LCII_HARDWARE.md § Video.
// Gate: tests/v8_video_test.cpp.

#pragma once
#include "V8Memory.h"
#include <cstdint>
#include <vector>

class V8Video {
public:
    explicit V8Video(V8Memory& mem) : mem_(mem) {}

    static void resolution(uint8_t montype, int& hres, int& vres) {
        switch (montype) {
        case 1:  hres = 640; vres = 870; break;   // 15" portrait
        case 2:  hres = 512; vres = 384; break;   // 12" RGB
        case 6:
        default: hres = 640; vres = 480; break;   // 13" RGB
        }
    }

    // Decode the current frame to packed 00RRGGBB (row-major, hres×vres).
    void decode(std::vector<uint32_t>& out) const {
        int hres, vres;
        resolution(mem_.monitorSense(), hres, vres);
        out.resize(size_t(hres) * vres);

        const uint8_t* vram = mem_.vram();
        const Ariel& pal = const_cast<V8Memory&>(mem_).ariel();
        uint32_t* dst = out.data();

        switch (mem_.videoConfig() & 7) {
        case 0:                                    // 1 bpp: pens $7F/$FF
            for (int y = 0; y < vres; y++)
                for (int x = 0; x < hres; x += 8) {
                    uint8_t px = vram[y * 1024 + x / 8];
                    for (int b = 0; b < 8; b++)
                        *dst++ = pal.pen(0x7F | ((px << b) & 0x80));
                }
            break;
        case 1:                                    // 2 bpp
            for (int y = 0; y < vres; y++)
                for (int x = 0; x < hres / 4; x++) {
                    uint8_t px = vram[y * 1024 + x];
                    for (int b = 0; b < 8; b += 2)
                        *dst++ = pal.pen(0x3F | ((px << b) & 0xC0));
                }
            break;
        case 2:                                    // 4 bpp
            for (int y = 0; y < vres; y++)
                for (int x = 0; x < hres / 2; x++) {
                    uint8_t px = vram[y * 1024 + x];
                    *dst++ = pal.pen(0x0F | (px & 0xF0));
                    *dst++ = pal.pen(0x0F | ((px << 4) & 0xF0));
                }
            break;
        case 3:                                    // 8 bpp
            for (int y = 0; y < vres; y++)
                for (int x = 0; x < hres; x++)
                    *dst++ = pal.pen(vram[y * 1024 + x]);
            break;
        case 4:                                    // 16 bpp x1r5g5b5, hres×2 pitch
        default:
            // 16 bpp at 640×480 needs 614400 B > the 512 KB VRAM window —
            // not a valid hardware combo, but reachable via setMonitorSense
            // + a depth write, so bound the read (out-of-window = black).
            for (int y = 0; y < vres; y++)
                for (int x = 0; x < hres; x++) {
                    size_t off = size_t(y * hres + x) * 2;
                    uint16_t px = (off + 1 < V8Memory::kVramSize)
                        ? uint16_t(vram[off] << 8 | vram[off + 1]) : 0;
                    *dst++ = uint32_t(((px >> 10) & 0x1F) << 19
                                    | ((px >> 5) & 0x1F) << 11
                                    | (px & 0x1F) << 3);
                }
            break;
        }
    }

private:
    V8Memory& mem_;
};
