// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Installation from CD-ROM, driven by the guest (TODO § D.3) ──
//
// The machine has a BLANK hard disk (HfsBlankVolume.h — formatted, empty,
// no boot blocks) and the Mac OS 8.1 retail CD. The ROM's 6→0 scan finds
// nothing bootable on the disk and boots the CD; the gate drives the CD's
// own installer from the Finder to the target volume, lets it copy the
// System, and asserts the guest wrote a BOOTABLE system disk:
//
//   boot      the machine came up from the disc (CD served a System's worth
//             of blocks, the target none)
//   install   the installer reached "The installation process has finished"
//             with the copy process gone
//   artefact  the host-owned target image now carries HFS boot blocks ('LK'
//             at the volume's LBA 0), a System and a Finder in its catalog,
//             and megabytes written — a disk that will boot
//   quit      Cmd-Q (the finish dialog's default) hands the front to the
//             Finder
//
// Observables are the guest's: CurApName for the installer process, the
// catalog of the host-owned target image for the System/Finder files, the
// per-target SCSI counters for who served the boot.
//
//   restart   Finder → Special → Restart, and the machine reboots FROM THE
//             INSTALLED DISK: the target serves the second boot, the disc
//             does not (fixed 2026-09-08 — the warm reset used to double-
//             fault the 040 to a halt; Q605Memory::consumeRestart now arms
//             the ROM overlay for the reset-vector fetch, q605_restart_etalon)
//
// POM68K_CDINSTALL_DISCOVER=N dumps each 600-frame step of the copy;
// POM68K_CDINSTALL_TRACE keeps the last 53C96 register accesses (the pair
// that read the Drive Setup polled-write stall, 2026-09-08).
// POM68K_DUMP=1 writes q605_cdinstall_*.ppm per phase.

#include "PortableEnv.h"
#include "HfsBlankVolume.h"
#include "Q605ApplicationHarness.h"

using namespace q605app;

namespace {

constexpr int kTargetId = 6;                    // wins the scan once bootable
constexpr int kCdId = 3;
constexpr uint64_t kTargetBytes = 250ull << 20;

bool menuUp() { return menuBarUp(decodeScreen()); }

// The CD's System and a freshly installed one draw desktops unlike the
// hard-disk image's, so the Finder check here is the menu bar plus quiet:
// the screen unchanged across three one-second samples, after the disk at
// `servedBy` has served a System's worth of blocks since `served0`.
bool bootToQuietFinder(Q605Memory& mem, int maxFrames, int servedBy, long served0) {
    while (mem.cpuHeld()) mem.tick(1000);
    uint64_t last = 0;
    int quiet = 0;
    for (int frame = 0; frame < maxFrames && !gCpu->isHalted(); frame++) {
        gCpu->runCycles(kFrameCycles);
        if (frame < 3000 || frame % 60) continue;
        if (mem.scsiDiskAt(servedBy).readBlocks - served0 < 1000 || !menuUp()) {
            quiet = 0;
            continue;
        }
        const uint64_t fp = screenFingerprint();
        quiet = fp == last ? quiet + 1 : 0;
        last = fp;
        if (quiet >= 3) { runFrames(300); return !gCpu->isHalted(); }
    }
    return false;
}

}  // namespace

int main() {
    const std::string rom = find(
        "roms/1MB ROMs/1993-10 - FF7439EE - LC475,575,Quadra 605,Performa 475,476,575,577,578.ROM");
    const std::string cd = testasset::findAny({ "cd/MAC_OS_8-1_RETAIL_0.ISO", "cd/macos81.iso",
                                                "input/MAC_OS_8-1_RETAIL_0.ISO",
                                                "hdv/MAC_OS_8-1_RETAIL_0.ISO" });
    if (rom.empty() || cd.empty()) {
        std::printf("SKIP: needs the FF7439EE ROM + cd/MAC_OS_8-1_RETAIL_0.ISO\n");
        return 0;
    }
    testasset::report({rom, cd});
    std::ifstream in(rom, std::ios::binary);
    const std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                       std::istreambuf_iterator<char>());
    if (romData.size() != Q605Memory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM is %zu bytes, want 1 MB\n", romData.size());
        return 1;
    }

    // The blank target, built by the host and handed over as a file; the
    // image stays in memory (no write-back) — the artefact is inspected there.
    const std::string targetPath = pom68kTempPath("q605_cdinstall_target.img");
    hfsblank::Layout layout;
    if (!hfsblank::writeFile(targetPath, hfsblank::build(kTargetBytes, "Cible", &layout))) {
        std::fprintf(stderr, "FAIL: cannot write %s\n", targetPath.c_str());
        return 1;
    }
    std::printf("target: %s, %u sectors, %u-byte blocks, %u blocks\n", targetPath.c_str(),
                layout.sectors, layout.allocBlockSize, layout.allocBlocks);

    // POM68K_CDINSTALL_TRACE=1 prints every SCSI CDB (ScsiDisk's own trace),
    // the way the Drive Setup stall of 2026-09-08 was read.
    pom68k::CoreConfig core = pom68k::defaultCoreConfig();
    if (std::getenv("POM68K_CDINSTALL_TRACE")) core.storage.scsiTrace = true;
    Q605Memory mem(core, 32u << 20);
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    if (!mem.attachScsi(targetPath, false, kTargetId)) {
        std::fprintf(stderr, "FAIL: could not attach the target\n");
        return 1;
    }
    if (!mem.attachCdrom(cd, kCdId)) { std::fprintf(stderr, "FAIL: could not attach the CD\n"); return 1; }
    std::remove(targetPath.c_str());
    const jit::ResolvedConfig jitConfig = testjit::resolveFromEnvironment();
    Cpu040 cpu(mem, jitConfig, pom68k::defaultCoreConfig().cpu,
               pom68k::defaultCoreConfig().diagnostics);
    mem.setCpu(&cpu);
    cpu.hardReset();
    gMem = &mem;
    gCpu = &cpu;
    gAzertyGuest = false;                       // the CD's System: US layout

    // ── boot from the CD ─────────────────────────────────────────────────
    if (!bootToQuietFinder(mem, 14000, kCdId, 0)) {
        std::fprintf(stderr, "FAIL: no Finder from the CD (halted=%d)\n", cpu.isHalted());
        return 1;
    }
    dump("q605_cdinstall_boot.ppm");
    std::printf("boot: CD served %ld blocks, target served %ld blocks (%ld written), processes %s\n",
                mem.scsiDiskAt(kCdId).readBlocks, mem.scsiDiskAt(kTargetId).readBlocks,
                mem.scsiDiskAt(kTargetId).writeBlocks, describe(runningProcesses()).c_str());

    // ── launch the installer from the CD's window ────────────────────────
    closeAllFinderWindows();
    dump("q605_cdinstall_desktop.ppm");
    if (std::getenv("POM68K_CDINSTALL_MENU")) {
        // Route discovery: pull the Special menu down and dump it held open,
        // to read the Restart row off the guest's own screen.
        steer(181, 9);
        mem.mouseButton(true);
        runFrames(30);
        dump("q605_cdinstall_special.ppm");
        steer(181, 300);                        // off the menu: release cancels
        runFrames(10);
        mem.mouseButton(false);
        runFrames(60);
        return 1;
    }
    openBySelect("mac os", 600);                // the disc "Mac OS 8.1"
    dump("q605_cdinstall_cdroot.ppm");
    openBySelect("install mac", 1800);          // "Install Mac OS 8.1" alias → Mac OS Install
    dump("q605_cdinstall_installer.ppm");
    std::printf("installer: processes %s\n", describe(runningProcesses()).c_str());

    // ── the four steps (discovery dumps of 2026-09-08) ───────────────────
    keyHold(0x24, 30); runFrames(900);          // Welcome → Continue
    dump("q605_cdinstall_destination.ppm");     // Select Destination: "Cible"
    keyHold(0x24, 30); runFrames(900);          // → Select
    dump("q605_cdinstall_readme.ppm");
    keyHold(0x24, 30); runFrames(900);          // Important Information → Continue
    dump("q605_cdinstall_license.ppm");
    keyHold(0x24, 30); runFrames(900);          // License → Continue → Agree/Disagree
    dump("q605_cdinstall_agree.ppm");
    click(450, 306, 900);                       // Agree (no default button)
    dump("q605_cdinstall_install.ppm");
    std::printf("install pane: processes %s\n", describe(runningProcesses()).c_str());

    // ── Start, then the copy: wait for the installer's last dialog ───────
    // The copy runs in the "Installer" process for the better part of an
    // emulated quarter hour; "The installation process has finished" is the
    // first screen that stays still for three seconds with the copy process
    // gone. Under POM68K_CDINSTALL_DISCOVER every step is dumped; under
    // POM68K_CDINSTALL_TRACE the last 53C96 register accesses are kept for
    // reading a stall at the chip's own level (the Drive Setup finding).
    const bool discover = std::getenv("POM68K_CDINSTALL_DISCOVER") != nullptr;
    struct Access { long frame; int reg; bool write; uint8_t value; long repeats; };
    std::vector<Access> ring;
    long frameNow = 0;
    if (std::getenv("POM68K_CDINSTALL_TRACE"))
        mem.scsi().onAccess = [&](int reg, bool write, uint8_t value) {
            if (!ring.empty() && ring.back().reg == reg && ring.back().write == write &&
                ring.back().value == value) { ring.back().repeats++; return; }
            if (ring.size() >= 400) ring.erase(ring.begin());
            ring.push_back({frameNow, reg, write, value, 1});
        };
    const long written0 = mem.scsiDiskAt(kTargetId).writeBlocks;
    keyHold(0x24, 30);                          // Start
    uint64_t last = 0;
    int still = 0, step = 0;
    bool finished = false;
    for (step = 1; step <= 200 && !cpu.isHalted(); step++) {
        for (int f = 0; f < 600; f++) { runFrames(1); frameNow++; }
        const uint64_t fp = screenFingerprint();
        still = fp == last ? still + 1 : 0;
        last = fp;
        const auto procs = runningProcesses();
        if (discover) {
            char name[64];
            std::snprintf(name, sizeof name, "q605_cdinstall_step%03d.ppm", step);
            dump(name);
            std::printf("step %d: processes %s, target +%ld written, CD %ld read, %s\n", step,
                        describe(procs).c_str(),
                        mem.scsiDiskAt(kTargetId).writeBlocks - written0,
                        mem.scsiDiskAt(kCdId).readBlocks, still >= 3 ? "still" : "moving");
            std::fflush(stdout);
        }
        if (still >= 3 && !processRuns(procs, "Installer") &&
            !processRuns(procs, "Drive Setup")) { finished = true; break; }
    }
    if (std::getenv("POM68K_CDINSTALL_TRACE")) {
        static const char* const kRegs[16] = { "TCLO", "TCMID", "FIFO", "CMD", "STAT/BUSID",
            "ISTAT/TMO", "SEQ/SYNP", "FLAGS/SYNO", "CFG1", "CLK", "TEST", "CFG2", "CFG3",
            "R13", "R14", "TCHI" };
        for (const Access& a : ring)
            std::fprintf(stderr, "[53c96 f%ld] %s %s %02X%s\n", a.frame,
                         a.write ? "W" : "R", kRegs[a.reg & 15], a.value,
                         a.repeats > 1 ? (" x" + std::to_string(a.repeats)).c_str() : "");
        mem.scsi().onAccess = nullptr;
    }
    const long written = mem.scsiDiskAt(kTargetId).writeBlocks - written0;
    dump("q605_cdinstall_finished.ppm");
    std::printf("copy: %s after %d steps, target +%ld blocks (%ld KB), CD %ld blocks read, "
                "processes %s\n", finished ? "finished" : "NOT finished", step, written,
                written / 2, mem.scsiDiskAt(kCdId).readBlocks,
                describe(runningProcesses()).c_str());
    if (!finished || cpu.isHalted()) {
        std::fprintf(stderr, "FAIL: the installer did not reach its last dialog\n");
        return 1;
    }

    // ── the artefact: a System on the target, with boot blocks ───────────
    // The façade puts the HFS volume at LBA 96; the installer writes the
    // 'LK' boot blocks there and a System and a Finder into the catalog.
    const std::vector<uint8_t>& target = mem.scsiDiskAt(kTargetId).image();
    const size_t hfs = 96 * 512;
    const bool bootBlocks = target.size() > hfs + 2 && target[hfs] == 'L' && target[hfs + 1] == 'K';
    const long systemNames = catalogCount(target, "System");
    const long finderNames = catalogCount(target, "Finder");
    std::printf("artefact: boot blocks %s, catalog 'System' x%ld 'Finder' x%ld, %ld KB written\n",
                bootBlocks ? "'LK'" : "ABSENT", systemNames, finderNames, written / 2);
    if (!bootBlocks || systemNames == 0 || finderNames == 0 || written < 100000) {
        std::fprintf(stderr, "FAIL: the target does not carry an installed System\n");
        return 1;
    }

    // ── Quit the installer to the Finder (the finish dialog's default) ───
    keyHold(0x24, 3);
    runFrames(600);
    const auto afterQuit = runningProcesses();
    dump("q605_cdinstall_quit.ppm");
    std::printf("quit: processes %s\n", describe(afterQuit).c_str());
    const bool quit = !processRuns(afterQuit, "Mac OS Install") &&
                      processRuns(afterQuit, "Finder");
    if (!quit)
        std::fprintf(stderr, "FAIL: the installer did not quit to the Finder\n");

    if (!quit) {
        std::fprintf(stderr, "FAIL: the install did not finish and quit to the "
                     "Finder\n");
        return 1;
    }
    std::printf("installed: a bootable Mac OS 8.1 is on the blank disk\n");

    // ── restart, and require the second boot to come from the target ──────
    // Special menu rows off the guest's own dump (POM68K_CDINSTALL_MENU,
    // scratchpad/2026-09-08/cdinstall/menu): Restart at y 87.
    steer(181, 9);
    mem.mouseButton(true);
    runFrames(30);
    steer(190, 87);
    runFrames(30);
    dump("q605_cdinstall_restart_item.ppm");
    mem.mouseButton(false);
    const long cdReads1 = mem.scsiDiskAt(kCdId).readBlocks;
    const long targetReads1 = mem.scsiDiskAt(kTargetId).readBlocks;
    bool left = false;
    for (long f = 0; f < 6000 && !left && !cpu.isHalted(); f += 30) {
        runFrames(30);
        left = !menuUp();
    }
    std::printf("restart: menu bar %s (halted=%d)\n", left ? "gone" : "STILL UP",
                cpu.isHalted());
    if (cpu.isHalted() || !left) {
        std::fprintf(stderr, "FAIL: the guest restart did not begin\n");
        return 1;
    }
    bool back = bootToQuietFinder(mem, 24000, kTargetId, targetReads1);
    for (int poll = 0; poll < 12 && !back; poll++) {
        keyHold(0x24, 30);
        back = bootToQuietFinder(mem, 3000, kTargetId, targetReads1);
    }
    const long cdReads2 = mem.scsiDiskAt(kCdId).readBlocks - cdReads1;
    const long targetReads2 = mem.scsiDiskAt(kTargetId).readBlocks - targetReads1;
    dump("q605_cdinstall_reboot.ppm");
    // A boot off the disc costs it thousands of blocks; a System that boots
    // from the target leaves the disc a data volume (dozens of blocks).
    const bool fromTarget = back && !cpu.isHalted() &&
                            targetReads2 > 2000 && targetReads2 > 5 * cdReads2;
    std::printf("reboot: Finder %s, target served %ld blocks, CD %ld — %s\n",
                back ? "up" : "NOT UP", targetReads2, cdReads2,
                fromTarget ? "BOOTED FROM THE INSTALLED DISK"
                           : "did NOT boot from the target");
    std::printf("%s — Quadra 605 installs Mac OS 8.1 from CD and boots the target\n",
                fromTarget ? "PASSED" : "FAILED");
    return fromTarget ? 0 : 1;
}

