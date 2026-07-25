// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "Valkyrie.h"

void Valkyrie::reset() {
    videoTiming_ = 0x80;            // valkyrie.cpp device_reset
    mode_ = 0;
    config_ = 0;
    intStatus_ = 0;
    monitorId_ = 0;
    palAddress_ = palIdx_ = 0;
    enabled_ = false;
    hres_ = vres_ = htotal_ = vtotal_ = 0;
    stride_ = 1024;
    pixelClock_ = 31334400;
    framePos_ = 0;
    prevLine_ = 0;
    for (auto& c : clut_) c[0] = c[1] = c[2] = 0;
}

// valkyrie.cpp recalc_mode: the timing number selects one of the hardwired
// modes from the Quadra 630 Developer Note. Only the four the ROM and Mac OS
// actually program are tabulated (as in MAME).
void Valkyrie::recalcMode() {
    if (videoTiming_ & 0x80) return;
    switch (videoTiming_ & 0x7F) {
        case 2:  hres_ = 512; vres_ = 384; htotal_ = 640;  vtotal_ = 407; stride_ = 64;  break;
        case 6:  hres_ = 640; vres_ = 480; htotal_ = 864;  vtotal_ = 525; stride_ = 80;  break;
        case 9:  hres_ = 832; vres_ = 624; htotal_ = 1072; vtotal_ = 690; stride_ = 104; break;
        case 11: hres_ = 640; vres_ = 480; htotal_ = 800;  vtotal_ = 525; stride_ = 80;  break;
        default: break;
    }
}

void Valkyrie::recalcIrq() {
    if (onIrq) onIrq(intStatus_ != 0);
}

uint8_t Valkyrie::readReg8(uint32_t off) {
    switch (off & 0xFF) {
        case 0x00: return videoTiming_;
        case 0x04: return mode_;
        case 0x10: return config_;
        case 0x14:                                  // live vblank level
            return (vtotal_ && prevLine_ >= int(vres_)) ? 1 : 0;
        case 0x1C: {
            // Monitor sense (valkyrie.cpp regs_r case 0x1c): the three sense
            // pins, in the upper nibble. Extended codes ($40 | bc<<4 | ac<<2
            // | ab) answer per driven pin, plain codes answer flat.
            uint8_t mon = monitorConfig_, res;
            if (mon & 0x40) {
                res = 7;
                if (monitorId_ == 0x4) res &= uint8_t(4 | (((mon >> 5) & 1) << 1) | ((mon >> 4) & 1));
                if (monitorId_ == 0x2) res &= uint8_t((((mon >> 3) & 1) << 2) | 2 | ((mon >> 2) & 1));
                if (monitorId_ == 0x1) res &= uint8_t((((mon >> 1) & 1) << 2) | ((mon & 1) << 1) | 1);
            } else {
                res = mon;
            }
            return uint8_t(res << 4);
        }
        default: return 0;
    }
}

void Valkyrie::writeReg8(uint32_t off, uint8_t v) {
    switch (off & 0xFF) {
        case 0x00:                                  // video timing number
            videoTiming_ = v;
            if (!(v & 0x80)) recalcMode();
            break;
        case 0x04:                                  // depth
            mode_ = uint8_t(v & 7);
            break;
        case 0x0C:                                  // subsystem config
            if (v == 1) recalcMode();
            break;
        case 0x10:                                  // config + VBL ack
            config_ = v;
            intStatus_ &= uint8_t(~1);
            recalcIrq();
            break;
        case 0x18:                                  // screen enable
            enabled_ = (v & 0x80) || v == 0x02;
            break;
        case 0x1C:                                  // drive the sense pins
            monitorId_ = uint8_t(v & 7);
            break;
        default:
            break;
    }
}

uint32_t Valkyrie::readRamdac32(uint32_t off) {
    switch ((off >> 2) & 3) {
        case 0:
            palIdx_ = 0;
            return uint32_t(palAddress_) << 24;
        case 1: {
            uint8_t idx = palIdx_;
            palIdx_ = uint8_t((palIdx_ + 1) % 3);
            return uint32_t(clut_[palAddress_][idx]) << 24;
        }
        default: return 0;
    }
}

void Valkyrie::writeRamdac32(uint32_t off, uint32_t data) {
    const uint8_t v = uint8_t(data >> 24);
    switch ((off >> 2) & 3) {
        case 0:
            palAddress_ = v;
            palIdx_ = 0;
            break;
        case 1:
            clut_[palAddress_][palIdx_] = v;
            if (++palIdx_ == 3) { palIdx_ = 0; palAddress_++; }
            break;
        default:
            break;
    }
}

// Frame clock: MAME arms a timer at line `m_vres` and re-arms it at 480 —
// POM68K keeps a free-running pixel-clock position instead (the Dafb cell
// pattern) and raises the VBL as the beam crosses the end of the display.
void Valkyrie::tick(int cpuCycles) {
    if (!vtotal_ || !htotal_ || !enabled_) return;
    framePos_ += int64_t(cpuCycles) * pixelClock_;
    const int64_t frameCycles = int64_t(htotal_) * vtotal_ * cpuHz_;
    while (framePos_ >= frameCycles) framePos_ -= frameCycles;
    const int line = int(framePos_ / (int64_t(htotal_) * cpuHz_));
    if (line != prevLine_) {
        const int vbl = int(vres_);
        // crossed the display end (wrapping included)
        if ((prevLine_ < vbl && line >= vbl) || line < prevLine_) {
            intStatus_ |= 1;
            recalcIrq();
        }
        prevLine_ = line;
    }
}
