// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Save-state RELAUNCH gate on the Macintosh Plus (68000 / MacMemory), the
// third-family sibling of q605_savestate_relaunch_etalon (040) and
// lcii_savestate_relaunch_etalon (030). Boot machine A to the Finder, write
// the snapshot to a FILE, build a FRESH MacMemory + Cpu68k that re-attaches
// the same host disk, load the file, run the scenario, require a live Finder
// — plus two fresh relaunches of one snapshot byte-identical to each other.
// This exercises the compact device set (MacVideo, the M0110 keyboard, the
// quadrature MacMouse, the NCR 5380, the IWM/SonyDrive, the VIA) through the
// fresh-graph load path a GUI restore takes. Soft-skips without the Plus ROM
// + a bootable System 6 image.

#include "PortableEnv.h"
#include "AssetFingerprint.h"
#include "BeyondBoot.h"
#include "Cpu68k.h"
#include "MacFrame.h"
#include "MacMemory.h"
#include "MacVideo.h"
#include "SaveState.h"
#include "SaveStateMachines.h"
#include "JitTestConfig.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {
std::string find(const char* rel) { return testasset::find(rel); }

constexpr long kScenarioFrames = 1200;

bool finderUp(MacMemory& mem) {
    MacVideo video;
    const uint32_t* fb = video.render(mem);
    auto blackRatio = [&](int y0, int y1) {
        long black = 0;
        for (int y = y0; y < y1; y++)
            for (int x = 0; x < 512; x++)
                if ((fb[size_t(y) * 512 + x] & 0xFF) < 0x80) black++;
        return double(black) / (512.0 * (y1 - y0));
    };
    const double menu = blackRatio(2, 16);
    const double desk = blackRatio(120, 240);
    return menu < 0.30 && desk > 0.40 && desk < 0.60;
}

void runScenario(MacMemory& mem, Cpu68k& cpu, MacFrameClock& fc, long frames) {
    for (long f = 0; f < frames && !cpu.isHalted(); f++) {
        if (f % 3 == 0) mem.mouseMove((f / 3) % 7 - 3, (f / 5) % 5 - 2);
        if (f == frames / 2)      mem.mouseButton(true);
        if (f == frames / 2 + 12) mem.mouseButton(false);
        fc.runFrame(cpu, mem);
    }
}

bool boot(MacMemory& mem, Cpu68k& cpu, MacFrameClock& fc) {
    for (long f = 0; f < 5400 && !cpu.isHalted(); f++) fc.runFrame(cpu, mem);
    for (int poll = 0; poll < 18 && !finderUp(mem) && !cpu.isHalted(); poll++) {
        mem.keyboard().enqueue(uint8_t((0x24 << 1) | 1));       // Return down
        for (int f = 0; f < 30; f++) fc.runFrame(cpu, mem);
        mem.keyboard().enqueue(uint8_t(((0x24 << 1) | 1) | 0x80)); // Return up
        for (long f = 0; f < 570; f++) fc.runFrame(cpu, mem);
    }
    return !cpu.isHalted() && finderUp(mem) && mem.scsi().commands > 50;
}
} // namespace

int main() {
    const std::string rom = find("roms/macplus.rom");
    std::string img = find("hdv/System 6.0.8 HD.dsk");
    if (img.empty()) img = find("hdv/System 7.1 HD.dsk");
    if (img.empty()) img = find("hdv/boot.vhd");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs roms/macplus.rom + a bootable System 6 hdv/ image\n");
        return 0;
    }
    testasset::report({ rom, img });
    std::ifstream in(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
    using Blob = std::vector<uint8_t>;
    const auto kKind = pom68k::SnapMachine::Plus;
    const jit::ResolvedConfig jitCfg = testjit::resolveFromEnvironment();

    // ── Machine A: boot, snapshot to a file ──────────────────────────────
    MacMemory memA(pom68k::defaultCoreConfig());
    if (!memA.loadRom(romData)) { std::fprintf(stderr, "FAIL: A bad ROM\n"); return 1; }
    Cpu68k cpuA(memA, jitCfg);
    memA.setCpu(&cpuA);
    cpuA.hardReset();
    if (!memA.attachScsi(img)) { std::fprintf(stderr, "FAIL: A bad disk\n"); return 1; }
    beyondboot::ensureBootDriverType(memA.scsiDisk().image());
    MacFrameClock fcA;
    fcA.resync(cpuA);
    if (!boot(memA, cpuA, fcA)) { std::fprintf(stderr, "FAIL: A no Finder to snapshot\n"); return 1; }
    Blob start;
    pom68k::save(memA, cpuA, kKind, start);
    std::printf("A boot: SCSI %ld, snapshot %zu bytes, %zu dirty block(s)\n",
                memA.scsi().commands, start.size(), memA.scsiDisk().dirtyBlocks());
    if (start.size() < 64) { std::fprintf(stderr, "FAIL: empty snapshot\n"); return 1; }

    const std::string snapPath = pom68kTempPath("compact_relaunch.pom68k");
    { std::ofstream out(snapPath, std::ios::binary);
      out.write(reinterpret_cast<const char*>(start.data()), std::streamsize(start.size()));
      if (!out) { std::fprintf(stderr, "FAIL: cannot write %s\n", snapPath.c_str()); return 1; } }

    runScenario(memA, cpuA, fcA, kScenarioFrames);
    if (cpuA.isHalted()) { std::fprintf(stderr, "FAIL: A halted in the scenario\n"); return 1; }
    Blob direct;
    pom68k::save(memA, cpuA, kKind, direct);

    Blob loaded;
    { std::ifstream f(snapPath, std::ios::binary);
      loaded.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()); }
    std::remove(snapPath.c_str());
    if (loaded != start) { std::fprintf(stderr, "FAIL: the file round-trip changed the snapshot\n"); return 1; }

    auto freshRelaunch = [&](Blob& out) -> bool {
        MacMemory mem(pom68k::defaultCoreConfig());
        if (!mem.loadRom(romData) || !mem.attachScsi(img)) return false;
        beyondboot::ensureBootDriverType(mem.scsiDisk().image());
        Cpu68k cpu(mem, jitCfg);
        mem.setCpu(&cpu);
        MacFrameClock fc;
        fc.resync(cpu);
        std::string err;
        if (!pom68k::load(mem, cpu, kKind, loaded.data(), loaded.size(), err) || !err.empty()) {
            std::fprintf(stderr, "FAIL: fresh load refused: %s\n", err.c_str());
            return false;
        }
        Blob reSave;
        pom68k::save(mem, cpu, kKind, reSave);
        if (reSave != start) {
            size_t i = 0; const size_t n = std::min(reSave.size(), start.size());
            while (i < n && reSave[i] == start[i]) i++;
            std::fprintf(stderr, "FAIL: fresh load->save not byte-identical to A "
                         "(divergence at byte %zu of %zu vs %zu)\n", i, reSave.size(), start.size());
            return false;
        }
        runScenario(mem, cpu, fc, kScenarioFrames);
        if (cpu.isHalted()) { std::fprintf(stderr, "FAIL: fresh machine halted\n"); return false; }
        if (!finderUp(mem)) { std::fprintf(stderr, "FAIL: fresh machine Finder GONE\n"); return false; }
        pom68k::save(mem, cpu, kKind, out);
        return true;
    };

    Blob relaunchB, relaunchC;
    if (!freshRelaunch(relaunchB) || !freshRelaunch(relaunchC)) return 1;
    std::printf("B relaunch: load->save byte-identical to A, Finder up\n");
    if (relaunchB != relaunchC) {
        size_t i = 0; const size_t n = std::min(relaunchB.size(), relaunchC.size());
        while (i < n && relaunchB[i] == relaunchC[i]) i++;
        std::fprintf(stderr, "FAIL: two fresh relaunches diverged (byte %zu of %zu vs %zu)\n",
                     i, relaunchB.size(), relaunchC.size());
        return 1;
    }
    std::printf("two fresh relaunches: byte-identical to each other; A-direct hash "
                "%016llx vs fresh-load hash %016llx (%s)\n",
                (unsigned long long)sav::hash(direct),
                (unsigned long long)sav::hash(relaunchB),
                relaunchB == direct ? "identical" : "cp-class normalization, expected");
    std::printf("PASSED — Macintosh Plus save-state relaunch into a fresh machine\n");
    return 0;
}
