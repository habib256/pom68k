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
// The A/B protocol and its own knobs live at the bottom of this file
// (POM68K_BENCH_COMPARE / _NULL / _WARMUP / _FLOOR, docs/MEASURING.md).

#pragma once

#include "jit/JitEngine.h"
#include "jit/JitMetrics.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <string>
#include <vector>
#include <thread>              // hardware_concurrency — the busy-host bar
#ifndef _WIN32
#include <unistd.h>            // environ — the knob stamp in `compare`
#endif

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
    // `instrs` counts native + block-fallback + window + TRACED; until the
    // tracer got its own counter the subtraction below absorbed its share
    // into "native" (5.1 % on the 68030, a third of the printed residency).
    const uint64_t notNative = s.slowInstrs + s.windowInstrs + s.traceInstrs;
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
                "window/interp %llu (%.1f%%) \xC2\xB7 tracing %llu (%.1f%%)\n",
                (unsigned long long)native, 100.0 * double(native) / double(retired),
                (unsigned long long)s.slowInstrs,
                100.0 * double(s.slowInstrs) / double(retired),
                (unsigned long long)(s.windowInstrs + s.interpInstrs),
                100.0 * double(s.windowInstrs + s.interpInstrs) / double(retired),
                (unsigned long long)s.traceInstrs,
                100.0 * double(s.traceInstrs) / double(retired));

    // The gauge proves its own sum (docs/MEASURING.md § R5): every retired
    // instruction is in exactly one bucket, and every not-native bucket is
    // attributed by exactly one Miss counter. An ECART here is a counting
    // bug in the engine, not a property of the guest.
    {
        uint64_t missSum = 0;
        for (int i = 0; i < int(jit::Miss::Count); i++) missSum += s.misses[i];
        const uint64_t attributed = s.windowInstrs + s.interpInstrs +
                                    s.traceInstrs;
        const int64_t gap = int64_t(missSum) - int64_t(attributed);
        std::printf("  identity: buckets %llu+%llu+%llu+%llu+%llu = %llu vs "
                    "retired %llu \xC2\xB7 misses %llu vs attributed %llu %s\n",
                    (unsigned long long)native,
                    (unsigned long long)s.slowInstrs,
                    (unsigned long long)s.windowInstrs,
                    (unsigned long long)s.interpInstrs,
                    (unsigned long long)s.traceInstrs,
                    (unsigned long long)(native + s.slowInstrs + s.windowInstrs +
                                         s.interpInstrs + s.traceInstrs),
                    (unsigned long long)retired,
                    (unsigned long long)missSum,
                    (unsigned long long)attributed,
                    gap == 0 ? "\xC2\xB7 OK"
                             : (std::string("\xC2\xB7 ECART ") +
                                std::to_string((long long)gap)).c_str());
    }
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

// ── The interleaved A/B (docs/MEASURING.md § 1) ───────────────────────────
// Two invocations minutes apart are NOT an A/B: a build, another session or
// a thermal state moves wall clock by more than most effects being chased.
// On 2026-08-18 that produced a published `+18 %` for a change worth −7.6 %,
// because one arm ran with a knob the other did not have. So the comparison
// lives HERE, in one process, alternating arms — and it is deliberately the
// only thing in this header that prints a delta, so a delta cannot be
// computed by eye from two separate runs.
//
//   POM68K_BENCH_COMPARE=N   N repeats PER ARM (0 = off, the plain report)
//   POM68K_BENCH_NULL=1      run arm A against ITSELF — the null experiment
//                            that MEASURES this host's floor (§ R2)
//   POM68K_BENCH_WARMUP=N    discarded pairs before the counted ones (1)
//   POM68K_BENCH_FLOOR=<pct> this host's reviewed floor, when it has no row
//                            in performance_budgets.tsv
//
// Each repeat rebuilds the machine from scratch: that is what a fresh
// process does, minus the process.
inline int compareRepeats() {
    if (const char* v = std::getenv("POM68K_BENCH_COMPARE")) {
        const int n = std::atoi(v);
        if (n > 0) return n < 3 ? 3 : n;      // R1: three is the floor
    }
    return 0;
}

// The null experiment: both arms are arm A. Whatever delta it reports is
// this host's floor and nothing else, because there is no effect in it to
// measure — so it calibrates R2's threshold instead of assuming one, and it
// is the only run that can prove the harness itself has no order bias.
inline bool nullExperiment() {
    const char* v = std::getenv("POM68K_BENCH_NULL");
    return v && *v && *v != '0';
}

// Discarded pairs. The FIRST run of anything pays a cold page cache for the
// ROM, the disk image and the code itself; counting it charges that once to
// whichever arm happened to go first. One pair (both arms) is dropped by
// default — POM68K_BENCH_WARMUP=0 for the impatient, at the price of a
// systematically slower first arm.
inline int warmupPairs() {
    if (const char* v = std::getenv("POM68K_BENCH_WARMUP")) return std::atoi(v);
    return 1;
}

// This host's reviewed noise floor, in percent. It comes from
// `performance_budgets.tsv` through CMake (`host_wallclock/any/<host>/
// noise_floor_permille`), the same way every other budget in this tree
// reaches a test: policy that was reviewed once, not a literal in a source
// file. Measure it with POM68K_BENCH_NULL=1 and write the result there.
inline double recordedFloorPercent() {
    if (const char* v = std::getenv("POM68K_BENCH_FLOOR")) return std::atof(v);
#ifdef POM68K_BENCH_NOISE_FLOOR_PERMILLE
    return POM68K_BENCH_NOISE_FLOOR_PERMILLE / 10.0;
#else
    return 0.0;                    // no row for this host: the run's own
#endif                             // spread is then the only bar
}

// When this binary was compiled. Printed beside the guest fingerprint
// because a stale binary must be VISIBLE in its own output, never inferred
// from the absence of a build line in a log: a `cd` that failed inside an
// `&&` chain once left this bench unrelinked, and its report was nearly read
// as "the change does not work" (docs/MEASURING.md § 2).
inline const char* buildStamp() { return __DATE__ " " __TIME__; }

// R1 says *name every knob that differed, including the ones you believe
// are irrelevant*. A rule addressed to a writer is a rule that gets
// forgotten in the write-up, so the RESULT carries its own knob set: every
// POM68K_* variable this process was started with, printed with the number.
// The `+18 %` was one arm running with the CACR flush armed and the other
// without; a pasted result that carries its environment cannot hide that.
inline std::string envStamp() {
#ifdef _WIN32
    return "(environment not enumerated on this host)";
#else
    std::string out;
    for (char** e = environ; e && *e; e++)
        if (std::strncmp(*e, "POM68K_", 7) == 0) {
            if (!out.empty()) out += "  ";
            out += *e;
        }
    return out.empty() ? std::string("(no POM68K_* set)") : out;
#endif
}

// What else the host was doing. A number measured while a build finished is
// not this machine's number: the 1-minute load average on 2026-08-18 fell
// from 36.6 to 0.55 in the quarter hour before a measurement, and nothing in
// the output would have said so. Printed at both ends, so a run that started
// quiet and ended loaded says it itself.
// Numeric 1-minute load, for the busy-host REFUSAL below. -1 = unknowable
// (then the stamp is all we have and the refusal cannot fire).
inline double load1() {
#ifndef _WIN32
    double la[1] = { -1.0 };
    if (getloadavg(la, 1) == 1) return la[0];
#endif
    return -1.0;
}

inline std::string loadStamp() {
#ifdef _WIN32
    return "load n/a";
#else
    double la[3] = { 0, 0, 0 };
    if (getloadavg(la, 3) < 0) return "load n/a";
    char buf[64];
    std::snprintf(buf, sizeof buf, "load %.2f %.2f %.2f", la[0], la[1], la[2]);
    return buf;
#endif
}

struct Sample { double secs = 0; uint64_t fp = 0; };

// Median, and the spread that decides whether a delta is a claim at all.
// R2: three identical runs on the 2026-08-18 x86-64 host spread 1.4 %, so
// anything under ~3 % from a single run is indistinguishable from nothing.
inline void summarise(std::vector<Sample> v, double& med,
                      double& lo, double& hi) {
    std::sort(v.begin(), v.end(),
              [](const Sample& a, const Sample& b) { return a.secs < b.secs; });
    med = v[v.size() / 2].secs;
    lo = v.front().secs;
    hi = v.back().secs;
}

inline double spreadPercent(double med, double lo, double hi) {
    return med > 0 ? 100.0 * (hi - lo) / med : 0.0;
}

// `run(arm)` must build a fresh machine, run the fixed budget and return its
// wall clock and architectural fingerprint. Arm 0 is the reference.
//
// The order is **ABBA**, not ABAB. Under a host that drifts one way — a
// thermal ramp, a cache filling, another job winding down — ABAB gives every
// B slot a later position than its paired A, so the drift lands entirely on
// one arm and reads as an effect. ABBA pairs each arm once early and once
// late, and first-order drift cancels. It costs nothing and it is the
// difference between measuring a change and measuring the afternoon.
template <class Fn>
int compare(const char* machine, const char* armA, const char* armB, Fn&& run) {
    const int n = compareRepeats();
    const bool nul = nullExperiment();
    const int bArm = nul ? 0 : 1;             // the null experiment runs A twice
    const char* labelB = nul ? "null(A)" : armB;

    for (int w = 0; w < warmupPairs(); w++) {   // discarded: cold page cache
        run(0);
        run(bArm);
        std::printf("  %s warmup pair %d discarded\n", machine, w + 1);
    }

    const std::string loadBefore = loadStamp();
    const double loadNumBefore = load1();
    std::vector<Sample> a, b;
    for (int i = 0; i < n; i++) {
        const bool aFirst = (i % 2) == 0;       // A B / B A / A B …
        if (aFirst) { a.push_back(run(0)); b.push_back(run(bArm)); }
        else        { b.push_back(run(bArm)); a.push_back(run(0)); }
        std::printf("  %s pair%-2d %-8s %6.2fs   %-8s %6.2fs   (%s first)\n",
                    machine, i + 1, armA, a.back().secs, labelB, b.back().secs,
                    aFirst ? armA : labelB);
    }
    const std::string loadAfter = loadStamp();
    const double loadNumAfter = load1();

    double aMed, aLo, aHi, bMed, bLo, bHi;
    summarise(a, aMed, aLo, aHi);
    summarise(b, bMed, bLo, bHi);

    // A timing claim whose fingerprint moved is not a timing claim: the two
    // arms ran different programs. Say so instead of printing a delta.
    bool same = true;
    for (const Sample& s : a) same = same && s.fp == a.front().fp;
    for (const Sample& s : b) same = same && s.fp == a.front().fp;

    // The bar is the WIDEST evidence of noise available, never the narrowest:
    // each arm's own spread, and this host's reviewed floor. Taking arm A's
    // spread alone is how the POM68K_JIT_ARM_BACKOFF "2.9 % win" survived a
    // first re-measurement — the arm that moved was B.
    const double spreadA = spreadPercent(aMed, aLo, aHi);
    const double spreadB = spreadPercent(bMed, bLo, bHi);
    const double recorded = recordedFloorPercent();
    double floorPct = spreadA > spreadB ? spreadA : spreadB;
    if (recorded > floorPct) floorPct = recorded;
    const double delta = aMed > 0 ? 100.0 * (bMed - aMed) / aMed : 0.0;

    std::printf("\n  %-8s median %6.2fs  (%.2f-%.2f, spread %.1f%%)\n",
                armA, aMed, aLo, aHi, spreadA);
    std::printf("  %-8s median %6.2fs  (%.2f-%.2f, spread %.1f%%)\n",
                labelB, bMed, bLo, bHi, spreadB);
    if (!same) {
        std::printf("  FINGERPRINT MOVED — the arms ran different programs; "
                    "this is not a timing result\n");
        return 1;
    }
    std::printf("  delta %+.1f%%  ·  floor %.1f%% (arm spreads %.1f/%.1f%%, "
                "host %.1f%%)  ·  %d repeats ABBA\n",
                delta, floorPct, spreadA, spreadB, recorded, n);
    std::printf("  fp=%016llx  ·  built %s  ·  %s -> %s\n",
                (unsigned long long)a.front().fp, buildStamp(),
                loadBefore.c_str(), loadAfter.c_str());
    std::printf("  knobs: %s\n", envStamp().c_str());

    // The busy-host refusal. The load stamp above was a footnote until a
    // compile ran concurrently with a measurement session and nothing
    // objected (2026-08-18, pointed out by the reader, not the tool — the
    // worst way to learn it). A stamp someone must notice is not a guard:
    // if the 1-minute load at either end exceeds a quarter of the hardware
    // threads, the arms did not get the machine to themselves and NOTHING
    // above is a claim — including a null experiment's floor, which would
    // freeze the neighbour's build into policy. Printed after the numbers,
    // so the run is not wasted; returned as failure, so it cannot be quoted
    // by a script that only checks the exit code.
    {
        const double threads = double(std::thread::hardware_concurrency());
        const double busyBar = threads > 4.0 ? threads / 4.0 : 1.0;
        const double busiest = loadNumBefore > loadNumAfter ? loadNumBefore
                                                            : loadNumAfter;
        if (busiest > busyBar) {
            std::printf("  HOST BUSY: 1-min load %.2f against a bar of %.1f "
                        "(%d hardware threads / 4).\n  Another process had "
                        "the machine during this run — every number above is "
                        "PROVISIONAL.\n  Re-run when the host is quiet; do "
                        "not record, quote or revert anything on this one.\n",
                        busiest, busyBar,
                        int(std::thread::hardware_concurrency()));
            return 1;
        }
    }

    if (nul) {
        // Nothing was compared, so everything this run produced is the
        // instrument's own noise. **Measured from the run alone**: the two
        // arm spreads and the delta, and deliberately NOT `floorPct`, which
        // already carries the recorded policy. Folding policy back into its
        // own calibration is a circle that can only ever confirm the number
        // already written down — the first version of these six lines did
        // exactly that and answered "record 3.0 %" to a host that had just
        // demonstrated 0.3 % (docs/MEASURING.md § 3: an instrument can be
        // perfectly precise about the wrong quantity).
        const double observed = delta < 0 ? -delta : delta;
        double measured = spreadA > spreadB ? spreadA : spreadB;
        if (observed > measured) measured = observed;
        std::printf("  NULL EXPERIMENT: nothing differed between the arms, so "
                    "%.1f%% is this host's MEASURED floor,\n"
                    "  not a result. Record %.0f permille as "
                    "host_wallclock/any/<host>/noise_floor_permille "
                    "(policy says %.1f%%).\n",
                    measured, measured * 10.0 + 0.5, recorded);
        if (recorded > 0 && measured > recorded) {
            std::printf("  HOST NOISIER THAN POLICY: %.1f%% measured against a "
                        "recorded %.1f%%. Either this host is busy (see the "
                        "load above)\n  or the recorded floor is stale — do "
                        "not measure anything else until that is settled.\n",
                        measured, recorded);
            return 1;
        }
        if (recorded > 0 && measured * 2.0 < recorded)
            // The symmetric error, and the one nobody looks for: a floor set
            // too HIGH silently buries every real effect under it. A rule
            // that forbids claims below 3 % on a host that resolves 0.5 %
            // does not protect the tree from noise, it protects noise from
            // being contradicted.
            std::printf("  POLICY TOO LOOSE: the recorded %.1f%% is more than "
                        "twice what this host actually does (%.1f%%). It is "
                        "burying\n  real effects between the two. Re-record "
                        "it — a floor is a measurement, not a safety margin."
                        "\n", recorded, measured);
        return 0;
    }

    const double magnitude = delta < 0 ? -delta : delta;
    if (magnitude <= floorPct)
        // Two-sided on purpose. A regression inside the floor is exactly as
        // unreal as a win inside it, and it is the more expensive mistake:
        // a win that is noise wastes a paragraph, a regression that is noise
        // reverts a good change.
        std::printf("  NOT A CLAIM: |delta| %.1f%% is inside this host's noise "
                    "floor of %.1f%% (docs/MEASURING.md § R2).\n"
                    "  Raise POM68K_BENCH_FRAMES or the repeat count; do not "
                    "raise your confidence.\n", magnitude, floorPct);
    return 0;
}

}  // namespace bench
