// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Gate `q605_cudalle_key_etalon`: KEYBOARD input through the REAL Cuda
// firmware. Repro for the 2026-07-23 field report: typing froze the whole
// emulator on the Quadra (host-side wedge in the SRQ-driven keyboard
// path — the mouse rides autopoll, the keyboard is the first real SRQ
// consumer). Boots Mac OS 8.1 to the Finder, types "8.8.8.8" as timed
// press/release pairs, and requires (a) the emulation to keep advancing
// (Ticks $016A — a host hang shows up as the CTest timeout), and (b) the
// keystrokes to reach the low-memory KeyMap ($0174).
//
// The 2026-07-31 diagnosis of ten months of RED: this disk image has Easy
// Access SLOW KEYS enabled, and Mac OS 8.1 registers its acceptance-delay
// wrapper as the ADB service routine for the keyboard — a key-DOWN held
// shorter than the acceptance delay is deliberately rejected by the guest
// (the field report's beep-per-letter IS the Slow Keys rejection beep);
// key-UPs pass straight through to the classic driver. The transport was
// never at fault. The timed phase therefore HOLDS each key 150 frames
// (~2.5 s) — accepted by Slow Keys and by a normal keyboard alike, so the
// gate stays green whether or not the image's setting is ever cleaned up.
// Do NOT "fix" this with the hold-Return Easy Access gesture: it is a
// toggle, and would turn Slow Keys back ON the day the image is fixed.
// KeyTime ($0186) is NOT a usable observable here: a Slow Keys periodic
// task copies Ticks into it continuously, keystrokes or not.

#include "Q605Memory.h"
#include "Cpu040.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

static std::string find(const char* rel) {
    for (const std::string base : { "", "../" }) {
        std::string p = base + rel;
        if (std::ifstream(p, std::ios::binary)) return p;
    }
    return {};
}

int main() {
    setenv("POM68K_CUDA_LLE", "1", 1);
    std::string rom = find("roms/1MB ROMs/1993-10 - FF7439EE - LC475,575,Quadra 605,Performa 475,476,575,577,578.ROM");
    std::string img = find("hdv/MacOS-8.1-boot.vhd");
    if (rom.empty() || img.empty()) { std::printf("SKIP: needs ROM+disk\n"); return 0; }

    std::ifstream rin(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(rin)), {});
    Q605Memory mem(32u << 20);
    if (!mem.loadRom(romData) || !mem.attachScsi(img)) return 1;
    if (!mem.cudaLleActive()) { std::printf("SKIP: needs roms/cuda/341s0788.bin\n"); return 0; }
    Cpu040 cpu(mem);
    mem.setCpu(&cpu);
    cpu.hardReset();
    while (mem.cpuHeld()) mem.tick(1000);

    auto rd32 = [&](uint32_t a) {
        return (uint32_t(mem.peek8(a)) << 24) | (uint32_t(mem.peek8(a + 1)) << 16)
             | (uint32_t(mem.peek8(a + 2)) << 8) | uint32_t(mem.peek8(a + 3));
    };

    constexpr int kFrame = 416667;                   // 25 MHz / ~60 Hz
    for (long f = 0; f < 9000 && !cpu.isHalted(); f++) cpu.runCycles(kFrame);
    std::printf("post-boot: Ticks=%u\n", rd32(0x016A));

    // "8.8.8.8" — main row ('8' = $1C, '.' = $2F) then the NUMERIC KEYPAD
    // ($5B = KP-8, $41 = KP-. — the 2026-07-23 field freeze was keypad-8).
    static const uint8_t kSeq[] = { 0x1C, 0x2F, 0x1C, 0x2F, 0x1C, 0x2F, 0x1C,
                                    0x5B, 0x41, 0x5B, 0x41, 0x5B, 0x41, 0x5B };
    bool keymapSeen = false;
    const uint32_t ticks0 = rd32(0x016A);
    for (uint8_t code : kSeq) {
        mem.keyEvent(code, true);                    // press
        // 150-frame hold: longer than the Slow Keys acceptance delay.
        for (int f = 0; f < 150 && !cpu.isHalted(); f++) {
            cpu.runCycles(kFrame);
            // KeyMap proper is EIGHT bytes ($0174-$017B) — $017C on is
            // KeypadMap territory, and scanning it once made a dead ADB
            // stack look half-alive. The main-row '8'/'.' land in it.
            for (int i = 0; i < 8 && !keymapSeen; i++)
                if (mem.peek8(0x0174 + uint32_t(i)) != 0) keymapSeen = true;
        }
        mem.keyEvent(code, false);                   // release
        for (int f = 0; f < 6 && !cpu.isHalted(); f++) cpu.runCycles(kFrame);
        std::printf("  key %02X done, Ticks=%u\n", code, rd32(0x016A));
    }
    uint32_t ticks1 = rd32(0x016A);

    // Stress phase: 500 tight press/release pairs (2 frames apart) to
    // hunt the host-command x autopoll collision (2026-07-23 field
    // freeze: OT panel PRAM writes + keypad typing wedged the ADB
    // manager in a spin loop at ~$D1F04 while the emulator ran fine).
    const uint32_t sTicks0 = rd32(0x016A);
    for (int i = 0; i < 500 && !cpu.isHalted(); i++) {
        const uint8_t code = kSeq[size_t(i) % (sizeof kSeq)];
        mem.keyEvent(code, true);
        cpu.runCycles(kFrame * 2);
        mem.keyEvent(code, false);
        cpu.runCycles(kFrame * 2);
        if (i % 100 == 99) {
            const uint32_t t = rd32(0x016A);
            std::printf("  stress %d/500 Ticks=%u\n", i + 1, t);
            if (t == sTicks0) break;                 // wedged early
        }
    }
    cpu.runCycles(kFrame * 120);
    const uint32_t sTicks1 = rd32(0x016A);
    const bool stressAlive = sTicks1 > sTicks0 + 100;
    std::printf("stress: Ticks %u -> %u %s\n", sTicks0, sTicks1,
                stressAlive ? "(alive)" : "(WEDGED)");
    ticks1 = sTicks1;

    const bool alive = ticks1 > ticks0 && stressAlive;
    std::printf("Ticks %u -> %u; KeyMap %s\n", ticks0, ticks1,
                keymapSeen ? "saw keys" : "SILENT");
    const bool ok = alive && keymapSeen && !cpu.isHalted();
    std::printf("%s\n", ok ? "PASS: keyboard types on the real firmware"
                           : "FAIL: keyboard path wedged or silent");
    return ok ? 0 : 1;
}
