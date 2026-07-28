// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── JIT benchmark harness (dev tool, not a CTest gate) ──
// A boot etalon is a poor stopwatch: it stops as soon as it recognises the
// Finder, so the two engines are timed over DIFFERENT amounts of guest work
// and the ratio flatters whichever one got there first. This harness runs a
// FIXED guest-cycle budget instead — same instructions, same peripheral
// schedule, both engines — so wall-clock time is the only variable and the
// ratio is the honest speed-up.
//
//   POM68K_BENCH_FRAMES  frames of 416 667 cycles to run (default 3000)
//   POM68K_CPU_ENGINE    interp (default) | jit
//   POM68K_BENCH_PPM     dump the final screen here (diagnosing a boot)
//
// It also prints a fingerprint of the architectural state at the end. Two
// engines that ran the same budget must print the SAME fingerprint — a
// cheap end-to-end equivalence check on top of jit_lockstep_test's
// instruction-by-instruction one.

#include "Cpu040.h"
#include "Q605Memory.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::string findAsset(std::initializer_list<const char*> names) {
    for (const char* name : names)
        for (const std::string& base : { std::string(), std::string("../") }) {
            std::string path = base + name;
            if (std::ifstream(path, std::ios::binary)) return path;
        }
    return {};
}

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
    const std::string romPath = findAsset({
        "roms/1MB ROMs/1993-10 - FF7439EE - LC475,575,Quadra 605,Performa 475,476,575,577,578.ROM",
        "roms/mame/macqd605/ff7439ee.bin", "roms/quadra605.rom", "roms/q605.rom"
    });
    const std::string diskPath = findAsset({ "hdv/MacOS-8.1-boot.vhd", "hdv/q605-boot.vhd" });
    if (romPath.empty() || diskPath.empty()) {
        std::printf("SKIP: needs the FF7439EE ROM + hdv/MacOS-8.1-boot.vhd\n");
        return 0;
    }

    std::ifstream in(romPath, std::ios::binary);
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
    Q605Memory mem(32u << 20);
    if (!mem.loadRom(rom) || !mem.attachScsi(diskPath)) {
        std::fprintf(stderr, "FAIL: could not load ROM/disk\n");
        return 1;
    }
    Cpu040 cpu(mem);
    mem.setCpu(&cpu);
    cpu.hardReset();
    while (mem.cpuHeld()) mem.tick(1000);

    int frames = 3000;
    if (const char* f = std::getenv("POM68K_BENCH_FRAMES")) frames = std::atoi(f);
    constexpr int kFrameCycles = 416667;               // 25 MHz / ~60 Hz

    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < frames && !cpu.isHalted(); i++) cpu.runCycles(kFrameCycles);
    const auto t1 = std::chrono::steady_clock::now();
    const double secs = std::chrono::duration<double>(t1 - t0).count();

    // Fingerprint: every architectural register plus the cycle clock. Two
    // engines given the same budget must agree bit for bit.
    uint64_t fp = 1469598103934665603ull;
    auto mix = [&fp](uint64_t v) {
        for (int i = 0; i < 8; i++) { fp ^= (v >> (i * 8)) & 0xFF; fp *= 1099511628211ull; }
    };
    for (int i = 0; i < 8; i++) { mix(cpu.getD(i)); mix(cpu.getA(i)); }
    mix(cpu.getPC()); mix(cpu.getSR()); mix(uint64_t(cpu.getClock()));
    mix(cpu.getUSP()); mix(cpu.getISP()); mix(cpu.getMSP());

    const jit::Stats::Snapshot s = cpu.jit().stats().snapshot();
    const uint64_t retired = s.instrs + s.interpInstrs;

    std::printf("engine=%-6s frames=%d cycles=%lld wall=%.2fs  fp=%016llx\n",
                cpu.engine() ? "jit" : "interp", frames,
                (long long)cpu.getClock(), secs, (unsigned long long)fp);
    if (cpu.engine()) {
        std::printf("  backend=%s  retired=%llu  %.1f Minstr/s  "
                    "blocks %llu compiled / %llu run (%.1f instr/run)\n",
                    cpu.jit().backendName(), (unsigned long long)retired,
                    secs > 0 ? double(retired) / secs / 1e6 : 0.0,
                    (unsigned long long)s.blocksCompiled,
                    (unsigned long long)s.blocksRun,
                    s.blocksRun ? double(s.instrs) / double(s.blocksRun) : 0.0);
        // The number that says whether the code generator is doing anything:
        // instructions that ran as HOST CODE, against those a compiled block
        // handed straight back to Moira, against those the engine ran on the
        // window path because no block covered them.
        const uint64_t notNative = s.slowInstrs + s.windowInstrs;
        const uint64_t native = s.instrs > notNative ? s.instrs - notNative : 0;
        std::printf("  native %llu (%.1f%%) · block fallback %llu (%.1f%%) · "
                    "window/interp %llu (%.1f%%)\n",
                    (unsigned long long)native, 100.0 * double(native) / double(retired),
                    (unsigned long long)s.slowInstrs,
                    100.0 * double(s.slowInstrs) / double(retired),
                    (unsigned long long)(s.windowInstrs + s.interpInstrs),
                    100.0 * double(s.windowInstrs + s.interpInstrs) / double(retired));
        std::printf("  dtlb %llu filled / %llu refused · %llu flush(es), "
                    "%llu from a write into translated code\n",
                    (unsigned long long)s.dtlbFills,
                    (unsigned long long)s.dtlbRefused,
                    (unsigned long long)s.flushes,
                    (unsigned long long)s.invalidations);
        for (int i = 0; i < int(jit::Exit::Count); i++)
            if (s.exits[i])
                std::printf("  exit %-14s %llu\n", jit::exitName(jit::Exit(i)),
                            (unsigned long long)s.exits[i]);
    }
    std::printf("  SCSI=%ld  pc=$%08X  %s\n", mem.scsi().commands, cpu.getPC(),
                cpu.isHalted() ? "HALTED" : "running");

    if (const char* ppm = std::getenv("POM68K_BENCH_PPM")) dumpPpm(mem, ppm);
    return 0;
}
