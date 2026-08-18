// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── The bench body, shared by every machine's benchmark ──
// A boot etalon is a poor stopwatch (it stops when it recognises the Finder,
// so two engines get timed over different amounts of guest work). A bench
// runs a FIXED guest-cycle budget instead, and everything below is the part
// that has nothing to do with which machine is running: the timed loop, the
// architectural fingerprint the engines must agree on, and the report.
//
// What the report answers, and why it is here rather than in each bench:
//   * wall time for a fixed budget      — the A/B number between engines;
//   * **× real time** — emulated cycles per second against the machine's own
//     clock. THE number `TODO.md` § 0·A is written in: "utilisable" means
//     ×1 or better on the target host, and a ratio is the only form of the
//     measurement that survives changing machine, host or budget.
//
// Env: POM68K_BENCH_FRAMES (budget, in frames), POM68K_CPU_ENGINE=interp|jit,
// POM68K_BENCH_ROM / POM68K_BENCH_DISK (explicit immutable-run inputs).

#pragma once

#include "jit/JitEngine.h"
#include "jit/JitMetrics.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <string>

namespace bench {

// Assets live under the source root; a bench runs from build/ or from it.
inline std::string findAsset(std::initializer_list<const char*> names) {
    for (const char* name : names)
        for (const std::string& base : { std::string(), std::string("../") }) {
            std::string path = base + name;
            if (std::ifstream(path, std::ios::binary)) return path;
        }
    return {};
}

inline int frames(int dflt) {
    if (const char* f = std::getenv("POM68K_BENCH_FRAMES")) {
        const int n = std::atoi(f);
        if (n > 0) return n;
    }
    return dflt;
}

// TWO cycle counts, and confusing them is a factor of four.
// `runCycles(n)` on every wrapper in this tree takes n **machine** cycles —
// peripheral time, the thing `cpuHz()` is the rate of — and advances Moira's
// clock by `n × cacheBoost_` (4 by default) so the core does a real 68030's
// or 68040's worth of work per frame. `getClock()` therefore reports the
// BOOSTED core clock. The × real-time ratio is guest time against wall time,
// so it must be computed on the machine budget; the first version of this
// harness used getClock() and printed ×7.94 for an LC II that was in fact
// running at ×1.99. `CLAUDE.md`: bus time is counted on machine cycles,
// never on the boosted clock — the same rule, on the stopwatch this time.
struct Result {
    double secs = 0;
    int64_t machineCycles = 0;   // guest (peripheral) time actually granted
    int64_t coreCycles = 0;      // Moira clock delta — boosted, for the record
    uint64_t fp = 0;             // architectural fingerprint
};

// The fingerprint: every architectural register plus the cycle clock. Two
// engines given the same budget must print the same one — a cheap end-to-end
// equivalence check on top of jit_lockstep_test's per-instruction one.
template <class Cpu>
uint64_t fingerprint(const Cpu& cpu) {
    uint64_t fp = 1469598103934665603ull;
    auto mix = [&fp](uint64_t v) {
        for (int i = 0; i < 8; i++) { fp ^= (v >> (i * 8)) & 0xFF; fp *= 1099511628211ull; }
    };
    Cpu& c = const_cast<Cpu&>(cpu);           // Moira's getters are non-const
    for (int i = 0; i < 8; i++) { mix(c.getD(i)); mix(c.getA(i)); }
    mix(c.getPC()); mix(c.getSR()); mix(uint64_t(c.getClock()));
    mix(c.getUSP()); mix(c.getISP()); mix(c.getMSP());
    return fp;
}

// `frame` runs ONE frame's worth of guest time however the caller wants it
// run — bare `runCycles`, or sliced with a raster catch-up at each boundary
// the way the GUI machine loop does it (`main.cpp runQuantumWithWire`). The
// distinction is not cosmetic: what the user sees in the window is the CPU
// *plus* the per-slice work, and a CPU-only bench cannot be compared with a
// ratio observed on screen.
template <class Cpu, class Fn>
Result runWith(Cpu& cpu, int nFrames, int64_t frameCycles, Fn&& frame) {
    const int64_t start = cpu.getClock();
    int ran = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (; ran < nFrames && !cpu.isHalted(); ran++) frame();
    const auto t1 = std::chrono::steady_clock::now();
    Result r;
    r.secs = std::chrono::duration<double>(t1 - t0).count();
    r.machineCycles = int64_t(ran) * frameCycles;
    r.coreCycles = cpu.getClock() - start;
    r.fp = fingerprint(cpu);
    return r;
}

template <class Cpu>
Result run(Cpu& cpu, int nFrames, int64_t frameCycles) {
    return runWith(cpu, nFrames, frameCycles,
                   [&cpu, frameCycles] { cpu.runCycles(frameCycles); });
}

template <class Cpu>
void report(const char* machine, const char* workload, const char* cpuFamily,
            Cpu& cpu, const Result& r, int64_t cpuHz) {
    const double guestSecs = cpuHz ? double(r.machineCycles) / double(cpuHz) : 0.0;
    const double core = r.secs > 0 ? double(r.coreCycles) / r.secs : 0.0;
    std::printf("%s engine=%-6s cycles=%lld machine (%lld core, boost \xC3\x97%.1f)"
                "  wall=%.2fs for %.2fs of guest time  \xC3\x97%.2f real time"
                "  %.1f core MHz  fp=%016llx\n",
                machine, cpu.engine() ? "jit" : "interp",
                (long long)r.machineCycles, (long long)r.coreCycles,
                r.machineCycles ? double(r.coreCycles) / double(r.machineCycles) : 0.0,
                r.secs, guestSecs, r.secs > 0 ? guestSecs / r.secs : 0.0,
                core / 1e6, (unsigned long long)r.fp);

    jit::MetricsRecord metrics;
    metrics.gate = "jit_fixed_cycle_bench";
    metrics.workload = workload;
    metrics.cpuFamily = cpuFamily;
    metrics.backend = cpu.engine() ? cpu.jit().backendName() : "interpreter";
    metrics.engine = cpu.engine() ? "jit" : "interp";
    metrics.machineCycles = uint64_t(r.machineCycles);
    metrics.coreCycles = uint64_t(r.coreCycles);
    metrics.wallNs = uint64_t(r.secs * 1.0e9);
    metrics.fingerprint = r.fp;
    metrics.realtimePermille = r.secs > 0
        ? uint64_t(guestSecs / r.secs * 1000.0) : 0;

    if (!cpu.engine()) {
        jit::emitMetrics(metrics);
        return;
    }

    const jit::Stats::Snapshot s = cpu.jit().stats().snapshot();
    const uint64_t retired = s.instrs + s.interpInstrs;
    if (!retired) {
        jit::emitMetrics(metrics);
        return;
    }

    std::printf("  backend=%s  retired=%llu  %.1f Minstr/s  "
                "blocks %llu compiled / %llu run (%.1f instr/run)\n",
                cpu.jit().backendName(), (unsigned long long)retired,
                r.secs > 0 ? double(retired) / r.secs / 1e6 : 0.0,
                (unsigned long long)s.blocksCompiled,
                (unsigned long long)s.blocksRun,
                s.blocksRun ? double(s.instrs) / double(s.blocksRun) : 0.0);

    // The number that says whether the code generator is doing anything:
    // instructions that ran as HOST CODE, against those a compiled block
    // handed straight back to Moira, against those the engine ran on the
    // window path because no block covered them.
    const uint64_t notNative = s.slowInstrs + s.windowInstrs;
    const uint64_t native = s.instrs > notNative ? s.instrs - notNative : 0;
    metrics.blocksCompiled = s.blocksCompiled;
    metrics.blocksRun = s.blocksRun;
    metrics.jitInstrs = s.instrs;
    metrics.nativeInstrs = native;
    metrics.interpInstrs = s.interpInstrs;
    metrics.slowInstrs = s.slowInstrs;
    metrics.windowInstrs = s.windowInstrs;
    metrics.nativeSharePermille = retired ? native * 1000 / retired : 0;
    std::printf("  native %llu (%.1f%%) \xC2\xB7 block fallback %llu (%.1f%%) \xC2\xB7 "
                "window/interp %llu (%.1f%%)\n",
                (unsigned long long)native, 100.0 * double(native) / double(retired),
                (unsigned long long)s.slowInstrs,
                100.0 * double(s.slowInstrs) / double(retired),
                (unsigned long long)(s.windowInstrs + s.interpInstrs),
                100.0 * double(s.windowInstrs + s.interpInstrs) / double(retired));
    std::printf("  window %llu armed / %llu refused \xC2\xB7 dtlb %llu filled / %llu refused"
                " \xC2\xB7 %llu flush(es), %llu from a write into translated code\n",
                (unsigned long long)s.windowArmed, (unsigned long long)s.windowFailed,
                (unsigned long long)s.dtlbFills, (unsigned long long)s.dtlbRefused,
                (unsigned long long)s.flushes, (unsigned long long)s.invalidations);

    // A flush total says what it cost; only the cause says what to do. On
    // the 68030 this line is the difference between "the emitters refuse
    // too much" and "the cache never gets to keep anything".
    if (s.flushes) {
        std::printf("  flush causes:");
        for (int i = 0; i < int(jit::Flush::Count); i++)
            if (s.flushCauses[i])
                std::printf("  %s %llu", jit::flushName(jit::Flush(i)),
                            (unsigned long long)s.flushCauses[i]);
        std::printf("\n");
    }

    // Why instructions never became generated code. Exit reasons say how a
    // block stopped; these say why one never started — the 68030's actual
    // throughput question (docs/JIT_BRINGUP.md § C.4bis).
    {
        uint64_t missed = 0;
        for (int i = 0; i < int(jit::Miss::Count); i++) missed += s.misses[i];
        if (missed) {
            std::printf("  not-native causes (instrs):");
            for (int i = 0; i < int(jit::Miss::Count); i++)
                if (s.misses[i])
                    std::printf("  %s %llu (%.1f%%)",
                                jit::missName(jit::Miss(i)),
                                (unsigned long long)s.misses[i],
                                100.0 * double(s.misses[i]) / double(retired));
            std::printf("\n");
        }
    }

    uint64_t exits = 0;
    for (int i = 0; i < int(jit::Exit::Count); i++) exits += s.exits[i];
    for (int i = 0; i < int(jit::Exit::Count); i++)
        if (s.exits[i])
            std::printf("  exit %-14s %12llu  (1 per %.1f instr)\n",
                        jit::exitName(jit::Exit(i)), (unsigned long long)s.exits[i],
                        double(retired) / double(s.exits[i]));
    if (exits)
        std::printf("  exits total    %12llu  (1 per %.1f instr)\n",
                    (unsigned long long)exits, double(retired) / double(exits));
    jit::emitMetrics(metrics);
}

}  // namespace bench
