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
    cpuRef.setEngine(0);
    cpuJit.setEngine(1);
    cpuRef.hardReset();
    cpuJit.hardReset();

    std::printf("[jit_lockstep] rom=%s backend=%s steps=%ld\n",
                romPath.c_str(), cpuJit.jit().backendName(), steps);

    for (long i = 0; i < steps; i++) {
        // One machine cycle of budget: both engines stop at the first
        // instruction boundary past it, so the two stay in step.
        cpuRef.runCycles(1);
        cpuJit.runCycles(1);

        const State r = capture(cpuRef);
        const State j = capture(cpuJit);
        if (same(r, j)) continue;

        std::printf("[jit_lockstep] DIVERGED after %ld steps\n", i);
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
                " / %llu replayed · window %llu armed (%llu refused)\n",
                (unsigned long long)s.instrs,
                (unsigned long long)s.interpInstrs,
                (unsigned long long)s.blocksCompiled,
                (unsigned long long)s.blocksRun,
                (unsigned long long)s.windowArmed,
                (unsigned long long)s.windowFailed);
    if (s.instrs == 0) {
        std::printf("[jit_lockstep] FAIL: the JIT never retired an instruction "
                    "— this gate proved nothing\n");
        return 1;
    }
    // The block cache is off by default (see JitConfig.h — it measured
    // slower than the window alone). When it IS on, it must have been
    // exercised, otherwise the block path goes untested.
    if (jit::blockCacheEnabled() && s.blocksRun == 0) {
        std::printf("[jit_lockstep] FAIL: POM68K_JIT_BLOCKS is on but no block "
                    "was ever replayed — the block path proved nothing\n");
        return 1;
    }
    return 0;
}
