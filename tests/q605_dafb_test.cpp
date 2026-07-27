// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Q8 gate — MEMCjr DAFB stride and Antelope pixel-depth control, plus the
// three clock generators that share the +$300 window (Gazelle on MEMCjr,
// DP8534 on djMEMC, DP8531 on the discrete DAFB of the Quadra 700).
// Reference: MAME dafb.cpp dafb_r/dafb_w, dafb_memcjr_device::ramdac_w,
// and clockgen_w at :884 (DP8531) / :1197 (DP8534) / :1322 (Gazelle).

#include "Q605Memory.h"

#include <cstdio>

namespace {
int gFails = 0;

void check(bool ok, const char* what) {
    std::printf("  %-62s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}

void write32(Q605Memory& mem, uint32_t addr, uint32_t value) {
    mem.write16(addr, uint16_t(value >> 16));
    mem.write16(addr + 2, uint16_t(value));
}

uint32_t read32(Q605Memory& mem, uint32_t addr) {
    return uint32_t(mem.read16(addr)) << 16 | mem.read16(addr + 2);
}

// MEMCjr exposes the low and high six bits of each 12-bit DAFB register
// through separate ports (djmemc.cpp dafb_holding_r/w).
void writeDafb12(Q605Memory& mem, uint32_t offset, uint16_t value) {
    write32(mem, 0x5000E07C, value >> 6);
    write32(mem, 0xF9800000 + offset, value & 0x3F);
}

uint16_t readDafb12(Q605Memory& mem, uint32_t offset) {
    uint16_t low = uint16_t(read32(mem, 0xF9800000 + offset) & 0x3F);
    uint16_t high = uint16_t(read32(mem, 0x5000E07C) & 0x3F);
    return uint16_t(low | (high << 6));
}

// ── The two non-MEMCjr clock generators, driven on a bare cell ─────────
// Each machine's memory controller routes the whole +$300 window into
// clockgenWrite8 with the offset it saw, so drive it the same way.

// DP8534 (djMEMC, dafb.cpp:1197): the parameter word is clocked into $303
// MSB first and committed by any write to $313. On commit MAME shifts it
// up by 2 and reads five BIT-REVERSED bytes out of the 40-bit result.
void dp8534Stream(Dafb& d, uint64_t word, int bits) {
    for (int i = bits - 1; i >= 0; --i)
        d.clockgenWrite8(0x303, uint8_t((word >> i) & 1));
    d.clockgenWrite8(0x313, 0);               // commit
}

// DP8531 (discrete DAFB, dafb.cpp:884): a nibble register file. Register
// n lives at $3n3 — only (offset & 3) == 3 is decoded — and writing
// register 15 latches the new clock.
void dp8531Reg(Dafb& d, int reg, uint8_t nibble) {
    d.clockgenWrite8(0x303 + uint32_t(reg << 4), nibble);
}

void clockgens() {
    // ── DP8534 ────────────────────────────────────────────────────────
    // Aim for P = 0, RCNT = 20, NCNT = 31 → VCO = 20 MHz × 31/20 = 31 MHz,
    // pclk = VCO/(P+1) = 31 MHz. Working backwards through the bit
    // reversal: param3 = 10 gives RCNT = param3<<1 = 20, param4 = 0x3E
    // gives NCNT = param4>>1 = 31, param1 = param2 = param5 = 0 gives
    // P = 0. Reversing those into their bytes yields the 40-bit word
    // $00_00_50_7C_00, i.e. a shifted-in value of $141F00.
    {
        Dafb d(25000000, Dafb::Clockgen::Dp8534);
        d.reset();
        check(d.pixelClock() == 31334400, "DP8534: reset pclk is the 31.3344 default");
        dp8534Stream(d, 0x141F00u, 38);
        check(d.pixelClock() == 31000000,
              "DP8534: P=0 RCNT=20 NCNT=31 programs pclk = 31 MHz");
        // The Gazelle port must be inert on this cell — feeding it the
        // Gazelle's own bit-bang pattern may not move the clock.
        const uint32_t held = d.pixelClock();
        for (int i = 0; i < 40; i++) {
            d.clockgenWrite8(0x3C3, 1);
            d.clockgenWrite8(0x3C3, 3);
        }
        check(d.pixelClock() == held, "DP8534: the Gazelle port ($3C3) is inert");
    }

    // ── DP8531 ────────────────────────────────────────────────────────
    // R = regs 6:5:4 = $014 = 20, P = 1 << reg 9 = 2, N modulus =
    // regs 3:2:1:0 = $005F → A = ($1F ^ $1F) = 0, B = $5F >> 5 = 2, so
    // N = 32(B-A) + 31(1+A) = 95 and VCO = (20 MHz / 20) × 95 = 95 MHz,
    // pclk = VCO / P = 47.5 MHz.
    {
        Dafb d(25000000, Dafb::Clockgen::Dp8531);
        d.reset();
        dp8531Reg(d, 0, 0xF); dp8531Reg(d, 1, 0x5);   // N modulus $005F
        dp8531Reg(d, 2, 0x0); dp8531Reg(d, 3, 0x0);
        dp8531Reg(d, 4, 0x4); dp8531Reg(d, 5, 0x1);   // R = $014 = 20
        dp8531Reg(d, 6, 0x0);
        dp8531Reg(d, 9, 0x1);                         // P = 2
        check(d.pixelClock() == 31334400,
              "DP8531: nothing latches until register 15 is written");
        dp8531Reg(d, 15, 0x0);                        // commit
        check(d.pixelClock() == 47500000,
              "DP8531: R=20 P=2 N=95 programs pclk = 47.5 MHz");

        // Only the byte at (offset & 3) == 3 carries a register nibble —
        // the other three lanes of the same longword must be ignored.
        d.clockgenWrite8(0x340, 0xF);                 // would corrupt R…
        d.clockgenWrite8(0x341, 0xF);
        d.clockgenWrite8(0x342, 0xF);
        dp8531Reg(d, 15, 0x0);                        // …re-commit
        check(d.pixelClock() == 47500000,
              "DP8531: lanes other than (offset & 3) == 3 are ignored");

        // Register 12 sits exactly on the Gazelle's $3C3 port: writing it
        // must be a plain register store, never a serial clock edge.
        dp8531Reg(d, 12, 0x3);
        check(d.pixelClock() == 47500000,
              "DP8531: register 12 ($3C3) does not act as a Gazelle edge");
    }
}
} // namespace

int main() {
    std::printf("q605_dafb_test — DAFB stride + Antelope depth (Q8)\n");

    Q605Memory mem(1u << 20);
    mem.reset();

    check(mem.dafbStride() == 1024, "reset stride is 1024 bytes");
    check(readDafb12(mem, 0x08) == 256, "stride register reads 32-bit words");

    constexpr uint32_t kBase = 0x12340;
    writeDafb12(mem, 0x00, uint16_t(kBase >> 9));
    writeDafb12(mem, 0x04, uint16_t((kBase >> 5) & 0x0F));
    check(mem.dafbBase() == kBase, "split base registers reconstruct the VRAM offset");
    check(readDafb12(mem, 0x00) == (kBase >> 9) &&
          readDafb12(mem, 0x04) == ((kBase >> 5) & 0x0F),
          "split base registers round-trip");

    writeDafb12(mem, 0x08, 160);              // 640-byte 8bpp scanline
    check(readDafb12(mem, 0x08) == 160, "stride register round-trips through holding port");
    check(mem.dafbStride() == 640, "stride register value is converted to bytes");

    writeDafb12(mem, 0x10, 0x08);             // convolution fixes pitch
    check(mem.dafbStride() == 1024, "config bit 3 forces the 1024-byte stride");
    writeDafb12(mem, 0x10, 0);
    check(mem.dafbStride() == 640, "clearing convolution restores programmed stride");

    constexpr uint32_t kPalAddress = 0xF9800200;
    constexpr uint32_t kPbctrl = 0xF9800220;
    write32(mem, kPbctrl, 0x00);
    check(mem.dafbMode() == 0 && mem.dafbDepth() == 1, "PCBR0 $00 selects 1 bpp");
    write32(mem, kPbctrl, 0x08);
    check(mem.dafbMode() == 1 && mem.dafbDepth() == 2, "PCBR0 $08 selects 2 bpp");
    write32(mem, kPbctrl, 0x10);
    check(mem.dafbMode() == 2 && mem.dafbDepth() == 4, "PCBR0 $10 selects 4 bpp");
    write32(mem, kPbctrl, 0x18);
    check(mem.dafbMode() == 3 && mem.dafbDepth() == 8, "PCBR0 $18 selects 8 bpp");
    check(read32(mem, kPbctrl) == 0x18, "PCBR0 readback preserves depth control");

    // Antelope PCBR1 high bits $C0 plus PCBR0 select bits $06 enable x555.
    write32(mem, kPbctrl, 0x06);
    write32(mem, kPalAddress, 1);
    write32(mem, kPbctrl, 0xC0);
    write32(mem, kPalAddress, 0);
    write32(mem, kPbctrl, 0x06);
    check(mem.dafbMode() == 5 && mem.dafbDepth() == 16, "Antelope PCBR1 selects x555");

    mem.reset();
    check(mem.dafbBase() == 0 && mem.dafbStride() == 1024 && mem.dafbDepth() == 1,
          "reset restores DAFB geometry");

    // ── Swatch CRTC → recalc_mode (dafb.cpp) ───────────────────────────
    // 640×480 Hi-Res: HAL=112 HFP=752 HPIX=896; VAL/VFP/VFPEQ in
    // half-lines: 90 / 1050 / 1050 → vres (1050>>1)-(90>>1) = 480,
    // vtotal 525. Geometry recomputes on the PCBR0 write (ramdac_w).
    check(mem.dafbHres() == 0 && mem.dafbVres() == 0,
          "geometry unknown before the Swatch is programmed");
    writeDafb12(mem, 0x140, 112);             // HAL
    writeDafb12(mem, 0x144, 752);             // HFP
    writeDafb12(mem, 0x148, 896);             // HPIX
    writeDafb12(mem, 0x15C, 90);              // VAL
    writeDafb12(mem, 0x160, 1050);            // VFP
    writeDafb12(mem, 0x164, 1050);            // VFPEQ
    check(readDafb12(mem, 0x144) == 752 && readDafb12(mem, 0x160) == 1050,
          "Swatch timing registers round-trip");
    write32(mem, kPbctrl, 0x18);              // 8bpp, clockdiv 1 → recalc
    check(mem.dafbHres() == 640 && mem.dafbVres() == 480,
          "recalc_mode derives 640x480 from HAL/HFP/VAL/VFP");

    // ── Gazelle clock generator (dafb_memcjr clockgen_w) ───────────────
    // M=2, N=1, P=2 (p_select 1), pclk select → 31.3344 MHz / 4.
    check(mem.dafbPixelClock() == 31334400, "reset pixel clock is 31.3344 MHz");
    {
        const uint32_t word = (2u << 13) | (1u << 6) | (1u << 4);
        for (int i = 0; i < 20; i++) {
            uint8_t bit = uint8_t((word >> i) & 1);
            mem.write8(0xF98003C3, bit);              // clock low
            mem.write8(0xF98003C3, uint8_t(bit | 2)); // rising edge latches
        }
    }
    check(mem.dafbPixelClock() == 31334400 / 4,
          "Gazelle M=2 N=1 P=2 programs pclk = 7.8336 MHz");

    // ── Extended monitor sense (dafb_r/dafb_w $1C) ─────────────────────
    check(readDafb12(mem, 0x1C) == 1, "plain type-6 monitor senses as 6^7");
    mem.setDafbMonitor(0x40 | (1 << 4) | (1 << 2) | 3);   // ext(1,1,3) VGA
    writeDafb12(mem, 0x1C, 3);                // drive pin 2 (id = 3^7 = 4)
    check(readDafb12(mem, 0x1C) == 2, "VGA ext code: driving pin 2 reads 5^7");
    writeDafb12(mem, 0x1C, 5);                // drive pin 1 (id = 2)
    check(readDafb12(mem, 0x1C) == 4, "VGA ext code: driving pin 1 reads 3^7");
    writeDafb12(mem, 0x1C, 6);                // drive pin 0 (id = 1)
    check(readDafb12(mem, 0x1C) == 0, "VGA ext code: driving pin 0 reads 7^7");
    mem.setDafbMonitor(6);

    // ── Swatch mode display disable (screen_update bit 0) ──────────────
    check(mem.dafbBlanked(), "reset: Swatch mode bit 0 blanks the display");
    writeDafb12(mem, 0x100, 0);
    check(!mem.dafbBlanked(), "clearing Swatch mode bit 0 unblanks");

    // ── The djMEMC / discrete-DAFB clock generators ────────────────────
    clockgens();

    std::printf("%s\n", gFails ? "FAILED" : "PASSED");
    return gFails ? 1 : 0;
}
