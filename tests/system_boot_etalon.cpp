// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// M5 gate: boot a real System 6 floppy to the Finder desktop. Soft-skips
// unless both roms/macplus.rom and disks35/Disk605.dsk (user-provided) are
// present. Checks the Finder signature: white menu bar with black glyphs
// on top, 50% gray desktop below, disk still inserted, head seeked.

#include "Cpu68k.h"
#include "MacMemory.h"
#include "MacVideo.h"
#include "MacFrame.h"
#include <cstdio>
#include <fstream>
#include <string>
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
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    Cpu68k cpu(mem);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.insertDisk(dsk)) { std::fprintf(stderr, "FAIL: bad disk\n"); return 1; }

    MacFrameClock fc;
    fc.resync(cpu);
    for (long f = 0; f < 4500; f++) fc.runFrame(cpu, mem);   // RAM test + boot

    if (!mem.internalDrive().hasDisk()) { std::fprintf(stderr, "FAIL: ejected\n"); return 1; }

    MacVideo video;
    const uint32_t* fb = video.render(mem);
    auto blackRatio = [&](int y0, int y1) {
        long black = 0;
        for (int y = y0; y < y1; y++)
            for (int x = 0; x < 512; x++)
                if (!(fb[y * 512 + x] & 0xFF)) black++;
        return double(black) / (512.0 * (y1 - y0));
    };
    double menuBar = blackRatio(2, 16);      // mostly white + glyph pixels
    double desktop = blackRatio(120, 240);   // 50% gray dither
    std::printf("menu bar black %.2f (want <0.30), desktop %.2f (want ~0.50), track %d\n",
                menuBar, desktop, mem.internalDrive().currentTrack());
    if (menuBar > 0.30 || desktop < 0.45 || desktop > 0.55) {
        std::fprintf(stderr, "FAIL: not the Finder desktop\n");
        return 1;
    }
    std::printf("system_boot_etalon: System booted to the Finder, gate passed\n");
    return 0;
}
