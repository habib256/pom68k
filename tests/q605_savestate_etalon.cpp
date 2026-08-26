// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Save-state gate under a REAL OS on the 040 side: boot Mac OS 8.1 to the
// 640×480×8 Finder on the Q605 machine, snapshot, run N frames of
// deterministic mouse activity, hash the machine; restore, run the SAME N
// frames, require the identical hash. The lcii_savestate_etalon pattern —
// this is what upgrades the 040 device chunks (DAFB mid-frame, AscIosb
// FIFOs, the Ncr53c96 session incl. its deferred-IRQ countdown, the Cuda
// LLE MCU mid-transaction) from unit-ROM-verified to real-OS-verified.
//
// The machine runs the default identity (LC 475, $A55A2221 — the tag the
// snapshot pins; the ROM and boot path are shared with the Quadra 605).
// Soft-skips without the FF7439EE ROM + hdv/MacOS-8.1-boot.vhd.
// Exit 0 = pass / soft-skip, 1 = fail.

#include "AssetFingerprint.h"
#include "Cpu040.h"
#include "Q605Memory.h"
#include "SaveState.h"
#include "SaveStateMachines.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {
std::string findAsset(std::initializer_list<const char*> names) {
    return testasset::findAny(names);
}

uint32_t peek32(const Q605Memory& mem, uint32_t addr) {
    return uint32_t(mem.peek8(addr)) << 24 |
           uint32_t(mem.peek8(addr + 1)) << 16 |
           uint32_t(mem.peek8(addr + 2)) << 8 |
           mem.peek8(addr + 3);
}

// q605_boot_etalon's screen decode + Finder signature, condensed: resolve
// the main GDevice PixMap, decode through the DAFB CLUT, and score the
// menu bar vs the desktop.
struct Screen {
    int width = 0, height = 0, depth = 0;
    uint32_t stride = 0, offset = 0;
    std::vector<uint32_t> pixels;
};

Screen decodeScreen(const Q605Memory& mem) {
    Screen s;
    uint32_t scrnBase = peek32(mem, 0x0824);
    uint32_t mainDevH = peek32(mem, 0x08A4);
    uint32_t mainDev = mainDevH ? peek32(mem, mainDevH) : 0;
    uint32_t pmapH = mainDev ? peek32(mem, mainDev + 0x16) : 0;
    uint32_t pmap = pmapH ? peek32(mem, pmapH) : 0;
    if (!pmap) return s;
    uint32_t pmBase = peek32(mem, pmap);
    uint32_t boundsA = peek32(mem, pmap + 0x06);
    uint32_t boundsB = peek32(mem, pmap + 0x0A);
    s.width  = int(boundsB & 0xFFFF) - int(boundsA & 0xFFFF);
    s.height = int(boundsB >> 16) - int(boundsA >> 16);
    s.depth  = mem.dafbDepth();
    s.stride = mem.dafbStride();
    s.offset = (pmBase ? pmBase : scrnBase) & (Q605Memory::kVramSize - 1);
    if (s.width <= 0 || s.width > 1600 || s.height <= 0 || s.height > 1200 ||
        (s.depth != 1 && s.depth != 2 && s.depth != 4 && s.depth != 8) ||
        s.stride < uint32_t((s.width * s.depth + 7) / 8) ||
        uint64_t(s.offset) + uint64_t(s.height) * s.stride > Q605Memory::kVramSize) {
        s = {};
        return s;
    }
    const uint8_t* vram = mem.vram();
    const uint8_t (*clut)[3] = mem.clut();
    s.pixels.resize(size_t(s.width) * s.height);
    for (int y = 0; y < s.height; y++) {
        uint32_t row = s.offset + uint32_t(y) * s.stride;
        for (int x = 0; x < s.width; x++) {
            uint8_t packed = vram[row + uint32_t(x * s.depth / 8)];
            uint8_t pen;
            if (s.depth == 1) pen = (packed >> (7 - (x & 7))) & 1;
            else if (s.depth == 2) pen = (packed >> (6 - 2 * (x & 3))) & 3;
            else if (s.depth == 4) pen = (x & 1) ? packed & 0x0F : packed >> 4;
            else pen = packed;
            const uint8_t* c = clut[pen];
            s.pixels[size_t(y) * s.width + x] =
                uint32_t(c[0]) << 16 | uint32_t(c[1]) << 8 | c[2];
        }
    }
    return s;
}

struct Stats { double mean = 0, deviation = 0; };
Stats luminanceStats(const Screen& s, int x0, int x1, int y0, int y1) {
    if (x1 > s.width) x1 = s.width;
    if (y1 > s.height) y1 = s.height;
    if (x0 >= x1 || y0 >= y1) return {};
    double sum = 0, sum2 = 0;
    long count = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            uint32_t p = s.pixels[size_t(y) * s.width + x];
            double lum = ((p >> 16) * 54 + ((p >> 8) & 0xFF) * 183 +
                          (p & 0xFF) * 19) / 256.0;
            sum += lum; sum2 += lum * lum; count++;
        }
    Stats r;
    if (count) {
        r.mean = sum / count;
        r.deviation = std::sqrt(sum2 / count - r.mean * r.mean);
    }
    return r;
}

bool finderSignature(const Screen& s) {
    if (s.width != 640 || s.height != 480 || s.depth != 8) return false;
    Stats m = luminanceStats(s, 0, s.width, 2, 16);
    Stats d = luminanceStats(s, 520, 630, 40, 430);
    return m.mean > 170 && m.mean < 235 &&
           m.deviation > 40 && m.deviation < 100 &&
           d.mean > 100 && d.mean < 190 &&
           d.deviation > 30 && d.deviation < 90 &&
           m.mean - d.mean > 35;
}

constexpr int kFrameCycles = 416667;          // 25 MHz / ~60 Hz

// N frames of deterministic desktop activity: the lcii_savestate_etalon
// scenario — a bounded mouse wiggle every third frame and one
// click-release mid-run, injected at frame boundaries so both the direct
// and the restored run see identical machine times.
void runScenario(Q605Memory& mem, Cpu040& cpu, long frames) {
    for (long f = 0; f < frames && !cpu.isHalted(); f++) {
        if (f % 3 == 0)
            mem.mouseMove((f / 3) % 7 - 3, (f / 5) % 5 - 2);
        if (f == frames / 2)      mem.mouseButton(true);
        if (f == frames / 2 + 12) mem.mouseButton(false);
        cpu.runCycles(kFrameCycles);
    }
}
} // namespace

int main() {
    std::string romPath = findAsset({
        "roms/1MB ROMs/1993-10 - FF7439EE - LC475,575,Quadra 605,Performa 475,476,575,577,578.ROM",
        "roms/mame/macqd605/ff7439ee.bin",
        "roms/quadra605.rom", "roms/q605.rom"
    });
    std::string diskPath = findAsset({
        "hdv/MacOS-8.1-boot.vhd", "hdv/q605-boot.vhd"
    });
    if (romPath.empty() || diskPath.empty()) {
        std::printf("SKIP: needs FF7439EE ROM + hdv/MacOS-8.1-boot.vhd\n");
        return 0;
    }
    testasset::report({ romPath, diskPath });

    std::ifstream in(romPath, std::ios::binary);
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
    if (rom.size() != Q605Memory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM is %zu bytes, want 1 MB\n", rom.size());
        return 1;
    }

    Q605Memory mem(pom68k::defaultCoreConfig(), 32u << 20);
    if (!mem.loadRom(rom) || !mem.attachScsi(diskPath)) {
        std::fprintf(stderr, "FAIL: could not load ROM/disk\n");
        return 1;
    }
    Cpu040 cpu(mem, jit::defaultResolvedConfig(),
               pom68k::defaultCoreConfig().cpu,
               pom68k::defaultCoreConfig().diagnostics);
    mem.setCpu(&cpu);
    cpu.hardReset();
    while (mem.cpuHeld()) mem.tick(1000);

    // ── Boot to the Finder (q605_boot_etalon's window + signature) ──────
    Screen screen;
    bool finder = false;
    for (int frame = 0; frame < 12000 && !cpu.isHalted(); frame++) {
        cpu.runCycles(kFrameCycles);
        if (frame >= 3600 && !(frame % 60) && mem.scsi().commands > 4000) {
            screen = decodeScreen(mem);
            if (finderSignature(screen)) { finder = true; break; }
        }
    }
    if (cpu.isHalted()) { std::fprintf(stderr, "FAIL: CPU halted during boot\n"); return 1; }
    std::printf("boot: %dx%d@%dbpp, SCSI commands %ld, finder=%d\n",
                screen.width, screen.height, screen.depth,
                mem.scsi().commands, finder ? 1 : 0);
    if (!finder) { std::fprintf(stderr, "FAIL: no Finder to snapshot\n"); return 1; }

    // ── Snapshot the live Finder ────────────────────────────────────────
    using Blob = std::vector<uint8_t>;
    const auto kKind = pom68k::SnapMachine::Lc475;   // default $A55A2221 identity
    Blob start;
    pom68k::save(mem, cpu, kKind, start);
    std::printf("snapshot: %zu bytes, %zu dirty SCSI block(s)\n",
                start.size(), mem.scsiDisk().dirtyBlocks());
    if (start.size() < 64) { std::fprintf(stderr, "FAIL: empty snapshot\n"); return 1; }

    // ── Direct run ──────────────────────────────────────────────────────
    const long kScenarioFrames = 1200;               // ≈20 s emulated
    runScenario(mem, cpu, kScenarioFrames);
    if (cpu.isHalted()) { std::fprintf(stderr, "FAIL: CPU halted (direct run)\n"); return 1; }
    Blob direct;
    pom68k::save(mem, cpu, kKind, direct);

    // ── Restore + byte-identical re-save ────────────────────────────────
    std::string err;
    if (!pom68k::load(mem, cpu, kKind, start.data(), start.size(), err)) {
        std::fprintf(stderr, "FAIL: load refused its own snapshot: %s\n", err.c_str());
        return 1;
    }
    if (!err.empty()) { std::fprintf(stderr, "FAIL: load warned: %s\n", err.c_str()); return 1; }
    Blob resaved;
    pom68k::save(mem, cpu, kKind, resaved);
    if (resaved != start) {
        size_t i = 0;
        const size_t n = std::min(resaved.size(), start.size());
        while (i < n && resaved[i] == start[i]) i++;
        std::fprintf(stderr, "FAIL: load→save not byte-identical "
                     "(first divergence at byte %zu of %zu vs %zu)\n",
                     i, resaved.size(), start.size());
        return 1;
    }
    std::printf("restore: load→save byte-identical\n");

    // ── Restored run: same frames, same machine required ────────────────
    runScenario(mem, cpu, kScenarioFrames);
    if (cpu.isHalted()) { std::fprintf(stderr, "FAIL: CPU halted (restored run)\n"); return 1; }
    Blob restored;
    pom68k::save(mem, cpu, kKind, restored);

    std::printf("determinism: direct %zu bytes (hash %016llx), "
                "restored %zu bytes (hash %016llx)\n",
                direct.size(), (unsigned long long)sav::hash(direct),
                restored.size(), (unsigned long long)sav::hash(restored));
    if (restored != direct) {
        size_t i = 0;
        const size_t n = std::min(restored.size(), direct.size());
        while (i < n && restored[i] == direct[i]) i++;
        std::fprintf(stderr, "FAIL: %ld frames after a restore diverge "
                     "(first divergence at byte %zu)\n", kScenarioFrames, i);
        return 1;
    }

    // Not a deterministic corpse: the restored machine still shows a
    // Finder menu bar.
    screen = decodeScreen(mem);
    if (!finderSignature(screen)) {
        std::fprintf(stderr, "FAIL: Finder signature gone after the restored run\n");
        return 1;
    }

    std::printf("PASSED — restore is bit-deterministic under Mac OS 8.1\n");
    return 0;
}
