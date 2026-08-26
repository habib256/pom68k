// POM68K — compact 68000 family boot gate: Mac SE / SE FDHD / Classic boot a
// System 6 floppy to the Finder desktop.
//
// These three are the Mac Plus machine (`MacMemory`) with a bigger ROM and
// ADB in place of the M0110 keyboard + quadrature mouse — the very PIC1654S
// ADB transceiver the Mac II runs as firmware LLE (mac128.cpp macse:
// `m_adbmodem->set_via_state((data & 0x30) >> 4)`). POM68K_COMPACT_MODEL
// picks the sibling: se (default) / sefdhd / classic.
//
// Finder signature (same as the Plus gate): white menu bar with black glyphs
// on top, 50 % gray dithered desktop below, disk still inserted.
// Soft-skips unless the model's ROM and disks35/Disk605.dsk are present.

#include "AssetFingerprint.h"
#include "Cpu68k.h"
#include "JitTestConfig.h"
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
    const char* which = getenv("POM68K_COMPACT_MODEL");
    MacMemory::Model model = MacMemory::Model::SE;
    const char* romRel = "roms/256KB ROMs/1987-03 - B2E362A8 - Mac SE.ROM";
    const char* name = "Macintosh SE";
    if (which && !std::strcmp(which, "sefdhd")) {
        model = MacMemory::Model::SEFDHD;
        romRel = "roms/256KB ROMs/1989-08 - B306E171 - Mac SE FDHD.ROM";
        name = "Macintosh SE FDHD";
    } else if (which && !std::strcmp(which, "classic")) {
        model = MacMemory::Model::Classic;
        romRel = "roms/512KB ROMs/1990-10 - A49F9914 - Mac Classic.rom";
        name = "Macintosh Classic";
    }

    std::string rom = find(romRel), dsk = find("disks35/Disk605.dsk");
    if (rom.empty() || dsk.empty()) {
        std::printf("SKIP: needs %s + disks35/Disk605.dsk\n", romRel);
        return 0;
    }
    testasset::report({ rom, dsk });
    std::ifstream in(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)), {});

    MacMemory mem(pom68k::defaultCoreConfig(), model);
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    const jit::ResolvedConfig jitConfig = testjit::resolveFromEnvironment();
    Cpu68k cpu(mem, jitConfig);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.insertDisk(dsk)) { std::fprintf(stderr, "FAIL: bad disk\n"); return 1; }
    if (!mem.adbLleActive())
        std::printf("note: ADB HLE fallback (roms/adbmodem/342s0440-b.bin absent)\n");

    // The compacts spend ~350 frames in ADBReInit and ~500 more in the 4 MB
    // RAM test before the disk driver even starts — a Plus reaches the Finder
    // in 4500, these need the longer run.
    const long kFrames = getenv("POM68K_FRAMES") ? atol(getenv("POM68K_FRAMES")) : 6000;
    MacFrameClock fc;
    fc.resync(cpu);
    for (long f = 0; f < kFrames; f++) fc.runFrame(cpu, mem);

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
    // Disk605 opens its "System Tools" window over y=29..208.  Sample the
    // unobscured desktop below it (and above the Trash icon), rather than
    // treating the window's white contents as failed desktop rendering.
    double desktop = blackRatio(240, 270);   // 50 % gray desktop dither
    std::printf("%s: menu bar black %.2f (want <0.30), desktop %.2f (want ~0.50), track %d\n",
                name, menuBar, desktop, mem.internalDrive().currentTrack());
    if (menuBar > 0.30 || menuBar < 0.01) {
        std::fprintf(stderr, "FAIL: no menu bar\n");
        return 1;
    }
    if (desktop < 0.40 || desktop > 0.60) {
        std::fprintf(stderr, "FAIL: no gray desktop\n");
        return 1;
    }
    std::printf("PASS: %s reaches the Finder\n", name);
    return 0;
}
