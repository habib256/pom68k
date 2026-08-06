// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// JIT gate, cycle-exact flavour. The 68040 twin of this harness
// (tests/jit_lockstep_test.cpp) proves that the JIT and the interpreter
// compute the same ARCHITECTURAL state. On a Mac Plus that is not the
// interesting question, and it is not the one this file exists to answer.
//
// Every other guest the engine reaches runs on Moira's Core::C68020, where
// the SYNC(x) macro expands to nothing (MoiraMacros.h:19) — so a fetch
// served from the JIT's code window changes no cycle accounting at all, and
// the interpreter/JIT clocks agree by construction. The compacts run on
// Core::C68000, where SYNC is real, MOIRA_PRECISE_TIMING is on, and this is
// the only family in POM68K whose timing claim is cycle-exact (`sst68000`,
// 1 000 058 vectors WITH cycles). There, `Moira::pomJitFetch000` has to
// re-charge by hand everything `read<C,PROG,Word,F>` would have charged:
// two SYNC(2)s, POLL_IPL, and the machine's own video/RAM bus contention,
// which `Cpu68k::applyContention` levies from inside read16() and which a
// windowed fetch would otherwise silently stop paying.
//
// So the assertion that matters here is `clock`, compared at EVERY
// instruction boundary. A drifting fetch cycle on this machine does not
// surface as a wrong pixel; it surfaces as a sound tempo, a VIA pulse
// width, or an IWM latch hold — days later, on a different gate.
//
// Two Mac Plus machines are built from the same ROM and the same (read-only)
// floppy, one on each engine, and stepped together from power-up.
// MacMemory/Cpu68k read neither the wall clock nor any host entropy, and
// SonyDrive defaults to write-back OFF, so two instances in one process are
// bit-for-bit reproducible.
//
// Soft-skips (exit 0) when the user-provided ROM is absent.

#include "Cpu68k.h"
#include "MacMemory.h"
#include "MacFrame.h"
#include "jit/JitConfig.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
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
    uint32_t d[8], a[8], pc, usp, isp;
    uint16_t sr;
    int64_t  clock;
};

State capture(const moira::Moira& cpu) {
    State s{};
    for (int i = 0; i < 8; i++) { s.d[i] = cpu.getD(i); s.a[i] = cpu.getA(i); }
    s.pc = cpu.getPC();
    s.usp = cpu.getUSP();
    s.isp = cpu.getISP();
    s.sr = cpu.getSR();
    s.clock = cpu.getClock();
    return s;
}

bool same(const State& x, const State& y) {
    return std::memcmp(&x, &y, sizeof(State)) == 0;
}

// Registers are not the whole architectural state, and a JIT bug in a STORE
// only shows up in a register much later, when something reads the byte
// back. On a 68000 the system globals live in the first 2 KB of RAM and are
// written constantly during a boot — a cheap, high-yield tripwire. peek8()
// is used rather than read8() because read8() latches the SCC and advances
// the IWM (MacMemory.cpp § peek8).
constexpr uint32_t kWatchBytes = 2048;
uint32_t ramDiff(const MacMemory& a, const MacMemory& b) {
    for (uint32_t i = 0; i < kWatchBytes; i++)
        if (a.peek8(i) != b.peek8(i)) return i;
    return 0xFFFFFFFF;
}

// Device-side state, assembled entirely from accessors that already exist —
// no lockstep plumbing was added to any device class for this gate. What is
// covered is what a cycle drift would move FIRST: the VIA's interrupt
// latches (its timers run on a CPU-cycle divider), the IWM's poll/hit ratio
// (the latch hold that the boost gate exists to protect), the drive's head
// position and motor, and the SCC's interrupt line.
struct Devices {
    uint8_t viaIfr, viaIer;
    long iwmReads, iwmHits, iwmOverwritten, iwmReReads;
    int  track;
    bool motor, hasDisk, sccIrq, overlay;
};

Devices captureDevices(MacMemory& m) {
    Devices d{};
    d.viaIfr = m.via().ifrRaw();
    d.viaIer = m.via().ierRaw();
    d.iwmReads = m.iwm().dataReads;
    d.iwmHits = m.iwm().dataHits;
    d.iwmOverwritten = m.iwm().overwritten;
    d.iwmReReads = m.iwm().reReads;
    d.track = m.internalDrive().currentTrack();
    d.motor = m.internalDrive().motorOn();
    d.hasDisk = m.internalDrive().hasDisk();
    d.sccIrq = m.sccIrq();
    d.overlay = m.overlay();
    return d;
}

bool same(const Devices& x, const Devices& y) {
    return std::memcmp(&x, &y, sizeof(Devices)) == 0;
}

void printDeviceDiff(const Devices& r, const Devices& j) {
    auto u32 = [](const char* n, long a, long b) {
        if (a != b) std::printf("  %-16s interp=%ld jit=%ld\n", n, a, b);
    };
    auto hex = [](const char* n, unsigned a, unsigned b) {
        if (a != b) std::printf("  %-16s interp=$%02X jit=$%02X\n", n, a, b);
    };
    auto flag = [](const char* n, bool a, bool b) {
        if (a != b) std::printf("  %-16s interp=%d jit=%d\n", n, a, b);
    };
    hex("VIA IFR", r.viaIfr, j.viaIfr);
    hex("VIA IER", r.viaIer, j.viaIer);
    u32("IWM polls", r.iwmReads, j.iwmReads);
    u32("IWM hits", r.iwmHits, j.iwmHits);
    u32("IWM overwritten", r.iwmOverwritten, j.iwmOverwritten);
    u32("IWM re-reads", r.iwmReReads, j.iwmReReads);
    u32("track", r.track, j.track);
    flag("motor", r.motor, j.motor);
    flag("disk present", r.hasDisk, j.hasDisk);
    flag("SCC IRQ", r.sccIrq, j.sccIrq);
    flag("overlay", r.overlay, j.overlay);
}

}  // namespace

int main(int argc, char** argv) {
    long steps = 2'000'000;
    if (const char* n = std::getenv("POM68K_JIT_LOCKSTEP_N")) steps = std::atol(n);
    if (argc > 1) steps = std::atol(argv[1]);

    const std::string romPath = findAsset({ "roms/macplus.rom",
                                            "roms/mac128k/macplus.rom" });
    if (romPath.empty()) {
        std::printf("[jit_lockstep_68000] no Mac Plus ROM — soft skip\n");
        return 0;
    }
    const std::vector<uint8_t> rom = readFile(romPath);

    static MacMemory memRef, memJit;
    static Cpu68k cpuRef(memRef), cpuJit(memJit);
    memRef.setCpu(&cpuRef);
    memJit.setCpu(&cpuJit);
    if (!memRef.loadRom(rom) || !memJit.loadRom(rom)) {
        std::printf("[jit_lockstep_68000] loadRom failed — soft skip\n");
        return 0;
    }

    // The ROM alone reaches the RAM test and stops at the "insert disk"
    // blinking icon, where almost every access is a VIA or IWM register and
    // the window is armed over ROM only. Attach the boot floppy when it is
    // there — READ-ONLY by default (SonyDrive::writeBack_ is false), so both
    // machines see identical media — and the comparison then covers the
    // System loading, the GCR read path and the Finder with it.
    const std::string diskPath = findAsset({ "disks35/Disk605.dsk" });
    if (!diskPath.empty()) {
        memRef.insertDisk(diskPath);
        memJit.insertDisk(diskPath);
    }

    cpuRef.setEngine(0);
    cpuJit.setEngine(1);
    cpuRef.hardReset();
    cpuJit.hardReset();

    std::printf("[jit_lockstep_68000] rom=%s disk=%s backend=%s steps=%ld\n",
                romPath.c_str(),
                diskPath.empty() ? "(none — ROM only)" : diskPath.c_str(),
                cpuJit.jit().backendName(), steps);

    // Cycles of budget per checkpoint. One compares the two engines after
    // (almost) every instruction — the sharpest possible test, and the one
    // that matters here, because a single mischarged fetch is exactly what
    // this seam could get wrong. A larger budget lets recorded blocks run
    // their full length and still leaves both engines stopping at the same
    // boundary, since both stop at the first one past the target.
    long budget = 1;
    if (const char* b = std::getenv("POM68K_JIT_LOCKSTEP_BUDGET")) budget = std::atol(b);
    if (budget < 1) budget = 1;
    long fineAt = -1;
    if (const char* f = std::getenv("POM68K_JIT_LOCKSTEP_FINE_AT")) fineAt = std::atol(f);
    std::printf("[jit_lockstep_68000] budget=%ld cycle(s) per comparison%s\n",
                budget, fineAt >= 0 ? " (fine from the marked step)" : "");

    // The last handful of boundaries: a divergence is almost never AT the
    // pc it is noticed at — least of all a timing one, which surfaces when
    // a peripheral is next sampled rather than where the cycle was lost.
    constexpr int kTrail = 8;
    struct Step { uint32_t pc; int64_t clock; } trail[kTrail] = {};
    int trailAt = 0;

    // ── the vertical blank has to come from HERE ────────────────────────
    // On this family the 60.15 Hz VBL is not generated by MacMemory::tick()
    // the way every other platform's is — MacFrameClock::runFrame() raises
    // VIA CA1 itself, between two runUntil() calls (MacFrame.h). A stepping
    // harness that only calls runCycles() therefore never delivers a single
    // vertical blank, and the machine spins in the ROM's early boot forever.
    //
    // That is not a hypothetical: the first version of this gate did
    // exactly that and looked *convincing* while doing it — 2.5 M
    // checkpoints, 666 M guest cycles, 83 M instructions fetched through
    // the window, all green. Every one of those fetches came from ROM
    // (instrumented and counted: RAM fetches = 0), because the guest never
    // got as far as running code out of RAM. It would have proved nothing
    // about the contention charge, which only applies to RAM.
    //
    // So the loop below reproduces runFrame()'s shape — run to the vblank
    // edge, raise CA1 on BOTH machines, run out the frame — while stepping
    // and comparing inside each stretch.
    long i = 0;
    bool done = false, diverged = false;

    auto compare = [&]() {
        const State r = capture(cpuRef);
        const State j = capture(cpuJit);
        const Devices dr = captureDevices(memRef);
        const Devices dj = captureDevices(memJit);
        const uint32_t bad = ramDiff(memRef, memJit);
        if (same(r, j) && same(dr, dj) && bad == 0xFFFFFFFF) return true;

        std::printf("[jit_lockstep_68000] DIVERGED after %ld steps\n", i);
        if (r.clock != j.clock)
            std::printf("  *** CLOCK DIVERGENCE — this is the failure mode "
                        "pomJitFetch000 exists to prevent ***\n");
        std::printf("  last boundaries (jit): ");
        for (int k = 0; k < kTrail; k++) {
            const Step& t = trail[(trailAt + k) % kTrail];
            if (t.pc) std::printf("$%08X@%lld ", t.pc, (long long)t.clock);
        }
        std::printf("\n");
        std::printf("  pc    interp=%08X  jit=%08X\n", r.pc, j.pc);
        std::printf("  sr    interp=%04X      jit=%04X\n", r.sr, j.sr);
        std::printf("  clock interp=%lld      jit=%lld  (delta %+lld)\n",
                    (long long)r.clock, (long long)j.clock,
                    (long long)(j.clock - r.clock));
        for (int k = 0; k < 8; k++)
            if (r.d[k] != j.d[k])
                std::printf("  D%d    interp=%08X  jit=%08X\n", k, r.d[k], j.d[k]);
        for (int k = 0; k < 8; k++)
            if (r.a[k] != j.a[k])
                std::printf("  A%d    interp=%08X  jit=%08X\n", k, r.a[k], j.a[k]);
        if (r.usp != j.usp) std::printf("  USP   interp=%08X  jit=%08X\n", r.usp, j.usp);
        if (r.isp != j.isp) std::printf("  ISP   interp=%08X  jit=%08X\n", r.isp, j.isp);
        if (!same(dr, dj)) {
            std::printf("  device state:\n");
            printDeviceDiff(dr, dj);
        }
        if (bad != 0xFFFFFFFF)
            std::printf("  RAM differs at $%08X: interp=%02X jit=%02X\n",
                        bad, memRef.peek8(bad), memJit.peek8(bad));
        diverged = true;
        return false;
    };

    // Step both machines toward an absolute clock target, comparing at each
    // checkpoint, never overshooting: both have to arrive at the vblank edge
    // on the same instruction or CA1 lands in two different places.
    auto stepTo = [&](int64_t target) {
        while (!done && !diverged && cpuRef.getClock() < target) {
            if (i >= steps) { done = true; return; }
            const long b = (fineAt >= 0 && i >= fineAt) ? 1 : budget;
            const int64_t room = std::min<int64_t>(b, target - cpuRef.getClock());
            trail[trailAt] = { cpuJit.getPC(), cpuJit.getClock() };
            trailAt = (trailAt + 1) % kTrail;
            cpuRef.runCycles(room > 0 ? room : 1);
            cpuJit.runCycles(room > 0 ? room : 1);
            if (!compare()) return;
            i++;
        }
    };

    int64_t frameBase = cpuRef.getClock() - (cpuRef.getClock() % kCyclesPerFrame);
    long frames = 0;
    while (!done && !diverged) {
        stepTo(frameBase + kVblankStart);
        if (done || diverged) break;
        memRef.via().raiseCa1();                 // vertical blank, both sides
        memJit.via().raiseCa1();
        memRef.updateIrq();
        memJit.updateIrq();
        stepTo(frameBase + kCyclesPerFrame);
        frameBase += kCyclesPerFrame;
        frames++;
    }
    if (diverged) return 1;

    // A green gate that exercised nothing is worse than a red one. The
    // engine must have retired real instructions THROUGH THE WINDOW, or the
    // comparison above proved only that the interpreter equals itself —
    // which is exactly what the compacts' first wiring did, reporting the
    // engine ON while retiring zero instructions (POM68K_JIT.md § 3.1).
    const jit::Stats::Snapshot s = cpuJit.jit().stats().snapshot();
    const Devices fin = captureDevices(memRef);
    std::printf("[jit_lockstep_68000] OK — %ld steps identical "
                "(clock included), %lld cycles / %ld frames of guest time\n",
                i, (long long)cpuJit.getClock(), frames);
    std::printf("  reached: overlay=%s IWM polls=%ld track=%d\n",
                fin.overlay ? "UP (still in early boot!)" : "down",
                fin.iwmReads, fin.track);
    std::printf("  jit instrs %llu · window %llu covered / %llu armed"
                " (%llu refused) · blocks %llu compiled / %llu replayed\n",
                (unsigned long long)s.instrs,
                (unsigned long long)s.windowInstrs,
                (unsigned long long)s.windowArmed,
                (unsigned long long)s.windowFailed,
                (unsigned long long)s.blocksCompiled,
                (unsigned long long)s.blocksRun);
    if (s.instrs == 0) {
        std::printf("[jit_lockstep_68000] FAIL: the JIT never retired an "
                    "instruction — this gate proved nothing\n");
        return 1;
    }
    // …and it must have retired them THROUGH THE WINDOW, which is the only
    // thing under test here. `windowArmed` is the right question and
    // `windowInstrs` is not: the latter counts only the direct
    // fetch-window loop (Engine::runWindow), so with the block path on it
    // stays at zero while every replayed instruction is still fetched
    // through the window — pomJitCovers() gates each one. Asserting on
    // windowInstrs alone failed the block variant on its first run, which
    // is precisely the false-negative this check exists to prevent, aimed
    // at itself.
    if (s.windowArmed == 0 || (s.windowInstrs == 0 && s.blocksRun == 0)) {
        std::printf("[jit_lockstep_68000] FAIL: no instruction was fetched "
                    "through the window (armed %llu, window %llu, blocks %llu)"
                    " — pomJitFetch000 was never executed, so this gate "
                    "proved nothing about it\n",
                    (unsigned long long)s.windowArmed,
                    (unsigned long long)s.windowInstrs,
                    (unsigned long long)s.blocksRun);
        return 1;
    }
    if (jit::blockCacheEnabled(cpuJit.jit().nativeBackend()) && s.blocksRun == 0) {
        std::printf("[jit_lockstep_68000] FAIL: POM68K_JIT_BLOCKS is on but no "
                    "block was ever replayed — the block path proved nothing\n");
        return 1;
    }
    // The reach assertion, and the reason it is here. The contention charge
    // pomJitFetch000 hands back only applies to RAM (Cpu68k::applyContention
    // returns immediately for ROM and I/O), so a run that never executes
    // guest code out of RAM cannot possibly catch a missing one — however
    // many hundreds of millions of cycles it reports. The overlay dropping
    // is what puts RAM under the program counter, and a floppy poll is what
    // says the boot actually got somewhere. Both were FALSE in the first
    // version of this gate, which was green.
    if (!diskPath.empty() && (fin.overlay || fin.iwmReads == 0)) {
        std::printf("[jit_lockstep_68000] FAIL: the run never left early boot "
                    "(overlay %s, %ld IWM polls) — no guest code ran from RAM, "
                    "so the contention path was never under test\n",
                    fin.overlay ? "still up" : "down", fin.iwmReads);
        return 1;
    }
    return 0;
}
