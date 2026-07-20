// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Q8 gate — MEMCjr DAFB stride and Antelope pixel-depth control.
// Reference: MAME dafb.cpp dafb_r/dafb_w and
// dafb_memcjr_device::ramdac_w.

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

    std::printf("%s\n", gFails ? "FAILED" : "PASSED");
    return gFails ? 1 : 0;
}
