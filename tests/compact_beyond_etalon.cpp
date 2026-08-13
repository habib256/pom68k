// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Beyond-boot on the Macintosh Plus (68000 cycle-exact, M0110 keyboard,
// SCSI) — shared-engine gate (BeyondBoot.h). The rig is the
// finder_boot_matrix bootPlus procedure: HD boot off attachScsi, the
// 512×342 1-bpp signature. Keys go down the M0110 wire — the transition
// byte is (virtual keycode << 1) | 1, with bit 7 set on release — through
// MacKeyboard::enqueue; the VIRTUAL codes (Cmd $37, 'n' $2D, Return $24)
// are the same ones ADB uses, so the shared persist gesture needs no
// per-machine table. Time is physical: no MMU on a 68000, low RAM is low
// RAM. POM68K_BEYOND=soak|persist. Soft-skips without assets.

#include "AssetFingerprint.h"
#include "BeyondBoot.h"
#include "Cpu68k.h"
#include "MacFrame.h"
#include "MacMemory.h"
#include "MacVideo.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

int main() {
    std::string rom = testasset::find("roms/macplus.rom");
    // System 7.1 first. Two versions are ruled out by measurement, not by
    // taste: System 6's Cmd-N is inert with no window open (round 2's
    // HD20SC finding), and System 7.0 creates the folder — a screen dump
    // at the gesture's peak shows the icon on the desktop — but never
    // flushes the volume: ONE write command in a whole session, the mount
    // flag, and none at all in the two emulated minutes that follow. The
    // gate spent a day being read as a broken keyboard because of it.
    // 7.1 writes the catalog and the folder survives the reset.
    std::string img = testasset::find("hdv/System 7.1 HD.dsk");
    if (img.empty()) img = testasset::find("hdv/System 7.0 HD.dsk");
    if (img.empty()) img = testasset::find("hdv/HD20SC.vhd");
    if (img.empty()) img = testasset::find("hdv/boot.vhd");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs roms/macplus.rom + a bootable hdv/ image\n");
        return 0;
    }
    testasset::report({ rom, img });

    std::ifstream in(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
    MacMemory mem;
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    Cpu68k cpu(mem);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk\n"); return 1; }
    beyondboot::ensureBootDriverType(mem.scsiDisk().image());

    MacFrameClock fc;
    fc.resync(cpu);
    auto frames = [&](long n) {
        for (long f = 0; f < n && !cpu.isHalted(); f++) fc.runFrame(cpu, mem);
    };
    MacVideo video;
    // M0110 Return tap, usable from boot() before the Hooks exist.
    auto h_keyReturnTap = [&]() {
        mem.keyboard().enqueue(uint8_t((0x24 << 1) | 1));
        frames(30);
        mem.keyboard().enqueue(uint8_t(((0x24 << 1) | 1) | 0x80));
    };
    auto finderUp = [&]() {
        const uint32_t* fb = video.render(mem);
        auto blackRatio = [&](int x0, int x1, int y0, int y1) {
            long black = 0;
            for (int y = y0; y < y1; y++)
                for (int x = x0; x < x1; x++)
                    if ((fb[y * 512 + x] & 0xFF) < 0x80) black++;
            return double(black) / (double(x1 - x0) * (y1 - y0));
        };
        const double menu = blackRatio(0, 512, 2, 16);
        const double desk = blackRatio(0, 512, 120, 240);
        static double lm = -1, ld = -1;
        if (menu != lm || desk != ld) {
            std::fprintf(stderr, "[finder] menu %.2f desk %.2f\n", menu, desk);
            lm = menu; ld = desk;
        }
        // bootPlus's signature: white menu bar, 50 % dither desktop.
        return menu < 0.30 && desk > 0.40 && desk < 0.60;
    };
    // Adaptive: System 7.0 boots slower on a Plus than the 5400 frames the
    // 6.x matrix cell budgets; poll up to ~16000 with a Return tap for the
    // dirty-volume alert after a persist reboot.
    auto boot = [&]() {
        frames(5400);
        for (int poll = 0; poll < 18; poll++) {
            if (cpu.isHalted()) return false;
            if (finderUp()) return true;
            h_keyReturnTap();
            frames(570);
        }
        return !cpu.isHalted() && finderUp();
    };

    if (!boot()) { std::fprintf(stderr, "FAIL: no Finder after boot\n"); return 1; }
    std::printf("Finder up 512x342, SCSI %ld\n", mem.scsi().commands);

    beyondboot::Hooks h;
    h.name = "Macintosh Plus";
    h.frames = frames;
    h.halted = [&]() { return cpu.isHalted(); };
    h.finderUp = finderUp;
    h.time = [&](uint32_t* out) {
        *out = uint32_t(mem.peek8(0x20C)) << 24 | uint32_t(mem.peek8(0x20D)) << 16
             | uint32_t(mem.peek8(0x20E)) << 8 | mem.peek8(0x20F);
        return true;
    };
    h.key = [&](uint8_t code, bool down) {
        // M0110 wire transition: (vk << 1) | 1, bit 7 = release.
        uint8_t t = uint8_t((code << 1) | 1);
        mem.keyboard().enqueue(down ? t : uint8_t(t | 0x80));
    };
    h.disk = [&]() -> std::vector<uint8_t>& { return mem.scsiDisk().image(); };
    h.writes = [&]() { return mem.scsiDisk().writeBlocks; };
    h.probe = [&]() {
        std::fprintf(stderr, "[keymap]");
        for (uint32_t a = 0x174; a < 0x17C; a++)
            std::fprintf(stderr, " %02X", mem.peek8(a));
        std::fprintf(stderr, "  (want Cmd+N bits live)\n");
        // Did the guest write ANYTHING to the volume since it mounted? A
        // System 7 mount alone dirties the MDB, so zero here means the
        // volume came up read-only and no keystroke could ever change it.
        std::fprintf(stderr, "[scsi] write cmds %ld blocks %ld\n",
                     mem.scsiDisk().writeCommands, mem.scsiDisk().writeBlocks);
    };
    h.reboot = [&]() {
        cpu.hardReset();
        fc.resync(cpu);
        return boot();
    };
    h.dump = [&](const char* mode) {
        const uint32_t* fb = video.render(mem);
        std::vector<uint32_t> copy(fb, fb + 512 * 342);
        beyondboot::dumpPpm((std::string("compact_beyond_") + mode + ".ppm").c_str(),
                            copy, 512, 342);
    };
    return beyondboot::run(h);
}
