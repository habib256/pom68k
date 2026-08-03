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
    clkM_ = clkN_ = clkP_ = 0;
    framePos_ = 0;
    prevLine_ = 0;
    for (auto& c : clut_) c[0] = c[1] = c[2] = 0;
    recalcIrq();     // drop the VBL line: don't depend on the owner's reset
                     // order to undo a latched onIrq(true)
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

// I2C clock generator (valkyrie.cpp write_data:542-573). Register 0 is
// written and ignored; 1/2/3 latch M/N/P and every write recomputes
//     pixelClock = 3986400 × 2^P × N / M
// then re-derives the mode (the refresh rate the frame clock uses).
//
// **Deliberate divergence from the oracle, in its favour.** MAME's guard
// for the 512×384 monitor reads `if ((m_M == 0) && (m_N == 0) && (m_P = 98))`
// — an ASSIGNMENT, so it fires on M==N==0 alone and clobbers P as a side
// effect. We implement its *effect* (M==0 is a division by zero anyway, and
// that is the configuration Apple's own note programs) without the typo:
// no register is corrupted by reading it. If a guest is ever seen to depend
// on P becoming 98, this is the line to revisit.
void Valkyrie::i2cWrite(uint8_t reg, uint8_t value) {
    switch (reg) {
        case 1: clkM_ = value; break;
        case 2: clkN_ = value; break;
        case 3: clkP_ = value; break;
        default: return;                 // reg 0: written, ignored
    }
    if (clkM_ == 0 && clkN_ == 0) {
        pixelClock_ = 15670000;          // the 512×384 "garbage" program
    } else if (clkM_ != 0) {
        pixelClock_ = uint32_t(3986400.0 * double(1u << (clkP_ & 31))
                               * double(clkN_) / double(clkM_));
    }
    recalcMode();
}

void Valkyrie::recalcIrq() {
    if (onIrq) onIrq(intStatus_ != 0);
}

uint8_t Valkyrie::readReg8(uint32_t off) {
    // Q630Memory hands an 8 KB offset (sub & 0x1FFF), so masking to 0xFF
    // aliased $100/$110/... onto the timing/config/sense registers MAME leaves
    // unmapped — an $110 write acked the VBL and dropped the frame interrupt.
    switch (off) {
        case 0x00: return videoTiming_;
        case 0x04: return mode_;
        case 0x10: return config_;
        case 0x14:                                  // live vblank level
            // LIVE, not the last ticked line. `prevLine_` only advances
            // inside tick(), i.e. at peripheral-batch granularity, so a
            // guest polling this register in a tight loop used to see the
            // beam move in steps of a whole batch. currentLine() is the
            // same arithmetic tick() uses — one formula, one scan position
            // (LLE_VS_HLE §1.1).
            return (vtotal_ && currentLine() >= int(vres_)) ? 1 : 0;
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
    switch (off) {                               // raw offset: see readReg8
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
    // valkyrie_device::ramdac_r decodes ONLY word offsets 0 and 1; masking to
    // & 3 aliased $10/$14 onto the palette address/data registers.
    switch (off >> 2) {
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
    switch (off >> 2) {                          // offsets 0/1 only: see read
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
// The scanline the beam is on, straight from the frame accumulator. The
// ONE place this arithmetic lives: tick() drives the VBL from it and the
// $14 blanking register reads it, so the two cannot drift apart.
int Valkyrie::currentLine() const {
    if (!vtotal_ || !htotal_) return 0;
    return int(framePos_ / (int64_t(htotal_) * cpuHz_));
}

void Valkyrie::tick(int cpuCycles) {
    if (!vtotal_ || !htotal_ || !enabled_) return;
    framePos_ += int64_t(cpuCycles) * pixelClock_;
    const int64_t frameCycles = int64_t(htotal_) * vtotal_ * cpuHz_;
    while (framePos_ >= frameCycles) {
        framePos_ -= frameCycles;
        // Completed frames, for the raster beam (VideoBeam::setPos).
        frameCount_++;
    }
    const int line = currentLine();
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
