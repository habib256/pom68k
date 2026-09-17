// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Gate: Mac OS formats and mounts a disk on the Quadra 630's IDE port.
//
// `ata_disk_test` pins the task file against hand-written registers. This
// one puts a blank IDE disk in front of a real System and lets the GUEST do
// the work: the Finder finds an unreadable disk, offers to initialize it,
// writes an HFS volume, and mounts it. POM68K issues no ATA command of its
// own — every one of them comes from the ROM's ATA Manager and the System.
//
// The two clicks are the dialog's own buttons, at the coordinates Mac OS
// 8.1 puts them on a 640×480 screen: « Initialize », then « Continue » on
// the erase confirmation. The disk is a blank file made here, so the gate
// destroys nothing and needs no image asset.
//
// What it guards: without the ATA interrupt reaching the CPU the dialog
// never appears at all (measured 2026-09-18 — the guest read one sector at
// boot and ignored the drive), so a regression there turns this red rather
// than slow.

#include "AssetFingerprint.h"
#include "Q630Cpu.h"
#include "Q630Memory.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    std::printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}
} // namespace

int main() {
    std::string romPath = testasset::findAny({
        "roms/1MB ROMs/1994-07 - 06684214 - LC,Quadra,Performa 630.ROM",
        "roms/mame/macqd630/06684214.bin", "roms/quadra630.rom" });
    std::string diskPath = testasset::findAny({ "hdv/MacOS-8.1-boot.vhd" });
    if (romPath.empty() || diskPath.empty()) {
        std::printf("SKIP: needs the 06684214 ROM + hdv/MacOS-8.1-boot.vhd\n");
        return 0;
    }
    testasset::report({ romPath, diskPath });

    const bool control = std::getenv("POM68K_IDE_NOCLICK") != nullptr;
    // A blank 100 MB IDE disk, made here. The two arms run in parallel
    // under ctest, so each owns its own file.
    const std::string idePath = control ? "q630_ide_untouched.img"
                                        : "q630_ide_etalon.img";
    {
        std::ofstream f(idePath, std::ios::binary);
        std::vector<char> zero(1 << 20, 0);
        for (int i = 0; i < 100; i++) f.write(zero.data(), std::streamsize(zero.size()));
    }

    std::ifstream in(romPath, std::ios::binary);
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
    pom68k::CoreConfig core;
    Q630Memory mem(core, 32u << 20);
    if (!mem.loadRom(rom) || !mem.attachScsi(diskPath)) {
        std::fprintf(stderr, "FAIL: could not load ROM/disk\n");
        return 1;
    }
    if (!mem.attachIde(idePath, true)) {
        std::fprintf(stderr, "FAIL: the IDE image was refused\n");
        return 1;
    }
    Q630Cpu cpu(mem, jit::defaultResolvedConfig(), core.cpu);
    mem.setCpu(&cpu);
    cpu.hardReset();
    while (mem.cpuHeld()) mem.tick(1000);

    constexpr int kFrameCycles = 550000;          // 33 MHz / ~60 Hz
    auto runFrames = [&](long n) {
        for (long f = 0; f < n && !cpu.isHalted(); f++) cpu.runCycles(kFrameCycles);
    };
    // 16 000 frames reaches the desktop with the dialog up; much beyond
    // that and Energy Saver blanks the screen, which is a different test.
    runFrames(16000);
    if (cpu.isHalted()) { std::fprintf(stderr, "FAIL: CPU halted\n"); return 1; }

    check(mem.ide().commands > 0, "the guest probes the IDE port on its own");
    check(mem.ide().sectorsRead > 0, "and reads the disk looking for a volume");
    const long writesBefore = mem.ide().sectorsWritten;
    check(writesBefore == 0, "without a gesture it writes nothing");

    if (!control) {
        // CLOSED LOOP on the guest's own pointer (low memory Mouse: $830 is
        // y, $832 is x), the technique lcii_beyond_etalon established.
        auto moveTo = [&](int tx, int ty) {
            for (int it = 0; it < 900; it++) {
                const int px = int16_t(mem.peek8(0x832) << 8 | mem.peek8(0x833));
                const int py = int16_t(mem.peek8(0x830) << 8 | mem.peek8(0x831));
                const int dx = tx - px, dy = ty - py;
                if (!dx && !dy) break;
                auto step = [](int d) {
                    int s = d / 2; if (!s) s = d > 0 ? 1 : (d < 0 ? -1 : 0);
                    return std::max(-8, std::min(8, s));
                };
                mem.mouseMove(step(dx), step(dy));
                runFrames(2);
            }
            runFrames(10);
        };
        auto clickAt = [&](int x, int y) {
            moveTo(x, y);
            mem.mouseButton(true);  runFrames(5);
            mem.mouseButton(false); runFrames(5);
        };
        clickAt(452, 230);                        // « Initialize »
        runFrames(600);
        clickAt(415, 167);                        // « Continue »
        runFrames(9000);
    }

    const long wrote = mem.ide().sectorsWritten;
    std::printf("IDE: %ld commands, %ld sectors read, %ld written\n",
                mem.ide().commands, mem.ide().sectorsRead, wrote);

    // The image itself is the evidence: an HFS volume's Master Directory
    // Block is 'BD' at byte 1024, where the guest's initialize puts it.
    uint8_t mdb[2] = {};
    { std::ifstream f(idePath, std::ios::binary);
      f.seekg(1024); f.read(reinterpret_cast<char*>(mdb), 2); }
    const bool hfs = mdb[0] == 'B' && mdb[1] == 'D';

    if (control) {
        check(wrote == 0, "a disk nobody clicked on is left alone");
        check(!hfs, "and no volume is written to it");
        std::printf(failures ? "FAILED\n" : "PASSED — control arm untouched\n");
    } else {
        check(wrote > 1000, "the guest formats the disk: thousands of sectors");
        check(hfs, "and an HFS volume is on the image, written by Mac OS");
        std::printf(failures ? "FAILED\n"
                             : "PASSED — Mac OS formatted and mounted an IDE disk\n");
    }
    std::remove(idePath.c_str());
    return failures ? 1 : 0;
}
