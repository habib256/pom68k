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
// Gate: tests/v8_video_test.cpp (O6.4), tests/v8_ramsize.cpp (register file).
//
// ── Two deliberate divergences from the oracle, both in our favour ──
//
// 1. **The key-color register is real here.** MAME declares m_key_color,
//    saves it in device_start — and then never uses it: key_color_r()
//    returns m_control and key_color_w() assigns m_control
//    (ariel.cpp:169-177). So on master, writing +3 clobbers the depth/
//    master-slave control register and reading it back answers control.
//    That is a plain aliasing bug, not silicon behaviour: ariel.cpp's own
//    header (:13-25) describes +3 as a separate overlay key register, and
//    the file says MAME simply does not implement the overlay path. Worth
//    reporting upstream. Mac OS never writes +3, so the two forms are
//    indistinguishable on every shipped profile — but ours cannot corrupt
//    the depth bits, so it stays.
//
// 2. **The window mirrors.** The V8 and VASP maps hand this cell an 8 KB
//    window and only A0-A1 are decoded — see V8Memory.cpp/VaspMemory.cpp,
//    where the divergence is argued in full.

#pragma once
#include "SaveState.h"
#include <cstdint>

class Ariel {
public:
    void reset() {
        addr_ = phase_ = ctrl_ = key_ = 0;
        for (auto& p : pal_) p = 0;
    }

    uint8_t read(uint32_t offset) {
        switch (offset & 3) {
        case 0: phase_ = 0; return addr_;    // address_r resets the RGB phase
                                             // (ariel.cpp:96-106, Brooktree)
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

    // ── Save states (SaveState.h) ───────────────────────────────────────
    // The CLUT plus the RGB write phase: a snapshot can land between the R
    // and G bytes of a palette entry, and the guest's next write expects to
    // continue that triple.
    template <class Ar> void visit(Ar& ar) { ar(pal_, addr_, phase_, ctrl_, key_); }

    // Pen n as packed 00RRGGBB
    uint32_t pen(int n) const {
        return uint32_t(pal_[n * 3] << 16 | pal_[n * 3 + 1] << 8 | pal_[n * 3 + 2]);
    }

private:
    uint8_t pal_[256 * 3] = {};
    uint8_t addr_ = 0, phase_ = 0, ctrl_ = 0, key_ = 0;
};
