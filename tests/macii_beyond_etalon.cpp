// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Beyond-boot on the Macintosh II (GLUE + NuBus Toby + 68020, ADB through
// the PIC1654S) — shared-engine gate (BeyondBoot.h). Rig, boot length and
// signature from macii_boot_etalon; Time is physical (no PMMU on a stock
// Mac II). The HD20SC image runs a System 6 Finder, whose new folder is
// "Empty Folder" — FolderProbe carries the System 6 names since 2026-08-13.
// POM68K_BEYOND=soak|persist. Soft-skips without assets.

#include "AssetFingerprint.h"
#include "BeyondBoot.h"
#include "Cpu020.h"
#include "MacIIMemory.h"
#include "TobyVideo.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

int main() {
    std::string rom = testasset::find("roms/macii.rom");
    if (rom.empty())
        rom = testasset::find("roms/512KB ROMs/1988-09 - 97221136 - Mac IIx & IIcx & SE30.ROM");
    // System 7.0 first — the macii_sys7_boot_etalon combination. The HD20SC
    // fallback boots a System 6 Finder, whose Cmd-N needs an open window, so
    // the persist gesture only works on a 7.x volume.
    std::string img = testasset::find("hdv/System 7.0 HD.dsk");
    if (img.empty()) img = testasset::find("hdv/System 7.1 HD.dsk");
    if (img.empty()) img = testasset::find("hdv/HD20SC.vhd");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs Mac II ROM + bootable hdv/ image\n");
        return 0;
    }
    testasset::report({ rom, img });

    std::ifstream rin(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(rin)), {});
    if (romData.size() != MacIIMemory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM size\n");
        return 1;
    }
    MacIIMemory mem;
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    mem.installTobyVideo();
    Cpu020 cpu(mem, true);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk\n"); return 1; }
    beyondboot::ensureBootDriverType(mem.scsiDisk().image());
    // 1/60 s of CPU TIME, not the boot etalon's 800×525 convenience
    // quantum: the soak counts seconds against frames, and the etalon's
    // quantum is 1.61× a real frame — 10800 of them read 289 s on a healthy
    // clock and failed the [135,225] window for it (round 2, 2026-08-13).
    const int64_t kFrame = MacIIMemory::kCpuHz / 60;

    auto frames = [&](long n) {
        for (long f = 0; f < n && !cpu.isHalted(); f++) cpu.runCycles(kFrame);
    };
    double lastMenu = -1, lastDesk = -1;
    auto finderUp = [&]() {
        TobyVideo* tv = mem.toby();
        std::vector<uint32_t> fb;
        tv->decode(fb);
        const int W = tv->hres(), H = tv->vres();
        if (W < 512 || H < 342) return false;
        auto blackRatio = [&](int x0, int x1, int y0, int y1) {
            long black = 0;
            for (int y = y0; y < y1; y++)
                for (int x = x0; x < x1; x++)
                    if (x < W && y < H && (fb[size_t(y) * W + x] & 0xFF) < 0x80)
                        black++;
            return double(black) / double(x1 - x0) / double(y1 - y0);
        };
        const double menu = blackRatio(0, W, 2, 20);
        const double desk = blackRatio(W / 2, W, 40, H - 40);
        if (menu != lastMenu || desk != lastDesk) {
            std::fprintf(stderr, "[finder] menu %.2f desk %.2f\n", menu, desk);
            lastMenu = menu; lastDesk = desk;
        }
        return menu < 0.35 && desk > 0.20 && desk < 0.70;
    };
    // Adaptive boot: run the bulk, then poll — pressing Return every ~20 s
    // while the Finder is not up, the finder_boot_matrix trick for the
    // System 7 CautionAlerts (and the dirty-volume alert after a persist
    // reboot). A tap on an alert dismisses it; on the Finder it is inert.
    auto boot = [&]() {
        frames(24000);
        for (int poll = 0; poll < 20; poll++) {
            if (cpu.isHalted()) return false;
            if (finderUp()) return true;
            mem.keyEvent(0x24, true);
            frames(30);
            mem.keyEvent(0x24, false);
            frames(1170);
        }
        return !cpu.isHalted() && finderUp();
    };

    if (!boot()) { std::fprintf(stderr, "FAIL: no Finder after boot\n"); return 1; }
    std::printf("Finder up, ADB %s, SCSI %ld\n",
                mem.adbVia().lle() ? "PIC LLE" : "HLE", mem.scsi().commands);

    beyondboot::Hooks h;
    h.name = "Macintosh II";
    h.frames = frames;
    h.halted = [&]() { return cpu.isHalted(); };
    h.finderUp = finderUp;
    h.time = [&](uint32_t* out) {
        *out = uint32_t(mem.peek8(0x20C)) << 24 | uint32_t(mem.peek8(0x20D)) << 16
             | uint32_t(mem.peek8(0x20E)) << 8 | mem.peek8(0x20F);
        return true;
    };
    h.key = [&](uint8_t code, bool down) { mem.keyEvent(code, down); };
    h.probe = [&]() {
        std::fprintf(stderr, "[keymap]");
        for (uint32_t a = 0x174; a < 0x17C; a++)
            std::fprintf(stderr, " %02X", mem.peek8(a));
        // Cmd = vk $37 → KeyMap byte 6 bit 7; N = vk $2D → byte 5 bit 5
        // (KeyMap bit = vk, bytes big-endian bit order).
        std::fprintf(stderr, "  (want Cmd+N bits live)\n");
    };
    h.disk = [&]() -> std::vector<uint8_t>& { return mem.scsiDisk().image(); };
    h.reboot = [&]() { cpu.hardReset(); return boot(); };
    h.dump = [&](const char* mode) {
        TobyVideo* tv = mem.toby();
        std::vector<uint32_t> fb;
        tv->decode(fb);
        beyondboot::dumpPpm((std::string("macii_beyond_") + mode + ".ppm").c_str(),
                            fb, tv->hres(), tv->vres());
    };
    return beyondboot::run(h);
}
