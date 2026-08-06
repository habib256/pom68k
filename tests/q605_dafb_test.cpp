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

// Drive the PCBR0/PCBR1 write sequence the DAFB driver uses to probe the
// RAMDAC (PCBR0 select bits $06, PCBR1 at palette address 1).
void pcbr1Dance(Dafb& d) {
    d.write32(0x220, 0x06);
    d.write32(0x200, 1);
    d.write32(0x220, 0xC0);
    d.write32(0x200, 0);
    d.write32(0x220, 0x06);
    d.write32(0x200, 1);              // leave $220 pointing at PCBR1
}

// ── Per-flavour version/RAMDAC matrix (findings #51/#52) ───────────────
// dafb.cpp: dafb_base ctor:84 (version 1, plain AC842 — Q700/Q900),
// dafb_q950_device:1105/1131 (version 3, AC842a, PCBR1 ID $01),
// dafb_memc/memcjr device_start (version 3, Antelope, PCBR1 ID $02).
void flavours() {
    {
        Dafb d(25000000, Dafb::Clockgen::Dp8531, 1, Dafb::Ramdac::Ac842);
        d.reset();
        check(((d.read32(0x2C) >> 9) & 7) == 1,
              "discrete DAFB: $2C reports version 1");
        pcbr1Dance(d);
        check(d.mode() != 5, "AC842: the dance never selects x555");
        check(d.read32(0x220) == 0x06,
              "AC842: $220 always reads PCBR0 (dafb.cpp:743-745)");
    }
    {
        Dafb d(25000000, Dafb::Clockgen::Dp8531, 3, Dafb::Ramdac::Ac842a);
        d.reset();
        check(((d.read32(0x2C) >> 9) & 7) == 3,
              "Q950 DAFB II: $2C reports version 3");
        pcbr1Dance(d);
        check(d.mode() == 5, "AC842a: the dance selects x555");
        check(d.read32(0x220) == 0xC1,
              "AC842a: PCBR1 version ID is $01 (dafb.cpp:1131)");
    }
    {
        Dafb d(25000000, Dafb::Clockgen::Dp8534, 3, Dafb::Ramdac::Antelope);
        d.reset();
        pcbr1Dance(d);
        check(d.mode() == 5 && d.read32(0x220) == 0xC2,
              "Antelope: x555 with PCBR1 version ID $02 (dafb.cpp:1258)");
    }
}

// ── Q700 512×384 quirk + sub-480 frame timing (findings #53/#54) ───────
void q700Mode512() {
    Dafb d(25000000, Dafb::Clockgen::Dp8531, 1, Dafb::Ramdac::Ac842);
    d.reset();
    // 512×384 (Apple 12" RGB): HAL=64 HFP=576 (raw hres 512), HPIX=640;
    // half-lines VAL=36 VFP=806 (raw vres 385, the ROM's off-by-1),
    // VFPEQ=814 → vtotal 407.
    d.write32(0x140, 64);             // HAL
    d.write32(0x144, 576);            // HFP
    d.write32(0x148, 640);            // HPIX
    d.write32(0x15C, 36);             // VAL
    d.write32(0x160, 806);            // VFP
    d.write32(0x164, 814);            // VFPEQ
    d.write32(0x220, 0x18);           // 8bpp PCBR0 → recalc_mode
    check(d.hres() == 512 && d.vres() == 384,
          "512×384: version 1 patches the off-by-1 vres (dafb.cpp:833-839)");
    check(d.base() == 0x1000,
          "512×384: version 1 forces base $1000 (dafb.cpp:836)");

    // #54: the programmed CRTC drives the frame even with vtotal < 480 —
    // 640×407 at the 31.3344 MHz reset pclk, not the legacy 60 Hz/525.
    int irqs = 0;
    d.onIrq = [&](bool s) { if (s) { irqs++; d.read32(0x114); } };
    d.write32(0x104, 1);              // VBL enable
    d.tick(1);
    const int64_t expect = int64_t(640) * 407 * 25000000 / 31334400;
    check(d.frameCycles() == expect && d.frameTotalLines() == 407,
          "sub-480 mode runs on its programmed totals (dafb.cpp:869-875)");
    for (int i = 0; i < 3 * int(expect) / 500; ++i) d.tick(500);
    check(irqs >= 2 && irqs <= 4,
          "VBL still fires once per frame (line 480 wraps mod 407)");
}
// ── §2.13 cosmetics: three deliberate divergences + one alignment ─────
// Audit docs/MAME_PARITY_AUDIT.md § 2.13. Each check below is a PIN: a
// later "parity diff" that imports MAME's form must trip one of them.
void cosmetics() {
    // (a) CLUT read phase. POM68K cycles R→G→B→R, as the silicon does.
    // MAME lets m_pal_idx run unbounded (dafb.cpp:726-731): its 4th
    // consecutive read answers 0, and the counter it leaves behind (3+)
    // then makes every following palette WRITE store nothing, because
    // ramdac_w's switch has no case above 2 and the `== 3` re-sync can
    // never fire again (dafb.cpp:772-790).
    {
        Dafb d(25000000, Dafb::Clockgen::Dp8534, 3, Dafb::Ramdac::Antelope);
        d.reset();
        d.write32(0x200, 0x10);
        d.write32(0x210, 0x11);                   // R
        d.write32(0x210, 0x22);                   // G
        d.write32(0x210, 0x33);                   // B
        d.write32(0x200, 0x10);                   // re-point, phase reset
        check(d.read32(0x210) == 0x11 && d.read32(0x210) == 0x22 &&
              d.read32(0x210) == 0x33, "CLUT reads cycle R, G, B");
        check(d.read32(0x210) == 0x11,
              "4th consecutive CLUT read wraps to red (MAME answers 0)");
        d.write32(0x210, 0x44);                   // phase is 1 = green
        check(d.clut()[0x10][1] == 0x44,
              "the wrapped read leaves the write phase usable");
    }

    // (b) Monochrome sense → blue drives all three primaries. This one is
    // an ALIGNMENT to dafb.cpp:758-770: sense 1 (15" Portrait) and 3 (21"
    // Two-Page) are B&W displays whose only live DAC is blue.
    {
        Dafb d(25000000, Dafb::Clockgen::Dp8531, 1, Dafb::Ramdac::Ac842);
        d.reset();
        d.setMonitor(6);                          // 13" colour
        d.write32(0x200, 4);
        d.write32(0x210, 0x10); d.write32(0x210, 0x20); d.write32(0x210, 0x30);
        check(d.clut()[4][0] == 0x10 && d.clut()[4][1] == 0x20 &&
              d.clut()[4][2] == 0x30,
              "colour sense: the CLUT stores R, G, B as written");

        d.setMonitor(1);                          // 15" Portrait, mono
        d.write32(0x200, 5);
        d.write32(0x210, 0x10); d.write32(0x210, 0x20); d.write32(0x210, 0x30);
        check(d.clut()[5][0] == 0x30 && d.clut()[5][1] == 0x30 &&
              d.clut()[5][2] == 0x30,
              "mono sense 1: blue drives all three primaries");
        check(d.read32(0x200) == 6,
              "mono writes still auto-increment the palette address");

        d.setMonitor(3);                          // 21" Two-Page, mono
        d.write32(0x200, 7);
        d.write32(0x210, 0xAA); d.write32(0x210, 0xBB); d.write32(0x210, 0xCC);
        check(d.clut()[7][0] == 0xCC && d.clut()[7][2] == 0xCC,
              "mono sense 3 replicates blue as well");
        d.setMonitor(6);
    }

    // (c) Convolution must NOT divide the stride register. MAME does
    // (`m_stride /= clockdiv`, dafb.cpp:847) — a destructive edit of the
    // register echo that survives convolution being switched off, and one
    // its own screen_update can never observe because it pins the pitch at
    // 1024 for the whole time convolution is on (dafb.cpp:267).
    {
        Dafb d(25000000, Dafb::Clockgen::Dp8531, 1, Dafb::Ramdac::Ac842);
        d.reset();
        // recalc_mode only reaches the convolution branch once the Swatch
        // has non-zero totals — program the 512×384 CRTC first.
        d.write32(0x140, 64);  d.write32(0x144, 576); d.write32(0x148, 640);
        d.write32(0x15C, 36);  d.write32(0x160, 806); d.write32(0x164, 814);
        d.write32(0x008, 160);                    // 640-byte scanline
        d.write32(0x010, 0x08);                   // convolution on
        d.write32(0x220, 0x18 | 0x20);            // 8bpp, clockdiv 2 → recalc
        check(d.hres() == 512 / 2 - 23,
              "convolution divides the horizontal res by the clock divider");
        check(d.read32(0x008) == 160,
              "convolution leaves the stride register echo alone");
        check(d.stride() == 1024, "convolution pins the pitch at 1024 anyway");
        d.write32(0x010, 0);                      // convolution off
        d.write32(0x220, 0x18 | 0x20);            // recalc a second time
        check(d.stride() == 640,
              "clearing convolution restores the programmed stride, twice over");
    }

    // (d) The aux-scanline interrupt enable (Swatch $104 bit 1) is taken
    // and ignored. MAME calls fatalerror() on it (dafb.cpp:646-649);
    // aborting the emulator on a register write is not an observable a
    // user can act on, so the divergence is deliberate.
    {
        Dafb d(25000000, Dafb::Clockgen::Dp8531, 1, Dafb::Ramdac::Ac842);
        d.reset();
        int irqs = 0;
        d.onIrq = [&](bool s) { if (s) irqs++; };
        d.write32(0x104, 2);                      // aux scanline ONLY
        for (int i = 0; i < 20000; ++i) d.tick(64);
        check(irqs == 0 && d.read32(0x108) == 0,
              "aux-scanline enable is accepted and silent (MAME fatalerrors)");
    }
}
} // namespace

int main() {
    std::printf("q605_dafb_test — DAFB stride + Antelope depth (Q8)\n");

    Q605Memory mem(1u << 20);
    mem.reset();

    check(mem.dafbStride() == 1024, "reset stride is 1024 bytes");
    check(readDafb12(mem, 0x08) == 256, "stride register reads 32-bit words");
    check(readDafb12(mem, 0x2C) == 0x600,
          "MEMCjr DAFB II reports version 3 in $2C bits 11-9");

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

    // ── Per-flavour version/RAMDAC + the Q700 512×384 quirk ────────────
    flavours();
    q700Mode512();

    // ── The cosmetic-parity pins (audit § 2.13) ────────────────────────
    cosmetics();

    std::printf("%s\n", gFails ? "FAILED" : "PASSED");
    return gFails ? 1 : 0;
}
