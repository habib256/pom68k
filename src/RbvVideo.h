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
// Whole-frame decode at frame end, like V8Video.
// Gate: tests/iisi_boot_etalon.cpp.

#pragma once
#include "RbvMemory.h"
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
        if (mem_.videoConfig() & 0x40)             // video disabled → black
            return;

        const bool mono = (mem_.monitorSense() & 7) == 1;
        const uint8_t* fb = mem_.framebuffer();
        const Ariel& pal = const_cast<RbvMemory&>(mem_).dac();
        // Portrait display drives the blue gun only (rbv.cpp:212-218).
        auto pen = [&](int n) {
            uint32_t p = pal.pen(n);
            if (!mono) return p;
            uint32_t b = p & 0xFF;
            return b << 16 | b << 8 | b;
        };
        uint32_t* dst = out.data();

        switch (mem_.videoConfig() & 7) {
        case 0:                                    // 1 bpp: pens $FE/$FF
            for (long i = 0; i < long(hres) * vres; i += 8) {
                uint8_t px = *fb++;
                for (int b = 7; b >= 0; b--)
                    *dst++ = pen(0xFE | ((px >> b) & 1));
            }
            break;
        case 1:                                    // 2 bpp: pens $FC-$FF
            for (long i = 0; i < long(hres) * vres; i += 4) {
                uint8_t px = *fb++;
                for (int b = 6; b >= 0; b -= 2)
                    *dst++ = pen(0xFC | ((px >> b) & 3));
            }
            break;
        case 2:                                    // 4 bpp: pens $F0-$FF
            for (long i = 0; i < long(hres) * vres; i += 2) {
                uint8_t px = *fb++;
                *dst++ = pen(0xF0 | (px >> 4));
                *dst++ = pen(0xF0 | (px & 0x0F));
            }
            break;
        case 3:                                    // 8 bpp
        default:
            for (long i = 0; i < long(hres) * vres; i++)
                *dst++ = pen(*fb++);
            break;
        }
    }

private:
    RbvMemory& mem_;
};
