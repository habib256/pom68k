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
//   POM68K_BENCH_COMPARE N repeats per arm, both arms in THIS process,
//                        counterbalanced ABBA — the only supported way to
//                        produce a delta (docs/MEASURING.md § R1)
//   POM68K_BENCH_ARMS    <a>,<b> — the two arms of the comparison, each
//                        `interp` or a backend key (threaded/x64/a64),
//                        head-to-head in the SAME ABBA process. Unset =
//                        the historical interp-vs-jit pair

//   POM68K_BENCH_NULL=1  run the reference arm against itself: measures
//                        this host's noise floor instead of assuming one
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
    std::string romPath = std::getenv("POM68K_BENCH_ROM")
        ? std::getenv("POM68K_BENCH_ROM") : findAsset({
        "roms/512KB ROMs/1992-03 - 35C28F5F - Mac LC II.ROM",
        "docs/512KB ROMs/1992-03 - 35C28F5F - Mac LC II.ROM"
    });
    const std::string diskPath = std::getenv("POM68K_BENCH_DISK")
        ? std::getenv("POM68K_BENCH_DISK") : findAsset({
        "hdv/lcii-boot.vhd", "hdv/boot.vhd", "hdv/GISTPERSO-boot.vhd"
    });
    if (romPath.empty() || diskPath.empty()) {
        std::printf("SKIP: needs the 512 KB LC II ROM + a bootable hdv/ image\n");
        return 0;
    }

    std::ifstream in(romPath, std::ios::binary);
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());

    constexpr int64_t kFrameCycles = 640 * 407;      // 60.15 Hz @ 15.6672 MHz
    const int frames = bench::frames(6000);

    // POM68K_BENCH_COMPARE=N — the interleaved A/B (docs/MEASURING.md § R1).
    // A fresh machine per repeat: that is what a separate process gives,
    // minus the process, and it is the only way the two arms see the same
    // page cache and the same thermal state.
    if (bench::compareRepeats() > 0) {
        // POM68K_BENCH_ARMS=<a>,<b> — head-to-head of two ENGINE arms in the
        // SAME ABBA process. Each side is `interp` or a backend key handed to
        // POM68K_JIT_BACKEND before that arm's machine is built — the Engine
        // resolves its environment at construction, so the knob binds per
        // arm. Unset, this is the historical interp-vs-jit comparison.
        // It exists because `threaded` vs a code generator used to be TWO
        // compare invocations against the interpreter, which reintroduces
        // exactly the between-process variance bench::compare removes.
        std::string armA = "interp", armB = "jit";
        if (const char* arms = std::getenv("POM68K_BENCH_ARMS")) {
            const std::string v = arms;
            const size_t comma = v.find(',');
            if (comma == std::string::npos || comma == 0 ||
                comma + 1 == v.size()) {
                std::fprintf(stderr,
                             "FAIL: POM68K_BENCH_ARMS wants \"<a>,<b>\" "
                             "(interp or a backend key per side)\n");
                return 1;
            }
            armA = v.substr(0, comma);
            armB = v.substr(comma + 1);
        }
        std::printf("lcii %s, %d repeats x 2 arms ABBA, %d frames per run, "
                    "built %s\n",
                    bench::nullExperiment() ? "NULL experiment (A vs A)"
                                            : "interleaved A/B",
                    bench::compareRepeats(), frames, bench::buildStamp());
        return bench::compare("lcii", armA.c_str(), armB.c_str(), [&](int arm) {
            const std::string& spec = arm == 0 ? armA : armB;
            const bool engineOn = spec != "interp";
            if (engineOn && spec != "jit")           // "jit" = whatever the
                setenv("POM68K_JIT_BACKEND", spec.c_str(), 1);   // env says
            V8Memory m;
            m.loadRom(rom);
            Cpu030 c(m, /*withFpu=*/true);
            m.setCpu(&c);
            c.setEngine(engineOn ? 1 : 0);
            c.hardReset();
            m.attachScsi(diskPath);
            ensureBootDriverType(m.scsiDisk().image());
            while (m.cpuHeld()) m.tick(1000);
            const bench::Result r = bench::run(c, frames, kFrameCycles);
            return bench::Sample{ r.secs, r.fp };
        });
    }

    V8Memory mem;
    if (!mem.loadRom(rom)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    Cpu030 cpu(mem, /*withFpu=*/true);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(diskPath)) { std::fprintf(stderr, "FAIL: bad disk image\n"); return 1; }
    ensureBootDriverType(mem.scsiDisk().image());
    while (mem.cpuHeld()) mem.tick(1000);            // the Egret holds it at power-on

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
    const char* workload = !cpu.engine() ? "lcii_interp"
        : std::string(cpu.jit().backendName()) == "threaded"
            ? "lcii_threaded" : "lcii_native_experimental";
    bench::report("lcii", workload, "68030", cpu, r, mem.cpuHz());
    std::printf("  SCSI=%ld  pc=$%08X  %s\n", mem.scsi().commands, cpu.getPC(),
                cpu.isHalted() ? "HALTED" : "running");

    // The 68030 on-chip i-cache (Moira.h § PomIcache). Reported here because
    // it is the ONE piece of 030 timing the JIT has to reproduce and cannot
    // inherit: the fetch window and the threaded backend charge it through
    // mmuFetchWord like the interpreter does, but generated code fetches no
    // instructions at all and would charge nothing. These three numbers must
    // therefore be IDENTICAL across engines at a fixed cycle budget — they
    // are the 030 half of the fingerprint. See docs/JIT_BRINGUP.md § B.
    const Cpu030::ICacheStats ic = cpu.icacheStats();
    const double hitPct = ic.hits + ic.misses
                        ? 100.0 * double(ic.hits) / double(ic.hits + ic.misses) : 0.0;
    std::printf("  icache: %lld fetches, %lld hits, %lld misses (%.2f%% hit)%s\n",
                (long long)ic.fetches, (long long)ic.hits, (long long)ic.misses,
                hitPct, cpu.icacheEnabled() ? "" : "  [CACR bit 0 clear]");

    if (const char* ppm = std::getenv("POM68K_BENCH_PPM")) dumpPpm(mem, ppm);
    return 0;
}
