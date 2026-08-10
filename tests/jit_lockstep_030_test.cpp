// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// JIT gate, 68030 flavour: the differential coverage the 030 machines have
// never had. Two identical Mac LC II machines are built from the same ROM
// and the same read-only disk image, one driven by the Moira interpreter
// and one by the JIT engine, and stepped from power-up while every
// architectural register, the three stacks, the cycle clock, the low RAM
// globals AND the 68030 instruction-cache counters are compared at each
// boundary.
//
// WHY IT EXISTS. `jit_lockstep_test` and its variants all run two Quadra
// 605s, so until this file the 68030 side of the engine had no
// instruction-level comparison at all — and that gap has already been paid
// for once: on 2026-07-30 the x86-64 code generator was handed the 030
// machines by `auto` and wedged the LC II in the ROM's Egret handshake,
// where `jit_lcii_boot_etalon` timed out after an HOUR while the same
// machine booted in 2 min 21 s on `threaded`. A boot etalon can only say
// "it did not arrive"; this says which instruction.
//
// THE I-CACHE IS PART OF THE COMPARISON, and it is the reason this gate is
// not just the Quadra one with a different machine class. Moira charges the
// 68030's on-chip i-cache overlay inside mmuFetchWord, BEFORE the JIT code
// window hook (MoiraExecMMU_cpp.h:421-454), and the threaded backend
// replays through Moira's own handlers — so today both engines charge it
// identically, and `clock` alone would catch a divergence. That stops being
// automatic the moment a code generator emits 030 blocks: generated code
// fetches no instructions and would charge nothing at all. Comparing
// fetches/hits/misses directly says WHICH of the two broke, instead of
// leaving a clock drift to be explained. See docs/JIT_BRINGUP.md § B.
//
// The LC II has no host-clock or host-entropy dependency, so two instances
// in one process are bit-for-bit reproducible.
//
// Soft-skips (exit 0) when the user-provided ROM is absent.

#include "AssetFingerprint.h"
#include "Cpu030.h"
#include "V8Memory.h"
#include "jit/JitConfig.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <initializer_list>
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

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>() };
}

// The LC II ROM's boot scan ($A07264) only loads a driver descriptor whose
// ddType is $6A, and images made by other tools usually carry $0001. The
// same non-destructive in-memory patch lcii_boot_etalon and jit_bench_lcii
// apply — and it has to be applied IDENTICALLY to both machines, which is
// why it is here rather than left to whichever one touches the image first.
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

struct State {
    uint32_t d[8], a[8], pc, usp, isp, msp;
    uint16_t sr;
    int64_t  clock;
};

State capture(const moira::Moira& cpu) {
    State s{};
    for (int i = 0; i < 8; i++) { s.d[i] = cpu.getD(i); s.a[i] = cpu.getA(i); }
    s.pc = cpu.getPC();
    s.usp = cpu.getUSP();
    s.isp = cpu.getISP();
    s.msp = cpu.getMSP();
    s.sr = cpu.getSR();
    s.clock = cpu.getClock();
    return s;
}

bool same(const State& x, const State& y) {
    return std::memcmp(&x, &y, sizeof(State)) == 0;
}

bool same(const Cpu030::ICacheStats& x, const Cpu030::ICacheStats& y) {
    return x.fetches == y.fetches && x.hits == y.hits && x.misses == y.misses;
}

// Registers are not the whole architectural state, and a JIT bug in a STORE
// only shows up in a register much later — when something reads the byte
// back. The 68k system globals live in the first 2 KB and are written
// constantly during a boot, which makes them a cheap, high-yield tripwire.
//
// On the V8 machines peek8() is PHYSICAL, and physical low RAM on an LC II
// is ordinary RAM (unlike the RBV boards, where it IS the framebuffer), so
// this window means what it says here.
constexpr uint32_t kWatchBytes = 2048;
uint32_t ramDiff(const V8Memory& a, const V8Memory& b) {
    for (uint32_t i = 0; i < kWatchBytes; i++)
        if (a.peek8(i) != b.peek8(i)) return i;
    return 0xFFFFFFFF;
}

}  // namespace

int main(int argc, char** argv) {
    // CALIBRATION, and it is not a taste question. An LC II reaches the
    // Finder at ~4.17 G machine cycles (16.7 G core at boost 4), so a
    // fine-grained walk cannot get anywhere near live code inside a gate's
    // time. The first version of this file ran 2 M steps at 256 cycles and
    // measured: 974 108 window arms, 965 562 of them REFUSED, and 2.6 % of
    // instructions on the JIT path — it never left the ROM power-on self
    // test, where the overlay and the MMU make the window unarmable. Over a
    // full `jit_bench_lcii` the same engine runs 100 % of instructions
    // through the window with a 2.7 % arm failure rate, so that 99 % was an
    // artefact of WHERE the gate was, not of what it was testing.
    //
    // At 8192 cycles per comparison the same budget of wall clock reaches
    // 89 % of instructions on the JIT path with a 5.6 % arm failure rate.
    // The coarse phase buys REACH; POM68K_JIT_LOCKSTEP_FINE_AT then drops
    // the tail to one cycle per comparison, which is what names an
    // instruction rather than a 8192-cycle window.
    long steps = 120'000;
    if (const char* n = std::getenv("POM68K_JIT_LOCKSTEP_N")) steps = std::atol(n);
    if (argc > 1) steps = std::atol(argv[1]);

    const std::string romPath = findAsset({
        "roms/512KB ROMs/1992-03 - 35C28F5F - Mac LC II.ROM",
        "roms/maclcii.rom"
    });
    if (romPath.empty()) {
        std::printf("[jit_lockstep_030] no Mac LC II ROM — soft skip\n");
        return 0;
    }
    const std::vector<uint8_t> rom = readFile(romPath);
    if (rom.size() < V8Memory::kRomSize) {
        std::printf("[jit_lockstep_030] ROM too small (%zu) — soft skip\n", rom.size());
        return 0;
    }

    // The engine is chosen per CPU object, so both machines live in one
    // process. Neither reads POM68K_CPU_ENGINE afterwards.
    static V8Memory memRef, memJit;
    static Cpu030 cpuRef(memRef, /*withFpu=*/true), cpuJit(memJit, /*withFpu=*/true);
    memRef.setCpu(&cpuRef);
    memJit.setCpu(&cpuJit);
    if (!memRef.loadRom(rom) || !memJit.loadRom(rom)) {
        std::printf("[jit_lockstep_030] loadRom failed — soft skip\n");
        return 0;
    }
    // The ROM alone only reaches the power-on self test, where every access
    // is an I/O register and the data path is never exercised. Attach the
    // boot disk when it is there — READ-ONLY, so both machines see identical
    // media — and the comparison then covers the System loading, the Finder,
    // and the whole SCSI/PMMU path with it.
    const std::string diskPath = findAsset({ "hdv/lcii-boot.vhd", "hdv/boot.vhd",
                                             "hdv/GISTPERSO-boot.vhd" });
    if (!diskPath.empty()) {
        memRef.attachScsi(diskPath);
        memJit.attachScsi(diskPath);
        ensureBootDriverType(memRef.scsiDisk().image());
        ensureBootDriverType(memJit.scsiDisk().image());
    }
    testasset::report({ romPath, diskPath });
    std::printf("[jit_lockstep_030] disk=%s\n",
                diskPath.empty() ? "(none — ROM POST only)" : diskPath.c_str());

    cpuRef.setEngine(0);
    cpuJit.setEngine(1);
    cpuRef.hardReset();
    cpuJit.hardReset();
    // The Egret holds the CPU in reset until its firmware has come up. Both
    // machines have to be released before the comparison starts, or the gate
    // spends its whole budget in the power-on self test and never reaches an
    // instruction the engine cares about — the exact way the Quadra lockstep
    // reported a green data path it had never exercised (CHANGELOG
    // 2026-07-28).
    while (memRef.cpuHeld()) memRef.tick(1000);
    while (memJit.cpuHeld()) memJit.tick(1000);

    std::printf("[jit_lockstep_030] rom=%s backend=%s steps=%ld\n",
                romPath.c_str(), cpuJit.jit().backendName(), steps);

    // Cycles of budget per checkpoint. One means the engines are compared
    // after (almost) every instruction — the sharpest test, and also a cap
    // of one instruction per compiled block. A larger budget lets blocks run
    // their full length and still leaves both engines stopping at the SAME
    // boundary, because both stop at the first one past the target.
    long budget = 8192;
    if (const char* b = std::getenv("POM68K_JIT_LOCKSTEP_BUDGET")) budget = std::atol(b);
    if (budget < 1) budget = 1;
    long fineAt = -1;
    if (const char* f = std::getenv("POM68K_JIT_LOCKSTEP_FINE_AT")) fineAt = std::atol(f);
    // What "fine" means, and it is NOT 1 by default here the way it is on
    // the interpreter-only gates. A one-cycle budget caps a block at one
    // instruction — and worse, it never gives the engine enough room to
    // build one at all: measured, a whole 60 000-step fine run compiled
    // nothing and retired ZERO JIT instructions, so it compared the
    // interpreter with itself and said "identical". A code generator cannot
    // be bisected at a budget it never wakes up for. 64 cycles is a handful
    // of instructions: sharp enough to name one, wide enough to be running.
    long fineBudget = 64;
    if (const char* f = std::getenv("POM68K_JIT_LOCKSTEP_FINE_BUDGET"))
        fineBudget = std::atol(f);
    if (fineBudget < 1) fineBudget = 1;
    std::printf("[jit_lockstep_030] budget=%ld cycle(s) per comparison", budget);
    if (fineAt >= 0) std::printf(", %ld from step %ld", fineBudget, fineAt);
    std::printf("\n");

    // The last handful of boundaries, for the report: a divergence is almost
    // never AT the pc it is noticed at.
    constexpr int kTrail = 8;
    struct Step { uint32_t pc; int64_t clock; } trail[kTrail] = {};
    int trailAt = 0;

    // POM68K_JIT_LOCKSTEP_TRACE_FROM=N — print pc and clock for BOTH machines
    // at every checkpoint from step N. The trail printed on divergence only
    // covers the JIT side, and a divergence where the two engines end a step
    // with the SAME clock at DIFFERENT pcs cannot be read from one side
    // alone: it says the costs differed earlier and happened to re-converge,
    // which is exactly the case this gate first met.
    long traceFrom = -1;
    if (const char* t = std::getenv("POM68K_JIT_LOCKSTEP_TRACE_FROM"))
        traceFrom = std::atol(t);

    for (long i = 0; i < steps; i++) {
        const long b = (fineAt >= 0 && i >= fineAt) ? fineBudget : budget;
        if (traceFrom >= 0 && i >= traceFrom) {
            std::printf("[trace] %6ld  interp pc=$%08X clk=%lld   jit pc=$%08X clk=%lld%s\n",
                        i, cpuRef.getPC(), (long long)cpuRef.getClock(),
                        cpuJit.getPC(), (long long)cpuJit.getClock(),
                        cpuRef.getPC() == cpuJit.getPC() &&
                        cpuRef.getClock() == cpuJit.getClock() ? "" : "   <-- APART");
        }
        trail[trailAt] = { cpuJit.getPC(), cpuJit.getClock() };
        trailAt = (trailAt + 1) % kTrail;
        cpuRef.runCycles(b);
        cpuJit.runCycles(b);

        const State r = capture(cpuRef);
        const State j = capture(cpuJit);
        const uint32_t bad = ramDiff(memRef, memJit);
        const Cpu030::ICacheStats icr = cpuRef.icacheStats();
        const Cpu030::ICacheStats icj = cpuJit.icacheStats();
        if (same(r, j) && bad == 0xFFFFFFFF && same(icr, icj)) continue;

        if (!same(icr, icj)) {
            std::printf("[jit_lockstep_030] 68030 i-cache accounting diverged\n");
            std::printf("  fetches interp=%lld jit=%lld\n",
                        (long long)icr.fetches, (long long)icj.fetches);
            std::printf("  hits    interp=%lld jit=%lld\n",
                        (long long)icr.hits, (long long)icj.hits);
            std::printf("  misses  interp=%lld jit=%lld\n",
                        (long long)icr.misses, (long long)icj.misses);
            std::printf("  (a code generator that emits 030 blocks must charge\n"
                        "   the overlay itself — docs/JIT_BRINGUP.md § B)\n");
        }
        if (bad != 0xFFFFFFFF)
            std::printf("[jit_lockstep_030] RAM differs at $%08X: interp=%02X jit=%02X\n",
                        bad, memRef.peek8(bad), memJit.peek8(bad));

        std::printf("[jit_lockstep_030] DIVERGED after %ld steps\n", i);
        std::printf("  last boundaries (jit): ");
        for (int k = 0; k < kTrail; k++) {
            const Step& t = trail[(trailAt + k) % kTrail];
            if (t.pc) std::printf("$%08X@%lld ", t.pc, (long long)t.clock);
        }
        std::printf("\n");
        std::printf("  pc    interp=%08X  jit=%08X\n", r.pc, j.pc);
        std::printf("  sr    interp=%04X      jit=%04X\n", r.sr, j.sr);
        std::printf("  clock interp=%lld      jit=%lld\n",
                    (long long)r.clock, (long long)j.clock);
        for (int k = 0; k < 8; k++)
            if (r.d[k] != j.d[k])
                std::printf("  D%d    interp=%08X  jit=%08X\n", k, r.d[k], j.d[k]);
        for (int k = 0; k < 8; k++)
            if (r.a[k] != j.a[k])
                std::printf("  A%d    interp=%08X  jit=%08X\n", k, r.a[k], j.a[k]);
        if (r.usp != j.usp) std::printf("  USP   interp=%08X  jit=%08X\n", r.usp, j.usp);
        if (r.isp != j.isp) std::printf("  ISP   interp=%08X  jit=%08X\n", r.isp, j.isp);
        if (r.msp != j.msp) std::printf("  MSP   interp=%08X  jit=%08X\n", r.msp, j.msp);
        return 1;
    }

    // A green gate that exercised nothing is worse than a red one.
    const jit::Stats::Snapshot s = cpuJit.jit().stats().snapshot();
    const Cpu030::ICacheStats ic = cpuJit.icacheStats();
    std::printf("[jit_lockstep_030] OK — %ld steps identical\n", steps);
    std::printf("  jit instrs %llu · interp fallback %llu · blocks %llu compiled"
                " / %llu replayed · window %llu armed (%llu refused)"
                " · dtlb %llu filled\n",
                (unsigned long long)s.instrs,
                (unsigned long long)s.interpInstrs,
                (unsigned long long)s.blocksCompiled,
                (unsigned long long)s.blocksRun,
                (unsigned long long)s.windowArmed,
                (unsigned long long)s.windowFailed,
                (unsigned long long)s.dtlbFills);
    std::printf("  icache %lld fetches / %lld hits / %lld misses (identical on both)\n",
                (long long)ic.fetches, (long long)ic.hits, (long long)ic.misses);
    if (s.instrs == 0) {
        std::printf("[jit_lockstep_030] FAIL: the JIT never retired an instruction "
                    "— this gate proved nothing\n");
        return 1;
    }
    // …and the sharper version of the same rule, which the first run of this
    // file failed: a gate can sit in the ROM power-on self test, compare two
    // interpreters for its whole budget and report green. Both thresholds
    // are deliberately loose — the healthy figures are 89 % of instructions
    // on the JIT path and a 5.6 % arm failure rate, the POST-only ones were
    // 2.6 % and 99.1 %, so anything in between is already a red flag worth
    // looking at rather than a boundary to tune.
    if (s.instrs < s.interpInstrs) {
        std::printf("[jit_lockstep_030] FAIL: only %llu of %llu instructions ran on "
                    "the JIT path — the gate never left the ROM self test\n",
                    (unsigned long long)s.instrs,
                    (unsigned long long)(s.instrs + s.interpInstrs));
        return 1;
    }
    if (s.windowArmed && s.windowFailed * 2 > s.windowArmed) {
        std::printf("[jit_lockstep_030] FAIL: %llu of %llu code-window arms refused "
                    "— the engine was never usable here\n",
                    (unsigned long long)s.windowFailed,
                    (unsigned long long)s.windowArmed);
        return 1;
    }
    // The i-cache comparison is only worth something if the cache was ON.
    // The LC II's System enables it via CACR early in the boot; a run that
    // never got there compared zero against zero.
    if (ic.fetches == 0) {
        std::printf("[jit_lockstep_030] FAIL: the 68030 i-cache overlay never "
                    "counted a fetch — the i-cache half proved nothing\n");
        return 1;
    }
    if (jit::blockCacheEnabled(cpuJit.jit().nativeBackend()) && s.blocksRun == 0) {
        std::printf("[jit_lockstep_030] FAIL: POM68K_JIT_BLOCKS is on but no block "
                    "was ever replayed — the block path proved nothing\n");
        return 1;
    }
    return 0;
}
