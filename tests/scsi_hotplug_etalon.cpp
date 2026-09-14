// POM68K — SCSI hot-plug contract on the Quadra 605 (docs/SCSI_HOTPLUG.md)
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Three facts the Disques window now states instead of guessing, each
// asserted on the guest's own tables (src/GuestScsiView.h):
//
//   1. After the boot, the drive queue holds a SCSI Manager driver for
//      SCSI 0 (refNum −33) and the VCB queue a volume on that drive — the
//      boot volume, by name.
//   2. A fixed disk attached to the bus AFTER the boot (the machine's
//      `attachScsi`, what Cmd::AttachDisk performs) answers the bus but
//      gets no driver and no volume: Classic Mac OS enumerates once.
//   3. A power cycle re-probes the bus and mounts it, by name.
//
// FF7439EE ROM + a bootable hdv/ image; the second disk is a blank HFS
// volume the gate builds (src/HfsBlankVolume.h). Soft-skips without them.

#include "AssetFingerprint.h"
#include "FinderSignature.h"
#include "GuestScsiView.h"
#include "HfsBlankVolume.h"
#include "JitTestConfig.h"
#include "Q605ApplicationHarness.h"
#include "PortableEnv.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace q605app;

namespace {

constexpr int kNewId = 2;
constexpr uint64_t kNewBytes = 20ull << 20;

pom68k::GuestScsiView view() {
    return pom68k::readGuestScsiView([](uint32_t a, uint8_t& out) {
        out = gMem->peek8(a);      // 040: low memory and heap read physically
        return true;
    });
}

void print(const char* when, const pom68k::GuestScsiView& v) {
    std::printf("[%s] valid=%d", when, v.valid ? 1 : 0);
    for (size_t id = 0; id < v.bays.size(); id++) {
        const pom68k::GuestScsiBay& b = v.bays[id];
        if (!b.driver) continue;
        std::printf("  SCSI%zu: drive %d%s%s%s", id, b.driveNum,
                    b.mounted ? " « " : "", b.mounted ? b.volume.c_str() : "",
                    b.mounted ? " »" : " (no volume)");
    }
    std::printf("\n");
}

} // namespace

int main() {
    const std::string rom = find(
        "roms/1MB ROMs/1993-10 - FF7439EE - LC475,575,Quadra 605,Performa 475,476,575,577,578.ROM");
    std::string img = find("hdv/MacOS-8.1-boot.vhd");
    if (img.empty()) img = find("hdv/boot.vhd");
    if (img.empty()) img = find("hdv/MacOS-7.6-boot.vhd");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs the FF7439EE ROM + a bootable hdv/ image\n");
        return 0;
    }
    testasset::report({rom, img});
    std::ifstream in(rom, std::ios::binary);
    const std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                       std::istreambuf_iterator<char>());
    if (romData.size() != Q605Memory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM is %zu bytes, want 1 MB\n", romData.size());
        return 1;
    }
    const std::string newPath = pom68kTempPath("scsi_hotplug_new.img");
    if (!hfsblank::writeFile(newPath, hfsblank::build(kNewBytes, "Branche"))) {
        std::fprintf(stderr, "FAIL: cannot write %s\n", newPath.c_str());
        return 1;
    }

    pom68k::CoreConfig core = pom68k::defaultCoreConfig();
    Q605Memory mem(core, 32u << 20);
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    if (!mem.attachScsi(img, false, 0)) {
        std::fprintf(stderr, "FAIL: could not attach %s\n", img.c_str());
        return 1;
    }
    const jit::ResolvedConfig jitConfig = testjit::resolveFromEnvironment();
    Cpu040 cpu(mem, jitConfig, pom68k::defaultCoreConfig().cpu,
               pom68k::defaultCoreConfig().diagnostics);
    mem.setCpu(&cpu);
    cpu.hardReset();
    gMem = &mem;
    gCpu = &cpu;

    // ── 1. the boot volume, from the guest's queues ───────────────────────
    if (!bootToFinder(14000)) {
        std::fprintf(stderr, "FAIL: no Finder (halted=%d)\n", cpu.isHalted());
        return 1;
    }
    pom68k::GuestScsiView v = view();
    print("boot", v);
    if (!v.valid || !v.bays[0].driver || !v.bays[0].mounted || v.bays[0].volume.empty()) {
        std::fprintf(stderr, "FAIL: SCSI 0 not reported as mounted by the guest\n");
        return 1;
    }
    const std::string bootName = v.bays[0].volume;
    if (v.bays[kNewId].driver) {
        std::fprintf(stderr, "FAIL: SCSI %d has a driver before anything is attached\n", kNewId);
        return 1;
    }

    // ── 2. a fixed disk joins the bus live: answers, but stays unknown ────
    if (!mem.attachScsi(newPath, false, kNewId)) {
        std::fprintf(stderr, "FAIL: live attach refused\n");
        return 1;
    }
    std::remove(newPath.c_str());
    runFrames(900);                       // 15 s of guest time
    v = view();
    print("attached", v);
    if (!v.valid || v.bays[kNewId].driver || v.bays[kNewId].mounted) {
        std::fprintf(stderr, "FAIL: the System noticed a fixed disk attached after boot — "
                             "the contract docs/SCSI_HOTPLUG.md § 1 states changed\n");
        return 1;
    }
    if (v.bays[0].volume != bootName) {
        std::fprintf(stderr, "FAIL: boot volume changed name across the attach\n");
        return 1;
    }

    // ── 3. power cycle: the ROM re-probes and the new volume mounts ───────
    // The second Finder is recognised by the guest's own word (CurApName),
    // not by the desktop's pixels: a second disk icon on the desktop is the
    // very thing this step produces, and the luminance signature of
    // bootToFinder() was calibrated on a one-disk desktop.
    cpu.hardReset();
    while (mem.cpuHeld()) mem.tick(1000);   // the Cuda holds the CPU through its reset
    bool finder = false;
    for (int frame = 0; frame < 16000 && !cpu.isHalted() && !finder; frame++) {
        runFrames(1);
        if (frame >= 2400 && !(frame % 60))
            finder = findersig::curApName(mem) == "Finder";
    }
    if (!finder) {
        print("no-finder", view());
        dump("scsi_hotplug_reboot.ppm");
        std::fprintf(stderr, "FAIL: no Finder after the power cycle (halted=%d, front '%s')\n",
                     cpu.isHalted(), findersig::curApName(mem).c_str());
        return 1;
    }
    runFrames(600);                       // let the Finder mount every volume
    v = view();
    print("rebooted", v);
    if (!v.valid || !v.bays[kNewId].mounted || v.bays[kNewId].volume != "Branche") {
        std::fprintf(stderr, "FAIL: SCSI %d not mounted as « Branche » after the power cycle\n",
                     kNewId);
        return 1;
    }
    if (v.bays[0].volume != bootName) {
        std::fprintf(stderr, "FAIL: boot volume not back after the power cycle\n");
        return 1;
    }
    std::printf("PASS: boot « %s » on SCSI 0; live attach on SCSI %d invisible until the "
                "power cycle, then mounted « Branche »\n", bootName.c_str(), kNewId);
    return 0;
}
