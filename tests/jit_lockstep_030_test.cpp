// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// JIT gate, 68030 flavour: the differential coverage the 030 machines have
// never had. Two identical Mac LC II machines are built from the same ROM
// and the same read-only disk image, one driven by the Moira interpreter
// and one by the JIT engine, and stepped from power-up while every
// architectural register, the three stacks, the cycle clock, the terminal
// instruction queue, the low RAM globals AND the 68030 instruction-cache
// counters are compared at each boundary.
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
#include "JitTestConfig.h"
#include "V8Memory.h"
#include "jit/JitConfig.h"

#include <algorithm>
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
    return testasset::findAny(names);
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
    uint16_t sr, ird, irc;
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
    s.ird = cpu.getIRD();
    s.irc = cpu.getIRC();
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
uint32_t ramDiff(const V8Memory& a, const V8Memory& b,
                 uint32_t bytes = kWatchBytes) {
    for (uint32_t i = 0; i < bytes; i++)
        if (a.peek8(i) != b.peek8(i)) return i;
    return 0xFFFFFFFF;
}

using PeriphTrace = std::vector<Cpu030::PeriphTracePoint>;

// Where each interrupt LANDED (the pc the handler will RTE back to). The
// peripheral-phase class diverges with every delivery identical — clock,
// devices, i-cache — and only the landing pc one instruction apart; this
// trace is what names that difference directly instead of inferring it
// from the i-cache walk seven steps later.
using IrqTrace = std::vector<Cpu030::IrqTracePoint>;

void collectIrqTrace(void* opaque, const Cpu030::IrqTracePoint& p) {
    static_cast<IrqTrace*>(opaque)->push_back(p);
}

void printIrqTraces(const IrqTrace& ref, const IrqTrace& jit) {
    const size_t common = std::min(ref.size(), jit.size());
    size_t first = 0;
    while (first < common && ref[first].pc == jit[first].pc &&
           ref[first].clock == jit[first].clock &&
           ref[first].level == jit[first].level &&
           ref[first].kind == jit[first].kind)
        ++first;
    std::printf("[jit_lockstep_030] irq trace: %zu interp / %zu jit,"
                " first difference %zu\n", ref.size(), jit.size(), first);
    const size_t from = first > 2 ? first - 2 : 0;
    const size_t to = std::min(std::max(ref.size(), jit.size()), first + 8);
    for (size_t k = from; k < to; ++k) {
        if (k < ref.size())
            std::printf("  interp[%zu] %s lvl=%d pc=%08X clk=%lld\n", k,
                        ref[k].kind ? "TAKE" : "pin ", ref[k].level,
                        ref[k].pc, (long long)ref[k].clock);
        if (k < jit.size())
            std::printf("  jit   [%zu] %s lvl=%d pc=%08X clk=%lld\n", k,
                        jit[k].kind ? "TAKE" : "pin ", jit[k].level,
                        jit[k].pc, (long long)jit[k].clock);
    }
}

struct WritePoint {
    uint32_t addr = 0, value = 0, pc = 0;
    uint32_t a0 = 0, a1 = 0, a2 = 0, a3 = 0;
    uint32_t bytes = 0;
    int64_t clock = 0;
    int native = 0;
};
using WriteTrace = std::vector<WritePoint>;

void collectWrite(void* opaque, moira::Moira* cpu, uint32_t addr,
                  uint32_t bytes, uint32_t value, uint32_t pc, int native) {
    constexpr uint32_t watched = 0x533E;
    if (addr > watched || addr + bytes <= watched) return;
    static_cast<WriteTrace*>(opaque)->push_back({
        addr, value, pc, cpu->getA(0), cpu->getA(1), cpu->getA(2),
        cpu->getA(3), bytes, cpu->getClock(), native
    });
}

void printWrite(const char* who, size_t i, const WritePoint& p) {
    std::printf("  %-6s[%zu] %c pc=%08X clk=%lld addr=%08X/%u value=%08X"
                " A0=%08X A1=%08X A2=%08X A3=%08X\n",
                who, i, p.native ? 'N' : 'I', p.pc,
                (long long)p.clock, p.addr, p.bytes, p.value,
                p.a0, p.a1, p.a2, p.a3);
}

void collectPeriphTrace(void* opaque, const Cpu030::PeriphTracePoint& p) {
    static_cast<PeriphTrace*>(opaque)->push_back(p);
}

bool same(const Cpu030::PeriphTracePoint& a,
          const Cpu030::PeriphTracePoint& b) {
    return a.pc == b.pc && a.clock == b.clock && a.machine == b.machine &&
           a.deadline == b.deadline && a.remainder == b.remainder &&
           a.target == b.target && a.phase == b.phase &&
           a.delivered == b.delivered && a.nextEvent == b.nextEvent &&
           a.deviceHash == b.deviceHash && a.icFetches == b.icFetches &&
           a.icHits == b.icHits && a.icMisses == b.icMisses;
}

void printPeriphPoint(const char* who, const Cpu030::PeriphTracePoint& p) {
    static const char* kSrc[] = { "sync", "stall", "chunk" };
    std::printf("  %-6s phase=%d pc=%08X clock=%lld machine=%lld target=%lld"
                " delivered=%d deadline=%lld"
                " remainder=%lld next=%d src=%s/%d old=%lld devices=%016llX"
                " ic=%lld/%lld/%lld\n",
                who, p.phase, p.pc, (long long)p.clock, (long long)p.machine,
                (long long)p.target, p.delivered,
                (long long)p.deadline, (long long)p.remainder, p.nextEvent,
                kSrc[p.src >= 0 && p.src <= 2 ? p.src : 0], p.syncCycles,
                (long long)p.oldDeadline,
                (unsigned long long)p.deviceHash,
                (long long)p.icFetches, (long long)p.icHits,
                (long long)p.icMisses);
}

// POM68K_JIT_LOCKSTEP_PERIPH_DUMP=prefix — the whole captured step, both
// arms, to prefix.interp / prefix.jit. The around-the-mismatch excerpts
// above cannot show where one arm INSERTED points; a diff of the full
// streams can.
void dumpPeriphTrace(const char* prefix, const char* arm,
                     const PeriphTrace& t) {
    std::string path = std::string(prefix) + "." + arm;
    std::FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return;
    static const char* kSrc[] = { "sync", "stall", "chunk" };
    for (size_t k = 0; k < t.size(); ++k) {
        const auto& p = t[k];
        std::fprintf(f, "[%zu] phase=%d pc=%08X clock=%lld machine=%lld"
                     " delivered=%d deadline=%lld remainder=%lld next=%d"
                     " src=%s/%d old=%lld devices=%016llX ic=%lld/%lld/%lld\n",
                     k, p.phase, p.pc, (long long)p.clock,
                     (long long)p.machine, p.delivered,
                     (long long)p.deadline, (long long)p.remainder,
                     p.nextEvent,
                     kSrc[p.src >= 0 && p.src <= 2 ? p.src : 0],
                     p.syncCycles, (long long)p.oldDeadline,
                     (unsigned long long)p.deviceHash,
                     (long long)p.icFetches, (long long)p.icHits,
                     (long long)p.icMisses);
    }
    std::fclose(f);
    std::printf("[jit_lockstep_030] periph trace dumped to %s (%zu points)\n",
                path.c_str(), t.size());
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
    static const jit::ResolvedConfig jitConfig =
        testjit::resolveFromEnvironment();
    static V8Memory memRef(pom68k::defaultCoreConfig());
    static V8Memory memJit(pom68k::defaultCoreConfig());
    static Cpu030 cpuRef(memRef, jitConfig, pom68k::defaultCoreConfig().cpu,
                         /*withFpu=*/true, false);
    static Cpu030 cpuJit(memJit, jitConfig, pom68k::defaultCoreConfig().cpu,
                         /*withFpu=*/true, false);
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
    std::string diskPath = testasset::overrideImage();
    if (diskPath.empty())
        diskPath = findAsset({ "hdv/lcii-boot.vhd", "hdv/boot.vhd",
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
    long periphTraceAt = -1;
    if (const char* t = std::getenv("POM68K_JIT_LOCKSTEP_PERIPH_TRACE_AT"))
        periphTraceAt = std::atol(t);
    long irqTraceFrom = -1;
    if (const char* t = std::getenv("POM68K_JIT_LOCKSTEP_IRQ_TRACE_FROM"))
        irqTraceFrom = std::atol(t);
    IrqTrace irqRef, irqJit;
    long fullRamAt = -1;
    if (const char* t = std::getenv("POM68K_JIT_LOCKSTEP_FULL_RAM_AT"))
        fullRamAt = std::atol(t);
    long writeTraceAt = -1;
    if (const char* t = std::getenv("POM68K_JIT_LOCKSTEP_WRITE_TRACE_AT"))
        writeTraceAt = std::atol(t);
    PeriphTrace periphRef, periphJit;
    WriteTrace writesRef, writesJit;

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
        if (i == periphTraceAt) {
            periphRef.clear();
            periphJit.clear();
            cpuRef.setPeriphTrace(&periphRef, collectPeriphTrace);
            cpuJit.setPeriphTrace(&periphJit, collectPeriphTrace);
        }
        if (i == irqTraceFrom) {
            cpuRef.setIrqTrace(&irqRef, collectIrqTrace);
            cpuJit.setIrqTrace(&irqJit, collectIrqTrace);
        }
        if (i == writeTraceAt) {
            writesRef.clear(); writesJit.clear();
            memRef.setWriteObserver(&writesRef, collectWrite);
            memJit.setWriteObserver(&writesJit, collectWrite);
            cpuJit.jit().setWriteObserver(&writesJit, collectWrite);
        }
        cpuRef.runCycles(b);
        cpuJit.runCycles(b);
        if (i == writeTraceAt) {
            memRef.setWriteObserver(nullptr, nullptr);
            memJit.setWriteObserver(nullptr, nullptr);
            cpuJit.jit().setWriteObserver(nullptr, nullptr);
            size_t first = 0, common = std::min(writesRef.size(), writesJit.size());
            while (first < common) {
                const auto& r = writesRef[first]; const auto& j = writesJit[first];
                if (r.addr != j.addr || r.bytes != j.bytes ||
                    r.value != j.value || r.pc != j.pc || r.clock != j.clock ||
                    r.a0 != j.a0 || r.a1 != j.a1 || r.a2 != j.a2 ||
                    r.a3 != j.a3) break;
                ++first;
            }
            std::printf("[jit_lockstep_030] write trace step %ld:"
                        " %zu interp / %zu jit, first difference %zu\n",
                        i, writesRef.size(), writesJit.size(), first);
            const size_t from = first > 2 ? first - 2 : 0;
            const size_t to = std::min(std::max(writesRef.size(), writesJit.size()),
                                       first + 4);
            for (size_t k = from; k < to; ++k) {
                if (k < writesRef.size()) printWrite("interp", k, writesRef[k]);
                if (k < writesJit.size()) printWrite("jit", k, writesJit[k]);
            }
        }
        if (i == periphTraceAt) {
            cpuRef.setPeriphTrace(nullptr, nullptr);
            cpuJit.setPeriphTrace(nullptr, nullptr);
            if (const char* d = std::getenv("POM68K_JIT_LOCKSTEP_PERIPH_DUMP")) {
                dumpPeriphTrace(d, "interp", periphRef);
                dumpPeriphTrace(d, "jit", periphJit);
            }
            const size_t common = std::min(periphRef.size(), periphJit.size());
            size_t first = 0;
            while (first < common && same(periphRef[first], periphJit[first]))
                ++first;
            if (first != periphRef.size() || first != periphJit.size()) {
                std::printf("[jit_lockstep_030] peripheral trace diverged at step %ld,"
                            " point %zu (%zu interp / %zu jit)\n",
                            i, first, periphRef.size(), periphJit.size());
                if (first < periphRef.size()) printPeriphPoint("interp", periphRef[first]);
                if (first < periphJit.size()) printPeriphPoint("jit", periphJit[first]);
                if (first) {
                    std::printf("  previous trace point was identical:\n");
                    printPeriphPoint("both", periphRef[first - 1]);
                }
                // A first mismatch is a symptom; how the gap MOVES across
                // the following deliveries is the diagnosis (grows = a
                // per-instruction charge difference, constant = a one-off,
                // shrinks = re-convergence and the step cut is the residue).
                for (size_t k = first + 1;
                     k < std::min(common, first + 8); ++k) {
                    printPeriphPoint("interp", periphRef[k]);
                    printPeriphPoint("jit", periphJit[k]);
                }
                // A count difference means one arm INSERTED points; the
                // pairwise walk above cannot show where. Name the first
                // pairwise index whose CLOCK differs (a hard slip, not the
                // cosmetic stale-pc one), and both tails.
                if (periphRef.size() != periphJit.size()) {
                    size_t hard = first;
                    while (hard < common &&
                           periphRef[hard].clock == periphJit[hard].clock &&
                           periphRef[hard].deviceHash == periphJit[hard].deviceHash)
                        ++hard;
                    std::printf("  first hard mismatch (clock/devices) at"
                                " point %zu of %zu common\n", hard, common);
                    if (hard < common) {
                        printPeriphPoint("interp", periphRef[hard]);
                        printPeriphPoint("jit", periphJit[hard]);
                    }
                    for (size_t k = common > 4 ? common - 4 : 0;
                         k < periphRef.size(); ++k)
                        printPeriphPoint("i-tail", periphRef[k]);
                    for (size_t k = common > 4 ? common - 4 : 0;
                         k < periphJit.size(); ++k)
                        printPeriphPoint("j-tail", periphJit[k]);
                }
            } else {
                std::printf("[jit_lockstep_030] peripheral trace step %ld:"
                            " %zu points identical\n", i, common);
            }
        }

        const State r = capture(cpuRef);
        const State j = capture(cpuJit);
        uint32_t bad = (fullRamAt >= 0 && i >= fullRamAt)
            ? ramDiff(memRef, memJit, 0x00A00000)
            : ramDiff(memRef, memJit);
        const Cpu030::ICacheStats icr = cpuRef.icacheStats();
        const Cpu030::ICacheStats icj = cpuJit.icacheStats();
        // POM68K_JIT_LOCKSTEP_ICTRACE=1 — an INSTRUMENT, not a relaxation:
        // print the counter delta at every boundary where it changes and
        // keep running, so a rare per-event skew (the 2026-08-29 ±2, two
        // events inside one compare window) gets a STEP NUMBER for the
        // dispatch ring instead of one aggregate at first detection. The
        // default gate behaviour is untouched.
        static const bool icTrace = std::getenv("POM68K_JIT_LOCKSTEP_ICTRACE");
        if (icTrace) {
            static long long lastFd = 0, lastHd = 0, lastMd = 0;
            const long long fd = (long long)icj.fetches - (long long)icr.fetches;
            const long long hd = (long long)icj.hits - (long long)icr.hits;
            const long long md = (long long)icj.misses - (long long)icr.misses;
            if (fd != lastFd || hd != lastHd || md != lastMd) {
                std::printf("[ictrace] step %ld clk=%lld  d(fetches)=%+lld"
                            " d(hits)=%+lld d(misses)=%+lld  jit pc=$%08X\n",
                            i, (long long)cpuJit.getClock(), fd, hd, md,
                            cpuJit.getPC());
                lastFd = fd; lastHd = hd; lastMd = md;
            }
            if (same(r, j)) continue;   // state still compared, counters traced
        }
        // The counters alone can agree while the CACHE CONTENT has already
        // parted (same totals, different lines): the state skew then shows
        // up thousands of steps later as a timing drift with equal counters
        // — which is exactly how the 2026-08-19 retained-cache divergence
        // presented. Compare the tags and valid bits too, so the FIRST
        // silent skew is the one the gate names.
        const bool icStateSame =
            std::memcmp(cpuRef.icacheState().tag, cpuJit.icacheState().tag,
                        sizeof(cpuRef.icacheState().tag)) == 0 &&
            std::memcmp(cpuRef.icacheState().valid, cpuJit.icacheState().valid,
                        sizeof(cpuRef.icacheState().valid)) == 0;
        if (same(r, j) && bad == 0xFFFFFFFF && same(icr, icj) && icStateSame)
            continue;

        // Once another observable has tripped, walking the complete LC II
        // RAM bus is cheap and names a successful-store divergence that the
        // low-globals tripwire would otherwise report much later.
        if (bad == 0xFFFFFFFF)
            bad = ramDiff(memRef, memJit, 0x00A00000);

        if (!icStateSame) {
            std::printf("[jit_lockstep_030] 68030 i-cache CONTENT diverged\n");
            for (int l = 0; l < 16; l++) {
                if (cpuRef.icacheState().tag[l] != cpuJit.icacheState().tag[l] ||
                    cpuRef.icacheState().valid[l] != cpuJit.icacheState().valid[l])
                    std::printf("  line %2d: interp tag=%08X valid=%X · "
                                "jit tag=%08X valid=%X\n", l,
                                cpuRef.icacheState().tag[l],
                                cpuRef.icacheState().valid[l],
                                cpuJit.icacheState().tag[l],
                                cpuJit.icacheState().valid[l]);
            }
        }
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

        {
            unsigned n = 0, head = 0;
            const jit::Engine::DispatchEv* ev = cpuJit.jit().dispatchRing(n, head);
            if (n) {
                static const char* kKind[] = { "flags", "backoff", "armfail",
                    "trace", "window", "block", "cacheline", "notready" };
                std::printf("[jit_lockstep_030] last %u jit dispatches"
                            " (oldest first):\n", n);
                long long lo = 0, hi = 0;   // clock window filter
                if (const char* w = std::getenv("POM68K_JIT_RING_CLK")) {
                    std::sscanf(w, "%lld,%lld", &lo, &hi);
                }
                for (unsigned k = 0; k < n; k++) {
                    const auto& e = ev[(head + jit::Engine::kDispatchRing - n + k)
                                       % jit::Engine::kDispatchRing];
                    if (hi && (e.clock < lo || e.clock > hi)) continue;
                    std::printf("  %-8s pc=%08X clk=%lld tgt=%lld exit=%u"
                                " instrs=%u\n",
                                kKind[e.kind < 8 ? e.kind : 7], e.pc,
                                (long long)e.clock, (long long)e.target,
                                e.exit, e.instrs);
                }
            }
        }
        if (irqTraceFrom >= 0) printIrqTraces(irqRef, irqJit);
        std::printf("[jit_lockstep_030] DIVERGED after %ld steps\n", i);
        std::printf("  last boundaries (jit): ");
        for (int k = 0; k < kTrail; k++) {
            const Step& t = trail[(trailAt + k) % kTrail];
            if (t.pc) std::printf("$%08X@%lld ", t.pc, (long long)t.clock);
        }
        std::printf("\n");
        std::printf("  pc    interp=%08X  jit=%08X\n", r.pc, j.pc);
        std::printf("  sr    interp=%04X      jit=%04X\n", r.sr, j.sr);
        std::printf("  queue interp=%04X/%04X jit=%04X/%04X (IRD/IRC)\n",
                    r.ird, r.irc, j.ird, j.irc);
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
    // Which of armWindow()'s four exits took the refusals. The aggregate
    // above says the engine was idle; only this line says whose fault it
    // was — the MMU probe, the memory map, the window geometry or the
    // post-arm coverage check (TODO § 1, the 100 % x64 030 refusal).
    std::printf("  arm refusals: probe %llu · notram %llu · degenerate %llu"
                " · pc-at-end %llu · uncovered %llu · pipe %llu\n",
                (unsigned long long)s.armFails[int(jit::ArmFail::Probe)],
                (unsigned long long)s.armFails[int(jit::ArmFail::NotRam)],
                (unsigned long long)s.armFails[int(jit::ArmFail::TooShort)],
                (unsigned long long)s.armFails[int(jit::ArmFail::PcAtEnd)],
                (unsigned long long)s.armFails[int(jit::ArmFail::Uncovered)],
                (unsigned long long)s.armFails[int(jit::ArmFail::Pipe)]);
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
                    "— the engine was never usable here (probe %llu, notram %llu, "
                    "degenerate %llu, pc-at-end %llu, uncovered %llu)\n",
                    (unsigned long long)s.windowFailed,
                    (unsigned long long)s.windowArmed,
                    (unsigned long long)s.armFails[int(jit::ArmFail::Probe)],
                    (unsigned long long)s.armFails[int(jit::ArmFail::NotRam)],
                    (unsigned long long)s.armFails[int(jit::ArmFail::TooShort)],
                    (unsigned long long)s.armFails[int(jit::ArmFail::PcAtEnd)],
                    (unsigned long long)s.armFails[int(jit::ArmFail::Uncovered)]);
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
    if (cpuJit.jit().config().blockCache && s.blocksRun == 0) {
        std::printf("[jit_lockstep_030] FAIL: POM68K_JIT_BLOCKS is on but no block "
                    "was ever replayed — the block path proved nothing\n");
        return 1;
    }
    return 0;
}
