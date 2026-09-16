// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// M5 gate: boot a real System floppy to the Finder desktop. Soft-skips
// unless roms/macplus.rom and the floppy (user-provided) are present.
// Checks the Finder signature: white menu bar with black glyphs on top,
// 50% gray desktop below, disk still inserted, head seeked.
//
// Usage: system_boot_etalon [--external] [disks35/<image>.dsk]
//   default image  disks35/Disk605.dsk — System 6.0.5 (the M5 cell)
//   --external     the floppy sits in drive B with drive A empty: the ROM
//                  must discover it there (the IWM drive-select line)
// Registered cells: system_boot_etalon (6.0.5, drive A),
// plus_system33_boot_etalon (System 3.3 on 800 K, drive A) and
// plus_system33_external_boot_etalon (the same in drive B) — the
// "Plus/System on floppy" cell the backlog waited on for an 800 K System
// image (TODO § Bloqué until 2026-09-16; the image came from the Infinite
// Mac clone and is pinned as disks35/ref/System 3.3.dsk).
// POM68K_SYSTEM_BOOT_PPM=<path> dumps the final screen as a PGM.

#include "AssetFingerprint.h"
#include "Cpu68k.h"
#include "JitTestConfig.h"
#include "MacMemory.h"
#include "MacVideo.h"
#include "MacFrame.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

static std::string find(const char* rel) {
    return testasset::find(rel);
}

int main(int argc, char** argv) {
    bool external = false;
    const char* image = "disks35/Disk605.dsk";
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--external") external = true;
        else image = argv[i];
    }
    std::string rom = find("roms/macplus.rom"), dsk = find(image);
    if (rom.empty() || dsk.empty()) {
        std::printf("SKIP: needs roms/macplus.rom + %s\n", image);
        return 0;
    }
    testasset::report({ rom, dsk });
    std::ifstream in(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
    MacMemory mem(pom68k::defaultCoreConfig());
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    const jit::ResolvedConfig jitConfig = testjit::resolveFromEnvironment();
    Cpu68k cpu(mem, jitConfig);
    mem.setCpu(&cpu);
    cpu.hardReset();
    SonyDrive& bootDrive = external ? mem.externalDrive() : mem.internalDrive();
    if (!bootDrive.insert(dsk)) { std::fprintf(stderr, "FAIL: bad disk\n"); return 1; }

    MacFrameClock fc;
    fc.resync(cpu);
    for (long f = 0; f < 4500; f++) fc.runFrame(cpu, mem);   // RAM test + boot

    if (!bootDrive.hasDisk()) { std::fprintf(stderr, "FAIL: ejected\n"); return 1; }
    if (external && (mem.externalDrive().nibblesRead == 0 ||
                     mem.internalDrive().nibblesRead > mem.externalDrive().nibblesRead)) {
        std::fprintf(stderr, "FAIL: boot did not come from the external drive "
                             "(internal=%ld external=%ld)\n",
                     mem.internalDrive().nibblesRead, mem.externalDrive().nibblesRead);
        return 1;
    }

    MacVideo video;
    const uint32_t* fb = video.render(mem);
    if (const char* ppm = getenv("POM68K_SYSTEM_BOOT_PPM")) {
        std::ofstream out(ppm, std::ios::binary);
        out << "P5\n512 342\n255\n";
        for (int i = 0; i < 512 * 342; i++) out.put(char((fb[i] & 0xFF) ? 255 : 0));
    }
    auto blackRatio = [&](int y0, int y1) {
        long black = 0;
        for (int y = y0; y < y1; y++)
            for (int x = 0; x < 512; x++)
                if (!(fb[y * 512 + x] & 0xFF)) black++;
        return double(black) / (512.0 * (y1 - y0));
    };
    double menuBar = blackRatio(2, 16);      // mostly white + glyph pixels
    // The boot floppy opens its "System Tools" window over y=29..208.  The
    // old y=120..239 probe therefore measured mostly the white window body,
    // not the desktop it claimed to qualify.  This lower strip is clear of
    // both that window and the Trash icon.
    double desktop = blackRatio(240, 270);   // 50% gray desktop dither
    std::printf("%s%s: menu bar black %.2f (want <0.30), desktop %.2f (want ~0.50), track %d\n",
                image, external ? " (drive B)" : "", menuBar, desktop,
                bootDrive.currentTrack());
    // Reference numbers for the SWIM1-IWM mount hunt (TODO §1): this is the
    // SAME image the LC II declares unreadable, read here by the raw IWM at
    // C7M by a driver that succeeds. The poll/hit/overwritten ratio is what
    // a HEALTHY guest read looks like — compare against lcii_beyond_etalon's
    // floppy scenario before blaming the medium or the GCR encoder.
    {
        const Iwm& iwm = mem.iwm();
        std::printf("IWM health: polls %ld, hits %ld (%.1f%%), overwritten %ld, "
                    "nibbles %ld\n", iwm.dataReads, iwm.dataHits,
                    iwm.dataReads ? 100.0 * double(iwm.dataHits)
                                  / double(iwm.dataReads) : 0.0,
                    iwm.overwritten, bootDrive.nibblesRead);
    }
    if (menuBar > 0.30 || desktop < 0.45 || desktop > 0.55) {
        std::fprintf(stderr, "FAIL: not the Finder desktop\n");
        return 1;
    }
    std::printf("system_boot_etalon: System booted to the Finder, gate passed\n");
    return 0;
}
