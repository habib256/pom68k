// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Toby / Mac II Video Card HLE (TFB 344-0001 + Bt453 RAMDAC).
// Register map from MAME nubus_m2video.cpp; decl ROM is external.

#pragma once
#include "NuBus.h"
#include "VideoBeam.h"
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

    const std::array<uint8_t, 16>& regs() const { return regs_; }
    bool vblDisabled() const { return vblDisable_; }
    long vblEnableWrites = 0;            // diagnostic: count of enable-side writes
    long tfbWrites = 0;
    long vramWrites = 0;
    uint8_t mode() const { return mode_; }
    int hres() const { return hres_; }
    int vres() const { return vres_; }
    // CRTC-derived frame geometry/clock (MAME nubus_m2video parity).
    uint32_t htotal() const { return htotal_; }
    uint32_t vtotal() const { return vtotal_; }
    int64_t frameCycles() const { return frameCycles_; }
    const std::array<uint32_t, 256>& palette() const { return pens_; }
    const std::vector<uint32_t>& vram() const { return vram_; }

    // Host decode: 00RRGGBB pixels, W×H (alpha forced — this card's
    // consumers upload straight from the surface).
    void decode(std::vector<uint32_t>& out) const;
    // Render visible rows [y0, y1) into an existing hres×vres surface.
    void decodeRows(std::vector<uint32_t>& out, int y0, int y1) const;
    // Advance the beam off the card's OWN CRTC-derived frame clock and
    // decode the rows it has crossed (LLE_VS_HLE §1.1, VideoBeam.h).
    void raster(std::vector<uint32_t>& out);
    const VideoBeam& beam() const { return beam_; }

    // ── Save states (SaveState.h contract) ──────────────────────────────
    // The whole card travels — VRAM (u32 words, no zero-run codec; half a
    // megabyte, dwarfed by the machine RAM blob), TFB registers, Bt453
    // CLUT and the CRTC-derived frame clock. bus_/slot_/irqCb_ are wiring.
    template <class Ar> void visit(Ar& ar) {
        ar(vram_, regs_, pens_, dacAddr_, dacRgb_, dacComp_, mode_, vblDisable_,
           hres_, vres_, htotal_, vtotal_,
           frameCycles_, framePos_, vblAcc_, vblLine_, frameCount_);
    }

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
    // Bt453 component cycle: which of R/G/B the next palette access is,
    // and the two components already latched for the entry being written.
    // Live state, not scratch — a guest can be interrupted between the red
    // and the blue write, and a snapshot has to resume mid-entry.
    uint8_t dacRgb_ = 0;
    uint8_t dacComp_[3] = {};
    // MAME bt45x increment_address (:200-208): step the component index,
    // and advance the entry only when it wraps.
    void dacStep() {
        dacRgb_ = uint8_t((dacRgb_ + 1) % 3);
        if (dacRgb_ == 0) dacAddr_++;
    }
    uint8_t mode_ = 0;
    bool vblDisable_ = true;
    int hres_ = W, vres_ = H;
    // Host machine + card crystals: Toby lives on the Mac II (15.6672 MHz
    // CPU); the card's pixel clock is a 30.24 MHz crystal (MAME
    // nubus_m2video.cpp:361).
    static constexpr int64_t kCpuHz = 15667200;
    static constexpr int64_t kPixClockHz = 30240000;
    uint32_t htotal_ = 896, vtotal_ = 525;   // MAME power-on defaults
    int64_t frameCycles_ = kCpuHz / 60;      // until the CRTC is programmed
    int64_t framePos_ = 0;
    uint64_t frameCount_ = 0;                // completed frames (raster seq)
    VideoBeam beam_;                         // not serialized: pure cache
    int64_t vblAcc_ = 0;
    bool vblLine_ = false;
    std::function<void(bool)> irqCb_;
};
