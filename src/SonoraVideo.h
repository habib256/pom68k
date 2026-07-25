// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Sonora video: whole-frame decode (LC III) ──
// Framebuffer at VRAM+0, rows packed at hres × bpp / 8 (no V8-style
// fixed pitch — MAME mv_sonora.cpp screen_update reads the scanlines
// back-to-back). Depth = video control reg 1 (0..4 = 1/2/4/8/16 bpp),
// pixels MSB-first, palette indices padded with low 1s, CLUT from the
// Sonora DAC. Resolution from the ACTIVE MODELINE (vctrl reg 0), not
// the monitor sense — the sense only tells the ROM what to program.
// Whole-frame decode at frame end, like V8Video.
// Gate: tests/lc3_boot_etalon.cpp.

#pragma once
#include "SonoraMemory.h"
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
        const SonoraMemory::Modeline* m = mem_.currentModeline();
        const int depth = mem_.videoDepth();
        if (!m || (mem_.videoMode() & 0x80) || depth > 4 ||
            (depth == 4 && !m->supports16bpp))
            return;                                // blanked → black

        const uint8_t* vram = mem_.vram();
        uint32_t* dst = out.data();
        switch (depth) {
        case 0:                                    // 1 bpp
            for (long i = 0; i < long(hres) * vres; i += 8) {
                uint8_t px = *vram++;
                for (int b = 0; b < 8; b++)
                    *dst++ = mem_.pen(0x7F | ((px << b) & 0x80));
            }
            break;
        case 1:                                    // 2 bpp
            for (long i = 0; i < long(hres) * vres; i += 4) {
                uint8_t px = *vram++;
                for (int b = 0; b < 8; b += 2)
                    *dst++ = mem_.pen(0x3F | ((px << b) & 0xC0));
            }
            break;
        case 2:                                    // 4 bpp
            for (long i = 0; i < long(hres) * vres; i += 2) {
                uint8_t px = *vram++;
                *dst++ = mem_.pen(0x0F | (px & 0xF0));
                *dst++ = mem_.pen(0x0F | ((px << 4) & 0xF0));
            }
            break;
        case 3:                                    // 8 bpp
            for (long i = 0; i < long(hres) * vres; i++)
                *dst++ = mem_.pen(*vram++);
            break;
        case 4:                                    // 16 bpp x1r5g5b5
            for (long i = 0; i < long(hres) * vres; i++) {
                uint16_t px = uint16_t(vram[0] << 8 | vram[1]);
                vram += 2;
                *dst++ = uint32_t(((px >> 10) & 0x1F) << 19
                                | ((px >> 5) & 0x1F) << 11
                                | (px & 0x1F) << 3);
            }
            break;
        }
    }

private:
    SonoraMemory& mem_;
};
