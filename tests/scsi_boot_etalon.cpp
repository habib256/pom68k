// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// M7 gate: a real compact ROM boots System 6 from a SCSI hard disk through the
// full NCR 5380 chain (arbitration, selection, command/data/status phases,
// pseudo-DMA). No floppy inserted, so the boot MUST come from SCSI. Checks
// the Finder desktop signature.  POM68K_COMPACT_MODEL selects Plus (default),
// SE, SE FDHD or Classic so every compact profile has an explicit SCSI proof.

#include "AssetFingerprint.h"
#include "Cpu68k.h"
#include "MacMemory.h"
#include "MacVideo.h"
#include "MacFrame.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

static std::string find(const char* rel) {
    return testasset::find(rel);
}

int main() {
    const char* which = std::getenv("POM68K_COMPACT_MODEL");
    MacMemory::Model model = MacMemory::Model::Plus;
    const char* romRel = "roms/macplus.rom";
    const char* name = "Macintosh Plus";
    if (which && !std::strcmp(which, "se")) {
        model = MacMemory::Model::SE;
        romRel = "roms/256KB ROMs/1987-03 - B2E362A8 - Mac SE.ROM";
        name = "Macintosh SE";
    } else if (which && !std::strcmp(which, "sefdhd")) {
        model = MacMemory::Model::SEFDHD;
        romRel = "roms/256KB ROMs/1989-08 - B306E171 - Mac SE FDHD.ROM";
        name = "Macintosh SE FDHD";
    } else if (which && !std::strcmp(which, "classic")) {
        model = MacMemory::Model::Classic;
        romRel = "roms/512KB ROMs/1990-10 - A49F9914 - Mac Classic.rom";
        name = "Macintosh Classic";
    }
    std::string rom = find(romRel), hd = find("hdv/HD20SC.vhd");
    if (rom.empty() || hd.empty()) {
        std::printf("SKIP: needs %s + hdv/HD20SC.vhd\n", romRel);
        return 0;
    }
    testasset::report({ rom, hd });
    std::ifstream in(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
    MacMemory mem(pom68k::defaultCoreConfig(), model);
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    Cpu68k cpu(mem, jit::defaultResolvedConfig());
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(hd)) { std::fprintf(stderr, "FAIL: bad SCSI image\n"); return 1; }

    MacFrameClock fc;
    fc.resync(cpu);
    // RAM test (~45 s) + SCSI probe + driver load + System launch.
    const long frames = model == MacMemory::Model::Plus ? 5400 : 6000;
    for (long f = 0; f < frames; f++) fc.runFrame(cpu, mem);

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
    std::printf("scsi_boot_etalon: %s booted from SCSI to the Finder, gate passed\n",
                name);
    return 0;
}
