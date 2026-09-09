// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// M5.5 gate: keyboard and mouse against the REAL System 6 drivers. Boots
// the Finder, injects quadrature mouse steps / button / M0110 key codes,
// and reads back what the System understood through its low-memory
// globals: RawMouse ($82C: v,h words), MBState ($172: $80 = up), KeyMap
// ($174: EIGHT bytes, one bit per virtual key code — a wider window
// reads $017C+ which is NOT KeyMap, and a positive assertion over it is
// a false green: that cost a debug round on 2026-07-29). $17C is
// KeypadMap, where the $79-prefixed keypad and arrow keys land; this gate
// reads it deliberately, and only for those. Soft-skips without
// roms/macplus.rom + disks35/Disk605.dsk.

#include "AssetFingerprint.h"
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
    return testasset::find(rel);
}

int main() {
    std::string rom = find("roms/macplus.rom"), dsk = find("disks35/Disk605.dsk");
    if (rom.empty() || dsk.empty()) {
        std::printf("SKIP: needs roms/macplus.rom + disks35/Disk605.dsk\n");
        return 0;
    }
    testasset::report({ rom, dsk });
    std::ifstream in(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
    MacMemory mem(pom68k::defaultCoreConfig());
    mem.loadRom(romData);
    Cpu68k cpu(mem, jit::defaultResolvedConfig());
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
    for (int i = 0; i < 8; i++) keymap0[i] = ram[0x174 + i];
    mem.keyboard().enqueue(0x01);                // 'a' down
    for (long f = 0; f < 60; f++) fc.runFrame(cpu, mem);
    int changedDown = -1;
    for (int i = 0; i < 8; i++)
        if (ram[0x174 + i] != keymap0[i]) changedDown = i;
    mem.keyboard().enqueue(0x81);                // 'a' up
    for (long f = 0; f < 60; f++) fc.runFrame(cpu, mem);
    bool backToIdle = true;
    for (int i = 0; i < 8; i++)
        if (ram[0x174 + i] != keymap0[i]) backToIdle = false;
    std::printf("KeyMap: down changed byte %d, released back to idle: %d\n",
                changedDown, backToIdle ? 1 : 0);
    if (changedDown < 0 || !backToIdle) {
        std::fprintf(stderr, "FAIL: key transition not seen by the System\n");
        return 1;
    }

    // ── Keypad and arrows: the $79-prefixed sequences (MacInput.cpp) ────
    // One layer above m0110_keypad_test: the wire bytes are only right if
    // the ROM's keyboard driver resolves them to a key. Press through
    // MacMemory::keyEvent — the production path — and read the bit the
    // System lit. Keypad keys do NOT land in KeyMap: they land in
    // KeypadMap ($17C, the four bytes right after KeyMap's eight), at bit
    // `code - $40` of that map, i.e. bit `64 + code - $40` of the window
    // scanned here. That is why an 8-byte KeyMap is the correct window for
    // a main-block key and the wrong one for these.
    auto pressBits = [&](const char* what, uint8_t vk) {
        uint8_t before[12];
        for (int i = 0; i < 12; i++) before[i] = ram[0x174 + i];
        mem.keyEvent(vk, true);
        for (long f = 0; f < 60; f++) fc.runFrame(cpu, mem);
        std::vector<int> bits;
        for (int i = 0; i < 12; i++) {
            uint8_t diff = uint8_t(ram[0x174 + i] ^ before[i]);
            while (diff) {
                const int b = __builtin_ctz(diff);
                diff = uint8_t(diff & (diff - 1));
                bits.push_back(i * 8 + b);
            }
        }
        std::sort(bits.begin(), bits.end());
        mem.keyEvent(vk, false);
        for (long f = 0; f < 60; f++) fc.runFrame(cpu, mem);
        bool idle = true;
        for (int i = 0; i < 12; i++) if (ram[0x174 + i] != before[i]) idle = false;
        std::printf("%-10s vk $%02X -> bits", what, vk);
        for (int b : bits) std::printf(" %d", b);
        std::printf("%s, released clean: %d\n", bits.empty() ? " (none)" : "",
                    idle ? 1 : 0);
        if (!idle) bits.clear();
        return bits;
    };
    // 64 + ($52 - $40) = 82 for keypad 0, and the arrows are keypad keys
    // on this keyboard: Left is code $46 (raw $0D behind the prefix), so
    // 64 + 6 = 70. Keypad + shares that raw code and is told apart ONLY by
    // the synthetic Shift the M0110A wraps it in — so it must light both
    // KeyMap bit 56 (Shift, $38) and the same keypad bit 70.
    const struct { const char* name; uint8_t vk; std::vector<int> want; } kPad[] = {
        { "keypad 0",  0x52, { 82 } },
        { "keypad 9",  0x5C, { 92 } },
        { "Clear",     0x47, { 71 } },
        { "Left",      0x3B, { 70 } },
        { "Up",        0x3E, { 77 } },
        { "keypad +",  0x45, { 56, 70 } },
    };
    for (const auto& k : kPad) {
        if (pressBits(k.name, k.vk) != k.want) {
            std::fprintf(stderr, "FAIL: %s did not reach the System as the "
                                 "$79-prefixed key it is\n", k.name);
            return 1;
        }
    }

    std::printf("input_etalon: mouse + button + keyboard accepted by System 6\n");
    return 0;
}
