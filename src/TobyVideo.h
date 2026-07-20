// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Toby / Mac II Video Card HLE (TFB 344-0001 + Bt453 RAMDAC).
// Register map from MAME nubus_m2video.cpp; decl ROM is external.

#pragma once
#include "NuBus.h"
#include <array>
#include <cstdint>
#include <vector>

class TobyVideo : public NuBusDevice {
public:
    static constexpr int W = 640;
    static constexpr int H = 480;
    static constexpr uint32_t kVramSize = 0x80000;

    explicit TobyVideo(NuBus& bus, int slot = 9);

    void reset();
    void setIrqHandler(std::function<void(bool)> cb) { irqCb_ = std::move(cb); }

    uint8_t  read8(uint32_t slotOff) override;
    uint16_t read16(uint32_t slotOff) override;
    uint32_t read32(uint32_t slotOff) override;
    void write8(uint32_t slotOff, uint8_t v) override;
    void write16(uint32_t slotOff, uint16_t v) override;
    void write32(uint32_t slotOff, uint32_t v) override;
    void tick(int cpuCycles) override;

    uint8_t mode() const { return mode_; }
    int hres() const { return hres_; }
    int vres() const { return vres_; }
    const std::array<uint32_t, 256>& palette() const { return pens_; }
    const std::vector<uint32_t>& vram() const { return vram_; }

    // Host decode: 00RRGGBB pixels, W×H.
    void decode(std::vector<uint32_t>& out) const;

private:
    enum Reg {
        LENGTH = 0, MISC, BASEHI, BASELO, SYNCINTERVAL = 4,
        VLINES_VPP = 5, VLINES = 6, VBACKPORCH = 7, SYNCINTERVAL8 = 8,
        HSYNCSTART, HSYNCFINISH, HALFLINE_EARLY, HALFLINE, HPIXELS_HLATE,
        HPIXELS, MISC2 = 15
    };

    void tfbWrite(int reg, uint32_t data, uint32_t memMask);
    void calcScreenParams();
    void vblPulse();
    uint32_t mapOff(uint32_t slotOff) const;

    NuBus& bus_;
    int slot_;
    std::vector<uint32_t> vram_;
    std::array<uint8_t, 16> regs_{};
    std::array<uint32_t, 256> pens_{};
    uint8_t dacAddr_ = 0;
    bool dacWrite_ = false;
    uint8_t mode_ = 0;
    bool vblDisable_ = true;
    int hres_ = W, vres_ = H;
    int64_t framePos_ = 0;
    int64_t vblAcc_ = 0;
    bool vblLine_ = false;
    std::function<void(bool)> irqCb_;
};
