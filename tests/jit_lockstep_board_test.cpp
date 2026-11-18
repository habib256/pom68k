// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Interpreter against translator on the 68040 boards that have no lockstep
// of their own: Quadra 630 (F108), Quadra 700 (Spike) and Centris 650
// (DjMEMC). jit_lockstep_test does this for the Quadra 605 with that
// board's hidden-state hooks; these boards expose none, so the comparison
// here is architectural only — every register, the clock, the supervisor
// stacks and the system globals (first 2 KB of RAM) at every checkpoint,
// and the whole of RAM every `POM68K_JIT_LOCKSTEP_FULL_EVERY` checkpoints
// and at the end.
//
//   jit_lockstep_board_test q630|q700|centris650 [steps]
//
// POM68K_JIT_LOCKSTEP_BUDGET (default 50 cycles) is the checkpoint spacing,
// as in jit_lockstep_test. The boot disk is attached in memory
// (writeBack = false) so both machines see identical media. Soft-skips
// (exit 0) without the board's ROM or a boot disk.

#include "AssetFingerprint.h"
#include "CentrisCpu.h"
#include "CentrisMemory.h"
#include "JitTestConfig.h"
#include "Q630Cpu.h"
#include "Q630Memory.h"
#include "Q700Cpu.h"
#include "Q700Memory.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace {

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>() };
}

struct State {
    uint32_t d[8], a[8], pc, usp, isp, msp, vbr, cacr, tc040;
    uint16_t sr;
    int64_t clock;
    bool halted;
};

State capture(const moira::Moira& cpu) {
    State s{};
    for (int i = 0; i < 8; i++) { s.d[i] = cpu.getD(i); s.a[i] = cpu.getA(i); }
    s.pc = cpu.getPC();
    s.usp = cpu.getUSP();
    s.isp = cpu.getISP();
    s.msp = cpu.getMSP();
    s.vbr = cpu.getVBR();
    s.cacr = cpu.getCACR();
    s.tc040 = cpu.getTC040();
    s.sr = cpu.getSR();
    s.clock = cpu.getClock();
    s.halted = cpu.isHalted();
    return s;
}

std::string describe(const State& r, const State& j) {
    char b[160];
    auto one = [&](const char* what, unsigned long long x, unsigned long long y) {
        std::snprintf(b, sizeof b, "%s interp $%llX jit $%llX", what, x, y);
        return std::string(b);
    };
    if (r.pc != j.pc) return one("PC", r.pc, j.pc);
    if (r.clock != j.clock) return one("clock", r.clock, j.clock);
    for (int i = 0; i < 8; i++) {
        if (r.d[i] != j.d[i]) { char w[4] = {'D', char('0' + i), 0}; return one(w, r.d[i], j.d[i]); }
        if (r.a[i] != j.a[i]) { char w[4] = {'A', char('0' + i), 0}; return one(w, r.a[i], j.a[i]); }
    }
    if (r.sr != j.sr) return one("SR", r.sr, j.sr);
    if (r.usp != j.usp) return one("USP", r.usp, j.usp);
    if (r.isp != j.isp) return one("ISP", r.isp, j.isp);
    if (r.msp != j.msp) return one("MSP", r.msp, j.msp);
    if (r.vbr != j.vbr) return one("VBR", r.vbr, j.vbr);
    if (r.cacr != j.cacr) return one("CACR", r.cacr, j.cacr);
    if (r.tc040 != j.tc040) return one("TC", r.tc040, j.tc040);
    if (r.halted != j.halted) return one("halted", r.halted, j.halted);
    return {};
}

// One board: two identical machines, an interpreter and a JIT.
template <class Mem, class Cpu>
int run(const char* name, std::function<std::unique_ptr<Mem>()> makeMem,
        const pom68k::CoreCpuConfig& cpuConfig,
        const std::string& romPath, const std::string& diskPath, long steps) {
    const std::vector<uint8_t> rom = readFile(romPath);
    static const jit::ResolvedConfig jitConfig = testjit::resolveFromEnvironment();
    auto memRef = makeMem();
    auto memJit = makeMem();
    if (!memRef->loadRom(rom) || !memJit->loadRom(rom) ||
        !memRef->attachScsi(diskPath) || !memJit->attachScsi(diskPath)) {
        std::printf("FAIL: [jit_lockstep_board] %s: could not load ROM/disk\n", name);
        return 1;
    }
    auto cpuRef = std::make_unique<Cpu>(*memRef, jitConfig, cpuConfig);
    auto cpuJit = std::make_unique<Cpu>(*memJit, jitConfig, cpuConfig);
    memRef->setCpu(cpuRef.get());
    memJit->setCpu(cpuJit.get());
    // POM68K_JIT_LOCKSTEP_SELF=1: interpreter against interpreter, the
    // proof that two instances of this board are deterministic in one
    // process (no host clock, no shared state) before any JIT verdict.
    const bool self = std::getenv("POM68K_JIT_LOCKSTEP_SELF") != nullptr;
    cpuRef->setEngine(0);
    cpuJit->setEngine(self ? 0 : 1);
    cpuRef->hardReset();
    cpuJit->hardReset();
    for (long g = 0; memRef->cpuHeld() && g < 400000; g++) memRef->tick(1000);
    for (long g = 0; memJit->cpuHeld() && g < 400000; g++) memJit->tick(1000);
    if (memRef->cpuHeld() || memJit->cpuHeld()) {
        std::printf("FAIL: [jit_lockstep_board] %s: the CPU was never released\n", name);
        return 1;
    }

    long budget = 50;
    if (const char* b = std::getenv("POM68K_JIT_LOCKSTEP_BUDGET")) budget = std::atol(b);
    if (budget < 1) budget = 1;
    long fullEvery = 200000;
    if (const char* f = std::getenv("POM68K_JIT_LOCKSTEP_FULL_EVERY")) fullEvery = std::atol(f);
    std::printf("[jit_lockstep_board] %s backend=%s steps=%ld budget=%ld full-RAM every %ld\n",
                name, cpuJit->jit().backendName(), steps, budget, fullEvery);

    const uint32_t ramBytes = memRef->ramBytes();
    auto fullRamDiff = [&]() -> uint32_t {
        for (uint32_t p = 0; p < ramBytes; ++p)
            if (memRef->peek8(p) != memJit->peek8(p)) return p;
        return 0xFFFFFFFF;
    };
    uint32_t lastPc[8] = {};
    for (long i = 0; i < steps; i++) {
        lastPc[i & 7] = cpuJit->getPC();
        cpuRef->runCycles(budget);
        cpuJit->runCycles(budget);
        const State r = capture(*cpuRef), j = capture(*cpuJit);
        std::string why = describe(r, j);
        uint32_t bad = 0xFFFFFFFF;
        if (why.empty()) {
            for (uint32_t p = 0; p < 2048; p++)
                if (memRef->peek8(p) != memJit->peek8(p)) { bad = p; break; }
            if (bad == 0xFFFFFFFF && fullEvery > 0 && (i + 1) % fullEvery == 0) bad = fullRamDiff();
            if (bad != 0xFFFFFFFF) {
                char b[96];
                std::snprintf(b, sizeof b, "RAM[$%08X] interp $%02X jit $%02X", bad,
                              memRef->peek8(bad), memJit->peek8(bad));
                why = b;
            }
        }
        if (!why.empty()) {
            std::printf("FAIL: [jit_lockstep_board] %s diverged at step %ld (clock %lld): %s\n",
                        name, i, (long long)r.clock, why.c_str());
            std::printf("  recent JIT pcs:");
            for (int k = 1; k <= 8; k++) std::printf(" %08X", lastPc[(i + k) & 7]);
            std::printf("\n");
            return 1;
        }
    }
    const uint32_t bad = fullRamDiff();
    if (bad != 0xFFFFFFFF) {
        std::printf("FAIL: [jit_lockstep_board] %s: RAM[$%08X] differs after %ld steps\n",
                    name, bad, steps);
        return 1;
    }
    const auto st = cpuJit->jit().stats().snapshot();
    if (st.blocksRun == 0 && !self) {
        std::printf("FAIL: [jit_lockstep_board] %s: the JIT never ran a block\n", name);
        return 1;
    }
    std::printf("[jit_lockstep_board] OK — %s: %ld steps identical (clock included), "
                "%lld cycles, full RAM identical; blocks compiled=%llu run=%llu\n",
                name, steps, (long long)cpuRef->getClock(),
                (unsigned long long)st.blocksCompiled, (unsigned long long)st.blocksRun);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string board = argc > 1 ? argv[1] : "q630";
    const long steps = argc > 2 ? std::atol(argv[2]) : 2'000'000;
    const std::string disk = testasset::findAny({ "hdv/MacOS-8.1-boot.vhd" });
    pom68k::CoreConfig core;

    if (board == "q630") {
        const std::string rom = testasset::findAny({
            "roms/1MB ROMs/1994-07 - 06684214 - LC,Quadra,Performa 630.ROM",
            "roms/mame/macqd630/06684214.bin", "roms/quadra630.rom" });
        if (rom.empty() || disk.empty()) {
            std::printf("SKIP: [jit_lockstep_board] q630 needs the 06684214 ROM + hdv/MacOS-8.1-boot.vhd\n");
            return 0;
        }
        testasset::report({ rom, disk });
        return run<Q630Memory, Q630Cpu>("Quadra 630",
            [&] { return std::make_unique<Q630Memory>(core, 32u << 20); },
            core.cpu, rom, disk, steps);
    }
    if (board == "q700") {
        const std::string rom = testasset::findAny({
            "roms/1MB ROMs/1991-10 - 420DBFF3 - Quadra 700&900 & PB140&170.ROM" });
        if (rom.empty() || disk.empty()) {
            std::printf("SKIP: [jit_lockstep_board] q700 needs the 420DBFF3 ROM + hdv/MacOS-8.1-boot.vhd\n");
            return 0;
        }
        testasset::report({ rom, disk });
        return run<Q700Memory, Q700Cpu>("Quadra 700",
            [&] { return std::make_unique<Q700Memory>(core, 32u << 20, Q700Memory::kCpuHz,
                                                      Q700Memory::Model::Spike); },
            core.cpu, rom, disk, steps);
    }
    if (board == "centris650") {
        const std::string rom = testasset::findAny({
            "roms/1MB ROMs/1993-02 - F1A6F343 - Quadra, Centris 610,650.ROM",
            "roms/centris650.rom" });
        if (rom.empty() || disk.empty()) {
            std::printf("SKIP: [jit_lockstep_board] centris650 needs the F1A6F343 ROM + hdv/MacOS-8.1-boot.vhd\n");
            return 0;
        }
        testasset::report({ rom, disk });
        return run<CentrisMemory, CentrisCpu>("Centris 650",
            [&] { return std::make_unique<CentrisMemory>(core, 36u << 20, CentrisMemory::kCpuHz650,
                                                         CentrisMemory::kIdCentris650); },
            core.cpu, rom, disk, steps);
    }
    std::fprintf(stderr, "usage: %s q630|q700|centris650 [steps]\n", argv[0]);
    return 2;
}
