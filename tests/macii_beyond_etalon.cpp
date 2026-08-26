// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Beyond-boot on the Macintosh II (GLUE + NuBus Toby + 68020, ADB through
// the PIC1654S) — shared-engine gate (BeyondBoot.h). Rig, boot length and
// signature from macii_boot_etalon; Time is physical (no PMMU on a stock
// Mac II). The HD20SC fallback runs a System 6 Finder, whose new folder is
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
    // System 7.1 first. The HD20SC fallback boots a System 6 Finder, whose
    // Cmd-N needs an open window; System 7.0 creates the folder and then
    // never flushes the volume (measured on the Plus, same image: one write
    // command per session and none after the gesture — see
    // compact_beyond_etalon.cpp). 7.1 is the first version that persists.
    std::string img = testasset::find("hdv/System 7.1 HD.dsk");
    if (img.empty()) img = testasset::find("hdv/System 7.0 HD.dsk");
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
    // 4 MB, not the 8 MB default — a rig choice with a measured reason.
    // System 7.1 sizes its disk cache from RAM, and on 8 MB it holds the
    // new folder's catalog blocks indefinitely: the Finder creates the
    // folder (a screen dump shows the icon), and the guest issues NOT ONE
    // write command in the next TEN emulated minutes. At 4 MB — the Plus's
    // size, and a period-correct Mac II — the same gesture reaches the
    // medium immediately. Nothing here is an emulator defect: a volume
    // that is never flushed is never flushed on real hardware either, and
    // this gate is about persistence, so it configures a machine that
    // persists rather than waiting on a cache that will not drain.
    MacIIMemory mem(pom68k::defaultCoreConfig(), 0x400000);
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    mem.installTobyVideo();
    Cpu020 cpu(mem, jit::defaultResolvedConfig(),
               pom68k::defaultCoreConfig().cpu, true);
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
        // No modal dialog: this volume opens one at every boot ("the alias
        // 'Infinite HD' could not be opened") and the two ratios above are
        // satisfied with it up, so boot() stopped tapping Return the moment
        // the menu bar appeared and the persist gesture went into a dialog
        // that swallows keys. BeyondBoot.h::lightRun carries the reasoning.
        const int white = beyondboot::lightRun(fb, W, H);
        if (menu != lastMenu || desk != lastDesk) {
            std::fprintf(stderr, "[finder] menu %.2f desk %.2f white %d\n",
                         menu, desk, white);
            lastMenu = menu; lastDesk = desk;
        }
        return menu < 0.35 && desk > 0.20 && desk < 0.70 &&
               white < beyondboot::kDialogRun;
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
            // 150 frames, the engine's hold everywhere: it outlasts a Slow
            // Keys acceptance delay, and a normal keyboard accepts it just
            // the same (pom68k-81-image-slow-keys). The old 30-frame tap
            // was below that threshold.
            mem.keyEvent(0x24, true);
            frames(150);
            mem.keyEvent(0x24, false);
            frames(1050);
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
        // Has the guest written ANYTHING since it mounted? A System 7 mount
        // alone clears the volume's clean-unmount bit, so a zero here is a
        // dead write path, not a Finder that did nothing.
        std::fprintf(stderr, "[scsi] write cmds %ld blocks %ld\n",
                     mem.scsiDisk().writeCommands, mem.scsiDisk().writeBlocks);
        std::fprintf(stderr, "[keymap]");
        for (uint32_t a = 0x174; a < 0x17C; a++)
            std::fprintf(stderr, " %02X", mem.peek8(a));
        // Cmd = vk $37 → KeyMap byte 6 bit 7; N = vk $2D → byte 5 bit 5
        // (KeyMap bit = vk, bytes big-endian bit order).
        std::fprintf(stderr, "  (want Cmd+N bits live)\n");
    };
    h.disk = [&]() -> std::vector<uint8_t>& { return mem.scsiDisk().image(); };
    h.writes = [&]() { return mem.scsiDisk().writeBlocks; };
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
