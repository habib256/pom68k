// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Finder → Special → Restart on the Quadra 605: the guest restarts and
//    the machine reboots from its disk (the Cuda counterpart of
//    lcii_restart_etalon, which is the Egret one) ──
//
// Every boot etalon starts from a hard reset the HOST performs. A restart is
// the guest doing it: the Finder's Restart runs the Shutdown Manager, which
// flushes the volume and pulls the machine's /RESET through the Cuda
// (firmware RESET_SYSTEM). The ROM warm-start path then boots the whole
// System again — a second, guest-triggered boot in one process.
//
// This gate was written the day that path was FOUND BROKEN. The Finder's
// Restart on the 8.1 CD/HD Finder blanked the screen and then HALTED the
// 68040: the Cuda pulled /RESET and onCpuReset armed the ROM overlay, but
// every ROM instruction the guest fetched before the reset boundary cleared
// the overlay again, so the CPU's reset-vector fetch read stale low RAM
// ($40810000) instead of the ROM's own vector and double-faulted to a halt
// (CHANGELOG 2026-09-08). The fix arms the overlay at the reset boundary in
// Q605Memory::consumeRestart (and V8Memory's, which had the same latent
// race); this gate is its reproducer.
//
// cuda_restart_test drives the firmware $11 synthetically and proves the
// mechanism; this proves the WHOLE path from the Finder's menu through the
// reboot, which is what the synthetic test could not see. Soft-skips
// without the FF7439EE ROM + hdv/MacOS-8.1-boot.vhd.

#include "PortableEnv.h"      // setenv() on MSVC (release Windows job)
#include "Q605ApplicationHarness.h"

using namespace q605app;

int main() {
    setenv("POM68K_CUDA_LLE", "1", 1);
    const std::string rom = find(
        "roms/1MB ROMs/1993-10 - FF7439EE - LC475,575,Quadra 605,Performa 475,476,575,577,578.ROM");
    const std::string img = find("hdv/MacOS-8.1-boot.vhd");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs the FF7439EE ROM + hdv/MacOS-8.1-boot.vhd\n");
        return 0;
    }
    testasset::report({rom, img});
    std::ifstream in(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
    if (romData.size() != Q605Memory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM is %zu bytes, want 1 MB\n", romData.size());
        return 1;
    }
    pom68k::CoreConfig core = pom68k::defaultCoreConfig();
    Q605Memory mem(core, 32u << 20);
    if (!mem.loadRom(romData) || !mem.attachScsi(img)) {
        std::fprintf(stderr, "FAIL: could not load ROM/disk\n");
        return 1;
    }
    Cpu040 cpu(mem, jit::defaultResolvedConfig(), core.cpu, core.diagnostics);
    mem.setCpu(&cpu);
    cpu.hardReset();
    gMem = &mem;
    gCpu = &cpu;

    if (!bootToFinder(12000)) {
        std::fprintf(stderr, "FAIL: no Finder after the first boot (halted=%d)\n",
                     cpu.isHalted());
        return 1;
    }
    const long firstBoot = mem.scsi().commands;
    const bool cudaLle = mem.cudaLleActive();
    std::printf("first boot: Finder up, SCSI %ld, Cuda %s\n", firstBoot,
                cudaLle ? "LLE" : "HLE");

    // Special → Restart, one mouse gesture. Menu rows off the guest's own
    // dump (POM68K_CDINSTALL_MENU / the desktop Special menu): Restart at
    // y 87 on the 8.1 Finder. The HD Finder's Special is at x ~181.
    steer(181, 9);
    mem.mouseButton(true);
    runFrames(30);
    steer(190, 87);
    runFrames(30);
    dump("q605_restart_item.ppm");
    mem.mouseButton(false);

    // The machine must LEAVE the Finder (the reset actually happened) and
    // then come back through a second, full SCSI boot — not halt.
    const long beforeReset = mem.scsi().commands;
    bool left = false;
    for (int i = 0; i < 3000 && !left && !cpu.isHalted(); i++) {
        runFrames(1);
        const std::string app = findersig::curApName(*gMem);
        if (!app.empty() && app != "Finder" && app != "MyEyes Extension") left = true;
    }
    std::printf("restart: machine %s the Finder after %ld frames (halted=%d)\n",
                left ? "left" : "did NOT leave", 0L, cpu.isHalted());
    if (cpu.isHalted()) {
        std::fprintf(stderr, "FAIL: the 040 HALTED on the restart (the overlay/"
                     "reset-vector double fault this gate exists for)\n");
        return 1;
    }
    if (!left) {
        std::fprintf(stderr, "FAIL: the restart never began (no reset)\n");
        return 1;
    }

    const bool rebooted = bootToFinder(16000);
    const long secondBoot = mem.scsi().commands - beforeReset;
    dump("q605_restart_reboot.ppm");
    std::printf("reboot: Finder %s, halted=%d, second boot served %ld SCSI commands "
                "(first %ld), front '%s'\n", rebooted ? "up" : "NOT UP",
                cpu.isHalted(), secondBoot, firstBoot, findersig::curApName(mem).c_str());
    // A warm restart re-reads the whole System off the disk: thousands of
    // SCSI commands, the same order of magnitude as the first boot.
    const bool ok = rebooted && !cpu.isHalted() && secondBoot > 2000;
    std::printf("%s — Quadra 605 guest restart etalon\n", ok ? "PASSED" : "FAILED");
    return ok ? 0 : 1;
}
