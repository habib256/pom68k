// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "TobyVideo.h"
#include <algorithm>

namespace {
constexpr int64_t kFrameTotal = 800 * 525;
constexpr int64_t kVblStart   = 800 * 480;
} // namespace

TobyVideo::TobyVideo(NuBus& bus, int slot) : bus_(bus), slot_(slot) {
    vram_.assign(kVramSize / 4, 0);
    reset();
}

void TobyVideo::reset() {
    std::fill(regs_.begin(), regs_.end(), 0);
    std::fill(pens_.begin(), pens_.end(), 0x00FFFFFFu);
    pens_[0] = 0x00FFFFFFu;                      // white (00RRGGBB)
    pens_[1] = 0x00000000u;                      // black
    mode_ = 0;
    vblDisable_ = true;
    hres_ = W;
    vres_ = H;
    framePos_ = 0;
    vblAcc_ = 0;
    vblLine_ = false;
    dacAddr_ = 0;
    dacWrite_ = false;
}

uint32_t TobyVideo::mapOff(uint32_t slotOff) const {
    // MAME card_map uses .mirror(0xf00000) — ignore A20-A23 for the
    // low 1 MB decode window (VRAM/TFB/DAC/VBL).
    return slotOff & 0x0FFFFFu;
}

uint8_t TobyVideo::read8(uint32_t slotOff) {
    uint32_t r = mapOff(slotOff);
    if (r < kVramSize)
        return uint8_t(vram_[r / 4] >> (24 - (r % 4) * 8));
    if (r >= 0x90000 && r < 0x90020) {
        int o = int(r - 0x90000) & 3;
        if (o == 2) return uint8_t(pens_[dacAddr_] ^ 0xFFFFFFFFu);
        if (o == 1 || o == 3) return uint8_t(dacAddr_ ^ 0xFF);
        return 0;
    }
    if (r >= 0xD0000 && r < 0xE0000)
        return vblLine_ ? 0x00 : 0xFF;
    return 0xFF;
}

uint16_t TobyVideo::read16(uint32_t slotOff) {
    return uint16_t(read8(slotOff) << 8) | read8(slotOff + 1);
}

uint32_t TobyVideo::read32(uint32_t slotOff) {
    uint32_t r = mapOff(slotOff);
    if (r < kVramSize && (r & 3) == 0) {
        size_t idx = r / 4;
        if (idx < vram_.size()) return vram_[idx] ^ 0xFFFFFFFFu;
    }
    return uint32_t(read8(slotOff) << 24) | uint32_t(read8(slotOff + 1) << 16)
         | uint32_t(read8(slotOff + 2) << 8) | read8(slotOff + 3);
}

void TobyVideo::write8(uint32_t slotOff, uint8_t v) {
    uint32_t r = mapOff(slotOff);
    if (r >= 0xA0000 && r < 0xB0000) {
        if (r & 4) {
            // Primary Init disables VBL before setup (`ADDA #$A0004; CLR.B`).
            // If it never reaches the matching enable @$A0000 (trap/MAC2
            // early return), the ROM $6DD8 wait starves. Keep VBL armed once
            // the framebuffer has been painted.
            if (vramWrites == 0)
                vblDisable_ = true;
        } else {
            vblDisable_ = false;
            vblEnableWrites++;
            bus_.setSlotIrq(slot_, false);
        }
        return;
    }
    if (r >= 0x90000 && r < 0x90020) {
        v ^= 0xFF;
        int o = int(r - 0x90000) & 3;
        if (o == 1 || o == 3) { dacAddr_ = v; dacWrite_ = (o == 3); }
        else if (o == 2 && dacWrite_) {
            pens_[dacAddr_ & 0xFF] = 0xFF000000u | uint32_t(v) * 0x010101u;
            dacAddr_++;
        }
        return;
    }
        if (r < kVramSize) {
            size_t idx = r / 4;
            if (idx >= vram_.size()) return;
            const int sh = 24 - int(r % 4) * 8;
            uint32_t w = vram_[idx];
            w = (w & ~uint32_t(0xFFu << sh)) | (uint32_t(v) << sh);
            vram_[idx] = w;
            vramWrites++;
            // Arm VBL once Primary Init starts painting — stands in for the
            // Decl ROM CLR.B @$A0000 that successful init would perform.
            if (vblDisable_) {
                vblDisable_ = false;
                vblEnableWrites++;
            }
            return;
        }
}

void TobyVideo::write16(uint32_t slotOff, uint16_t v) {
    write8(slotOff, uint8_t(v >> 8));
    write8(slotOff + 1, uint8_t(v));
}

void TobyVideo::write32(uint32_t slotOff, uint32_t v) {
    uint32_t r = mapOff(slotOff);
    // VBL enable/disable is byte-decoded; a long write still hits bit 2 of
    // the address (MAME umask32 on vbl_w). Route before VRAM.
    if (r >= 0xA0000 && r < 0xB0000) {
        write8(slotOff, uint8_t(v >> 24));
        return;
    }
    if (r >= 0x90000 && r < 0x90020) {
        write8(slotOff, uint8_t(v >> 24));
        write8(slotOff + 1, uint8_t(v >> 16));
        write8(slotOff + 2, uint8_t(v >> 8));
        write8(slotOff + 3, uint8_t(v));
        return;
    }
    if (r < kVramSize && (r & 3) == 0) {
        size_t idx = r / 4;
        if (idx < vram_.size()) vram_[idx] = v ^ 0xFFFFFFFFu;
        return;
    }
    if (r >= 0x80000 && r < 0x90000) {
        tfbWrite(int((r - 0x80000) >> 2) & 0xF, v, 0xFFFFFFFFu);
        return;
    }
}

void TobyVideo::tfbWrite(int reg, uint32_t data, uint32_t memMask) {
    data ^= 0xFFFFFFFFu;
    if (memMask == 0xFF000000u) data >>= 24;
    regs_[reg & 0xF] = uint8_t(data & 0xFF);
    tfbWrites++;
    if ((reg & 0xF) == MISC2) calcScreenParams();
}

void TobyVideo::calcScreenParams() {
    mode_ = (regs_[MISC2] >> 4) & 3;
    const uint32_t halfline = (regs_[HALFLINE] | ((regs_[HALFLINE_EARLY] >> 7) << 8)) + 2;
    const uint32_t hpixels  = ((regs_[HPIXELS] << 2) | ((regs_[HPIXELS_HLATE] >> 6) & 3)) + 2;
    const uint32_t vlines   = ((regs_[VLINES] << 3) | ((regs_[VLINES_VPP] >> 5) & 7)) + 1;
    hres_ = int((halfline + hpixels) * (16 >> mode_));
    vres_ = int(vlines / 2);
    if (hres_ <= 0) hres_ = W;
    if (vres_ <= 0) vres_ = H;
}

void TobyVideo::vblPulse() {
    if (!vblDisable_) {
        // Edge per frame: a level that stays high after the first VBL never
        // re-enters NuBus::setSlotIrq (no-op on same state), so the $6DD8
        // soft-flag wait would miss VIA2 CA1 if the first pulse landed
        // before IER was armed. Real Toby drops the line when SW ACKs the
        // VBL latch; pulse low→high approximates that cadence.
        bus_.setSlotIrq(slot_, false);
        bus_.setSlotIrq(slot_, true);
        if (irqCb_) irqCb_(true);
    }
}

void TobyVideo::tick(int cpuCycles) {
    // Drive VBL at ~60.15 Hz in CPU cycles (Mac II = 15.6672 MHz), not
    // raw pixel clocks — 800×525 as a cycle count undersampled IRQs.
    constexpr int64_t kFrameCycles = 15667200 / 60;   // ≈ 261 120
    framePos_ += cpuCycles;
    while (framePos_ >= kFrameCycles) {
        framePos_ -= kFrameCycles;
        vblPulse();
    }
    // Active-display vs blanking for the $D0000 sense port (any fraction).
    vblLine_ = framePos_ >= (kFrameCycles * 480 / 525);
}

void TobyVideo::decode(std::vector<uint32_t>& out) const {
    out.assign(size_t(hres_) * size_t(vres_), 0xFFFFFFFFu);
    // VRAM is stored as big-endian lanes inside uint32_t words (write8 uses
    // shift 24-(off%4)*8). Reading via host uint8_t* on LE reverses each
    // longword and splits 1-bpp glyphs (Sad Mac halves). Match write8.
    auto be8 = [this](uint32_t byteOff) -> uint8_t {
        const size_t idx = byteOff / 4;
        if (idx >= vram_.size()) return 0;
        return uint8_t(vram_[idx] >> (24 - int(byteOff % 4) * 8));
    };
    constexpr uint32_t kBase = 0x20;              // visible origin in VRAM

    switch (mode_) {
    case 0:
        for (int y = 0; y < vres_; y++)
            for (int x = 0; x < hres_ / 8; x++) {
                uint8_t px = be8(kBase + uint32_t(y * 128 + x));
                for (int b = 0; b < 8; b++) {
                    int xi = x * 8 + b;
                    if (xi < hres_)
                        out[y * hres_ + xi] = pens_[(px >> (7 - b)) & 1] | 0xFF000000u;
                }
            }
        break;
    case 1:
        for (int y = 0; y < vres_; y++)
            for (int x = 0; x < hres_ / 4; x++) {
                uint8_t px = be8(kBase + uint32_t(y * 256 + x));
                for (int b = 0; b < 4; b++) {
                    int xi = x * 4 + b;
                    if (xi < hres_)
                        out[y * hres_ + xi] = pens_[(px >> (6 - b * 2)) & 3] | 0xFF000000u;
                }
            }
        break;
    case 2:
        for (int y = 0; y < vres_; y++)
            for (int x = 0; x < hres_ / 2; x++) {
                uint8_t px = be8(kBase + uint32_t(y * 512 + x));
                if (x * 2 < hres_) out[y * hres_ + x * 2]     = pens_[(px >> 4) & 0xF] | 0xFF000000u;
                if (x * 2 + 1 < hres_) out[y * hres_ + x * 2 + 1] = pens_[px & 0xF] | 0xFF000000u;
            }
        break;
    case 3:
        for (int y = 0; y < vres_; y++)
            for (int x = 0; x < hres_; x++)
                out[y * hres_ + x] = pens_[be8(kBase + uint32_t(y * 1024 + x))] | 0xFF000000u;
        break;
    }
}
