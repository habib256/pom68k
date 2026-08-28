// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Beyond-boot on the AIO family (TODO § 2, named next after the Q605's
// third leg): the Macintosh LC 520 — Sonora machine like the LC III the
// roster already soaks, but the half the LC III legs never touch: the
// separate universal ROM ($EDE66CBD), a CUDA MCU instead of the Egret
// (341s0060 v2.40 on a real 68HC05 when the dump is present), and the
// built-in 640×480 display that comes up in 8-bpp COLOR — which is why the
// signature below is lc520_boot_etalon's luminance one, not the sibling
// gates' blue-channel ratio (the System 7.5 desktop weave reads near-solid
// black in blue). Rig and thresholds are lc520_boot_etalon's; scenario
// engine is the shared tests/BeyondBoot.h (POM68K_BEYOND=soak|persist).
// The Time read goes through the Mmu030Peek walk, universally right on any
// 030. Soft-skips without the 1 MB EDE66CBD ROM + a bootable hdv/ image.

#include "AssetFingerprint.h"
#include "BeyondBoot.h"
#include "FinderSignature.h"
#include "Mmu030Peek.h"
#include "SonoraCpu.h"
#include "SonoraMemory.h"
#include "SonoraVideo.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

int main() {
    std::string rom = testasset::find("roms/maclc520.rom");
    if (rom.empty())
        rom = testasset::find("roms/1MB ROMs/1993-10 - EDE66CBD - Color Classic II & LC 550 & Performa 275,550,560 & Macintosh TV.ROM");
    std::string img = testasset::overrideImage();
    if (img.empty()) img = testasset::find("hdv/lc3-boot.vhd");
    // Versioned reference before the unversioned mutable image — the § 1
    // fixture rule, applied here from day one instead of retrofitted: this
    // gate's signature is ratio-based and image-tolerant.
    if (img.empty()) img = testasset::find("hdv/ref/GISTPERSO-boot.vhd");
    if (img.empty()) img = testasset::find("hdv/GISTPERSO-boot.vhd");
    if (img.empty()) img = testasset::find("hdv/boot.vhd");
    if (img.empty()) img = testasset::find("hdv/System 7.5 HD.dsk");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs the 1 MB EDE66CBD ROM + a bootable hdv/ image\n");
        return 0;
    }
    testasset::report({ rom, img });

    std::ifstream in(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
    if (romData.size() != SonoraMemory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM is %zu bytes, want 1 MB\n", romData.size());
        return 1;
    }

    // LC 520 identity + Cuda transport, exactly lc520_boot_etalon's rig.
    SonoraMemory mem(pom68k::defaultCoreConfig(), 0x800000,
                     SonoraMemory::kCpuHz, SonoraMemory::kIdLc520,
                     /*cudaAdb=*/true);
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    mem.setMonitorSense(6);                  // built-in 640×480 RGB
    SonoraCpu cpu(mem, jit::defaultResolvedConfig(),
                  pom68k::defaultCoreConfig().cpu, /*withFpu=*/true);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk image\n"); return 1; }
    beyondboot::ensureBootDriverType(mem.scsiDisk().image());
    const int64_t kFrame = SonoraMemory::kCpuHz / 60;

    auto frames = [&](long n) {
        for (long f = 0; f < n && !cpu.isHalted(); f++) cpu.runCycles(kFrame);
    };
    int W = 640, H = 480;
    // lc520_boot_etalon's luminance signature (8-bpp color weave), plus the
    // roster's own no-modal-dialog rule. lightRun keys on the BLUE channel,
    // which stays honest here too: the orange/green weave is blue-poor, an
    // alert's white body is not.
    struct Screen { double menu, desk; int run; };
    auto screen = [&]() {
        SonoraVideo video(mem);
        std::vector<uint32_t> fb;
        video.decode(fb);
        video.size(W, H);
        auto darkRatio = [&](int x0, int x1, int y0, int y1) {
            long dark = 0;
            for (int y = y0; y < y1; y++)
                for (int x = x0; x < x1; x++) {
                    uint32_t p = fb[size_t(y) * W + x];
                    int luma = (2 * int((p >> 16) & 0xFF) + 5 * int((p >> 8) & 0xFF)
                              + int(p & 0xFF)) / 8;
                    if (luma < 0x80) dark++;
                }
            return double(dark) / (double(x1 - x0) * (y1 - y0));
        };
        Screen s{ darkRatio(0, W, 2, 16),
                  darkRatio(W - 112, W, 40, H - 44),
                  beyondboot::lightRun(fb, W, H) };
        std::fprintf(stderr, "[finder] menu %.2f desk %.2f run %d\n",
                     s.menu, s.desk, s.run);
        return s;
    };
    auto finderUp = [&]() {
        const Screen s = screen();
        return s.menu < 0.30 && s.desk > 0.35 && s.desk < 0.80 &&
               s.run < beyondboot::kDialogRun;
    };
    // The GIST PERSO reference volume AUTO-OPENS two Finder windows at boot
    // ("GIST PERSO" and the ~400-px-wide "JEUX"), and a window's white body
    // is indistinguishable from an alert's to lightRun (402 measured against
    // the alert's 306-406 — no gap). So the windows are CLOSED before the
    // no-dialog rule is armed: Cmd-Option-W closes them all, and only a
    // light run that SURVIVES that is treated as a modal dialog. Cmd is
    // established before the letter — the IIvx two-transition ADB lesson.
    // ADB codes are PHYSICAL keys and the reference volume runs a FRENCH
    // System: on its AZERTY layout the letter W sits on code $06 (the
    // QWERTY Z position), and sending $0D typed Cmd-Option-Z — an undo,
    // no visible effect, which is exactly what the first run showed (run
    // 402 unchanged through two "closes"). Both codes are sent: whichever
    // is not W on the guest's layout lands on Z, and Cmd-Option-Z in the
    // Finder is harmless.
    int closeAttempt = 0;
    auto closeWindows = [&]() {
        for (uint8_t w : { uint8_t(0x06), uint8_t(0x0D) }) {
            mem.keyEvent(0x37, true);        // Cmd
            frames(12);
            mem.keyEvent(0x3A, true);        // Option
            frames(12);
            mem.keyEvent(w, true);           // W (AZERTY, then QWERTY)
            frames(75);
            mem.keyEvent(w, false);
            mem.keyEvent(0x3A, false);
            mem.keyEvent(0x37, false);
            frames(300);
        }
        SonoraVideo video(mem);
        std::vector<uint32_t> fb;
        video.decode(fb);
        beyondboot::dumpPpm(("aio_close_" + std::to_string(closeAttempt++)
                             + ".ppm").c_str(), fb, W, H);
    };
    auto boot = [&]() {
        while (mem.cpuHeld()) mem.tick(1000);
        frames(16000);
        int closes = 0;
        for (int poll = 0; poll < 16; poll++) {
            if (cpu.isHalted()) return false;
            const Screen s = screen();
            const bool ratios = s.menu < 0.30 && s.desk > 0.35 && s.desk < 0.80;
            if (ratios && s.run < beyondboot::kDialogRun) return true;
            if (ratios && closes < 2) {
                // Desktop is up behind open windows — close them, then
                // re-judge. A real alert survives Cmd-Option-W (twice at
                // most, so this cannot alternate forever) and falls through
                // to the Return dismissal below.
                closeWindows();
                closes++;
                continue;
            }
            mem.keyEvent(0x24, true);
            frames(150);
            mem.keyEvent(0x24, false);
            frames(450);
        }
        return !cpu.isHalted() && finderUp();
    };

    if (!boot()) { std::fprintf(stderr, "FAIL: no Finder after boot\n"); return 1; }
    std::printf("Finder up %dx%d depth %d, TC=$%08X, ADB %s, SCSI %ld\n", W, H,
                mem.videoDepth(), cpu.getTC(),
                mem.egretLleActive() ? "LLE" : "HLE", mem.scsi().commands);

    beyondboot::Hooks h;
    h.name = "Macintosh LC 520";
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
    h.reboot = [&]() { cpu.hardReset(); return boot(); };
    h.dump = [&](const char* mode) {
        SonoraVideo video(mem);
        std::vector<uint32_t> fb;
        video.decode(fb);
        beyondboot::dumpPpm((std::string("aio_beyond_") + mode + ".ppm").c_str(),
                            fb, W, H);
    };
    // The § 2 house rule: a persist leg that fails on "NO candidate folder
    // name appeared" binds frontApp + focusFinder BEFORE anything else is
    // suspected. Bound from day one here. The walk-based reader is the
    // universally right one on a 030 (identity while the PMMU is off).
    auto peekLogical = [&](uint32_t a) {
        uint32_t phys = 0;
        if (!mmu030peek::translate(cpu.getTC(), cpu.getCRP(), cpu.getSRP(),
                                   a, 5,
                                   [&](uint32_t x) { return mem.peek8(x); },
                                   &phys))
            return -1;
        return int(mem.peek8(phys));
    };
    h.frontApp = [&]() { return findersig::curApNameAt(peekLogical); };
    // Open-loop but safe: saturate the pointer into the bottom-right of the
    // 640×480 desktop (below the Corbeille, empty weave) and click once.
    // The pointer starts top-left after boot, where a blind click would
    // open the Apple menu.
    h.focusFinder = [&]() {
        beyondboot::mark("focus: click the lower-right desktop");
        for (int i = 0; i < 90; i++) { mem.mouseMove(8, 6); frames(2); }
        for (int i = 0; i < 6; i++) { mem.mouseMove(-6, -5); frames(2); }
        mem.mouseButton(true);
        frames(10);
        mem.mouseButton(false);
        frames(60);
        if (h.dump) h.dump("focus");
    };
    // GIST PERSO holds its catalog in the File Manager cache like the RBV
    // and Duo volumes do — the Finder writes it when something else touches
    // the volume. Same keyboard-only stir as rbv_beyond_etalon: Cmd-Shift-3
    // drops a screen shot on the startup volume. Shift is held, so the '3'
    // keycode $14 produces the digit on the AZERTY layout too.
    h.stir = [&]() {
        beyondboot::mark("stir: Cmd-Shift-3 (screen shot to the boot volume)");
        mem.keyEvent(0x37, true);            // Cmd
        frames(6);
        mem.keyEvent(0x38, true);            // Shift
        frames(6);
        mem.keyEvent(0x14, true);            // '3'
        frames(150);
        mem.keyEvent(0x14, false);
        frames(6);
        mem.keyEvent(0x38, false);
        mem.keyEvent(0x37, false);
        frames(300);
    };
    // Same 7.5.x idle display blank as the LC III (Sonora hardware blank
    // plus a software fill whose owner wants a real event): mouse, a
    // harmless Shift, then a desktop click, and seconds to repaint.
    h.wake = [&]() {
        std::fprintf(stderr, "[wake] videoMode=$%02X before\n", mem.videoMode());
        for (int i = 0; i < 8; i++) {
            mem.mouseMove(i & 1 ? 5 : -5, i & 2 ? 2 : -2);
            frames(15);
        }
        mem.keyEvent(0x38, true);
        frames(150);
        mem.keyEvent(0x38, false);
        frames(120);
        mem.mouseButton(true);
        frames(30);
        mem.mouseButton(false);
        frames(300);
        std::fprintf(stderr, "[wake] videoMode=$%02X after\n", mem.videoMode());
    };
    return beyondboot::run(h);
}
