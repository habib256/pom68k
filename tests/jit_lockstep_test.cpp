// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// JIT gate: the decisive one. Two identical Quadra 605 machines are built
// from the same ROM, one driven by the Moira interpreter and one by the JIT
// engine, and stepped one instruction at a time from power-up while EVERY
// architectural register, the supervisor stacks and the cycle clock are
// compared at each boundary.
//
// This is the "differential-test every compiled block against the
// oracle-converged interpreter" that TODO.md asks for, and it is what
// invariant 1 of src/jit/POM68K_JIT.md rests on: the interpreter is the
// reference, any divergence is a JIT bug. It also serves every future
// backend unchanged — POM68K_JIT_BACKEND selects what gets compared.
//
// The Quadra 605 machine has no host-clock or host-entropy dependency
// (nothing in Q605Memory/CudaLle reads the wall clock), so two instances in
// one process are bit-for-bit reproducible; the ROM alone drives them.
//
// Soft-skips (exit 0) when the user-provided ROM is absent.

#include "AssetFingerprint.h"
#include "Cpu040.h"
#include "Q605Memory.h"
#include "jit/JitConfig.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>() };
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

// Registers are not the whole architectural state, and a JIT bug in a STORE
// shows up in a register only much later — when something reads the byte
// back. The 68k system globals live in the first 2 KB of RAM and are written
// constantly during a boot, which makes them a cheap, high-yield tripwire.
constexpr uint32_t kWatchBytes = 2048;
uint32_t ramDiff(const Q605Memory& a, const Q605Memory& b) {
    for (uint32_t i = 0; i < kWatchBytes; i++)
        if (a.peek8(i) != b.peek8(i)) return i;
    return 0xFFFFFFFF;
}

bool same(const Cpu040::LockstepDebug& a, const Cpu040::LockstepDebug& b) {
    return a.clock == b.clock &&
           a.lastPeriphClock == b.lastPeriphClock &&
           a.periphAccum == b.periphAccum &&
           a.periphDeadline == b.periphDeadline &&
           a.iplChangeClock == b.iplChangeClock &&
           a.iplChangeClockPrev == b.iplChangeClockPrev &&
           a.flags == b.flags && a.iplDeferred == b.iplDeferred &&
           a.irqDelay == b.irqDelay && a.iplPin == b.iplPin &&
           a.iplSampled == b.iplSampled && a.iplPrev == b.iplPrev;
}

bool same(const Q605Memory::LockstepDebug& a,
          const Q605Memory::LockstepDebug& b) {
    return a.via1 == b.via1 && a.cuda == b.cuda &&
           a.cudaLle == b.cudaLle && a.adb == b.adb &&
           a.scc == b.scc && a.asc == b.asc && a.swim == b.swim &&
           a.scsi == b.scsi && a.dafb == b.dafb &&
           a.machine == b.machine && a.nextEvent == b.nextEvent &&
           a.ipl == b.ipl;
}

void printHiddenDiff(const Cpu040::LockstepDebug& r,
                     const Cpu040::LockstepDebug& j,
                     const Q605Memory::LockstepDebug& mr,
                     const Q605Memory::LockstepDebug& mj) {
    auto cpu64 = [](const char* name, int64_t a, int64_t b) {
        if (a != b) std::printf("  %-18s interp=%lld jit=%lld\n", name,
                                (long long)a, (long long)b);
    };
    auto cpu32 = [](const char* name, int a, int b) {
        if (a != b) std::printf("  %-18s interp=%d jit=%d\n", name, a, b);
    };
    cpu64("clock", r.clock, j.clock);
    cpu64("lastPeriphClock", r.lastPeriphClock, j.lastPeriphClock);
    cpu64("periphAccum", r.periphAccum, j.periphAccum);
    cpu64("periphDeadline", r.periphDeadline, j.periphDeadline);
    cpu64("iplChangeClock", r.iplChangeClock, j.iplChangeClock);
    cpu64("iplChangePrev", r.iplChangeClockPrev, j.iplChangeClockPrev);
    cpu32("flags", r.flags, j.flags);
    cpu32("iplDeferred", r.iplDeferred, j.iplDeferred);
    cpu32("irqDelay", r.irqDelay, j.irqDelay);
    cpu32("iplPin", r.iplPin, j.iplPin);
    cpu32("iplSampled", r.iplSampled, j.iplSampled);
    cpu32("iplPrev", r.iplPrev, j.iplPrev);
    cpu32("memory IPL", mr.ipl, mj.ipl);
    cpu32("next event", mr.nextEvent, mj.nextEvent);
    cpu32("VIA2 IFR", mr.pvIfr, mj.pvIfr);
    cpu32("VIA2 IER", mr.pvIer, mj.pvIer);

    auto hash = [](const char* name, uint64_t a, uint64_t b) {
        if (a != b) std::printf("  %-18s interp=%016llX jit=%016llX\n", name,
                                (unsigned long long)a, (unsigned long long)b);
    };
    hash("VIA1 state", mr.via1, mj.via1);
    hash("Cuda HLE state", mr.cuda, mj.cuda);
    hash("Cuda LLE state", mr.cudaLle, mj.cudaLle);
    hash("ADB state", mr.adb, mj.adb);
    hash("SCC state", mr.scc, mj.scc);
    hash("ASC state", mr.asc, mj.asc);
    hash("SWIM state", mr.swim, mj.swim);
    hash("SCSI state", mr.scsi, mj.scsi);
    hash("DAFB state", mr.dafb, mj.dafb);
    hash("machine pacing", mr.machine, mj.machine);
    cpu64("ASC accumulator", mr.ascCycAcc, mj.ascCycAcc);
    cpu64("SWIM last CPU", mr.swimLastCpu, mj.swimLastCpu);
    cpu64("SWIM accumulator", mr.swimCycAcc, mj.swimCycAcc);
    cpu64("CA1 accumulator", mr.tickAcc, mj.tickAcc);
    cpu32("SCC IRQ latch", mr.sccIrq, mj.sccIrq);
    cpu32("ASC IRQ line", mr.ascLine, mj.ascLine);
}

}  // namespace

int main(int argc, char** argv) {
    long steps = 5'000'000;
    if (const char* n = std::getenv("POM68K_JIT_LOCKSTEP_N")) steps = std::atol(n);
    if (argc > 1) steps = std::atol(argv[1]);

    const std::string romPath = findAsset({
        "roms/1MB ROMs/1993-10 - FF7439EE - LC475,575,Quadra 605,Performa 475,476,575,577,578.ROM",
        "roms/mame/macqd605/ff7439ee.bin",
        "roms/quadra605.rom", "roms/q605.rom"
    });
    if (romPath.empty()) {
        std::printf("[jit_lockstep] no Quadra 605 ROM — soft skip\n");
        return 0;
    }
    const std::vector<uint8_t> rom = readFile(romPath);
    if (rom.size() < Q605Memory::kRomSize) {
        std::printf("[jit_lockstep] ROM too small (%zu) — soft skip\n", rom.size());
        return 0;
    }

    // The engine is chosen per CPU object, so both machines can live in one
    // process. Neither reads POM68K_CPU_ENGINE afterwards.
    static Q605Memory memRef, memJit;
    static Cpu040 cpuRef(memRef), cpuJit(memJit);
    memRef.setCpu(&cpuRef);
    memJit.setCpu(&cpuJit);
    if (!memRef.loadRom(rom) || !memJit.loadRom(rom)) {
        std::printf("[jit_lockstep] loadRom failed — soft skip\n");
        return 0;
    }
    // The ROM alone only ever reaches the power-on self test, where every
    // memory access is an I/O register and the JIT's inline data path is
    // never exercised. Attach the boot disk when it is there — READ-ONLY, so
    // both machines see identical media — and the comparison then covers the
    // System loading, the Finder, and the whole SCSI/paging path with it.
    const std::string diskPath = findAsset({ "hdv/MacOS-8.1-boot.vhd",
                                             "hdv/q605-boot.vhd" });
    if (!diskPath.empty()) {
        memRef.attachScsi(diskPath);
        memJit.attachScsi(diskPath);
    }
    testasset::report({ romPath, diskPath });
    std::printf("[jit_lockstep] disk=%s\n",
                diskPath.empty() ? "(none — ROM POST only)" : diskPath.c_str());

    cpuRef.setEngine(0);
    cpuJit.setEngine(1);
    cpuRef.hardReset();
    cpuJit.hardReset();
    // The Cuda holds the CPU in reset until its firmware has come up. Both
    // machines have to be released before the comparison starts, or this
    // gate spends its whole budget in the power-on self test and never sees
    // an instruction the code generator cares about. (It did exactly that
    // until 2026-07-28 — the JIT's data path was reported green by a run
    // that had never performed a single data-TLB fill.)
    while (memRef.cpuHeld()) memRef.tick(1000);
    while (memJit.cpuHeld()) memJit.tick(1000);

    std::printf("[jit_lockstep] rom=%s backend=%s steps=%ld\n",
                romPath.c_str(), cpuJit.jit().backendName(), steps);

    // Cycles of budget per checkpoint. One means the two engines are
    // compared after (almost) every instruction, which is the sharpest
    // possible test but also caps a compiled block at one instruction. A
    // larger budget lets blocks run their full length — including a loop
    // closing on itself entirely inside generated code — and still leaves
    // both engines stopping at the SAME boundary, because both stop at the
    // first one past the target.
    long budget = 1;
    if (const char* b = std::getenv("POM68K_JIT_LOCKSTEP_BUDGET")) budget = std::atol(b);
    if (budget < 1) budget = 1;
    // Getting deep into a boot at one cycle per comparison takes far longer
    // than anyone will wait, but a coarse budget only says WHICH 256 cycles
    // diverged. So: run coarse to get there, then drop to one cycle per
    // comparison for the last stretch, which names the instruction.
    long fineAt = -1;
    if (const char* f = std::getenv("POM68K_JIT_LOCKSTEP_FINE_AT")) fineAt = std::atol(f);
    std::printf("[jit_lockstep] budget=%ld cycle(s) per comparison%s\n", budget,
                fineAt >= 0 ? " (fine from the marked step)" : "");
    const bool hidden = std::getenv("POM68K_JIT_LOCKSTEP_HIDDEN") != nullptr;
    if (hidden) std::printf("[jit_lockstep] hidden CPU/device state comparison enabled\n");
    long traceAt = -1;
    if (const char* t = std::getenv("POM68K_JIT_LOCKSTEP_TRACE_AT")) traceAt = std::atol(t);
    long activeStep = -1;
    auto trace = [&](const char* who) {
        return [&, who](const char* event, const Cpu040::LockstepDebug& d) {
            if (activeStep != traceAt) return;
            const Cpu040& cpu = std::strcmp(who, "interp") == 0 ? cpuRef : cpuJit;
            std::printf("[jit_lockstep] trace step=%ld %-6s %-5s "
                        "pc=$%08X clk=%lld last=%lld acc=%lld deadline=%lld "
                        "D3=$%08X SR=$%04X flags=$%X pin=%u sampled=%u "
                        "memIpl=%d next=%d\n",
                        activeStep, who, event,
                        cpu.getPC(),
                        (long long)d.clock, (long long)d.lastPeriphClock,
                        (long long)d.periphAccum, (long long)d.periphDeadline,
                        cpu.getD(3), cpu.getSR(), d.flags, d.iplPin, d.iplSampled,
                        std::strcmp(who, "interp") == 0 ? memRef.iplLevel() : memJit.iplLevel(),
                        std::strcmp(who, "interp") == 0 ? memRef.cyclesToNextEvent()
                                                        : memJit.cyclesToNextEvent());
        };
    };
    if (traceAt >= 0) {
        cpuRef.onLockstepEvent = trace("interp");
        cpuJit.onLockstepEvent = trace("jit");
    }

    // The last handful of instruction boundaries, for the report: a
    // divergence is almost never AT the pc it is noticed at.
    constexpr int kTrail = 8;
    struct Step { uint32_t pc; int64_t clock; } trail[kTrail] = {};
    int trailAt = 0;

    for (long i = 0; i < steps; i++) {
        activeStep = i;
        if (i == traceAt) {
            uint32_t fullBad = 0xFFFFFFFF;
            for (uint32_t p = 0; p < memRef.ramBytes(); ++p) {
                if (memRef.peek8(p) != memJit.peek8(p)) { fullBad = p; break; }
            }
            if (fullBad == 0xFFFFFFFF) {
                std::printf("[jit_lockstep] full RAM identical before traced step %ld\n", i);
            } else {
                std::printf("[jit_lockstep] full RAM already differs before step %ld "
                            "at $%08X: interp=%02X jit=%02X\n", i, fullBad,
                            memRef.peek8(fullBad), memJit.peek8(fullBad));
            }
        }
        const long b = (fineAt >= 0 && i >= fineAt) ? 1 : budget;
        trail[trailAt] = { cpuJit.getPC(), cpuJit.getClock() };
        trailAt = (trailAt + 1) % kTrail;
        cpuRef.runCycles(b);
        cpuJit.runCycles(b);

        const State r = capture(cpuRef);
        const State j = capture(cpuJit);
        const uint32_t bad = ramDiff(memRef, memJit);
        Cpu040::LockstepDebug cr{}, cj{};
        Q605Memory::LockstepDebug mr{}, mj{};
        bool hiddenSame = true;
        if (hidden) {
            cr = cpuRef.lockstepDebug();
            cj = cpuJit.lockstepDebug();
            mr = memRef.lockstepDebug();
            mj = memJit.lockstepDebug();
            hiddenSame = same(cr, cj) && same(mr, mj);
        }
        if (same(r, j) && bad == 0xFFFFFFFF && hiddenSame) continue;
        if (!hiddenSame) {
            std::printf("[jit_lockstep] first hidden CPU/device divergence\n");
            printHiddenDiff(cr, cj, mr, mj);
        }
        if (bad != 0xFFFFFFFF)
            std::printf("[jit_lockstep] RAM differs at $%08X: interp=%02X jit=%02X\n",
                        bad, memRef.peek8(bad), memJit.peek8(bad));

        std::printf("[jit_lockstep] DIVERGED after %ld steps\n", i);
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

    // A green gate that exercised nothing is worse than a red one. The JIT
    // must have retired real instructions AND replayed real blocks, or the
    // comparison above proved only that the interpreter equals itself.
    const jit::Stats::Snapshot s = cpuJit.jit().stats().snapshot();
    std::printf("[jit_lockstep] OK — %ld steps identical\n", steps);
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
    if (s.instrs == 0) {
        std::printf("[jit_lockstep] FAIL: the JIT never retired an instruction "
                    "— this gate proved nothing\n");
        return 1;
    }
    // With a code-generating backend the block cache is the whole engine,
    // and even on `threaded` the gate registers a variant that forces it on
    // — either way, if it is on it must have been exercised, or the block
    // path went untested behind a green light.
    if (jit::blockCacheEnabled(cpuJit.jit().nativeBackend()) && s.blocksRun == 0) {
        std::printf("[jit_lockstep] FAIL: POM68K_JIT_BLOCKS is on but no block "
                    "was ever replayed — the block path proved nothing\n");
        return 1;
    }
    return 0;
}
