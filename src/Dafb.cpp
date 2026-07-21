// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "Dafb.h"
#include <cstring>

void Dafb::reset() {
    std::memset(regs_, 0, sizeof regs_);
    std::memset(clut_, 0, sizeof clut_);
    intStatus_ = 0;
    swatchIntEnable_ = 0;
    cursorLine_ = 0;
    palAddress_ = palIdx_ = 0;
    ac842Pbctrl_ = pcbr1_ = 0;
    base_ = 0;
    stride_ = 1024;
    config_ = 0;
    mode_ = 0;
    std::memset(hParams_, 0, sizeof hParams_);
    std::memset(vParams_, 0, sizeof vParams_);
    swatchMode_ = 1;                             // display disabled at reset
    hres_ = vres_ = htotal_ = vtotal_ = 0;
    monitorId_ = 0;
    pixelClock_ = 31334400;
    gazShift_ = 0; gazBits_ = 0; gazLastClock_ = 0;
    gazMclk_ = 31334400;
    framePos_ = 0;
    prevLine_ = 0;
}

void Dafb::recalcIrq() {
    // write_irq → MEMCjr via2_irq_w<0x40>: nubus bit 6, active low.
    if (onIrq) onIrq(intStatus_ != 0);
}

uint32_t Dafb::read32(uint32_t off) {
    switch (off & 0x3FC) {
        // main ($000): dafb_r
        case 0x00: return (base_ >> 9) & 0xFFF;
        case 0x04: return (base_ >> 5) & 0x0F;
        case 0x08: return stride_ >> 2;           // stride in 32-bit words
        case 0x10: return config_;
        case 0x1C: {
            // Inverse of monitor sense (dafb_r $1c). Plain codes come back
            // whole; extended (type 6/7) codes are probed by driving one ID
            // pin at a time ($1C writes) and reading the other two.
            uint8_t mon = monitorConfig_;
            uint8_t res;
            if (mon & 0x40) {                     // extended code ext(bc,ac,ab)
                res = 7;
                if (monitorId_ == 0x4)
                    res &= uint8_t(4 | (((mon >> 5) & 1) << 1) | ((mon >> 4) & 1));
                if (monitorId_ == 0x2)
                    res &= uint8_t((((mon >> 3) & 1) << 2) | 2 | ((mon >> 2) & 1));
                if (monitorId_ == 0x1)
                    res &= uint8_t((((mon >> 1) & 1) << 2) | ((mon & 1) << 1) | 1);
            } else {
                res = mon;
            }
            return res ^ 7u;
        }
        case 0x24: return regs_[0x24 >> 2];       // SCSI ctrl (vestigial on
        case 0x28: return regs_[0x28 >> 2];       // MEMCjr — TurboSCSI moved
                                                  // into PrimeTime)
        case 0x2C: return (regs_[0x2C >> 2] & 0x1FF) | (3u << 9);  // version 3
        // Swatch ($100): swatch_r
        case 0x108: return intStatus_;
        case 0x10C: intStatus_ &= ~4u; recalcIrq(); return 0;
        case 0x114: intStatus_ &= ~1u; recalcIrq(); return 0;
        case 0x124: case 0x128: case 0x12C: case 0x130: case 0x134:
        case 0x138: case 0x13C: case 0x140: case 0x144: case 0x148:
            return hParams_[((off & 0x3FC) - 0x124) >> 2];
        case 0x14C: case 0x150: case 0x154: case 0x158: case 0x15C:
        case 0x160: case 0x164:
            return vParams_[((off & 0x3FC) - 0x14C) >> 2];
        // RAMDAC ($200): ramdac_r (Antelope PCBR1 dance)
        case 0x200: palIdx_ = 0; return palAddress_;
        case 0x210: {
            uint8_t c = clut_[palAddress_][palIdx_ % 3];
            if (++palIdx_ == 3) palIdx_ = 0;
            return c;
        }
        case 0x220:
            if (palAddress_ == 1 && (ac842Pbctrl_ & 0x06) == 0x06)
                return pcbr1_;
            return ac842Pbctrl_;
        default:
            if ((off & 0x300) == 0x300) return 0;   // Gazelle clockgen
            return regs_[(off >> 2) & 0xFF];
    }
}

void Dafb::write32(uint32_t off, uint32_t v) {
    uint32_t idx = (off >> 2) & 0xFF;
    regs_[idx] = v;
    switch (off & 0x3FC) {
        case 0x00:
            base_ = (base_ & 0x1E0) | ((v & 0xFFF) << 9);
            break;
        case 0x04:
            base_ = (base_ & ~0x1E0u) | ((v & 0x0F) << 5);
            break;
        case 0x08:
            stride_ = v << 2;                     // register is 32-bit words
            break;
        case 0x10:
            config_ = uint16_t(v);
            break;
        case 0x1C:
            // Drive monitor sense lines (dafb_w $1c): 0 = drive, 1 = tri-state.
            monitorId_ = uint8_t((v & 7) ^ 7);
            break;
        case 0x100:                               // Swatch mode; bit 0 = blank
            swatchMode_ = v;
            break;
        case 0x104:                               // Swatch int enable
            swatchIntEnable_ = v;
            if (!(v & 1)) intStatus_ &= ~1u;
            if (!(v & 4)) intStatus_ &= ~4u;
            recalcIrq();
            break;
        case 0x10C: intStatus_ &= ~4u; recalcIrq(); break;
        case 0x114: intStatus_ &= ~1u; recalcIrq(); break;
        case 0x118: cursorLine_ = v & 0xFFF; break;
        case 0x124: case 0x128: case 0x12C: case 0x130: case 0x134:
        case 0x138: case 0x13C: case 0x140: case 0x144: case 0x148:
            // HSERR/HLFLN/HEQ/HSP/HBWAY/HBRST/HBP/HAL/HFP/HPIX
            hParams_[((off & 0x3FC) - 0x124) >> 2] = uint16_t(v);
            break;
        case 0x14C: case 0x150: case 0x154: case 0x158: case 0x15C:
        case 0x160: case 0x164:
            // VHLINE/VSYNC/VBPEQ/VBP/VAL/VFP/VFPEQ (half-line units)
            vParams_[((off & 0x3FC) - 0x14C) >> 2] = uint16_t(v);
            break;
        case 0x200: palAddress_ = uint8_t(v); palIdx_ = 0; break;
        case 0x210:
            clut_[palAddress_][palIdx_] = uint8_t(v);
            if (++palIdx_ == 3) { palIdx_ = 0; palAddress_++; }
            break;
        case 0x220:                               // Antelope PCBR0/PCBR1
            if (palAddress_ == 1 && (ac842Pbctrl_ & 0x06) == 0x06)
                pcbr1_ = uint8_t(v & 0xF0) | 0x02;   // Antelope version ID
            else {
                ac842Pbctrl_ = uint8_t(v);
                if ((pcbr1_ & 0xC0) == 0xC0 && (ac842Pbctrl_ & 0x06) == 0x06) {
                    mode_ = 5;                    // Antelope x555
                } else {
                    switch (ac842Pbctrl_ & 0x1C) {
                        case 0x00: mode_ = 0; break; // 1 bpp
                        case 0x08: mode_ = 1; break; // 2 bpp
                        case 0x10: mode_ = 2; break; // 4 bpp
                        case 0x18: mode_ = 3; break; // 8 bpp
                        case 0x1C: mode_ = 4; break; // 24 bpp
                    }
                }
                recalcMode();                     // ramdac_w → recalc_mode()
            }
            break;
        default: break;
    }
}

// dafb.cpp recalc_mode(): derive the active area and totals from the
// Swatch CRTC parameters. HFP-HAL is the active width; VAL/VFP are in
// half-line units (interlace). The AC842a clock-divider bits (PCBR0
// 6-5) multiply the width in non-convolved modes and divide it when
// convolution (config bit 3) is on; MEMCjr machines never enable
// convolution (no NTSC/PAL support), the branch is kept for register
// parity. Interlace (config bit 2) doubles the vertical.
void Dafb::recalcMode() {
    enum { HAL = 7, HFP = 8, HPIX = 9, VAL = 4, VFP = 5, VFPEQ = 6 };
    htotal_ = hParams_[HPIX];
    vtotal_ = vParams_[VFPEQ] >> 1;
    if (!htotal_ || !vtotal_) return;

    hres_ = uint32_t(hParams_[HFP]) - hParams_[HAL];
    vres_ = uint32_t(vParams_[VFP] >> 1) - (vParams_[VAL] >> 1);

    const uint32_t clockdiv = 1u << ((ac842Pbctrl_ & 0x60) >> 5);
    if (config_ & 0x08) {                        // convolution (see above)
        hres_ /= clockdiv;
        hres_ -= 23;                             // dafb.cpp Q700 quirk
    } else {
        hres_ *= clockdiv;
        htotal_ *= clockdiv;
    }
    if (config_ & 0x04) {                        // interlace
        vres_ <<= 1;
        vtotal_ <<= 1;
    }
}

// Gazelle clock generator (dafb_memcjr clockgen_w): a 20-bit word is
// bit-banged into byte port $3C3 — bit 0 = data, bit 1 = clock (latch
// on rising edge). Layout (LSB first): bit 0 = /pclk-select, bits 5-4 =
// log2 P, bits 12-6 = N, bits 19-13 = M; clock = N/(M·P) × 31.3344 MHz.
void Dafb::clockgenWrite8(uint32_t off, uint8_t v) {
    if ((off & 0xFF) != 0xC3) return;
    if ((v & 2) && !(gazLastClock_ & 2)) {
        gazShift_ >>= 1;
        gazShift_ |= (v & 1) ? (1u << 19) : 0;
        if (++gazBits_ == 20) {
            gazBits_ = 0;
            const bool pclkSelect = !(gazShift_ & 1);
            const uint32_t P = 1u << ((gazShift_ >> 4) & 3);
            const uint32_t N = (gazShift_ >> 6) & 0x7F;
            const uint32_t M = (gazShift_ >> 13) & 0x7F;
            if (M && P) {
                const uint32_t clk =
                    uint32_t(31334400.0 * double(N) / (double(M) * double(P)));
                if (pclkSelect) pixelClock_ = clk;
                else            gazMclk_ = clk;
            }
            gazShift_ = 0;
        }
    }
    gazLastClock_ = v;
}

// Swatch frame clock: derived from the programmed CRTC totals and the
// Gazelle pixel clock once the ROM has set them (640×480 Hi-Res =
// 896 × 525 at the driver's programmed pclk); the 60 Hz / 525-line
// legacy shape covers pre-init. The VBL timer fires at line 480, the
// cursor timer at the programmed line, each frame, when enabled by
// Swatch $104 (dafb.cpp vbl_tick/cursor_tick — MAME hardcodes line 480
// for VBL too).
void Dafb::tick(int cpuCycles) {
    framePos_ += cpuCycles;
    int64_t frameLen = cpuHz_ / 60;
    int totalLines = 525;
    if (htotal_ && vtotal_ > 480 && pixelClock_ >= 1000000) {
        frameLen = int64_t(htotal_) * vtotal_ * cpuHz_ / pixelClock_;
        totalLines = int(vtotal_);
    }
    if (framePos_ >= frameLen) framePos_ -= frameLen;
    int line = int(framePos_ * totalLines / frameLen);
    if (line != prevLine_) {
        bool wrap = line < prevLine_;
        auto crossed = [&](int target) {
            return wrap ? (target > prevLine_ || target <= line)
                        : (target > prevLine_ && target <= line);
        };
        uint8_t st = intStatus_;
        if ((swatchIntEnable_ & 1) && crossed(480)) st |= 1;
        if ((swatchIntEnable_ & 4) && crossed(int(cursorLine_ % totalLines)))
            st |= 4;
        prevLine_ = line;
        if (st != intStatus_) { intStatus_ = st; recalcIrq(); }
    }
}
