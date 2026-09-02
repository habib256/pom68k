// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Beyond-boot on the LC III (Sonora + Egret 341S0851 + 68030 @ 25 MHz) —
// the fifth machine, and the first on the shared engine (BeyondBoot.h).
// Rig, boot loop and Finder signature are lc3_boot_etalon's; the Time read
// goes through the Mmu030Peek walk, which reduces to a physical read while
// the PMMU is off and walks the live tables when it is on — the universally
// right read on any 030. POM68K_BEYOND=soak|persist, one CTest entry each.
// Soft-skips without the LC III ROM + a bootable hdv/ image.

#include "AssetFingerprint.h"
#include "BeyondBoot.h"
#include "FinderSignature.h"
#include "Mmu030Peek.h"
#include "SonoraCpu.h"
#include "SonoraMemory.h"
#include "SonoraVideo.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

int main() {
    std::string rom = testasset::find("roms/maclc3.rom");
    if (rom.empty())
        rom = testasset::find("roms/1MB ROMs/1993-02 - ECBBC41C - Mac LC III.ROM");
    std::string img = testasset::overrideImage();
    if (img.empty()) img = testasset::find("hdv/lc3-boot.vhd");
    // The SMALL clean reference for this 1bpp signature family: the plain
    // grey System 7.5 desktop. GISTPERSO's desktop pattern reads ~0.93
    // here and rejects a live Finder (measured 2026-09-02), so it is NOT
    // in this chain; MacPack stays as the compatible last resort.
    if (img.empty()) img = testasset::find("hdv/System 7.5 HD.dsk");
    if (img.empty()) img = testasset::find("hdv/boot.vhd");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs the 1 MB LC III ROM + a bootable hdv/ image\n");
        return 0;
    }
    testasset::report({ rom, img });

    std::ifstream in(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
    if (romData.size() != SonoraMemory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM is %zu bytes\n", romData.size());
        return 1;
    }

    // 8 MB, and the persist leg still flushes: the Mac II's 4 MB remedy was
    // TRIED here on 2026-09-02 and measured useless — folder created
    // (gesture dump: icon selected on the desktop), then not one write in
    // two emulated minutes at either RAM size. This machine is the Duo's
    // case, and the stir hook below ends the session through Special →
    // Shut Down instead.
    SonoraMemory mem(pom68k::defaultCoreConfig(), 0x800000);
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    mem.setMonitorSense(2);                  // 512×384 12" RGB — the etalon frame
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
    int W = 512, H = 384;
    // lc3_boot_etalon's EXACT signature: blue-channel black ratio, menu bar
    // rows 2-16, and the icon-free RIGHT-HAND STRIP (W-112..W, 40..H-44) —
    // not a centre band, which windows and dialogs wander into over a soak.
    auto finderUp = [&]() {
        SonoraVideo video(mem);
        std::vector<uint32_t> fb;
        video.decode(fb);
        video.size(W, H);
        auto blackRatio = [&](int x0, int x1, int y0, int y1) {
            long black = 0;
            for (int y = y0; y < y1; y++)
                for (int x = x0; x < x1; x++)
                    if ((fb[size_t(y) * W + x] & 0xFF) < 0x80) black++;
            return double(black) / (double(x1 - x0) * (y1 - y0));
        };
        const double menu = blackRatio(0, W, 2, 16);
        const double desk = blackRatio(W - 112, W, 40, H - 44);
        const int run = beyondboot::lightRun(fb, W, H);
        std::fprintf(stderr, "[finder] menu %.2f desk %.2f run %d\n",
                     menu, desk, run);
        // …plus "no modal dialog". Two ratios are satisfied WITH an alert
        // on screen, which is how this gate came to send a whole persist
        // gesture into one: run against hdv/System 7.5.5 HD.dsk it reported
        // a Finder, and the dump showed "The alias 'Infinite HD' could not
        // be opened" with Stop/Continue, eating every key. Its own volume
        // never raises one, so the blindness was invisible — the roster's
        // own rule (BeyondBoot.h::lightRun), applied here too.
        //
        // 250, not the shared kDialogRun (200): this gate's own desktop —
        // hdv/boot.vhd, "MacPack" — measures **208**, and 200 would fail
        // every run of the two legs that are green today. The alert it has
        // to catch measures 306-406 on the same screen, so the gap is
        // still wide. Both numbers are from this gate's own output.
        return menu < 0.30 && desk > 0.35 && desk < 0.65 && run < 250;
    };
    auto boot = [&]() {
        while (mem.cpuHeld()) mem.tick(1000);
        frames(16000);
        // Poll and dismiss, the shape the rest of the roster uses: a
        // 150-frame Return hold (above a Slow Keys acceptance delay),
        // then look again.
        for (int poll = 0; poll < 16; poll++) {
            if (cpu.isHalted()) return false;
            if (finderUp()) return true;
            mem.keyEvent(0x24, true);
            frames(150);
            mem.keyEvent(0x24, false);
            frames(450);
        }
        return !cpu.isHalted() && finderUp();
    };

    if (!boot()) {
        {   // what the detector rejected, for eyes (POM68K_DUMP)
            SonoraVideo video(mem);
            std::vector<uint32_t> fb;
            video.decode(fb);
            video.size(W, H);
            beyondboot::dumpPpm("sonora_beyond_bootfail.ppm", fb, W, H);
        }
        std::fprintf(stderr, "FAIL: no Finder after boot\n");
        return 1;
    }
    std::printf("Finder up %dx%d, TC=$%08X, ADB %s, SCSI %ld\n", W, H,
                cpu.getTC(), mem.egretLleActive() ? "LLE" : "HLE",
                mem.scsi().commands);

    beyondboot::Hooks h;
    h.name = "Macintosh LC III";
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
    // Who is in front — the Stickies trap's THIRD machine (Duo 2026-08-15,
    // Mac II 2026-09-01, here 2026-09-02): the clean 7.5 reference carries
    // Startup Items that the flush tool skipped with a held Shift but a
    // plain gate boot runs, and Cmd-N then feeds whatever they left
    // frontmost. The walk, not peek8: this System lives behind the PMMU.
    auto walk8 = [&](uint32_t a) -> int {
        uint32_t phys = 0;
        if (!mmu030peek::translate(cpu.getTC(), cpu.getCRP(), cpu.getSRP(),
                                   a, 5,
                                   [&](uint32_t x) { return mem.peek8(x); },
                                   &phys))
            return -1;
        return int(mem.peek8(phys));
    };
    h.frontApp = [&]() { return findersig::curApNameAt(walk8); };
    // The LC III will not flush its volume any other way — 4 MB or 8, two
    // emulated minutes after Cmd-N there is NOT ONE write (the Duo's
    // 2026-08-14 finding, on its third machine 2026-09-02). So the stir is
    // the Duo's: end the session the way a user would, Special → Shut
    // Down, which flushes every volume on the way out. System 7 menus need
    // press-DRAG-release (lcii_shutdown_flush's lesson), and the pointer
    // reads through the PMMU walk like every other low-mem global here.
    auto pointer = [&](int& x, int& y) {
        const int xh = walk8(0x832), xl = walk8(0x833);
        const int yh = walk8(0x830), yl = walk8(0x831);
        if (xh < 0 || xl < 0 || yh < 0 || yl < 0) return false;
        x = int16_t(uint16_t(xh) << 8 | uint16_t(xl));
        y = int16_t(uint16_t(yh) << 8 | uint16_t(yl));
        return true;
    };
    auto steer = [&](int tx, int ty, int tol) {
        int px = 0, py = 0;
        for (int it = 0; it < 400; it++) {
            if (!pointer(px, py)) return false;
            const int dx = tx - px, dy = ty - py;
            if (std::abs(dx) <= tol && std::abs(dy) <= tol) return true;
            auto step = [](int d) {
                int v = d / 2;
                if (!v) return 0;
                if (std::abs(v) < 2) v = d > 0 ? 2 : -2;
                return std::max(-8, std::min(8, v));
            };
            mem.mouseMove(step(dx), step(dy));
            frames(2);
        }
        std::fprintf(stderr, "stir steer: wanted (%d,%d), reached (%d,%d)\n",
                     tx, ty, px, py);
        return false;
    };
    h.stir = [&]() {
        beyondboot::mark("stir: Special -> Shut Down");
        if (!steer(232, 8, 3)) return;         // the "Special" title
        mem.mouseButton(true);
        frames(18);
        if (!steer(242, 141, 4)) {             // drag onto "Shut Down"
            mem.mouseButton(false);
            return;
        }
        frames(12);
        mem.mouseButton(false);
        frames(120);
        if (h.dump) h.dump("stir");
    };
    h.probe = [&]() {
        std::fprintf(stderr, "[scsi] write cmds %ld blocks %ld\n",
                     mem.scsiDisk().writeCommands, mem.scsiDisk().writeBlocks);
        std::fprintf(stderr, "[keymap]");
        for (uint32_t a = 0x174; a < 0x17C; a++) {
            const int b = walk8(a);
            std::fprintf(stderr, " %02X", b < 0 ? 0xEE : unsigned(b));
        }
        std::fprintf(stderr, "  (want Cmd+N bits live)\n");
    };
    // Keyboard, not a desktop click: the proven channel on every machine
    // that reached this trap, and quitting the startup app leaves the
    // Finder as what remains (macii_beyond_etalon's reasoning).
    h.focusFinder = [&]() {
        for (int attempt = 0; attempt < 3; attempt++) {
            if (findersig::curApNameAt(walk8) == "Finder") return;
            beyondboot::mark("focus: Cmd-Q the startup application");
            mem.keyEvent(0x37, true);          // Cmd down
            frames(6);
            mem.keyEvent(0x0C, true);          // 'q', held past Slow Keys
            frames(150);
            mem.keyEvent(0x0C, false);
            frames(6);
            mem.keyEvent(0x37, false);
            frames(900);
            if (h.dump) h.dump("focus");
        }
    };
    h.reboot = [&]() { cpu.hardReset(); return boot(); };
    h.dump = [&](const char* mode) {
        SonoraVideo video(mem);
        std::vector<uint32_t> fb;
        video.decode(fb);
        beyondboot::dumpPpm((std::string("sonora_beyond_") + mode + ".ppm").c_str(),
                            fb, W, H);
    };
    // 7.5.5 blanks the Sonora display after ~3 idle minutes (the frame
    // reads all-black while the machine and its clock stay alive). Wake it
    // like a user: mouse first, then a harmless Shift tap — some dimmers
    // ignore motion and wake on a keystroke — and give the System several
    // seconds to repaint. videoMode is printed so a hardware blank
    // (bit 7) is distinguishable from a software black fill.
    h.wake = [&]() {
        std::fprintf(stderr, "[wake] videoMode=$%02X before\n", mem.videoMode());
        for (int i = 0; i < 8; i++) {
            mem.mouseMove(i & 1 ? 5 : -5, i & 2 ? 2 : -2);
            frames(15);
        }
        mem.keyEvent(0x38, true);            // Shift down (no text effect)
        frames(150);
        mem.keyEvent(0x38, false);
        frames(120);
        // A CLICK, not just motion: videoMode stayed $02 through the mouse
        // and Shift, so the black frame is a software fill whose owner
        // wants a real event. A click on the empty desktop is harmless.
        mem.mouseButton(true);
        frames(30);
        mem.mouseButton(false);
        frames(300);                         // ~5 s to repaint
        std::fprintf(stderr, "[wake] videoMode=$%02X after\n", mem.videoMode());
    };
    return beyondboot::run(h);
}
