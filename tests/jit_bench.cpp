// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── JIT benchmark harness, Quadra 605 (dev tool, not a CTest gate) ──
// A boot etalon is a poor stopwatch: it stops as soon as it recognises the
// Finder, so the two engines are timed over DIFFERENT amounts of guest work
// and the ratio flatters whichever one got there first. This harness runs a
// FIXED guest-cycle budget instead — same instructions, same peripheral
// schedule, both engines — so wall-clock time is the only variable and the
// ratio is the honest speed-up.
//
//   POM68K_BENCH_FRAMES  frames of 416 667 cycles to run (default 3000)
//   POM68K_CPU_ENGINE    interp | jit (unset = 68040 default, currently jit)
//   POM68K_BENCH_COMPARE / POM68K_BENCH_NULL — the one-process interleaved
//                        A/B and the host-floor calibration (MEASURING.md)
//   POM68K_BENCH_PPM     dump the final screen here (diagnosing a boot)
//
// The timed loop, the fingerprint and the report live in `BenchHarness.h`,
// shared with `jit_bench_lcii` — only the machine is assembled here. Two
// engines that ran the same budget must print the SAME fingerprint: a cheap
// end-to-end equivalence check on top of jit_lockstep_test's
// instruction-by-instruction one.

#include "BenchHarness.h"
#include "Cpu040.h"
#include "Q605Memory.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {

using bench::findAsset;

uint32_t peek32(const Q605Memory& mem, uint32_t addr) {
    return uint32_t(mem.peek8(addr)) << 24 | uint32_t(mem.peek8(addr + 1)) << 16 |
           uint32_t(mem.peek8(addr + 2)) << 8 | mem.peek8(addr + 3);
}

// The 8-bit DAFB framebuffer through the main GDevice's CLUT, written as a
// binary PPM — enough to SEE what the machine is showing when a boot
// etalon's luminance signature disagrees with expectations.
void dumpPpm(const Q605Memory& mem, const char* path) {
    uint32_t mainDevH = peek32(mem, 0x08A4);
    uint32_t mainDev = mainDevH ? peek32(mem, mainDevH) : 0;
    uint32_t pmapH = mainDev ? peek32(mem, mainDev + 0x16) : 0;
    uint32_t pmap = pmapH ? peek32(mem, pmapH) : 0;
    if (!pmap) { std::fprintf(stderr, "bench: no PixMap to dump\n"); return; }

    uint32_t boundsA = peek32(mem, pmap + 0x06), boundsB = peek32(mem, pmap + 0x0A);
    int top = int(int16_t(boundsA >> 16)), left = int(int16_t(boundsA & 0xFFFF));
    int bottom = int(int16_t(boundsB >> 16)), right = int(int16_t(boundsB & 0xFFFF));
    const int w = right - left, h = bottom - top;
    if (w <= 0 || h <= 0 || w > 2048 || h > 2048) return;

    const uint32_t base = peek32(mem, pmap) & (Q605Memory::kVramSize - 1);
    const uint32_t stride = mem.dafbStride();
    const int depth = mem.dafbDepth();
    const uint8_t* vram = mem.vram();
    const uint8_t (*clut)[3] = mem.clut();
    if (!vram || !clut || uint64_t(base) + uint64_t(h) * stride > Q605Memory::kVramSize) return;

    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << w << " " << h << "\n255\n";
    for (int y = 0; y < h; y++) {
        const uint32_t row = base + uint32_t(y) * stride;
        for (int x = 0; x < w; x++) {
            const uint8_t packed = vram[row + uint32_t(x * depth / 8)];
            uint8_t pen;
            if (depth == 1)      pen = (packed >> (7 - (x & 7))) & 1;
            else if (depth == 2) pen = (packed >> (6 - 2 * (x & 3))) & 3;
            else if (depth == 4) pen = (x & 1) ? packed & 0x0F : packed >> 4;
            else                 pen = packed;
            const uint8_t* c = clut[pen];
            const char rgb[3] = { char(c[0]), char(c[1]), char(c[2]) };
            out.write(rgb, 3);
        }
    }
    std::fprintf(stderr, "bench: screen %dx%d@%dbpp -> %s\n", w, h, depth, path);
}

}  // namespace

int main() {
    const std::string romPath = std::getenv("POM68K_BENCH_ROM")
        ? std::getenv("POM68K_BENCH_ROM") : findAsset({
        "roms/1MB ROMs/1993-10 - FF7439EE - LC475,575,Quadra 605,Performa 475,476,575,577,578.ROM",
        "roms/mame/macqd605/ff7439ee.bin", "roms/quadra605.rom", "roms/q605.rom"
    });
    const std::string diskPath = std::getenv("POM68K_BENCH_DISK")
        ? std::getenv("POM68K_BENCH_DISK")
        : findAsset({ "hdv/MacOS-8.1-boot.vhd", "hdv/q605-boot.vhd" });
    if (romPath.empty() || diskPath.empty()) {
        std::printf("SKIP: needs the FF7439EE ROM + hdv/MacOS-8.1-boot.vhd\n");
        return 0;
    }

    std::ifstream in(romPath, std::ios::binary);
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());

    // POM68K_BENCH_COMPARE=N — the interleaved A/B, POM68K_BENCH_NULL=1 the
    // host-floor calibration. Same protocol as the LC II twin; the shared
    // contract (ABBA order, warm-up pair, floor, provenance stamps) lives in
    // BenchHarness.h and docs/MEASURING.md § 1 owns the why.
    if (bench::compareRepeats() > 0) {
        constexpr int64_t kCmpFrameCycles = 416667;    // 25 MHz / ~60 Hz
        const int cmpFrames = bench::frames(3000);
        std::printf("q605 %s, %d repeats x 2 arms ABBA, %d frames per run, "
                    "built %s\n",
                    bench::nullExperiment() ? "NULL experiment (A vs A)"
                                            : "interleaved A/B",
                    bench::compareRepeats(), cmpFrames, bench::buildStamp());
        return bench::compare("q605", "interp", "jit", [&](int arm) {
            Q605Memory m(32u << 20);
            m.loadRom(rom);
            m.attachScsi(diskPath);
            Cpu040 c(m);
            m.setCpu(&c);
            c.setEngine(arm);                          // 0 interp, 1 jit
            c.hardReset();
            while (m.cpuHeld()) m.tick(1000);
            const bench::Result r = bench::run(c, cmpFrames, kCmpFrameCycles);
            return bench::Sample{ r.secs, r.fp };
        });
    }

    Q605Memory mem(32u << 20);
    if (!mem.loadRom(rom) || !mem.attachScsi(diskPath)) {
        std::fprintf(stderr, "FAIL: could not load ROM/disk\n");
        return 1;
    }
    Cpu040 cpu(mem);
    mem.setCpu(&cpu);
    cpu.hardReset();
    while (mem.cpuHeld()) mem.tick(1000);

    constexpr int64_t kFrameCycles = 416667;           // 25 MHz / ~60 Hz
    const int frames = bench::frames(3000);

    const bench::Result r = bench::run(cpu, frames, kFrameCycles);
    const char* workload = cpu.engine() ? "q605_jit" : "q605_interp";
    bench::report("q605", workload, "68040", cpu, r, mem.cpuHz());
    std::printf("  SCSI=%ld  pc=$%08X  %s\n", mem.scsi().commands, cpu.getPC(),
                cpu.isHalted() ? "HALTED" : "running");

    if (const char* ppm = std::getenv("POM68K_BENCH_PPM")) dumpPpm(mem, ppm);
    return 0;
}
