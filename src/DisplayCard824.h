// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Apple Macintosh Display Card 8•24 (MDC 1.2, 341-0868) ──
// A NuBus video card: JMFB framebuffer controller, CRTC, clock synthesizer
// and RAMDAC with 1 MB of VRAM — 1/2/4/8 bpp indexed and 24-bit direct
// colour at 640×480, eight-bit indexed up to 1152×870. The guest's own
// declaration-ROM driver programs every register; nothing here invents a
// mode.
//
// Source of truth: MAME 0.285 src/devices/bus/nubus/nubus_48gc.cpp (jmfb
// device, 8•24 profile): register map :349-355, reset values :480-508,
// sense table :244-263, CRTC/clock arithmetic :748-811, beam status :856-936,
// RAMDAC :1073-1106, clock synthesizer :1108-1141, packed RGB view
// :1143-1158. The declaration ROM is the user's 32 KB 341-0868 dump,
// byte lane 3 (`$78`), installed at the top of the slot (MAME
// install_declaration_rom without mirroring). MAME's macii.cpp:995 puts
// this card in slot 9 by default.
//
// The bus. Every 68020 access reaches a NuBus card here as bytes
// (MacIIMemory splits words; Moira splits longs into two words), but the
// JMFB/CRTC/RAMDAC registers are wired to the low data lanes and "the
// firmware relies on smearing" (MAME): the 68020 replicates a byte or word
// onto the lanes a 32-bit port reads. `RegLatch` rebuilds the one bus cycle
// the CPU made — a long write is ONE register write carrying its low word,
// never two — from the byte sequence (see writeReg()).
//
// Not modelled (MAME neither): interlaced modes, so the NTSC/PAL monitors
// are not offered; precise interrupt timing within the line.
// Gates: tests/display_card824_test.cpp, tests/macii_824_etalon.cpp
// (cmake/Pom68kMachineGates.cmake registers its variants).

#pragma once
#include "DeviceSnapshot.h"
#include "NuBus.h"
#include "SaveState.h"
#include "VideoBeam.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

class DisplayCard824 : public NuBusDevice, public NuBusDisplay {
public:
    static constexpr uint32_t kVramSize = 0x100000;          // 1 MB (8•24)
    static constexpr std::size_t kDeclRomSize = 0x8000;      // 341-0868

    // Monitors the card's sense lines can report (MAME f_monitors index).
    // Offered: the four colour displays, each taken to the Finder by the
    // card's own driver (macii_824_etalon). Not offered: the interlaced
    // NTSC/PAL entries, which need modes neither MAME nor this model
    // implement, and the monochrome Portrait and Two-Page displays — on
    // them the driver's final CLUT load leaves the blue channel the mono
    // scan-out reads at zero, a black screen MAME's identical scan-out
    // cannot arbitrate (`macii_824_etalon --monitor portrait` with the
    // names restored reproduces it; TODO.md).
    enum Monitor : uint8_t {
        kRgb21 = 0, kPortraitMono = 1, kRgb12 = 2, kTwoPageMono = 3,
        kHiRes = 6, kRgb16 = 13
    };
    static bool monitorSupported(uint8_t m) {
        return m == kRgb21 || m == kRgb12 || m == kHiRes || m == kRgb16;
    }
    // Startup spelling ↔ monitor ("hires", "rgb21", …); false if unknown.
    static bool monitorFromName(const std::string& name, uint8_t& out);
    // POM68K_MONITOR's width ↔ monitor (512, 640, 832, 1152), the spelling
    // the V8 and Sonora boards' built-in video already use; false if none.
    static bool monitorFromWidth(int width, uint8_t& out);

    DisplayCard824(NuBus& bus, int slot, int64_t cpuHz,
                   uint8_t monitor = kHiRes);

    // Power-on/reset state of the card (MAME device_reset). VRAM is cleared.
    void reset();
    int slot() const { return slot_; }
    uint8_t monitor() const { return monitor_; }

    // The raw 32 KB ROM as dumped → the bus image installed at the top of
    // the slot. Empty when the bytes are not a byte-lane-3 declaration ROM.
    static std::vector<uint8_t> installDeclRom(const std::vector<uint8_t>& raw);
    // installDeclRom() of the file at `path`; empty when it is missing or
    // is not this card's ROM.
    static std::vector<uint8_t> loadDeclRom(const std::string& path);
    // Where the dump is looked for, relative to the repository root (the
    // machine also tries each from build/). The archive path is the layout
    // the MAME romset ships it under (assets.lock).
    static constexpr const char* kDeclRomCandidates[] = {
        "roms/341-0868.bin",
        "roms/archive/macroms/Misc/Video cards/Apple Macintosh Display Card 8-24/Trident 8-24/341-0868.BIN",
    };

    // ── NuBusDevice ─────────────────────────────────────────────────────
    uint8_t  read8(uint32_t slotOff) override;
    uint16_t read16(uint32_t slotOff) override;
    uint32_t read32(uint32_t slotOff) override;
    void write8(uint32_t slotOff, uint8_t v) override;
    void write16(uint32_t slotOff, uint16_t v) override;
    void write32(uint32_t slotOff, uint32_t v) override;
    void tick(int cpuCycles) override;

    // ── NuBusDisplay ────────────────────────────────────────────────────
    void raster(std::vector<uint32_t>& out) override;
    int width() const override { return width_; }
    int height() const override { return height_; }
    const char* displayName() const override { return "Display Card 8•24"; }

    // Whole visible frame, 00RRGGBB, width()×height() (tests, publish).
    void decode(std::vector<uint32_t>& out) const;

    // ── Inspection (tests, status) ──────────────────────────────────────
    uint16_t control() const { return control_; }
    bool scanning() const { return (control_ >> 6) & 1; }   // transfer bit
    uint8_t ramdacMode() const { return ramdacMode_; }
    int bitsPerPixel() const;
    uint32_t base() const { return base_; }
    uint32_t stride() const { return stride_; }
    bool vblEnabled() const { return !vblDisable_; }
    bool irqLevel() const { return irq_; }
    int64_t frameCycles() const { return frameCycles_; }
    uint32_t pen(int i) const { return pens_[size_t(i) & 0xFF]; }
    const std::vector<uint8_t>& vram() const { return vram_; }
    uint64_t frameCount() const { return frameCount_; }
    // Before the card's ROM ran: geometry not yet programmed.
    bool configured() const { return crtcValid_; }

    // ── Debugger (DeviceSnapshot.h): members only, never a register read.
    void debugFields(std::vector<pom68k::dev::Field>& out) const {
        POM_DEVICE_FIELDS(out, monitor_, control_, preload_, base_, stride_,
                          ramdacMode_, clutOffs_, vblDisable_, irq_, width_,
                          height_, frameCycles_, frameCount_);
    }

    // ── Save states (SaveState.h contract) ──────────────────────────────
    // The card is guest state end to end: VRAM (zero-run blob), JMFB,
    // CRTC, clock synthesizer, RAMDAC and CLUT, the frame clock and the
    // live slot line. Its slot, monitor and declaration ROM are setup
    // (the machine records which slots carry a card and refuses a
    // mismatch). The beam cursor is a cache.
    template <class Ar> void visit(Ar& ar) {
        ar.blob(vram_);
        ar(control_, preload_, base_, stride_, colors_, clutCnt_, clutOffs_,
           ramdacMode_, ramdacConv_, hhalf_, hactive_, hbporch_, hsync_,
           hfporch_, vactive_, vbporch_, vsync_, vfporch_, vblDisable_,
           multiplier_, modulus_, pdiv_, pens_, latch_, latchMask_,
           latchReg_, framePos_, frameCount_, irq_);
        if constexpr (Ar::loading) {
            if (vram_.size() != kVramSize || clutCnt_ > 2) { ar.fail(); return; }
            updateCrtc();
        }
    }

private:
    uint32_t regRead(uint32_t reg) const;           // reg = slotOff & ~3
    void regWrite(uint32_t reg, uint32_t value);
    void writeReg(uint32_t slotOff, uint8_t v);
    void flushLatch();
    void updateCrtc();
    void setIrq(bool on);
    uint8_t senseBits() const;

    uint8_t vramRead(uint32_t off) const;
    void vramWrite(uint32_t off, uint8_t v);
    bool packedView() const { return (control_ >> 2) & 1; }
    bool convolution() const { return (control_ >> 5) & 1; }
    void decodeRows(std::vector<uint32_t>& out, int y0, int y1) const;
    uint32_t screenPixel(uint32_t rgb) const;

    NuBus& bus_;
    int slot_;
    int64_t cpuHz_;
    uint8_t monitor_;

    std::vector<uint8_t> vram_;
    uint16_t control_ = 0x0002, preload_ = 256 - 8;
    uint32_t base_ = 0, stride_ = 80 / 4;
    std::array<uint8_t, 3> colors_{};
    uint8_t clutCnt_ = 0, clutOffs_ = 0;
    uint8_t ramdacMode_ = 0, ramdacConv_ = 0;
    uint16_t hhalf_ = 0, hactive_ = 286, hbporch_ = 22, hsync_ = 30, hfporch_ = 18;
    uint16_t vactive_ = 1740, vbporch_ = 78, vsync_ = 6, vfporch_ = 6;
    bool vblDisable_ = true;
    uint16_t multiplier_ = 190, modulus_ = 19;
    uint8_t pdiv_ = 1;
    std::array<uint32_t, 256> pens_{};

    // One CPU bus cycle rebuilt from its bytes (writeReg()).
    std::array<uint8_t, 4> latch_{};
    uint8_t latchMask_ = 0;          // lanes written in the current cycle
    uint32_t latchReg_ = 0;          // register the cycle addresses

    // Derived from the CRTC + clock synthesizer (updateCrtc, not saved).
    bool crtcValid_ = false;
    int width_ = 640, height_ = 480;
    int vlines_ = 525, top_ = 0;
    int hpixels_ = 800, left_ = 0, halflinePixels_ = 0;   // screen pixels
    int64_t frameCycles_ = 0;        // a whole frame, in CPU cycles
    int64_t activeCycles_ = 0;       // visible part, from the first visible line

    // Frame clock: 0 = first visible line, VBL at activeCycles_.
    int64_t framePos_ = 0;
    uint64_t frameCount_ = 0;
    bool irq_ = false;

    VideoBeam beam_;
};
