// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Save-state RELAUNCH gate on the LC II (68030 / V8 / Egret), the sibling of
// q605_savestate_relaunch_etalon: the cross-instance half that the in-place
// lcii_savestate_etalon cannot prove. Boot machine A to the Finder, write
// the snapshot to a FILE, build a FRESH V8Memory + Cpu030 that re-attaches
// the same host disk, load the file, run the scenario, and require a live
// Finder — plus two fresh relaunches of one snapshot byte-identical to each
// other (cross-instance determinism). This exercises the 030 device chunks
// (V8 video, the Egret MCU, the SWIM1 IWM/ISM, the 53C80) through the
// fresh-graph load path a GUI restore actually takes. Soft-skips without the
// LC II ROM + a bootable image.

#include "PortableEnv.h"
#include "AssetFingerprint.h"
#include "Cpu030.h"
#include "V8Memory.h"
#include "V8Video.h"
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

void ensureBootDriverType(std::vector<uint8_t>& img) {
    if (img.size() < 512 || img[0] != 'E' || img[1] != 'R') return;
    int count = (img[0x10] << 8) | img[0x11];
    for (int i = 0; i < count && 0x12 + i * 8 + 8 <= 512; i++) {
        int e = 0x12 + i * 8;
        if (((img[e + 6] << 8) | img[e + 7]) == 0x6A) return;
    }
    if (count >= 1 && 0x12 + count * 8 + 8 <= 512) {
        int src = 0x12, dst = 0x12 + count * 8;
        for (int k = 0; k < 8; k++) img[dst + k] = img[src + k];
        img[dst + 6] = 0x00; img[dst + 7] = 0x6A;
        img[0x10] = uint8_t((count + 1) >> 8);
        img[0x11] = uint8_t(count + 1);
    }
}

constexpr int64_t kFrame = 640 * 407;         // 60.15 Hz @ 15.6672 MHz
constexpr long kScenarioFrames = 1200;

void runScenario(V8Memory& mem, Cpu030& cpu, long frames) {
    for (long f = 0; f < frames && !cpu.isHalted(); f++) {
        if (f % 3 == 0) mem.mouseMove((f / 3) % 7 - 3, (f / 5) % 5 - 2);
        if (f == frames / 2)      mem.mouseButton(true);
        if (f == frames / 2 + 12) mem.mouseButton(false);
        cpu.runCycles(kFrame);
    }
}

bool finderUp(V8Memory& mem) {
    V8Video video(mem);
    std::vector<uint32_t> fb;
    video.decode(fb);
    const int W = 512;
    auto blackRatio = [&](int x0, int x1, int y0, int y1) {
        long black = 0;
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++)
                if ((fb[y * W + x] & 0xFF) < 0x80) black++;
        return double(black) / (double(x1 - x0) * (y1 - y0));
    };
    return blackRatio(0, W, 2, 16) < 0.30 &&
           blackRatio(400, W, 40, 340) > 0.35 && blackRatio(400, W, 40, 340) < 0.65;
}

bool boot(V8Memory& mem, Cpu030& cpu) {
    while (mem.cpuHeld()) mem.tick(1000);
    for (long f = 0; f < 16000 && !cpu.isHalted(); f++) cpu.runCycles(kFrame);
    return !cpu.isHalted() && finderUp(mem) && mem.scsi().commands > 50;
}
} // namespace

int main() {
    std::string rom = find("roms/512KB ROMs/1992-03 - 35C28F5F - Mac LC II.ROM");
    std::string img = find("hdv/lcii-boot.vhd");
    if (img.empty()) img = find("hdv/boot.vhd");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs the 512 KB LC II ROM + a bootable hdv/ image\n");
        return 0;
    }
    testasset::report({ rom, img });
    std::ifstream in(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
    if (romData.size() != V8Memory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM is %zu bytes, want 512 KB\n", romData.size());
        return 1;
    }
    using Blob = std::vector<uint8_t>;
    const auto kKind = pom68k::SnapMachine::LcII;
    const jit::ResolvedConfig jitCfg = testjit::resolveFromEnvironment();

    // ── Machine A: boot, snapshot to a file ──────────────────────────────
    V8Memory memA(pom68k::defaultCoreConfig());
    if (!memA.loadRom(romData)) { std::fprintf(stderr, "FAIL: A bad ROM\n"); return 1; }
    Cpu030 cpuA(memA, jitCfg, pom68k::defaultCoreConfig().cpu, true);
    memA.setCpu(&cpuA);
    cpuA.hardReset();
    if (!memA.attachScsi(img)) { std::fprintf(stderr, "FAIL: A bad disk\n"); return 1; }
    ensureBootDriverType(memA.scsiDisk().image());
    if (!boot(memA, cpuA)) { std::fprintf(stderr, "FAIL: A no Finder to snapshot\n"); return 1; }
    Blob start;
    pom68k::save(memA, cpuA, kKind, start);
    std::printf("A boot: SCSI %ld, snapshot %zu bytes, %zu dirty block(s)\n",
                memA.scsi().commands, start.size(), memA.scsiDisk().dirtyBlocks());
    if (start.size() < 64) { std::fprintf(stderr, "FAIL: empty snapshot\n"); return 1; }

    const std::string snapPath = pom68kTempPath("lcii_relaunch.pom68k");
    { std::ofstream out(snapPath, std::ios::binary);
      out.write(reinterpret_cast<const char*>(start.data()), std::streamsize(start.size()));
      if (!out) { std::fprintf(stderr, "FAIL: cannot write %s\n", snapPath.c_str()); return 1; } }

    runScenario(memA, cpuA, kScenarioFrames);
    if (cpuA.isHalted()) { std::fprintf(stderr, "FAIL: A halted in the scenario\n"); return 1; }
    Blob direct;
    pom68k::save(memA, cpuA, kKind, direct);

    // ── Read the file back; two fresh machines must load it identically ──
    Blob loaded;
    { std::ifstream f(snapPath, std::ios::binary);
      loaded.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()); }
    std::remove(snapPath.c_str());
    if (loaded != start) { std::fprintf(stderr, "FAIL: the file round-trip changed the snapshot\n"); return 1; }

    auto freshRelaunch = [&](Blob& out) -> bool {
        V8Memory mem(pom68k::defaultCoreConfig());
        if (!mem.loadRom(romData) || !mem.attachScsi(img)) return false;
        ensureBootDriverType(mem.scsiDisk().image());
        Cpu030 cpu(mem, jitCfg, pom68k::defaultCoreConfig().cpu, true);
        mem.setCpu(&cpu);
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
        runScenario(mem, cpu, kScenarioFrames);
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
    std::printf("PASSED — LC II save-state relaunch into a fresh machine\n");
    return 0;
}
