// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "TobyVideo.h"
#include <algorithm>

TobyVideo::TobyVideo(NuBus& bus, int slot) : bus_(bus), slot_(slot) {
    vram_.assign(kVramSize / 4, 0);
    reset();
}

void TobyVideo::reset() {
    std::fill(regs_.begin(), regs_.end(), 0);
    // Bt453 entries are indexed HIGH-aligned by decode() (pens_[px & 0x80]
    // at 1 bpp), so the old pens_[1] = black seeding no longer corresponds to
    // "bit set" — the driver programs 0-127 black / 128-255 white itself.
    std::fill(pens_.begin(), pens_.end(), 0x00FFFFFFu);
    for (int i = 0; i < 128; i++) pens_[i] = 0x00000000u;   // pre-DAC default
    mode_ = 0;
    vblDisable_ = true;
    hres_ = W;
    vres_ = H;
    htotal_ = 896;                               // MAME power-on defaults
    vtotal_ = 525;
    frameCycles_ = kCpuHz / 60;                  // until the CRTC is programmed
    framePos_ = 0;
    vblAcc_ = 0;
    vblLine_ = false;
    dacAddr_ = 0;
}

uint32_t TobyVideo::mapOff(uint32_t slotOff) const {
    // MAME card_map uses .mirror(0xf00000) — ignore A20-A23 for the
    // low 1 MB decode window (VRAM/TFB/DAC/VBL).
    return slotOff & 0x0FFFFFu;
}

uint8_t TobyVideo::read8(uint32_t slotOff) {
    uint32_t r = mapOff(slotOff);
    if (r < kVramSize)
        return uint8_t(~uint8_t(vram_[r / 4] >> (24 - (r % 4) * 8)));
    if (r >= 0x90000 && r < 0x90020) {
        // MAME maps the Bt453 with .umask32(0xff000000), so its handler's
        // `offset & 3` is the 32-bit WORD index masked — not the byte index.
        // Decoding (byte & 3) picked a lane the hardware does not use, so the
        // real TFB driver's writes ($9001C address, $90018 data — both word
        // offsets 7 and 6) fell through and the CLUT was never programmed.
        if ((r & 3) != 0) return 0;              // only byte lane 0 is wired
        int o = (int(r - 0x90000) >> 2) & 3;
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
        // MAME vbl_w (nubus_m2video.cpp:270-281): bit 2 of the offset selects
        // disable (unconditional, IRQ line untouched) vs enable+ack. The
        // disable was once gated on `vramWrites == 0` ("keep VBL armed after
        // first paint", a synthetic-decl-ROM-era guard); System 7.6's video
        // driver teardown disables the card here, then SIntRemoves its
        // handler, then unmasks — with the disable swallowed the slot 9 line
        // stayed asserted against an empty queue and the ROM dispatcher
        // recursed the stack 6 MB into the system heap (the
        // iifx_persist_etalon F-LINE storm at pc=1).
        if (r & 4) {
            vblDisable_ = true;
        } else {
            vblDisable_ = false;
            vblEnableWrites++;
            bus_.setSlotIrq(slot_, false);
        }
        return;
    }
    if (r >= 0x80000 && r < 0x90000) {
        // TFB register file — byte path. The machine splits every slot
        // write into bytes (MacIIMemory → NuBus::write8), so the register
        // decode must live here, not only in write32: MAME tfb_w stores
        // `(data ^ 0xffffffff) & 0xff` whatever the lane (nubus_m2video.cpp
        // :253-268), so the last lane of a long write carries the value.
        // Before this branch the guest's CRTC/depth programming was
        // silently DROPPED — unnoticed because the reset defaults happen
        // to be the only mode the ROM uses (640×480×1).
        tfbWrite(int((r - 0x80000) >> 2) & 0xF, uint32_t(v), 0xFFFFFFFFu);
        return;
    }
    if (r >= 0x90000 && r < 0x90020) {
        if ((r & 3) != 0) return;                // only byte lane 0 is wired
        v ^= 0xFF;
        int o = (int(r - 0x90000) >> 2) & 3;     // word index, per MAME's umask32
        if (o == 1 || o == 3) dacAddr_ = v;      // palette_w is unconditional
        else if (o == 2) {
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
            // Inverted, like write32 above and MAME's vram_w: the byte path
            // stored raw, so pixel values and the (inverted) DAC address were
            // in opposite polarities.
            w = (w & ~uint32_t(0xFFu << sh)) | (uint32_t(uint8_t(~v)) << sh);
            vram_[idx] = w;
            vramWrites++;
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
    // CRTC-derived frame clock (LLE — the DAFB Q8.1 treatment applied to
    // Toby; MAME nubus_m2video.cpp calc_screen_params): the frame period is
    // htotal × vtotal ticks of the card's 30.24 MHz pixel crystal
    // (`attotime::from_ticks(m_htotal * m_vtotal, 30.24_MHz_XTAL)`).
    const uint32_t hsyncstart  = regs_[HSYNCSTART] + 2;
    const uint32_t hsyncfinish = regs_[HSYNCFINISH] + 2;
    const uint32_t hearly      = (regs_[HALFLINE_EARLY] & 0x7f) + 2;
    const uint32_t hlate       = (regs_[HPIXELS_HLATE] & 0x3f) + 2;
    const uint32_t vfrontporch = (regs_[VLINES_VPP] & 0x1f) + 1;
    const uint32_t vsyncfinish = (regs_[SYNCINTERVAL8] & 0x7f) + 1;
    const uint32_t vbackporch  = (regs_[VBACKPORCH] & 0x3f) + 8;
    htotal_ = (halfline + hpixels + hsyncstart + hsyncfinish + hearly + hlate)
              * (16u >> mode_);
    vtotal_ = (vfrontporch * 2) + vsyncfinish + (vbackporch * 2) + (vlines / 2);
    frameCycles_ = int64_t(kCpuHz) * htotal_ * vtotal_ / kPixClockHz;
    // A half-programmed CRTC (the driver writes registers one by one)
    // yields nonsense totals; every real Toby mode refreshes near 60-67 Hz
    // (≥ ~230k CPU cycles). Keep the 60 Hz fallback until the programmed
    // frame is plausible so mid-programming states can't storm the VBL.
    if (frameCycles_ < 50000) frameCycles_ = kCpuHz / 60;
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
    // Frame clock derived from the guest-programmed CRTC (calcScreenParams:
    // htotal × vtotal pixel-crystal ticks converted to CPU cycles). Before
    // the driver programs the CRTC the fallback is a plain 60 Hz frame.
    framePos_ += cpuCycles;
    while (framePos_ >= frameCycles_) {
        framePos_ -= frameCycles_;
        // Completed frames, for the raster beam (VideoBeam::setPos): the
        // position alone is modulo, so a decoder sampling once per frame at
        // a fixed phase could not tell a whole frame from no time at all.
        frameCount_++;
        vblPulse();
    }
    // Active-display vs blanking for the $D0000 sense port: active for the
    // visible-lines fraction of the frame, blanking for the rest.
    const int64_t active = vtotal_
        ? frameCycles_ * vres_ / int(vtotal_)
        : frameCycles_ * 480 / 525;
    vblLine_ = framePos_ >= active;
}

void TobyVideo::decode(std::vector<uint32_t>& out) const {
    out.assign(size_t(hres_) * size_t(vres_), 0xFFFFFFFFu);
    decodeRows(out, 0, vres_);
}

// Advance the beam off the card's own CRTC frame clock and decode the rows
// it has crossed. `active` is the visible fraction of the frame, the same
// ratio tick() uses for the $D0000 blanking sense — one geometry, not two.
void TobyVideo::raster(std::vector<uint32_t>& out) {
    const size_t need = size_t(hres_) * size_t(vres_);
    if (out.size() != need) {
        out.assign(need, 0xFFFFFFFFu);
        beam_.restartFrame();
    }
    const int64_t active = vtotal_
        ? frameCycles_ * vres_ / int(vtotal_)
        : frameCycles_ * 480 / 525;
    beam_.setGeometry(frameCycles_, active,
                      vtotal_ ? int(vtotal_) : 525, vres_);
    if (!beam_.valid()) { decodeRows(out, 0, vres_); return; }
    beam_.setPos(framePos_, frameCount_);
    beam_.pumpRows([&](int a, int b) { decodeRows(out, a, b); });
}

void TobyVideo::decodeRows(std::vector<uint32_t>& out, int y0, int y1) const {
    if (out.size() < size_t(hres_) * size_t(vres_)) return;
    y0 = y0 < 0 ? 0 : y0;
    y1 = y1 > vres_ ? vres_ : y1;
    if (y0 >= y1) return;
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
        for (int y = y0; y < y1; y++)
            for (int x = 0; x < hres_ / 8; x++) {
                uint8_t px = be8(kBase + uint32_t(y * 128 + x));
                for (int b = 0; b < 8; b++) {
                    int xi = x * 8 + b;
                    if (xi < hres_)
                        out[y * hres_ + xi] = pens_[(px << b) & 0x80] | 0xFF000000u;
                }
            }
        break;
    case 1:
        for (int y = y0; y < y1; y++)
            for (int x = 0; x < hres_ / 4; x++) {
                uint8_t px = be8(kBase + uint32_t(y * 256 + x));
                for (int b = 0; b < 4; b++) {
                    int xi = x * 4 + b;
                    if (xi < hres_)
                        out[y * hres_ + xi] = pens_[(px << (b * 2)) & 0xC0] | 0xFF000000u;
                }
            }
        break;
    case 2:
        for (int y = y0; y < y1; y++)
            for (int x = 0; x < hres_ / 2; x++) {
                uint8_t px = be8(kBase + uint32_t(y * 512 + x));
                if (x * 2 < hres_) out[y * hres_ + x * 2]     = pens_[px & 0xF0] | 0xFF000000u;
                if (x * 2 + 1 < hres_) out[y * hres_ + x * 2 + 1] = pens_[(px & 0x0F) << 4] | 0xFF000000u;
            }
        break;
    case 3:
        for (int y = y0; y < y1; y++)
            for (int x = 0; x < hres_; x++)
                out[y * hres_ + x] = pens_[be8(kBase + uint32_t(y * 1024 + x))] | 0xFF000000u;
        break;
    }
}
