// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Beyond-boot on the Macintosh IIfx (OSS + two Apple PIC IOPs, 68030 @
// 40 MHz, Toby on slot 9, ADB through the IOP firmware) — shared-engine
// gate (BeyondBoot.h). Rig, early-exit boot and signature from
// iifx_boot_etalon (the recorded reference reaches the Finder around
// f=539); keys travel through the IOP firmware ↔ AdbLine; Time through the
// Mmu030Peek walk. POM68K_BEYOND=soak|persist. Soft-skips without the IIfx
// ROM, the real Toby declaration ROM 342-0008-a and a bootable image.

#include "AssetFingerprint.h"
#include "BeyondBoot.h"
#include "IIfxCpu.h"
#include "IIfxMemory.h"
#include "Mmu030Peek.h"
#include "TobyVideo.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

int main() {
    std::string rom = testasset::find("roms/512KB ROMs/1990-03 - 4147DD77 - Mac IIfx.ROM");
    std::string toby = testasset::find("roms/342-0008-a.bin");
    if (toby.empty())
        toby = testasset::find("roms/archive/macroms/Misc/Video cards/Apple Macintosh II Video Card/342-0008-a.bin");
    if (toby.empty())
        toby = testasset::find("roms/archive/macroms/68k/256k/Macintosh II/342-0008-a.bin");
    std::string img = testasset::find("hdv/GISTPERSO-boot.vhd");
    if (img.empty()) img = testasset::find("hdv/boot.vhd");
    if (rom.empty() || toby.empty() || img.empty()) {
        std::printf("SKIP: needs the Mac IIfx ROM, Toby 342-0008-a and a bootable image\n");
        return 0;
    }
    testasset::report({ { "rom", rom }, { "declrom", toby }, { "disk", img } });

    std::ifstream rin(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(rin)), {});
    if (romData.size() != IIfxMemory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM size\n");
        return 1;
    }
    IIfxMemory mem;
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    if (!mem.installTobyVideo(toby)) {
        std::fprintf(stderr, "FAIL: bad Toby declaration ROM\n");
        return 1;
    }
    IIfxCpu cpu(mem, /*withFpu=*/true);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk\n"); return 1; }
    beyondboot::ensureBootDriverType(mem.scsiDisk().image());
    const int64_t kFrame = 40000000LL * 100 / 6015;
    const int W = TobyVideo::W, H = TobyVideo::H;

    auto frames = [&](long n) {
        for (long f = 0; f < n && !cpu.isHalted(); f++) cpu.runCycles(kFrame);
    };
    auto finderUp = [&]() {
        std::vector<uint32_t> fb;
        mem.toby()->decode(fb);
        auto blackRatio = [&](int x0, int x1, int y0, int y1) {
            long black = 0;
            for (int y = y0; y < y1; y++)
                for (int x = x0; x < x1; x++)
                    if ((fb[size_t(y) * W + x] & 0xFF) < 0x80) black++;
            return double(black) / double(x1 - x0) / double(y1 - y0);
        };
        const double menu = blackRatio(0, W, 2, 20);
        const double desk = blackRatio(W / 2, W, 40, H - 40);
        static double lm = -1, ld = -1;
        if (menu != lm || desk != ld) {
            std::fprintf(stderr, "[finder] menu %.2f desk %.2f\n", menu, desk);
            lm = menu; ld = desk;
        }
        // iifx_boot_etalon's signature (its 7.6-FR desktop reads up to
        // 0.79), plus a menu-bar FLOOR. Without it the gate called the
        // Finder up while the ROM had only painted the desktop pattern and
        // the menu bar still held nothing but the Apple: 0.060 of ink
        // against 0.183 once "Fichier Édition Présentation Famille Spécial"
        // is drawn (measured on this gate's own dumps, 2026-08-13). The
        // whole persist gesture went into that half-booted machine, opened
        // the volume's window, and the gate reported it as a dead IOP
        // keyboard path. An upper bound alone cannot tell an empty menu bar
        // from a full one — only a lower bound can.
        //
        // The floor also covers what `lightRun` covers elsewhere, and does
        // it better here: a modal alert DIMS the menu bar, so the ink falls
        // below the floor on its own. Adding the run test as well made this
        // gate's soak red — this desktop is a dark noisy 7.6-FR pattern and
        // an ordinary Finder window on it reads as a dialog.
        return menu > 0.12 && menu < 0.35 && desk > 0.20 && desk < 0.90;
    };
    auto boot = [&]() {
        // Early-exit poll, as the boot etalon does: the IIfx reference
        // reaches the Finder around f=539 of a 18000-frame ceiling. From
        // frame 3000 on, tap Return every ~20 s — the persist reboot lands
        // on the dirty-volume alert, which sat at menu 0.50 / desk 0.40 for
        // the whole ceiling in round 2 until a keystroke dismissed nothing.
        for (long f = 0; f < 24000 && !cpu.isHalted(); f++) {
            cpu.runCycles(kFrame);
            if (f % 60 == 59 && mem.scsi().commands > 500 && finderUp())
                return true;
            if (f >= 3000 && f % 1200 == 0) {
                // 150 frames, the engine's hold everywhere: it outlasts a
                // Slow Keys acceptance delay and a normal keyboard takes it
                // just the same (pom68k-81-image-slow-keys). The 30-frame
                // tap this used to send was below that threshold, which is
                // why the dirty-volume alert sat at menu 0.50 / desk 0.40
                // through the whole ceiling "despite Return taps".
                mem.keyEvent(0x24, true);
                for (int k = 0; k < 150; k++) cpu.runCycles(kFrame);
                mem.keyEvent(0x24, false);
            }
        }
        return !cpu.isHalted() && finderUp();
    };

    if (!boot()) { std::fprintf(stderr, "FAIL: no Finder after boot\n"); return 1; }
    std::printf("Finder up, SCSI %ld\n", mem.scsi().commands);

    beyondboot::Hooks h;
    h.name = "Macintosh IIfx";
    h.frames = frames;
    h.halted = [&]() { return cpu.isHalted(); };
    h.finderUp = finderUp;
    h.time = [&](uint32_t* out) {
        uint32_t v = 0;
        for (int i = 0; i < 4; i++) {
            uint32_t phys = 0;
            if (!mmu030peek::translate(cpu.getTC(), cpu.getCRP(), cpu.getSRP(),
                                       0x20C + uint32_t(i), 5,
                                       [&](uint32_t a) { return mem.peek8(a); },
                                       &phys))
                return false;
            v = v << 8 | mem.peek8(phys);
        }
        *out = v;
        return true;
    };
    h.key = [&](uint8_t code, bool down) { mem.keyEvent(code, down); };
    h.disk = [&]() -> std::vector<uint8_t>& { return mem.scsiDisk().image(); };
    h.writes = [&]() { return mem.scsiDisk().writeBlocks; };
    // A CPU reset, not a machine reset — TRIED and reverted 2026-08-13.
    // `mem.reset()` looks more faithful and is worse here: it wipes the
    // IIfx's PRAM (`rtc_.factoryDefaults()`) and zeroes the Duo's GSC mode
    // registers, and the guest does not rewrite the latter, so the second
    // boot decoded at the wrong depth and the screen came back as noise
    // while the machine was plainly alive behind it. It also did not fix
    // what it was tried for: the IIfx's second boot fails identically
    // either way, and even on a machine rebuilt from scratch.
    h.reboot = [&]() { cpu.hardReset(); return boot(); };
    h.dump = [&](const char* mode) {
        std::vector<uint32_t> fb;
        mem.toby()->decode(fb);
        beyondboot::dumpPpm((std::string("iifx_beyond_") + mode + ".ppm").c_str(),
                            fb, W, H);
    };
    return beyondboot::run(h);
}
