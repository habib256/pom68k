// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── JIT benchmark harness, LC II (dev tool, not a CTest gate) ──
// The 68030 twin of `jit_bench`. It exists because `TODO.md` § 0·A names the
// LC II as the machine the speed objective is written about ("~×1,3 temps
// réel en turbo sur un x86 costaud ; inutilisable sur Pi 400") and there was
// no instrument that could say whether that ×1,3 was the interpreter or the
// engine — a boot etalon cannot, since it stops at the Finder and times two
// engines over different amounts of guest work.
//
// Same contract as `jit_bench`: a FIXED guest-cycle budget, an architectural
// fingerprint both engines must agree on, and a × real-time ratio against
// the machine's own 15.6672 MHz clock.
//
//   POM68K_BENCH_FRAMES  frames of 640×407 cycles (default 6000 ≈ 1.56 G)
//   POM68K_CPU_ENGINE    interp (default) | jit
//   POM68K_JIT_BACKEND   threaded | x86-64 | aarch64 (auto gives an 030
//                        `threaded`: the code generators declare 68040 only)
//   POM68K_BENCH_PPM     dump the final screen here (diagnosing a boot)
//   POM68K_BENCH_SLICES  cut each frame into N slices and catch the raster
//                        beam up at every boundary — the GUI machine loop's
//                        own shape (`main.cpp runQuantumWithWire`), which is
//                        1 slice with the network off and **64** with
//                        AppleTalk on, i.e. by default. 0 (the default here)
//                        runs the CPU alone, so the two numbers PRICE the
//                        per-slice work rather than leaving it invisible.
//
// The Finder is up by ~16 000 frames (≈4.17 G cycles, `lcii_boot_etalon`);
// budgets below that measure the System-loading regime, above it the idle
// Finder — the two regimes `POM68K_JIT.md` § 3 reports separately.

#include "BenchHarness.h"
#include "Cpu030.h"
#include "V8Memory.h"
#include "V8Video.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {

using bench::findAsset;

// The LC II ROM's boot scan ($A07264) only loads a driver descriptor whose
// ddType is $6A, and images made by other tools usually carry $0001. Same
// non-destructive in-memory patch `lcii_boot_etalon` applies.
void ensureBootDriverType(std::vector<uint8_t>& img) {
    if (img.size() < 512 || img[0] != 'E' || img[1] != 'R') return;
    int count = (img[0x10] << 8) | img[0x11];        // sbDrvrCount
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

void dumpPpm(V8Memory& mem, const char* path) {
    V8Video video(mem);
    std::vector<uint32_t> fb;
    video.decode(fb);                                // 512×384, 00RRGGBB
    const int w = 512, h = int(fb.size()) / 512;
    if (h <= 0) return;
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << w << " " << h << "\n255\n";
    for (int i = 0; i < w * h; i++) {
        const uint32_t p = fb[i];
        const char rgb[3] = { char(p >> 16), char(p >> 8), char(p) };
        out.write(rgb, 3);
    }
    std::fprintf(stderr, "bench: screen %dx%d -> %s\n", w, h, path);
}

}  // namespace

int main() {
    std::string romPath = findAsset({
        "roms/512KB ROMs/1992-03 - 35C28F5F - Mac LC II.ROM",
        "docs/512KB ROMs/1992-03 - 35C28F5F - Mac LC II.ROM"
    });
    const std::string diskPath = findAsset({
        "hdv/lcii-boot.vhd", "hdv/boot.vhd", "hdv/GISTPERSO-boot.vhd"
    });
    if (romPath.empty() || diskPath.empty()) {
        std::printf("SKIP: needs the 512 KB LC II ROM + a bootable hdv/ image\n");
        return 0;
    }

    std::ifstream in(romPath, std::ios::binary);
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
    V8Memory mem;
    if (!mem.loadRom(rom)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    Cpu030 cpu(mem, /*withFpu=*/true);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(diskPath)) { std::fprintf(stderr, "FAIL: bad disk image\n"); return 1; }
    ensureBootDriverType(mem.scsiDisk().image());
    while (mem.cpuHeld()) mem.tick(1000);            // the Egret holds it at power-on

    constexpr int64_t kFrameCycles = 640 * 407;      // 60.15 Hz @ 15.6672 MHz
    const int frames = bench::frames(6000);

    // 0 = CPU alone. N ≥ 1 = the GUI's quantum: N slices per frame, the
    // raster beam caught up at each boundary (V8Video::raster is what
    // LcMachine::emulateQuantum passes as its onSlice callback).
    int slices = 0;
    if (const char* s = std::getenv("POM68K_BENCH_SLICES")) slices = std::atoi(s);
    V8Video video(mem);
    std::vector<uint32_t> fb;

    bench::Result r;
    if (slices <= 0) {
        r = bench::run(cpu, frames, kFrameCycles);
    } else {
        const int64_t per = kFrameCycles / slices;
        r = bench::runWith(cpu, frames, per * slices, [&] {
            for (int i = 0; i < slices; i++) { cpu.runCycles(per); video.raster(fb); }
        });
        std::printf("  quantum: %d slice(s)/frame with a raster catch-up each"
                    " (GUI shape)\n", slices);
    }
    bench::report("lcii  ", cpu, r, mem.cpuHz());
    std::printf("  SCSI=%ld  pc=$%08X  %s\n", mem.scsi().commands, cpu.getPC(),
                cpu.isHalted() ? "HALTED" : "running");

    if (const char* ppm = std::getenv("POM68K_BENCH_PPM")) dumpPpm(mem, ppm);
    return 0;
}
