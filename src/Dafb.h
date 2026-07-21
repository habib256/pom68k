// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── DAFB II video cell (MEMCjr integrated, version 3) — MAME dafb.cpp ──
// Register window map (dafb_base::map): +$000 main regs, +$100 Swatch
// CRTC/interrupts, +$200 Antelope RAMDAC (revised AC842a), +$300 Gazelle
// clock generator. Registers below $200 are 12-bit; the MEMCjr's 6+6-bit
// bus-holding split over that window is the MEMORY CONTROLLER's concern
// and lives in Q605Memory (djmemc.cpp dafb_holding_r/w) — this class
// speaks full register values. The frame buffer itself also stays with
// the bus decoder (Q605Memory::vram_): rendering is host-side, so the
// cell only owns the register/CRTC/CLUT/clock state.
//
// Parity status vs dafb.cpp (2026-07-21 pass): Swatch CRTC timing →
// recalc_mode()-derived geometry, Gazelle bit-banged pixel clock,
// extended monitor sense, display-disable bit, VBL/cursor interrupts,
// RAMDAC modes incl. Antelope x555. Remaining gaps: no VRAM
// arbitration/timing; VBL line hard-coded at 480 (as in MAME).
// Gate: tests/q605_dafb_test.cpp.

#pragma once
#include <cstdint>
#include <functional>

class Dafb {
public:
    explicit Dafb(int64_t cpuHz) : cpuHz_(cpuHz) {}

    void reset();

    // Full-value register access at window offsets $000-$3FF (the caller
    // has already applied the MEMCjr holding merge / 12-bit clamp for
    // offsets below $200). Reads carry MAME's side effects (interrupt
    // clears, palette index stepping).
    uint32_t read32(uint32_t off);
    void     write32(uint32_t off, uint32_t v);
    // Gazelle serial byte port ($3C3): bit 0 = data, bit 1 = clock.
    void     clockgenWrite8(uint32_t off, uint8_t v);
    // Raw echo file (byte-lane staging for partial u32 bus writes).
    uint32_t rawReg(uint32_t off) const { return regs_[(off >> 2) & 0xFF]; }
    void     setRawReg(uint32_t off, uint32_t v) { regs_[(off >> 2) & 0xFF] = v; }

    // Frame clock: derives the frame length from the programmed CRTC
    // totals and pixel clock (legacy 60 Hz / 525 lines until the ROM has
    // programmed them); fires VBL (line 480, as MAME) and cursor-line
    // interrupts through onIrq.
    void tick(int cpuCycles);
    std::function<void(bool)> onIrq;     // interrupt summary line (level)

    uint32_t base() const { return base_; }
    uint32_t stride() const { return (config_ & 0x08) ? 1024u : stride_; }
    uint8_t  mode() const { return mode_; }
    uint8_t  depth() const {
        static constexpr uint8_t depths[] = { 1, 2, 4, 8, 24, 16 };
        return mode_ < sizeof depths ? depths[mode_] : 1;
    }
    // Swatch-derived geometry (0 until the CRTC is programmed).
    uint32_t hres() const { return hres_; }
    uint32_t vres() const { return vres_; }
    uint32_t pixelClock() const { return pixelClock_; }
    bool     blanked() const { return (swatchMode_ & 1) != 0; }
    // Monitor on the sense pins: plain codes 0-7 or ext(bc,ac,ab) =
    // $40|bc<<4|ac<<2|ab. Default 6 = 13" 640×480 Hi-Res RGB.
    void setMonitor(uint8_t code) { monitorConfig_ = code; }
    const uint32_t* regs() const { return regs_; }
    const uint8_t (*clut() const)[3] { return clut_; }

private:
    void recalcIrq();
    void recalcMode();

    int64_t  cpuHz_;
    uint32_t regs_[0x100] = {};    // raw register file (echo backing)
    uint8_t  intStatus_ = 0;       // bit 0 = VBL, bit 2 = cursor scanline
    uint32_t swatchIntEnable_ = 0; // $104: bit 0 VBL, bit 2 cursor line
    uint32_t cursorLine_ = 0;      // $118
    uint8_t  palAddress_ = 0, palIdx_ = 0;       // Antelope RAMDAC
    uint8_t  ac842Pbctrl_ = 0, pcbr1_ = 0;
    uint32_t base_ = 0;            // $000/$004, byte offset into VRAM
    uint32_t stride_ = 1024;       // $008, bytes (register stores 32-bit words)
    uint16_t config_ = 0;          // $010; bit 3 forces convolution stride 1024
    uint8_t  mode_ = 0;            // AC842 PCBR0: 0=1,1=2,2=4,3=8,4=24,5=16 bpp
    uint8_t  clut_[256][3] = {};

    // Swatch CRTC timing ($124-$148 / $14C-$164) and derived geometry.
    uint16_t hParams_[10] = {};    // HSERR..HPIX
    uint16_t vParams_[7] = {};     // VHLINE..VFPEQ (half-lines)
    uint32_t swatchMode_ = 1;      // $100; bit 0 = display disable (reset: on)
    uint32_t hres_ = 0, vres_ = 0;
    uint32_t htotal_ = 0, vtotal_ = 0;

    // Monitor sense ($1C): host-driven pins + attached monitor code.
    uint8_t  monitorConfig_ = 6;
    uint8_t  monitorId_ = 0;

    // Gazelle clock generator ($3C3).
    uint32_t pixelClock_ = 31334400;
    uint32_t gazShift_ = 0;
    int      gazBits_ = 0;
    uint8_t  gazLastClock_ = 0;
    uint32_t gazMclk_ = 31334400;

    // Frame clock.
    int64_t framePos_ = 0;
    int     prevLine_ = 0;
};
