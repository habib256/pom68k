// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── "Valkyrie" video cell (Quadra 630 / LC 580) — MAME valkyrie.cpp ──
// The cost-reduced replacement for DAFB on the last 68k desktops: no CRTC
// to program, just a *video timing number* selecting one of a handful of
// hardwired modes, a depth register, a 3-byte RAMDAC and a 1 MB VRAM whose
// frame buffer starts at +$1000 (valkyrie.cpp screen_update).
//
// Window map (valkyrie_device::map), inside the $50Fxxxxx I/O mirror:
//   $50F2A000  byte registers (offset = A0-A12, only a handful decoded)
//   $50F24000  RAMDAC, u32 with the payload in the TOP byte
//   $F9000000  VRAM (1 MB; the F108 maps the same RAM)
//
// Registers: $00 video timing (bit 7 = blank), $04 depth (0=1,1=2,2=4,
// 3=8,4=16 bpp), $0C subsystem config (1 = recalc), $10 config/int ack,
// $14 vblank level, $18 screen enable, $1C monitor sense drive/read.
// The row stride is `strideForMode << depthIndex` (valkyrie.cpp:154).
//
// The pixel clock is programmed over I2C by the Cuda (valkyrie is slave
// $28, write_data M/N/P) — POM68K does not model that bus, so the clock
// stays at the 31.3344 MHz default, which only affects the refresh rate
// the frame clock derives. Gate: tests/q630_boot_etalon.cpp.

#pragma once
#include <cstdint>
#include <functional>

class Valkyrie {
public:
    explicit Valkyrie(int64_t cpuHz) : cpuHz_(cpuHz) {}

    void reset();

    uint8_t  readReg8(uint32_t off);
    void     writeReg8(uint32_t off, uint8_t v);
    uint32_t readRamdac32(uint32_t off);
    void     writeRamdac32(uint32_t off, uint32_t v);

    // Frame clock: fires the VBL interrupt at the end of each display
    // period, as valkyrie.cpp's vbl_tick does (level line through onIrq).
    void tick(int cpuCycles);
    std::function<void(bool)> onIrq;

    // Geometry the host renderer needs.
    uint32_t hres() const { return hres_; }
    uint32_t vres() const { return vres_; }
    // valkyrie.cpp:307 stores `data & 7`, but only indices 0-4 are real modes
    // (valkyrie.cpp:154-237 draws nothing for 5-7). Clamp at the source so a
    // guest-programmed index can never scale the stride past the VRAM window —
    // the renderer's own clamps are what save us today.
    static constexpr uint8_t kModes = 5;
    uint8_t  depth() const {
        static constexpr uint8_t d[kModes] = { 1, 2, 4, 8, 16 };
        return mode_ < kModes ? d[mode_] : 1;
    }
    uint32_t stride() const {
        return uint32_t(stride_) << (mode_ < kModes ? mode_ : 0);
    }
    uint32_t base() const { return kFbOffset; }     // fixed: VRAM + $1000
    bool     blanked() const { return (videoTiming_ & 0x80) != 0; }
    uint32_t pixelClock() const { return pixelClock_; }
    const uint8_t (*clut() const)[3] { return clut_; }
    // Monitor on the sense pins: plain codes 0-7 or ext(bc,ac,ab) =
    // $40|bc<<4|ac<<2|ab. Default 6 = Mac Hi-Res 640×480 (MAME's default).
    void setMonitor(uint8_t code) { monitorConfig_ = code; }

    static constexpr uint32_t kFbOffset = 0x1000;

    // ── Save states (SaveState.h contract) ──────────────────────────────
    // Registers, RAMDAC/CLUT and the frame clock phase. cpuHz_ is
    // construction; onIrq is re-bound by the machine.
    template <class Ar> void visit(Ar& ar) {
        ar(videoTiming_, mode_, config_, intStatus_,
           monitorConfig_, monitorId_, palAddress_, palIdx_, clut_,
           hres_, vres_, htotal_, vtotal_, stride_, pixelClock_,
           enabled_, framePos_, prevLine_);
    }

private:
    void recalcMode();
    void recalcIrq();

    int64_t  cpuHz_;
    uint8_t  videoTiming_ = 0x80;   // bit 7 = blanked (reset state)
    uint8_t  mode_ = 0;             // depth index
    uint8_t  config_ = 0;
    uint8_t  intStatus_ = 0;
    uint8_t  monitorConfig_ = 6, monitorId_ = 0;
    uint8_t  palAddress_ = 0, palIdx_ = 0;
    uint8_t  clut_[256][3] = {};
    uint32_t hres_ = 0, vres_ = 0, htotal_ = 0, vtotal_ = 0;
    uint16_t stride_ = 1024;
    uint32_t pixelClock_ = 31334400;
    bool     enabled_ = false;      // reg $18 screen enable

    int64_t  framePos_ = 0;         // pixel-clock position in the frame
    int      prevLine_ = 0;
};
