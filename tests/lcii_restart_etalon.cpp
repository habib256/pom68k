// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Finder → Spécial → Redémarrer: the guest restarts itself ──
//
// Every boot etalon starts from a hard reset the HOST performs. A restart
// the GUEST asks for walks a different path: the Finder's Shutdown Manager
// runs the shutdown procedures, unmounts the volumes (the MDB's
// cleanly-unmounted bit is written), the System calls the ROM's restart
// trap, and the machine must come back through the ROM's warm-start path —
// PRAM and the Egret intact, the SCSI bus re-enumerated, the volume
// remounted clean, the Finder rebuilt. None of that is exercised by a host
// reset (TODO § C.1).
//
// Keyboard-only steering is the rule elsewhere, but Restart has no key
// equivalent in System 7.5; the menu is pulled down with the mouse, steered
// closed-loop against the low-memory Mouse global (the same read the
// Speedometer census uses), and the item is chosen by its position in the
// Spécial menu. POM68K_RESTART_DISCOVER=1 stops with the menu held open
// and dumps it, so the positions can be read off the guest's own screen
// rather than assumed. POM68K_DUMP=1 writes lcii_restart_*.ppm per phase.

#include "LciiApplicationHarness.h"

#include <algorithm>
#include <cmath>

using namespace lciiapp;

int main() {
    const std::string rom =
        find("roms/512KB ROMs/1992-03 - 35C28F5F - Mac LC II.ROM");
    std::string img = testasset::overrideImage();
    if (img.empty()) img = find("hdv/GISTPERSO-boot.vhd");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs the LC II ROM + hdv/GISTPERSO-boot.vhd\n");
        return 0;
    }
    testasset::report({rom, img});
    std::ifstream in(rom, std::ios::binary);
    const std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                       std::istreambuf_iterator<char>());
    V8Memory mem(pom68k::defaultCoreConfig());
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    const jit::ResolvedConfig jitConfig = testjit::resolveFromEnvironment();
    Cpu030 cpu(mem, jitConfig, pom68k::defaultCoreConfig().cpu,
               /*withFpu=*/true);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk\n"); return 1; }
    ensureBootDriverType(mem.scsiDisk().image());
    while (mem.cpuHeld()) mem.tick(1000);
    gMem = &mem;
    gCpu = &cpu;

    runFrames(16000);
    for (int poll = 0; poll < 20 && !finderUp(); poll++) {
        keyHold(0x24, 150);
        runFrames(600);
    }
    if (!finderUp() || cpu.isHalted()) {
        std::fprintf(stderr, "FAIL: no Finder after the first boot\n");
        return 1;
    }
    const long scsiFirst = mem.scsi().commands;
    std::printf("boot: Finder up, SCSI %ld, front app '%s'\n", scsiFirst,
                frontApplication().c_str());
    dump("lcii_restart_boot.ppm");

    auto pointer = [&](int& x, int& y) {
        x = int16_t(uint16_t(mem.peek8(0x832)) << 8 | mem.peek8(0x833));
        y = int16_t(uint16_t(mem.peek8(0x830)) << 8 | mem.peek8(0x831));
    };
    auto steer = [&](int tx, int ty) {
        int px = 0, py = 0;
        for (int it = 0; it < 800; it++) {
            pointer(px, py);
            const int dx = tx - px, dy = ty - py;
            if (!dx && !dy) break;
            auto step = [](int d) {
                int s = d / 2;
                if (!s) s = d > 0 ? 1 : (d < 0 ? -1 : 0);
                return std::max(-8, std::min(8, s));
            };
            mem.mouseMove(step(dx), step(dy));
            runFrames(1);
        }
        pointer(px, py);
        if (std::abs(px - tx) > 2 || std::abs(py - ty) > 2)
            std::fprintf(stderr, "steer: wanted (%d,%d), reached (%d,%d)\n",
                         tx, ty, px, py);
    };

    // The Finder's own menu bar (read off lcii_simcity_gate_*_boot.ppm):
    // Spécial sits at x 316-360. Press on it, drag down the open menu to
    // the item, release: one gesture, the way a person does it.
    steer(338, 8);
    mem.mouseButton(true);
    runFrames(60);
    dump("lcii_restart_menu.ppm");
    if (std::getenv("POM68K_RESTART_DISCOVER")) {
        std::printf("discover: Spécial menu held open — read the item rows "
                    "off lcii_restart_menu.ppm\n");
        mem.mouseButton(false);
        return 0;
    }
    // Rows read off the discovery dump (scratchpad/2026-09-07/restart/
    // lcii_restart_menu.png), not assumed: Ranger la fenêtre 27, Vider la
    // Corbeille 43, Éjecter 73, Initialiser le disque 91, Redémarrer 121,
    // Éteindre 139; the open menu spans x 310-470.
    constexpr int kRestartY = 121;
    steer(360, kRestartY);
    runFrames(30);
    dump("lcii_restart_item.ppm");
    mem.mouseButton(false);
    std::printf("restart: released on Redémarrer at (360,%d)\n", kRestartY);

    // The restart path: shutdown procedures, volume flush, ROM warm start,
    // then the whole boot again. Watch the screen leave the Finder (the
    // menu bar goes away) and come back.
    bool left = false;
    long frames = 0;
    for (; frames < 6000 && !left; frames += 30) {
        runFrames(30);
        left = !finderUp();
    }
    std::printf("restart: Finder %s after %ld frames (SCSI %ld)\n",
                left ? "gone" : "STILL UP", frames, mem.scsi().commands);
    dump("lcii_restart_leaving.ppm");
    if (!left) {
        std::fprintf(stderr, "FAIL: the Finder never went away — the restart "
                     "did not begin\n");
        return 1;
    }
    const long scsiRestart = mem.scsi().commands;
    runFrames(16000);
    for (int poll = 0; poll < 20 && !finderUp(); poll++) {
        keyHold(0x24, 150);
        runFrames(600);
    }
    const bool back = finderUp() && !cpu.isHalted();
    const long scsiSecond = mem.scsi().commands - scsiRestart;
    dump("lcii_restart_reboot.ppm");
    std::printf("reboot: Finder %s, halted=%d, SCSI +%ld over the second boot "
                "(first boot %ld), front app '%s', fp=%016llx screen=%016llx\n",
                back ? "up" : "NOT UP", cpu.isHalted(), scsiSecond, scsiFirst,
                frontApplication().c_str(),
                (unsigned long long)bench::fingerprint(cpu),
                (unsigned long long)screenFingerprint());
    // A warm restart reads the volume again from the driver descriptor
    // up: a second boot that issued fewer than half the first boot's SCSI
    // commands did not boot, whatever the screen shows.
    const bool ok = back && scsiSecond > scsiFirst / 2 &&
                    frontApplication() == "Finder";
    std::printf("%s — LC II guest restart etalon\n", ok ? "PASSED" : "FAILED");
    return ok ? 0 : 1;
}
