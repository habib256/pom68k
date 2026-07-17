// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Ariel RAMDAC (343S1045/343S1069) ──
// Brooktree-style CLUT behind the V8 at $F24000: +0 address register
// (resets the RGB write phase), +1 palette data (R, G, B then address
// auto-increment), +2 control (bits 0-2 depth, bit 3 master/slave),
// +3 key color. 256 entries. Whole-frame video decode reads pens().
// Source of truth: MAME ariel.cpp:3-27,62-93 (master 2026-07-15),
// pinned in docs/LCII_HARDWARE.md § Video.
// Gate: tests/v8_video_test.cpp (O6.4).

#pragma once
#include <cstdint>

class Ariel {
public:
    void reset() {
        addr_ = phase_ = ctrl_ = key_ = 0;
        for (auto& p : pal_) p = 0;
    }

    uint8_t read(uint32_t offset) {
        switch (offset & 3) {
        case 0: return addr_;
        case 1: {                            // read cycles the same RGB phase
            uint8_t v = pal_[addr_ * 3 + phase_];
            if (++phase_ == 3) { phase_ = 0; addr_++; }
            return v;
        }
        case 2: return ctrl_;
        default: return key_;
        }
    }

    void write(uint32_t offset, uint8_t v) {
        switch (offset & 3) {
        case 0: addr_ = v; phase_ = 0; break;
        case 1:
            pal_[addr_ * 3 + phase_] = v;
            if (++phase_ == 3) { phase_ = 0; addr_++; }
            break;
        case 2: ctrl_ = v; break;
        default: key_ = v; break;
        }
    }

    // Pen n as packed 00RRGGBB
    uint32_t pen(int n) const {
        return uint32_t(pal_[n * 3] << 16 | pal_[n * 3 + 1] << 8 | pal_[n * 3 + 2]);
    }

private:
    uint8_t pal_[256 * 3] = {};
    uint8_t addr_ = 0, phase_ = 0, ctrl_ = 0, key_ = 0;
};
