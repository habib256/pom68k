// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// M7 gate: the real ROM boots System 6 from a SCSI hard disk through the
// full NCR 5380 chain (arbitration, selection, command/data/status phases,
// pseudo-DMA). No floppy inserted, so the boot MUST come from SCSI. Checks
// the Finder desktop signature. Soft-skips without roms/macplus.rom +
// hdv/HD20SC.vhd.

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
    std::string rom = find("roms/macplus.rom"), hd = find("hdv/HD20SC.vhd");
    if (rom.empty() || hd.empty()) {
        std::printf("SKIP: needs roms/macplus.rom + hdv/HD20SC.vhd\n");
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
    if (!mem.attachScsi(hd)) { std::fprintf(stderr, "FAIL: bad SCSI image\n"); return 1; }

    MacFrameClock fc;
    fc.resync(cpu);
    // RAM test (~45 s) + SCSI probe + driver load + System launch.
    for (long f = 0; f < 5400; f++) fc.runFrame(cpu, mem);

    MacVideo video;
    const uint32_t* fb = video.render(mem);
    auto blackRatio = [&](int y0, int y1) {
        long black = 0;
        for (int y = y0; y < y1; y++)
            for (int x = 0; x < 512; x++)
                if (!(fb[y * 512 + x] & 0xFF)) black++;
        return double(black) / (512.0 * (y1 - y0));
    };
    double menuBar = blackRatio(2, 16);
    double desktop = blackRatio(120, 240);
    std::printf("menu bar black %.2f (want <0.30), desktop %.2f (want ~0.50)\n",
                menuBar, desktop);
    if (menuBar > 0.30 || desktop < 0.45 || desktop > 0.55) {
        std::fprintf(stderr, "FAIL: not the Finder desktop — SCSI boot failed\n");
        return 1;
    }
    std::printf("scsi_boot_etalon: System booted from SCSI to the Finder, gate passed\n");
    return 0;
}
