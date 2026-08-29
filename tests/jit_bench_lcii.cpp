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
//   POM68K_JIT_BACKEND   threaded | x86-64 | aarch64 (auto consults each
//                        backend's autoFamilies SPEED declaration — a
//                        family is earned per backend on D.1 evidence,
//                        JIT_BRINGUP § C.5)
//   POM68K_BENCH_COMPARE N repeats per arm, both arms in THIS process,
//                        counterbalanced ABBA — the only supported way to
//                        produce a delta (docs/MEASURING.md § R1)
//   POM68K_BENCH_ARMS    <a>,<b> — the two arms of the comparison, each
//                        `interp` or a backend key (threaded/x64/a64),
//                        optionally `<backend>@score=N` to compare two
//                        profitability policies in one ABBA process,
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
#include "JitTestConfig.h"
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
        // Arm modifiers, each binding one Engine-resolved knob per arm the
        // way `@score=` always has: `@restart=0|1` and `@bsrw=0|1` are the
        // two admission experiments of JIT_BRINGUP § C.4nonies, and their
        // D.1 speed evidence is exactly a same-process A/B of one knob.
        struct ArmSpec {
            std::string label;
            std::string backend;
            std::string score;
            bool scoreOverride = false;
            std::string restart;
            bool restartOverride = false;
            std::string bsrw;
            bool bsrwOverride = false;
        };
        const auto parseArm = [](const std::string& text, ArmSpec& out) {
            out.label = text;
            size_t at = text.find('@');
            out.backend = text.substr(0, at);
            if (out.backend.empty()) return false;
            while (at != std::string::npos) {
                const size_t next = text.find('@', at + 1);
                const std::string modifier =
                    text.substr(at + 1, next == std::string::npos
                                            ? std::string::npos
                                            : next - at - 1);
                const size_t eq = modifier.find('=');
                if (eq == std::string::npos || eq + 1 == modifier.size())
                    return false;
                const std::string key = modifier.substr(0, eq);
                const std::string val = modifier.substr(eq + 1);
                if (key == "score") {
                    char* end = nullptr;
                    const long score = std::strtol(val.c_str(), &end, 10);
                    if (!end || *end || score < 0 || score > (1L << 30))
                        return false;
                    out.score = std::to_string(score);
                    out.scoreOverride = true;
                } else if (key == "restart" || key == "bsrw") {
                    if (val != "0" && val != "1") return false;
                    if (key == "restart") {
                        out.restart = val;
                        out.restartOverride = true;
                    } else {
                        out.bsrw = val;
                        out.bsrwOverride = true;
                    }
                } else {
                    return false;
                }
                at = next;
            }
            return true;
        };
        ArmSpec specs[2];
        if (!parseArm(armA, specs[0]) || !parseArm(armB, specs[1])) {
            std::fprintf(stderr,
                         "FAIL: arm syntax is <backend> with optional "
                         "@score=N (0 <= N <= 2^30), @restart=0|1, "
                         "@bsrw=0|1 modifiers\n");
            return 1;
        }
        const char* originalScore = std::getenv("POM68K_JIT_PROFIT_SCORE");
        const bool hadOriginalScore = originalScore != nullptr;
        const std::string savedScore = originalScore ? originalScore : "";
        const char* originalRestart = std::getenv("POM68K_JIT_RESTART_BASE");
        const bool hadOriginalRestart = originalRestart != nullptr;
        const std::string savedRestart = originalRestart ? originalRestart : "";
        const char* originalBsrw = std::getenv("POM68K_JIT_BSRW");
        const bool hadOriginalBsrw = originalBsrw != nullptr;
        const std::string savedBsrw = originalBsrw ? originalBsrw : "";
        std::printf("lcii %s, %d repeats x 2 arms ABBA, %d frames per run, "
                    "built %s\n",
                    bench::nullExperiment() ? "NULL experiment (A vs A)"
                                            : "interleaved A/B",
                    bench::compareRepeats(), frames, bench::buildStamp());
        return bench::compare("lcii", armA.c_str(), armB.c_str(), [&](int arm) {
            const ArmSpec& spec = specs[arm];
            if (spec.scoreOverride)
                setenv("POM68K_JIT_PROFIT_SCORE", spec.score.c_str(), 1);
            else if (hadOriginalScore)
                setenv("POM68K_JIT_PROFIT_SCORE", savedScore.c_str(), 1);
            else
                unsetenv("POM68K_JIT_PROFIT_SCORE");
            if (spec.restartOverride)
                setenv("POM68K_JIT_RESTART_BASE", spec.restart.c_str(), 1);
            else if (hadOriginalRestart)
                setenv("POM68K_JIT_RESTART_BASE", savedRestart.c_str(), 1);
            else
                unsetenv("POM68K_JIT_RESTART_BASE");
            if (spec.bsrwOverride)
                setenv("POM68K_JIT_BSRW", spec.bsrw.c_str(), 1);
            else if (hadOriginalBsrw)
                setenv("POM68K_JIT_BSRW", savedBsrw.c_str(), 1);
            else
                unsetenv("POM68K_JIT_BSRW");
            const bool engineOn = spec.backend != "interp";
            if (engineOn && spec.backend != "jit")   // "jit" = whatever the
                setenv("POM68K_JIT_BACKEND", spec.backend.c_str(), 1); // env says
            V8Memory m(pom68k::defaultCoreConfig());
            m.loadRom(rom);
            const jit::ResolvedConfig jitConfig =
                testjit::resolveFromEnvironment();
            Cpu030 c(m, jitConfig, pom68k::defaultCoreConfig().cpu,
                     /*withFpu=*/true, false);
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

    V8Memory mem(pom68k::defaultCoreConfig());
    if (!mem.loadRom(rom)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    const jit::ResolvedConfig jitConfig = testjit::resolveFromEnvironment();
    Cpu030 cpu(mem, jitConfig, pom68k::defaultCoreConfig().cpu,
               /*withFpu=*/true, false);
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
    // POM68K_BENCH_JIT_PROGRESS=<N frames>: print the engine's gauges every
    // N frames, on stderr, unbuffered. The instrument that separates the
    // three shapes a wall-clock sink can take — an eviction storm, a
    // compile storm, or one call that never returns — on a run that will
    // never reach the end-of-run report (the 2026-08-29 mode-2 wedge).
    int jitProgress = 0;
    if (const char* jp = std::getenv("POM68K_BENCH_JIT_PROGRESS"))
        jitProgress = std::atoi(jp);
    if (jitProgress > 0 && cpu.engine()) {
        int frame = 0;
        r = bench::runWith(cpu, frames, kFrameCycles, [&] {
            cpu.runCycles(kFrameCycles);
            if (++frame % jitProgress) return;
            const jit::Stats::Snapshot sn = cpu.jit().stats().snapshot();
            std::fprintf(stderr,
                "[jitprog] f=%d clk=%lld pc=$%08X instrs=%llu trace=%llu "
                "interp=%llu compiled=%llu run=%llu evict=%llu trips=%llu "
                "flush=%llu(guard %llu) armed=%llu failed=%llu\n",
                frame, (long long)cpu.getClock(), cpu.getPC(),
                (unsigned long long)sn.instrs,
                (unsigned long long)sn.traceInstrs,
                (unsigned long long)sn.interpInstrs,
                (unsigned long long)sn.blocksCompiled,
                (unsigned long long)sn.blocksRun,
                (unsigned long long)sn.evictions,
                (unsigned long long)sn.invalidations,
                (unsigned long long)sn.flushes,
                (unsigned long long)sn.flushCauses[int(jit::Flush::Guard)],
                (unsigned long long)sn.windowArmed,
                (unsigned long long)sn.windowFailed);
        });
    } else if (slices <= 0) {
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
