// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Gate for the debugger service (src/DebugSession.h, DebugCpuTarget.h) at
// the real machine ownership boundary: a MachineHost runs its own machine
// thread, this test is the GUI thread, and the two exchange only commands
// and snapshots. One synthetic rig per CPU family the tree emulates —
// 68000 (Plus map), 68020 (Mac II map), 68030 (LC II map), 68040 (Quadra
// 605 map) — runs a three-instruction loop in RAM. No ROM, no media.
//
// What is gated: a breakpoint stops BEFORE its instruction and exactly one
// loop iteration apart; a step retires exactly one instruction; Continue
// resumes; breakpoints added and removed while the machine runs; Pause is
// acknowledged at an instruction boundary and holds the clock; inspection
// while paused; a reset while paused republishes; the effective engine is
// the interpreter while stops are armed and the user's engine afterwards;
// the debugger's request bits never enter a save state; teardown releases a
// breakpoint's hold.

#include "Cpu020.h"
#include "Cpu030.h"
#include "Cpu040.h"
#include "Cpu68k.h"
#include "DemoRom.h"
#include "MacIIMemory.h"
#include "MacMemory.h"
#include "MachineHost.h"
#include "PortableEnv.h"
#include "Q605Memory.h"
#include "SaveStateMachines.h"
#include "V8Memory.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

static int gFails = 0;
static void check(bool ok, const char* family, const char* what) {
    std::printf("%s [%s] %s\n", ok ? "ok  " : "FAIL", family, what);
    if (!ok) gFails++;
}

namespace {

using pom68k::dbg::ByteState;
using pom68k::dbg::Command;
using pom68k::dbg::Snapshot;
using pom68k::dbg::Space;
using pom68k::dbg::StopReason;

struct FakeAudio {
    bool started() const { return false; }
    size_t buffered() const { return 0; }
    size_t targetBuffered() const { return 1; }
    void pushRaw(std::vector<float>&, int) {}
    void pushFrame(std::vector<float>&, int) {}
    void pushRawStereo(std::vector<float>&, int) {}
    void pushFrameStereo(std::vector<float>&, int) {}
};

template <class Mem, class Cpu>
struct Host : MachineHost<Host<Mem, Cpu>, Mem, Cpu, FakeAudio> {
    using Base = MachineHost<Host<Mem, Cpu>, Mem, Cpu, FakeAudio>;
    using Base::Base;
    static constexpr bool kStereo = false;
    static constexpr moira::i64 kQuantum = 20000;
    std::atomic<long> quanta{0};

    int64_t frameCycles() const { return kQuantum; }
    void emulateQuantum() {
        this->cpu.runCycles(kQuantum);
        ++quanta;
        this->framesRun_++;
    }
    bool drainAudio() { return false; }
    void renderFrame(std::vector<uint32_t>& fb, int& w, int& h) {
        w = h = 2;
        fb.assign(4, 0);
    }
    void publishStatus() {}
};

// L0: MOVEQ #0,D0 / L: ADDQ.L #1,D0 / ADDQ.L #1,D1 / BRA.S L
constexpr uint32_t kCode = 0x2000, kLoop = 0x2002, kBp = 0x2004, kBra = 0x2006;
constexpr uint8_t kProgram[] = {0x70, 0x00, 0x52, 0x80, 0x52, 0x81, 0x60, 0xFA};

template <class Mem>
bool placeProgram(Mem& mem) {
    uint32_t len = 0;
    uint8_t* p = mem.dataSpan(kCode, len, true);
    if (!p || len < sizeof kProgram) return false;
    std::memcpy(p, kProgram, sizeof kProgram);
    return true;
}

template <class Cpu>
void startAt(Cpu& cpu) {
    cpu.setSR(0x2700);                      // supervisor, every IRQ masked
    cpu.setD(0, 0);
    cpu.setD(1, 0);
    cpu.debugger.jump(kCode);
    cpu.setPC0(kCode);
}

using SnapPtr = std::shared_ptr<const Snapshot>;

// Polls the published snapshot (the GUI's only view) until `ok` holds.
SnapPtr waitFor(pom68k::dbg::Session& s, const std::function<bool(const Snapshot&)>& ok,
                int ms = 5000) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    for (;;) {
        SnapPtr snap = s.snapshot();
        if (ok(*snap)) return snap;
        if (std::chrono::steady_clock::now() > end) return nullptr;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

uint64_t post(pom68k::dbg::Session& s, Command::Kind k, uint32_t addr = 0) {
    Command c;
    c.kind = k;
    c.addr = addr;
    return s.post(c);
}

template <class Mem, class Cpu>
void scenario(const char* fam, Mem& mem, Cpu& cpu, int engine,
              const std::function<void()>& overlayOff) {
    overlayOff();
    check(!mem.overlay(), fam, "the boot overlay is down");
    check(placeProgram(mem), fam, "the loop is written into plain RAM");
    cpu.setEngine(engine);
    startAt(cpu);
    FakeAudio audio;
    Host<Mem, Cpu> host(mem, cpu, audio);
    auto& dbg = host.debug;
    host.start();

    // ── A breakpoint stops before its instruction ──────────────────────
    uint64_t id = post(dbg, Command::Kind::AddBreakpoint, kBp);
    SnapPtr s = waitFor(dbg, [&](const Snapshot& x) {
        return x.acked >= id && x.stopped && x.reason == StopReason::Breakpoint;
    });
    check(s != nullptr, fam, "a breakpoint posted while running stops the CPU");
    if (!s) { host.stop(); return; }
    check(s->regs.pc == kBp, fam, "the stop is at the breakpoint, before it executes");
    check(s->regs.d[0] == s->regs.d[1] + 1, fam,
          "ADDQ D0 retired, ADDQ D1 not yet — an instruction boundary");
    check(s->inQuantum, fam, "a breakpoint holds the machine inside its quantum");
    check(s->breakpoints.size() == 1 && s->breakpoints[0] == kBp, fam,
          "the snapshot lists the breakpoint");
    check(s->requestedEngine == engine && s->effectiveEngine == 0, fam,
          "armed stops run on the interpreter, the request is kept");
    check(!s->disasm.empty() && s->disasm[0].addr == kBp &&
              s->disasm[0].readable && s->disasm[0].breakpoint &&
              s->disasm[0].text.find("addq") != std::string::npos,
          fam, "disassembly follows the PC through the logical view");
    const uint32_t d0 = s->regs.d[0];
    const long quantaAtStop = host.quanta.load();
    // Engine-retired instructions, read while the machine is held.
    auto engineInstrs = [&] { return cpu.jit().stats().snapshot().instrs; };
    const uint64_t engineAtStop = engineInstrs();

    // ── Step retires exactly one instruction ───────────────────────────
    uint64_t g = s->generation;
    id = post(dbg, Command::Kind::Step);
    s = waitFor(dbg, [&](const Snapshot& x) {
        return x.generation > g && x.acked >= id && x.stopped &&
               x.reason == StopReason::Step;
    });
    check(s && s->regs.pc == kBra && s->regs.d[1] == d0 && s->regs.d[0] == d0,
          fam, "one step: PC at BRA, D1 caught up with D0");
    check(host.quanta.load() == quantaAtStop, fam,
          "the step completes inside the same quantum");

    // ── Continue: exactly one more iteration ───────────────────────────
    g = s ? s->generation : 0;
    id = post(dbg, Command::Kind::Continue);
    s = waitFor(dbg, [&](const Snapshot& x) {
        return x.generation > g && x.acked >= id && x.stopped &&
               x.reason == StopReason::Breakpoint;
    });
    check(s && s->regs.pc == kBp && s->regs.d[0] == d0 + 1, fam,
          "Continue stops on the next pass, one iteration later");
    check(engineInstrs() == engineAtStop, fam,
          "while a stop is armed the accelerated engine retires nothing");

    // ── Remove while stopped, run, add while running ───────────────────
    post(dbg, Command::Kind::RemoveBreakpoint, kBp);
    id = post(dbg, Command::Kind::Continue);
    s = waitFor(dbg, [&](const Snapshot& x) {
        return x.acked >= id && !x.stopped && x.breakpoints.empty();
    });
    check(s && s->effectiveEngine == engine, fam,
          "with no stop armed the user's engine runs again");
    const long q0 = host.quanta.load();
    waitFor(dbg, [&](const Snapshot&) { return host.quanta.load() > q0 + 2; });
    check(host.quanta.load() > q0 + 2, fam, "the machine runs without a breakpoint");
    check((engineInstrs() > engineAtStop) == (engine == 1), fam,
          engine ? "disarmed, the accelerated engine retires the loop again"
                 : "the interpreter engine never retires through the JIT");
    id = post(dbg, Command::Kind::AddBreakpoint, kLoop);
    s = waitFor(dbg, [&](const Snapshot& x) {
        return x.acked >= id && x.stopped && x.reason == StopReason::Breakpoint;
    });
    check(s && s->regs.pc == kLoop && s->regs.d[0] == s->regs.d[1], fam,
          "a breakpoint added while running stops at its own address");
    post(dbg, Command::Kind::ClearBreakpoints);
    id = post(dbg, Command::Kind::Continue);
    s = waitFor(dbg, [&](const Snapshot& x) { return x.acked >= id && !x.stopped; });
    check(s != nullptr, fam, "Continue after clearing resumes");

    // ── Pause is acknowledged at a boundary and holds the clock ────────
    id = post(dbg, Command::Kind::Pause);
    s = waitFor(dbg, [&](const Snapshot& x) {
        return x.acked >= id && x.stopped && x.reason == StopReason::Pause;
    });
    check(s && !s->inQuantum, fam, "Pause is acknowledged between two quanta");
    check(s && s->regs.pc >= kLoop && s->regs.pc <= kBra && !(s->regs.pc & 1), fam,
          "the paused PC is one of the loop's instruction addresses");
    if (s) {
        const int64_t clock = s->coreClock;
        const long q = host.quanta.load();
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        Command view;
        view.kind = Command::Kind::ViewMemory;
        view.addr = kCode;
        view.length = sizeof kProgram;
        view.space = Space::Logical;
        id = dbg.post(view);
        s = waitFor(dbg, [&](const Snapshot& x) { return x.acked >= id; });
        check(s && s->coreClock == clock && host.quanta.load() == q, fam,
              "a paused machine runs no quantum and its clock holds");
        bool same = s && s->memory.bytes.size() == sizeof kProgram;
        for (size_t i = 0; same && i < sizeof kProgram; ++i)
            same = s->memory.bytes[i] == kProgram[i] &&
                   s->memory.state[i] == ByteState::Ok;
        check(same, fam, "memory is inspectable while paused");
    }

    // ── Teardown releases a hold inside a quantum ──────────────────────
    post(dbg, Command::Kind::AddBreakpoint, kBp);
    id = post(dbg, Command::Kind::Continue);
    s = waitFor(dbg, [&](const Snapshot& x) {
        return x.acked >= id && x.stopped && x.reason == StopReason::Breakpoint;
    });
    check(s && s->regs.pc == kBp, fam, "a breakpoint re-armed after a pause stops");
    host.stop();                               // must not hang
    check(true, fam, "stop() joins a machine thread held at a breakpoint");

    // ── A reset while paused is republished; breakpoints survive it ────
    // A second host on the same CPU: the first one's thread is gone, so the
    // test may touch the CPU again.
    Host<Mem, Cpu> again(mem, cpu, audio);
    auto& dbg2 = again.debug;
    id = post(dbg2, Command::Kind::Pause);     // before the first quantum
    again.start();
    s = waitFor(dbg2, [&](const Snapshot& x) {
        return x.acked >= id && x.stopped && x.reason == StopReason::Pause;
    });
    check(s && s->breakpoints.size() == 1, fam,
          "the CPU's breakpoint list outlives its host session");
    if (s) {
        const uint64_t gen = s->generation;
        const uint32_t pc = s->regs.pc;
        again.push({Host<Mem, Cpu>::Cmd::HardReset});
        s = waitFor(dbg2, [&](const Snapshot& x) {
            return x.generation > gen && x.stopped && x.regs.pc != pc;
        });
        check(s != nullptr, fam, "a reset applied while paused republishes the registers");
        check(s && s->breakpoints.size() == 1 && s->breakpoints[0] == kBp, fam,
              "breakpoints survive a reset");
    }
    again.stop();
    cpu.debugger.breakpoints.removeAll();
}

// Moira-level: the request bits are host state. A state saved with a
// breakpoint armed is byte-identical to one saved without, and loading a
// state never arms nor disarms the live breakpoint list.
struct CountingHook final : pom68k::dbg::StopHook {
    int hits = 0;
    void cpuStopped(bool, moira::u32) override { ++hits; }
};

void saveStateIsolation(Q605Memory& mem, Cpu040& cpu) {
    const char* fam = "68040 state";
    (void)mem.read8(0x40000000);
    placeProgram(mem);
    cpu.setEngine(0);
    startAt(cpu);
    cpu.runCycles(200);
    std::vector<uint8_t> plain, armed;
    pom68k::save(mem, cpu, pom68k::SnapMachine::Q605, plain);
    cpu.debugger.breakpoints.setAt(kBp);
    pom68k::save(mem, cpu, pom68k::SnapMachine::Q605, armed);
    check(plain == armed, fam, "an armed breakpoint does not change a save state");

    CountingHook hook;
    cpu.setDebugStopHook(&hook);
    std::string err;
    check(pom68k::load(mem, cpu, pom68k::SnapMachine::Q605, plain.data(),
                       plain.size(), err), fam, "the plain state loads");
    cpu.runCycles(200);
    check(hook.hits > 0, fam, "a live breakpoint stays armed across a load");
    cpu.debugger.breakpoints.removeAll();
    hook.hits = 0;
    check(pom68k::load(mem, cpu, pom68k::SnapMachine::Q605, armed.data(),
                       armed.size(), err), fam, "the armed-at-save state loads");
    cpu.runCycles(200);
    check(hook.hits == 0, fam, "a state saved while armed arms nothing");
    cpu.setDebugStopHook(nullptr);
}

} // namespace

int main() {
    const auto& cfg = pom68k::defaultCoreConfig();
    {
        static MacMemory mem(cfg, MacMemory::Model::Plus);
        mem.installRom(kDemoRom, kDemoRomSize);
        static Cpu68k cpu(mem, jit::defaultResolvedConfig());
        mem.setCpu(&cpu);
        cpu.hardReset();
        scenario("68000", mem, cpu, 0, [&] {
            mem.write8(0xEFE7FE, 0xFF);        // VIA DDRA: PA out
            mem.write8(0xEFE3FE, 0x00);        // VIA ORA: PA4 low, overlay off
        });
    }
    {
        static MacIIMemory mem(cfg);
        static Cpu020 cpu(mem, jit::defaultResolvedConfig(), cfg.cpu);
        mem.setCpu(&cpu);
        scenario("68020", mem, cpu, 0, [&] {
            mem.write8(0x50000200, 0x00);      // VIA1 ORA: PA4 low, overlay off
        });
    }
    // A reset leaves a ROM-less CPU halted on its $FFFFFFFF vectors, so
    // each 68030/68040 case gets a fresh rig rather than inheriting it.
    auto lcii = [&](const char* fam, int engine) {
        auto mem = std::make_unique<V8Memory>(cfg);
        auto cpu = std::make_unique<Cpu030>(*mem, jit::defaultResolvedConfig(),
                                            cfg.cpu);
        mem->setCpu(cpu.get());
        V8Memory& m = *mem;
        scenario(fam, m, *cpu, engine, [&] { (void)m.read8(0xA00000); });
    };
    lcii("68030", 0);
    lcii("68030 accelerated", 1);
    auto q605 = [&](const char* fam, int engine) {
        auto mem = std::make_unique<Q605Memory>(cfg, 8u << 20);
        auto cpu = std::make_unique<Cpu040>(*mem, jit::defaultResolvedConfig(),
                                            cfg.cpu, cfg.diagnostics);
        mem->setCpu(cpu.get());
        Q605Memory& m = *mem;
        if (fam) scenario(fam, m, *cpu, engine, [&] { (void)m.read8(0x40000000); });
        else saveStateIsolation(m, *cpu);
    };
    q605("68040", 0);
    q605("68040 accelerated", 1);
    q605(nullptr, 0);
    std::printf("%s\n", gFails ? "debug_session_test: FAILED"
                               : "debug_session_test: all checks passed");
    return gFails ? 1 : 0;
}
