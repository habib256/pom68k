// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// M1 gate: end-to-end smoke test. Boots the built-in demo ROM through the
// real machine path — reset vectors fetched under the overlay, overlay
// cleared via the VIA, framebuffer painted in RAM by Moira-executed 68000
// code — then checks the painted pattern.

#include "Cpu68k.h"
#include "MacMemory.h"
#include "DemoRom.h"
#include <cstdio>
#include <cstdlib>

#define CHECK(cond, msg) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL: %s\n", msg); return 1; } \
    std::printf("ok: %s\n", msg); } while (0)

int main() {
    MacMemory mem;
    mem.installRom(kDemoRom, kDemoRomSize);
    Cpu68k cpu(mem);
    cpu.hardReset();

    CHECK(mem.overlay(), "overlay asserted after reset");
    CHECK(cpu.getPC0() == 0x400010 || cpu.getPC() >= 0x400010,
          "PC fetched from overlay reset vector");

    // First frame paint ≈ 200k cycles; run 1M to be safely past it.
    cpu.runCycles(1000000);

    CHECK(!mem.overlay(), "overlay cleared by VIA ORA write");

    const uint8_t* ram = mem.ram();
    uint32_t fb = mem.mainScreenBase();
    CHECK(fb == 0x3FA700, "main screen buffer at ramSize-0x5900");
    // Frame 0 pattern: row 0 = $F0F0F0F0, row y = rol(pattern, y).
    // The demo may already be past frame 0 — accept any rotation, but the
    // diagonal property must hold: row y+8 repeats row y, row y+1 = rol(row y).
    CHECK(ram[fb] != 0x00 && ram[fb] != 0xFF, "framebuffer row 0 painted");
    CHECK(ram[fb] == ram[fb + 4], "pattern repeats across the row");
    // Diagonal invariant: row y+1 = rol(row y, 1). The paint cursor may sit
    // between one sampled pair (the demo repaints continuously and the run
    // can stop mid-frame), so require 3 of 4 pairs to hold.
    auto row = [&](int y) {
        uint32_t o = fb + uint32_t(y) * 64;
        return (uint32_t(ram[o]) << 24) | (uint32_t(ram[o+1]) << 16)
             | (uint32_t(ram[o+2]) << 8) | uint32_t(ram[o+3]);
    };
    int good = 0;
    for (int y : { 0, 100, 200, 340 }) {
        uint32_t a = row(y), b = row(y + 1);
        if (b == ((a << 1) | (a >> 31))) good++;
    }
    CHECK(good >= 3, "diagonal stripes (rol per row) on 3+ sampled pairs");
    // Last row painted too (full 342-row frame completed).
    CHECK(ram[fb + 341u * 64] != 0x00 && ram[fb + 341u * 64] != 0xFF,
          "row 341 painted (full frame)");

    // Clock advanced ≈ requested cycles (cycle-exact core, not wall-clock).
    CHECK(cpu.getClock() >= 1000000, "CPU clock advanced by requested cycles");

    std::printf("cpu_smoke: all checks passed\n");
    return 0;
}
