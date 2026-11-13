// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The SST oracle corpora replayed through the translator (TODO.md § Moteur
// › Socle, "Rejouer les corpus à travers le JIT"). Shared by sst68030.cpp
// and sst68040.cpp when they are built with SST_JIT=1 (targets
// sst68030_jit / sst68040_jit).
//
// Each vector's initial state is loaded into two cores on identical flat
// buses: the interpreter, which the plain sst gates judge against WinUAE,
// and the same core driven by jit::Engine. Both run to the SAME clock
// target — the cycle at which the interpreter finished the vector's own
// unit, plus a slack that carries both into the words after it — and every
// register, the clock, the halt state and the WHOLE bus must agree. The
// interpreter is the reference (CLAUDE.md, "The interpreter is the accuracy
// oracle"); this harness judges the translator against it on states no
// Macintosh boot reaches.
//
// A first pass traces and translates. The tracer interprets the block it
// records, so on that pass the vector's own instruction is not generated
// code; the judged pass reloads the same bytes and state over the warm
// block cache (--cold skips the warm-up). Between the passes the
// translated core's whole bus is cleared: generated code and the data
// window store straight into memory without passing write8, so no list of
// written addresses is complete on that side (found on the first run: a
// warm-pass store read back by the judged pass looked like a wrong flag).

#pragma once

#include "JitTestConfig.h"
#include "jit/JitEngine.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

namespace sstjit {

struct Options {
    long slack = 64;        // --slack N: cycles run past the vector's unit
    bool warm = true;       // --cold: judge the tracer's own pass
    bool ring = false;      // --ring: print the last dispatches on a failure
    bool fpu = false;       // compare the FP registers too
};

// Cpu: the harness's TestCpu (a moira::Moira with `mem`, `dirty`,
// `clearDirty()` and, under SST_JIT, a `guard` the write path notes).
template <class Cpu>
struct Pair {
    std::unique_ptr<Cpu> ref, nat;
    std::unique_ptr<jit::Engine> engine;
};

template <class Cpu>
jit::MemoryHooks hooks(Cpu* cpu)
{
    jit::MemoryHooks h;
    h.self = cpu;
    h.codeSpan = [](void* s, uint32_t p, uint32_t& len) -> const uint8_t* {
        auto& c = *static_cast<Cpu*>(s);
        p &= uint32_t(c.mem.size() - 1);
        len = uint32_t(c.mem.size()) - p;
        return c.mem.data() + p;
    };
    h.dataSpan = [](void* s, uint32_t p, uint32_t& len, int) -> uint8_t* {
        auto& c = *static_cast<Cpu*>(s);
        p &= uint32_t(c.mem.size() - 1);
        len = uint32_t(c.mem.size()) - p;
        return c.mem.data() + p;
    };
    h.setGuard = [](void* s, jit::CodeGuard* g) { static_cast<Cpu*>(s)->guard = g; };
    h.ramBytes = [](void* s) { return uint32_t(static_cast<Cpu*>(s)->mem.size()); };
    return h;
}

// Heap-owned: jit::Engine is about a megabyte inline
// (tests/jit_asset_free_lockstep_test.cpp, SyntheticCpu).
template <class Cpu>
void build(Pair<Cpu>& p, moira::Model model, uint32_t guestFamily)
{
    static const jit::ResolvedConfig config = testjit::resolveFromEnvironment();
    p.ref = std::make_unique<Cpu>();
    p.nat = std::make_unique<Cpu>();
    p.ref->setModel(model);
    p.nat->setModel(model);
    p.engine = std::make_unique<jit::Engine>(*p.nat, hooks(p.nat.get()),
                                             guestFamily, config);
    p.engine->setEnabled(true);
}

template <class Cpu>
bool same(Cpu& a, Cpu& b, bool fpu, std::string& why)
{
    char buf[256];
    auto diff = [&](const char* what, unsigned long x, unsigned long y) {
        std::snprintf(buf, sizeof buf, "%s interp $%lX jit $%lX", what, x, y);
        why = buf;
        return false;
    };
    if (a.getPC0() != b.getPC0()) return diff("PC", a.getPC0(), b.getPC0());
    if (a.getClock() != b.getClock())
        return diff("clock", (unsigned long)a.getClock(), (unsigned long)b.getClock());
    for (int i = 0; i < 8; i++)
        if (a.getD(i) != b.getD(i)) { char w[4] = {'D', char('0' + i), 0}; return diff(w, a.getD(i), b.getD(i)); }
    for (int i = 0; i < 7; i++)
        if (a.getA(i) != b.getA(i)) { char w[4] = {'A', char('0' + i), 0}; return diff(w, a.getA(i), b.getA(i)); }
    if (a.getSP()  != b.getSP())  return diff("A7",  a.getSP(),  b.getSP());
    if (a.getUSP() != b.getUSP()) return diff("USP", a.getUSP(), b.getUSP());
    if (a.getISP() != b.getISP()) return diff("ISP", a.getISP(), b.getISP());
    if (a.getMSP() != b.getMSP()) return diff("MSP", a.getMSP(), b.getMSP());
    if (a.getSR()  != b.getSR())  return diff("SR",  a.getSR(),  b.getSR());
    if (a.getIRD() != b.getIRD()) return diff("IRD", a.getIRD(), b.getIRD());
    if (a.getIRC() != b.getIRC()) return diff("IRC", a.getIRC(), b.getIRC());
    if (a.isHalted() != b.isHalted()) return diff("halted", a.isHalted(), b.isHalted());
    if (a.getVBR() != b.getVBR()) return diff("VBR", a.getVBR(), b.getVBR());
    if (a.getSFC() != b.getSFC()) return diff("SFC", a.getSFC(), b.getSFC());
    if (a.getDFC() != b.getDFC()) return diff("DFC", a.getDFC(), b.getDFC());
    if (a.getCACR() != b.getCACR()) return diff("CACR", a.getCACR(), b.getCACR());
    if (a.getCAAR() != b.getCAAR()) return diff("CAAR", a.getCAAR(), b.getCAAR());
    if (a.getCRP() != b.getCRP()) return diff("CRP", (unsigned long)a.getCRP(), (unsigned long)b.getCRP());
    if (a.getSRP() != b.getSRP()) return diff("SRP", (unsigned long)a.getSRP(), (unsigned long)b.getSRP());
    if (a.getTC()  != b.getTC())  return diff("TC",  a.getTC(),  b.getTC());
    if (a.getTT0() != b.getTT0()) return diff("TT0", a.getTT0(), b.getTT0());
    if (a.getTT1() != b.getTT1()) return diff("TT1", a.getTT1(), b.getTT1());
    if (a.getMMUSR() != b.getMMUSR()) return diff("MMUSR", a.getMMUSR(), b.getMMUSR());
    if (a.getTC040() != b.getTC040()) return diff("TC040", a.getTC040(), b.getTC040());
    if (a.getMMUSR040() != b.getMMUSR040()) return diff("MMUSR040", a.getMMUSR040(), b.getMMUSR040());
    if (a.getURP040() != b.getURP040()) return diff("URP040", a.getURP040(), b.getURP040());
    if (a.getSRP040() != b.getSRP040()) return diff("SRP040", a.getSRP040(), b.getSRP040());
    if (fpu) {
        for (int i = 0; i < 8; i++) {
            uint32_t x[3], y[3];
            a.getFP(i, x); b.getFP(i, y);
            for (int j = 0; j < 3; j++)
                if (x[j] != y[j]) {
                    char w[16];
                    std::snprintf(w, sizeof w, "FP%d.%d", i, j);
                    return diff(w, x[j], y[j]);
                }
        }
        if (a.getFPCR()  != b.getFPCR())  return diff("FPCR",  a.getFPCR(),  b.getFPCR());
        if (a.getFPSR()  != b.getFPSR())  return diff("FPSR",  a.getFPSR(),  b.getFPSR());
        if (a.getFPIAR() != b.getFPIAR()) return diff("FPIAR", a.getFPIAR(), b.getFPIAR());
    }
    if (std::memcmp(a.mem.data(), b.mem.data(), a.mem.size()) != 0) {
        const auto m = std::mismatch(a.mem.begin(), a.mem.end(), b.mem.begin());
        const uint32_t addr = uint32_t(m.first - a.mem.begin());
        std::snprintf(buf, sizeof buf, "RAM[$%06X] interp $%02X jit $%02X",
                      addr, a.mem[addr], b.mem[addr]);
        why = buf;
        return false;
    }
    return true;
}

template <class Cpu>
void clearAll(Cpu& c)
{
    std::fill(c.mem.begin(), c.mem.end(), 0);
    c.dirty.clear();
}

// setup(cpu, vector) loads a vector exactly as the plain harness does;
// scrub(cpu, vector) returns the interpreter's bus to zero (its write list
// is complete: it has no engine and no data window).
template <class Cpu, class Vector, class Setup, class Scrub>
bool run(Pair<Cpu>& pr, const Vector& v, const Options& o, Setup setup,
         Scrub scrub, std::string& why)
{
    Cpu& ref = *pr.ref;
    Cpu& nat = *pr.nat;
    setup(ref, v);
    const moira::i64 c0 = ref.getClock();     // reset() adds cycles: align on it
    for (long step = 0; step < (v.steps > 0 ? v.steps : 1); ++step) ref.execute();
    const moira::i64 target = ref.getClock() + o.slack;
    pr.engine->flushAll();                     // the bytes under any old block changed
    if (o.warm) {
        setup(nat, v);
        nat.setClock(c0);
        pr.engine->executeUntil(target);
        clearAll(nat);
    }
    setup(nat, v);
    nat.setClock(c0);
    ref.executeUntil(target);
    pr.engine->executeUntil(target);
    const bool ok = same(ref, nat, o.fpu, why);
    if (!ok && o.ring) {
        const auto m = std::mismatch(ref.mem.begin(), ref.mem.end(), nat.mem.begin());
        if (m.first != ref.mem.end()) {
            const uint32_t at = uint32_t(m.first - ref.mem.begin()) & ~0xFu;
            for (uint32_t row = at - 32; row < at + 32; row += 16) {
                std::printf("      %06X interp", row);
                for (uint32_t k = 0; k < 16; k++) std::printf(" %02X", ref.mem[(row + k) & (ref.mem.size() - 1)]);
                std::printf(" | jit");
                for (uint32_t k = 0; k < 16; k++) std::printf(" %02X", nat.mem[(row + k) & (nat.mem.size() - 1)]);
                std::printf("\n");
            }
        }
        unsigned count = 0, head = 0;
        const auto* ev = pr.engine->dispatchRing(count, head);
        const unsigned n = count < 16 ? count : 16;
        if (!n) std::printf("      (no dispatch ring: set POM68K_JIT_DISPATCH_RING=1)\n");
        for (unsigned k = n; k > 0; k--) {
            const auto& e = ev[(head + jit::Engine::kDispatchRing - k) % jit::Engine::kDispatchRing];
            std::printf("      ring pc=%08X clock=%lld target=%lld kind=%u exit=%u instrs=%u\n",
                        e.pc, (long long)e.clock, (long long)e.target, e.kind, e.exit, e.instrs);
        }
    }
    scrub(ref, v);
    clearAll(nat);
    return ok;
}

// Prints the engine's census and fails the run when native code was
// required (POM68K_JIT_REQUIRE_NATIVE, the jit-fast contract) and none ran.
template <class Cpu>
bool report(const Pair<Cpu>& pr, const char* tag)
{
    const auto st = pr.engine->stats().snapshot();
    const unsigned long long native =
        st.instrs - st.slowInstrs - st.windowInstrs - st.traceInstrs;
    std::printf("[%s --jit] backend=%s blocks compiled=%llu run=%llu instrs=%llu "
                "native=%llu slow=%llu window=%llu trace=%llu interp=%llu\n",
                tag, pr.engine->backendName(),
                (unsigned long long)st.blocksCompiled, (unsigned long long)st.blocksRun,
                (unsigned long long)st.instrs, native,
                (unsigned long long)st.slowInstrs, (unsigned long long)st.windowInstrs,
                (unsigned long long)st.traceInstrs, (unsigned long long)st.interpInstrs);
    if (std::getenv("POM68K_JIT_REQUIRE_NATIVE") &&
        (!pr.engine->nativeBackend() || native == 0)) {
        std::printf("[%s --jit] FAIL: no native code ran (backend %s)\n",
                    tag, pr.engine->backendName());
        return false;
    }
    return true;
}

}  // namespace sstjit
