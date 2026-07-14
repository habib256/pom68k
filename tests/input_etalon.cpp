// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// M5.5 gate: keyboard and mouse against the REAL System 6 drivers. Boots
// the Finder, injects quadrature mouse steps / button / M0110 key codes,
// and reads back what the System understood through its low-memory
// globals: RawMouse ($82C: v,h words), MBState ($172: $80 = up), KeyMap
// ($174: 16 bytes, one bit per virtual key code). Soft-skips without
// roms/macplus.rom + disks35/Disk605.dsk.

#include "Cpu68k.h"
#include "MacMemory.h"
#include "MacFrame.h"
#include <cstdio>
#include <fstream>
#include <string>
#include <map>
#include <algorithm>
#include <vector>

static std::string find(const char* rel) {
    for (const std::string base : { std::string(), std::string("../") }) {
        std::string p = base + rel;
        if (std::ifstream(p, std::ios::binary)) return p;
    }
    return {};
}

int main() {
    std::string rom = find("roms/macplus.rom"), dsk = find("disks35/Disk605.dsk");
    if (rom.empty() || dsk.empty()) {
        std::printf("SKIP: needs roms/macplus.rom + disks35/Disk605.dsk\n");
        return 0;
    }
    std::ifstream in(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
    MacMemory mem;
    mem.loadRom(romData);
    Cpu68k cpu(mem);
    mem.setCpu(&cpu);
    cpu.hardReset();
    mem.insertDisk(dsk);

    MacFrameClock fc;
    fc.resync(cpu);
    for (long f = 0; f < 4500; f++) fc.runFrame(cpu, mem);   // boot to Finder

    const uint8_t* ram = mem.ram();
    auto rawMouseH = [&] { return int((ram[0x82E] << 8) | ram[0x82F]); };
    auto rawMouseV = [&] { return int((ram[0x82C] << 8) | ram[0x82D]); };

    // ── Mouse motion ────────────────────────────────────────────────────
    int h0 = rawMouseH(), v0 = rawMouseV();
    mem.mouse().move(60, 40);                    // right + down
    for (long f = 0; f < 120; f++) fc.runFrame(cpu, mem);
    int h1 = rawMouseH(), v1 = rawMouseV();
    std::printf("mouse: (%d,%d) -> (%d,%d)  (want ~+60,+40)\n", h0, v0, h1, v1);
    // ±2: quadrature loses one count at direction changes (real mice too)
    auto near = [](int got, int want) { return got >= want - 2 && got <= want + 2; };
    if (!near(h1 - h0, 60) || !near(v1 - v0, 40)) {
        std::fprintf(stderr, "FAIL: mouse deltas wrong (dh=%d dv=%d)\n", h1 - h0, v1 - v0);
        return 1;
    }
    mem.mouse().move(-30, -20);                  // left + up
    for (long f = 0; f < 120; f++) fc.runFrame(cpu, mem);
    int h2 = rawMouseH(), v2 = rawMouseV();
    std::printf("mouse: -> (%d,%d)  (want ~-30,-20)\n", h2, v2);
    if (!near(h2 - h1, -30) || !near(v2 - v1, -20)) {
        std::fprintf(stderr, "FAIL: reverse deltas wrong (dh=%d dv=%d)\n", h2 - h1, v2 - v1);
        return 1;
    }

    // ── Mouse button (MBState $172: $80 = up, $00 = down) ───────────────
    mem.mouse().setButton(true);
    for (long f = 0; f < 30; f++) fc.runFrame(cpu, mem);
    uint8_t down = ram[0x172];
    mem.mouse().setButton(false);
    for (long f = 0; f < 30; f++) fc.runFrame(cpu, mem);
    uint8_t up = ram[0x172];
    std::printf("MBState: down=%02X up=%02X (want 00 / 80)\n", down, up);
    if ((down & 0x80) != 0 || (up & 0x80) == 0) {
        std::fprintf(stderr, "FAIL: button state not reflected\n");
        return 1;
    }

    // ── Keyboard: press 'a' (M0110 transition code = keycode*2+1... wire
    // code $01 for virtual key 0) and expect a KeyMap bit to appear ──────
    uint8_t keymap0[16];
    for (int i = 0; i < 16; i++) keymap0[i] = ram[0x174 + i];
    mem.keyboard().enqueue(0x01);                // 'a' down
    for (long f = 0; f < 60; f++) fc.runFrame(cpu, mem);
    int changedDown = -1;
    for (int i = 0; i < 16; i++)
        if (ram[0x174 + i] != keymap0[i]) changedDown = i;
    mem.keyboard().enqueue(0x81);                // 'a' up
    for (long f = 0; f < 60; f++) fc.runFrame(cpu, mem);
    bool backToIdle = true;
    for (int i = 0; i < 16; i++)
        if (ram[0x174 + i] != keymap0[i]) backToIdle = false;
    std::printf("KeyMap: down changed byte %d, released back to idle: %d\n",
                changedDown, backToIdle ? 1 : 0);
    if (changedDown < 0 || !backToIdle) {
        std::fprintf(stderr, "FAIL: key transition not seen by the System\n");
        return 1;
    }

    std::printf("input_etalon: mouse + button + keyboard accepted by System 6\n");
    return 0;
}
