// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Interpreter against translator on the boards that have no lockstep of
// their own: the 68040 Quadra 630 (F108), Quadra 700 (Spike) and Centris
// 650 (DjMEMC), and the 68030 Mac IIci (RBV), LC III (Sonora), Mac IIvx
// (VASP), PowerBook 170/180 (Jaws/Niagra, PMU firmware LLE) and PowerBook
// Duo 230 (MSC, PG&E firmware LLE), the SE/30 (Glue, 68030) and the Mac
// IIfx (OSS and its IOPs). jit_lockstep_test does this for the Quadra 605 with that
// board's hidden-state hooks; these boards expose none, so the comparison
// here is architectural only — every register, the clock, the supervisor
// stacks and the system globals (first 2 KB of RAM) at every checkpoint,
// and the whole of RAM every `POM68K_JIT_LOCKSTEP_FULL_EVERY` checkpoints
// and at the end.
//
//   jit_lockstep_board_test q630|q700|centris650|iici|lc3|iivx|pb170|pb180|duo230|se30|iifx [steps]
//
// POM68K_JIT_LOCKSTEP_BUDGET (default 50 cycles) is the checkpoint spacing,
// as in jit_lockstep_test. The boot disk is attached in memory
// (writeBack = false) so both machines see identical media. Soft-skips
// (exit 0) without the board's ROM or a boot disk.

#include "AssetFingerprint.h"
#include "Cpu020.h"
#include "IIfxCpu.h"
#include "IIfxMemory.h"
#include "MacIIMemory.h"
#include "BoardIdentity.h"
#include "MscCpu.h"
#include "MscMemory.h"
#include "CentrisCpu.h"
#include "CentrisMemory.h"
#include "JitTestConfig.h"
#include "Q630Cpu.h"
#include "Q630Memory.h"
#include "Q700Cpu.h"
#include "Q700Memory.h"
#include "Pb030Cpu.h"
#include "Pb030Memory.h"
#include "Pb030Profile.h"
#include "RbvCpu.h"
#include "RbvMemory.h"
#include "SonoraCpu.h"
#include "SonoraMemory.h"
#include "VaspCpu.h"
#include "VaspMemory.h"

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
        std::function<std::unique_ptr<Cpu>(Mem&, const jit::ResolvedConfig&)> makeCpu,
        const std::string& romPath, const std::string& diskPath, long steps,
        std::function<bool(Mem&)> prepare = {}) {
    const std::vector<uint8_t> rom = readFile(romPath);
    static const jit::ResolvedConfig jitConfig = testjit::resolveFromEnvironment();
    auto memRef = makeMem();
    auto memJit = makeMem();
    if (!memRef->loadRom(rom) || !memJit->loadRom(rom) ||
        (prepare && (!prepare(*memRef) || !prepare(*memJit))) ||
        !memRef->attachScsi(diskPath) || !memJit->attachScsi(diskPath)) {
        std::printf("FAIL: [jit_lockstep_board] %s: could not load ROM/disk\n", name);
        return 1;
    }
    auto cpuRef = makeCpu(*memRef, jitConfig);
    auto cpuJit = makeCpu(*memJit, jitConfig);
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
    // Boards whose MCU holds the CPU in reset (Egret, Cuda, PMU, PG&E) are
    // released before the comparison; the Glue boards have no such hold.
    if constexpr (requires(Mem& m) { m.cpuHeld(); }) {
        for (long g = 0; memRef->cpuHeld() && g < 400000; g++) memRef->tick(1000);
        for (long g = 0; memJit->cpuHeld() && g < 400000; g++) memJit->tick(1000);
        if (memRef->cpuHeld() || memJit->cpuHeld()) {
            std::printf("FAIL: [jit_lockstep_board] %s: the CPU was never released\n", name);
            return 1;
        }
    }

    long budget = 50;
    if (const char* b = std::getenv("POM68K_JIT_LOCKSTEP_BUDGET")) budget = std::atol(b);
    if (budget < 1) budget = 1;
    // POM68K_JIT_LOCKSTEP_JITTER=N: each checkpoint lands 1..N cycles after
    // the previous one (fixed-seed xorshift), so the translator's exits fall
    // at arbitrary points of real ROM and System code, not on a grid.
    long jitter = 0;
    if (const char* j = std::getenv("POM68K_JIT_LOCKSTEP_JITTER")) jitter = std::atol(j);
    uint64_t rng = 0x9E3779B97F4A7C15ull;
    long fullEvery = 200000;
    if (const char* f = std::getenv("POM68K_JIT_LOCKSTEP_FULL_EVERY")) fullEvery = std::atol(f);
    std::printf("[jit_lockstep_board] %s backend=%s steps=%ld budget=%ld jitter=%ld full-RAM every %ld\n",
                name, cpuJit->jit().backendName(), steps, budget, jitter, fullEvery);

    const uint32_t ramBytes = memRef->ramBytes();
    auto fullRamDiff = [&]() -> uint32_t {
        for (uint32_t p = 0; p < ramBytes; ++p)
            if (memRef->peek8(p) != memJit->peek8(p)) return p;
        return 0xFFFFFFFF;
    };
    uint32_t lastPc[8] = {};
    for (long i = 0; i < steps; i++) {
        lastPc[i & 7] = cpuJit->getPC();
        long b = budget;
        if (jitter > 0) {
            rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
            b = 1 + long(rng % uint64_t(jitter));
        }
        cpuRef->runCycles(b);
        cpuJit->runCycles(b);
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
    // The translated side must actually have run translated: blocks, or —
    // the threaded backend's default (POM68K_JIT_BLOCKS unset) — the fetch
    // window, which interprets from host code pointers.
    if (st.blocksRun == 0 && st.windowInstrs == 0 && !self) {
        std::printf("FAIL: [jit_lockstep_board] %s: the engine never ran (no block, no window)\n", name);
        return 1;
    }
    std::printf("[jit_lockstep_board] OK — %s: %ld steps identical (clock included), "
                "%lld cycles, full RAM identical; blocks compiled=%llu run=%llu window instrs=%llu\n",
                name, steps, (long long)cpuRef->getClock(),
                (unsigned long long)st.blocksCompiled, (unsigned long long)st.blocksRun,
                (unsigned long long)st.windowInstrs);
    // Flush causes: a backend whose compile count runs far ahead of its
    // block runs is invalidating something in a loop.
    std::printf("[jit_lockstep_board]   flushes=%llu invalidations=%llu:",
                (unsigned long long)st.flushes, (unsigned long long)st.invalidations);
    for (int i = 0; i < int(jit::Flush::Count); i++)
        if (st.flushCauses[i])
            std::printf(" %s=%llu", jit::flushName(jit::Flush(i)),
                        (unsigned long long)st.flushCauses[i]);
    std::printf("\n");
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
            [&](Q630Memory& m, const jit::ResolvedConfig& j) { return std::make_unique<Q630Cpu>(m, j, core.cpu); },
            rom, disk, steps);
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
            [&](Q700Memory& m, const jit::ResolvedConfig& j) { return std::make_unique<Q700Cpu>(m, j, core.cpu); },
            rom, disk, steps);
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
            [&](CentrisMemory& m, const jit::ResolvedConfig& j) { return std::make_unique<CentrisCpu>(m, j, core.cpu); },
            rom, disk, steps);
    }
    // The 68030 boards (the LC II has jit_lockstep_030_test): System 7.5.
    const std::string disk75 = testasset::findAny({ "hdv/System 7.5 HD.dsk", "hdv/boot.vhd" });
    auto need = [&](const char* b, const std::string& rom) {
        if (!rom.empty() && !disk75.empty()) return true;
        std::printf("SKIP: [jit_lockstep_board] %s needs its ROM + hdv/System 7.5 HD.dsk\n", b);
        return false;
    };
    if (board == "iici") {
        const std::string rom = testasset::findAny({
            "roms/512KB ROMs/1989-09 - 368CADFE - Mac IIci.ROM", "roms/maciici.rom" });
        if (!need("iici", rom)) return 0;
        testasset::report({ rom, disk75 });
        return run<RbvMemory, RbvCpu>("Mac IIci",
            [&] { return std::make_unique<RbvMemory>(core, 0x800000, RbvMemory::kCpuHzCi, true); },
            [&](RbvMemory& m, const jit::ResolvedConfig& j) {
                return std::make_unique<RbvCpu>(m, j, core.cpu, true); },
            rom, disk75, steps);
    }
    if (board == "lc3") {
        const std::string rom = testasset::findAny({
            "roms/1MB ROMs/1993-02 - ECBBC41C - Mac LC III.ROM", "roms/maclc3.rom" });
        if (!need("lc3", rom)) return 0;
        testasset::report({ rom, disk75 });
        return run<SonoraMemory, SonoraCpu>("LC III",
            [&] { return std::make_unique<SonoraMemory>(core, 0x800000); },
            [&](SonoraMemory& m, const jit::ResolvedConfig& j) {
                return std::make_unique<SonoraCpu>(m, j, core.cpu, true); },
            rom, disk75, steps);
    }
    if (board == "iivx") {
        const std::string rom = testasset::findAny({
            "roms/1MB ROMs/1992-10 - 4957EB49 - Mac IIvx & IIvi or Performa 600.ROM",
            "roms/maciivx.rom" });
        if (!need("iivx", rom)) return 0;
        testasset::report({ rom, disk75 });
        return run<VaspMemory, VaspCpu>("Mac IIvx",
            [&] { return std::make_unique<VaspMemory>(core, 0x800000, VaspMemory::kCpuHzVx,
                                                      VaspMemory::kIdIIvx); },
            [&](VaspMemory& m, const jit::ResolvedConfig& j) {
                return std::make_unique<VaspCpu>(m, j, core.cpu, true); },
            rom, disk75, steps);
    }
    // PowerBook 030: Jaws (170) and Niagra (180), PMU firmware LLE.
    if (board == "pb170" || board == "pb180") {
        const bool niagra = board == "pb180";
        const auto profile = pom68k::pb030Profile(niagra ? pom68k::SnapMachine::Pb180
                                                         : pom68k::SnapMachine::Pb170);
        const std::string rom = testasset::findAny({ niagra
            ? "roms/1MB ROMs/1992-10 - E33B2724 - Powerbook 160 & 165 & 165c & 180 & 180c.ROM"
            : "roms/1MB ROMs/1991-10 - 420DBFF3 - Quadra 700&900 & PB140&170.ROM" });
        const std::string fw = testasset::findAny({ niagra
            ? "roms/archive/macroms/pmu/Powerbook-160-165-180__Powerbook 160 PMU.bin"
            : "roms/archive/macroms/pmu/Powerbook-140-145-170__PMU.bin" });
        const std::string disk755 = testasset::findAny({ "hdv/System 7.5.5 HD.dsk" });
        if (rom.empty() || fw.empty() || disk755.empty()) {
            std::printf("SKIP: [jit_lockstep_board] %s needs its ROM, PMU firmware and hdv/System 7.5.5 HD.dsk\n",
                        board.c_str());
            return 0;
        }
        testasset::report({ rom, fw, disk755 });
        const std::vector<uint8_t> firmware = readFile(fw);
        return run<Pb030Memory, Pb030Cpu>(niagra ? "PowerBook 180" : "PowerBook 170",
            [&] { return std::make_unique<Pb030Memory>(core, 8u << 20, profile.cpuHz, niagra,
                                                       profile.colorHeight); },
            [&](Pb030Memory& m, const jit::ResolvedConfig& j) {
                return std::make_unique<Pb030Cpu>(m, j, core.cpu, profile.fpu); },
            rom, disk755, steps,
            [&](Pb030Memory& m) { m.setMonitorSense(7); return m.pmu().loadFirmware(firmware); });
    }
    // SE/30 (Glue, Cpu020 in 68030 mode, its video ROM).
    if (board == "se30") {
        const std::string rom = testasset::findAny({
            "roms/256KB ROMs/1988-09 - 97221136 - Mac II FDHD & IIx & IIcx.ROM" });
        const std::string vrom = testasset::findAny({ "roms/se30/se30vrom.uk6" });
        const std::string hd = testasset::findAny({ "hdv/HD20SC.vhd" });
        if (rom.empty() || vrom.empty() || hd.empty()) {
            std::printf("SKIP: [jit_lockstep_board] se30 needs the 97221136 ROM, se30vrom.uk6 and hdv/HD20SC.vhd\n");
            return 0;
        }
        testasset::report({ rom, vrom, hd });
        return run<MacIIMemory, Cpu020>("SE/30",
            [&] { return std::make_unique<MacIIMemory>(core, 0x800000, MacIIMemory::Model::SE30); },
            [&](MacIIMemory& m, const jit::ResolvedConfig& j) {
                return std::make_unique<Cpu020>(m, j, core.cpu, true, true); },
            rom, hd, steps,
            [&](MacIIMemory& m) { return m.installSe30Video(vrom); });
    }
    // Mac IIfx (OSS, the two IOPs; the Toby video card's declaration ROM).
    if (board == "iifx") {
        const std::string rom = testasset::findAny({
            "roms/512KB ROMs/1990-03 - 4147DD77 - Mac IIfx.ROM" });
        const std::string toby = testasset::findAny({ "roms/342-0008-a.bin",
            "roms/archive/macroms/Misc/Video cards/Apple Macintosh II Video Card/342-0008-a.bin",
            "roms/archive/macroms/68k/256k/Macintosh II/342-0008-a.bin" });
        if (rom.empty() || toby.empty() || disk75.empty()) {
            std::printf("SKIP: [jit_lockstep_board] iifx needs the 4147DD77 ROM, the Toby ROM and hdv/System 7.5 HD.dsk\n");
            return 0;
        }
        testasset::report({ rom, toby, disk75 });
        return run<IIfxMemory, IIfxCpu>("Mac IIfx",
            [&] { return std::make_unique<IIfxMemory>(core); },
            [&](IIfxMemory& m, const jit::ResolvedConfig& j) {
                return std::make_unique<IIfxCpu>(m, j, true); },
            rom, disk75, steps,
            [&](IIfxMemory& m) { return m.installTobyVideo(toby); });
    }
    // PowerBook Duo 230 (MSC), PG&E power manager firmware LLE.
    if (board == "duo230") {
        const std::string rom = testasset::findAny({
            "roms/1MB ROMs/1992-10 - ECFA989B - Powerbook 210 & 230 & 250.ROM" });
        const std::string pge = testasset::findAny({ "roms/pge/pge_boot.bin" });
        const std::string disk755 = testasset::findAny({ "hdv/System 7.5.5 HD.dsk" });
        if (rom.empty() || pge.empty() || disk755.empty()) {
            std::printf("SKIP: [jit_lockstep_board] duo230 needs its ROM, roms/pge/pge_boot.bin and hdv/System 7.5.5 HD.dsk\n");
            return 0;
        }
        testasset::report({ rom, pge, disk755 });
        const pom68k::MscBoardIdentity id = pom68k::mscBoardIdentity(pom68k::SnapMachine::Duo230);
        return run<MscMemory, MscCpu>("PowerBook Duo 230",
            [&] { return std::make_unique<MscMemory>(core, 8u << 20, id.cpuHz, id.boxId, id.cscPanel); },
            [&](MscMemory& m, const jit::ResolvedConfig& j) {
                return std::make_unique<MscCpu>(m, j, core.cpu, m.hasCsc()); },
            rom, disk755, steps,
            [](MscMemory& m) { return m.pgeActive(); });
    }
    std::fprintf(stderr, "usage: %s q630|q700|centris650|iici|lc3|iivx|pb170|pb180|duo230|se30|iifx [steps]\n", argv[0]);
    return 2;
}
