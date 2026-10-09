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
// breakpoint's hold. Edits while paused: a register, a PC that reloads the
// prefetch queue (the next step executes the NEW instruction), an SR that
// swaps the active stack, registers the model lacks, odd/I-O PCs and
// device writes refused, edits refused while running, and a code write
// that the accelerated engine must not outlive (translated blocks are
// dropped: the edited loop's invariant holds across JIT-run quanta).
// Access and exception stops: a write and a read watchpoint stop after
// the accessing instruction with the access described; a watched opcode
// fetch never stops; TRAP #0 and a filtered A-line stop at the handler's
// first instruction with the stacked PC (and trap word); a non-matching
// trap filter does not stop; and a catchpoint survives a CPU reset.
// Step over/out on a recursive routine: step over a recursive BSR stops at
// ITS return, not a deeper invocation's; step out of the middle level
// ignores the deepest level's RTS; TRAP and A-line are stepped over
// through their handlers; a breakpoint inside a stepped-over call wins and
// leaves no stray step behind; a Pause cancels a step out that never
// returns.

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
using pom68k::dbg::Access;
using pom68k::dbg::Catch;
using pom68k::dbg::Command;
using pom68k::dbg::Reg;
using pom68k::dbg::Watchpoint;
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

uint64_t setReg(pom68k::dbg::Session& s, Reg r, uint32_t v) {
    Command c;
    c.kind = Command::Kind::SetRegister;
    c.reg = r;
    c.value = v;
    return s.post(c);
}

uint64_t poke(pom68k::dbg::Session& s, uint32_t addr, std::vector<uint8_t> data) {
    Command c;
    c.kind = Command::Kind::WriteMemory;
    c.addr = addr;
    c.space = Space::Logical;
    c.data = std::move(data);
    return s.post(c);
}

SnapPtr ackedBy(pom68k::dbg::Session& s, uint64_t id) {
    return waitFor(s, [&](const Snapshot& x) { return x.acked >= id; });
}

// With the loop's second ADDQ edited to #2, D1 - 2*D0 is the same at every
// boundary of the loop except just before that ADDQ, where it is 2 lower.
uint32_t loopInvariant(const Snapshot& x) {
    return x.regs.d[1] - 2 * x.regs.d[0] + (x.regs.pc == kBp ? 2 : 0);
}


// The access/exception program, at $3000, with its two handlers at $3100.
constexpr uint32_t kProg2 = 0x3000, kRead = 0x3004, kTrap = 0x3008,
                   kALine = 0x300A, kAfterALine = 0x300C, kData = 0x2100,
                   kTrapHandler = 0x3100, kALineHandler = 0x3102;
const std::vector<uint8_t> kProgram2 = {
    0x21, 0xC0, 0x21, 0x00,      // MOVE.L D0,($2100).W
    0x24, 0x38, 0x21, 0x00,      // MOVE.L ($2100).W,D2
    0x4E, 0x40,                  // TRAP #0
    0xA3, 0x1E,                  // an OS A-line (_NewPtr, flag bits 9/8)
    0x52, 0x80,                  // ADDQ.L #1,D0
    0x60, 0xF0};                 // BRA.S $3000
const std::vector<uint8_t> kHandlers = {
    0x4E, 0x73,                  // $3100: RTE
    0x54, 0xAF, 0x00, 0x02,      // $3102: ADDQ.L #2,2(A7) — skip the A-line
    0x4E, 0x73};                 //        RTE

std::vector<uint8_t> be32(uint32_t v) {
    return {uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)};
}

uint64_t addWatch(pom68k::dbg::Session& s, uint32_t addr, uint8_t len, Access a) {
    Command c;
    c.kind = Command::Kind::AddWatchpoint;
    c.watch = {addr, len, a};
    return s.post(c);
}

uint64_t catchCmd(pom68k::dbg::Session& s, Command::Kind k, uint8_t vector,
                  uint16_t trap = 0) {
    Command c;
    c.kind = k;
    c.catchpoint = {vector, trap};
    return s.post(c);
}


// The recursive program for step over/out, at $3200.
constexpr uint32_t kRec = 0x3200, kRecCall = 0x3202, kRecAfter = 0x3206,
                   kSub = 0x320C, kSubCmp = 0x320E, kSubRecurse = 0x3216,
                   kSubRts = 0x321A;
const std::vector<uint8_t> kRecursive = {
    0x70, 0x00,                          // $3200 MOVEQ #0,D0
    0x61, 0x00, 0x00, 0x08,              // $3202 BSR.W sub
    0x52, 0x81,                          // $3206 ADDQ.L #1,D1
    0x60, 0xF6,                          // $3208 BRA.S $3200
    0x4E, 0x71,                          // $320A NOP
    0x52, 0x80,                          // $320C sub: ADDQ.L #1,D0
    0x0C, 0x80, 0x00, 0x00, 0x00, 0x03,  // $320E CMPI.L #3,D0
    0x6C, 0x04,                          // $3214 BGE.S $321A
    0x61, 0x00, 0xFF, 0xF4,              // $3216 BSR.W sub (recursion)
    0x4E, 0x75};                         // $321A RTS

template <class Mem, class Cpu>
void scenario(const char* fam, Mem& mem, Cpu& cpu, int engine, uint32_t ioAddr,
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

    // ── Edits while paused ─────────────────────────────────────────────
    if (s && s->stopped) {
        id = setReg(dbg, Reg::D0, 0x12345678);
        s = ackedBy(dbg, id);
        check(s && s->regs.d[0] == 0x12345678 && s->message.empty(), fam,
              "a data register edit is published");
        id = setReg(dbg, Reg::PC, kCode);
        s = ackedBy(dbg, id);
        check(s && s->regs.pc == kCode && !s->disasm.empty() &&
                  s->disasm[0].text.find("moveq") != std::string::npos,
              fam, "a PC edit moves the PC and the disassembly");
        id = post(dbg, Command::Kind::Step);
        s = waitFor(dbg, [&](const Snapshot& x) {
            return x.acked >= id && x.stopped && x.reason == StopReason::Step;
        });
        check(s && s->regs.pc == kLoop && s->regs.d[0] == 0, fam,
              "the step after a PC edit executes the new instruction (MOVEQ)");
        const uint32_t pc = s ? s->regs.pc : 0;
        for (uint32_t bad : {kCode + 1, ioAddr}) {
            id = setReg(dbg, Reg::PC, bad);
            s = ackedBy(dbg, id);
            check(s && s->regs.pc == pc && !s->message.empty(), fam,
                  bad & 1 ? "an odd PC is refused" : "a PC on a device register is refused");
        }
        const uint32_t isp = s ? s->regs.a[7] : 0;
        setReg(dbg, Reg::USP, 0x3000);
        id = setReg(dbg, Reg::SR, 0x0700);
        s = ackedBy(dbg, id);
        check(s && !s->supervisor && s->regs.a[7] == 0x3000 && s->regs.sr == 0x0700,
              fam, "an SR edit to user mode makes USP the active A7");
        id = setReg(dbg, Reg::SR, 0x2700);
        s = ackedBy(dbg, id);
        check(s && s->supervisor && s->regs.a[7] == isp && s->regs.usp == 0x3000,
              fam, "and back to supervisor restores the interrupt stack");
        const bool has020 = std::string(fam).rfind("68000", 0) != 0;
        id = setReg(dbg, Reg::MSP, 0x4000);
        s = ackedBy(dbg, id);
        check(s && (has020 ? s->regs.msp == 0x4000 && s->message.empty()
                           : !s->message.empty()),
              fam, has020 ? "MSP is editable from the 68020"
                          : "the 68000 refuses an MSP edit");
        id = poke(dbg, ioAddr, {0x00});
        s = ackedBy(dbg, id);
        check(s && !s->message.empty(), fam, "a write to a device register is refused");

        // A code edit the accelerated engine has already translated.
        id = poke(dbg, kBp, {0x54, 0x81});            // ADDQ.L #2,D1
        s = ackedBy(dbg, id);
        check(s && s->message.empty() && s->disasm.size() > 1 &&
                  s->disasm[1].text.find("#$2, D1") != std::string::npos,
              fam, "a RAM write while paused is visible to the disassembly");
        auto runThenPause = [&] {
            uint64_t c = post(dbg, Command::Kind::Continue);
            waitFor(dbg, [&](const Snapshot& x) { return x.acked >= c && !x.stopped; });
            const long q = host.quanta.load();
            waitFor(dbg, [&](const Snapshot&) { return host.quanta.load() > q + 3; });
            c = post(dbg, Command::Kind::Pause);
            return waitFor(dbg, [&](const Snapshot& x) {
                return x.acked >= c && x.stopped && x.reason == StopReason::Pause;
            });
        };
        const uint64_t before = engineInstrs();
        SnapPtr p1 = runThenPause();
        SnapPtr p2 = runThenPause();
        check(p1 && p2 && p2->regs.d[0] != p1->regs.d[0] &&
                  loopInvariant(*p1) == loopInvariant(*p2),
              fam, "the edited loop runs as edited (D1 grows twice as fast)");
        check((engineInstrs() > before) == (engine == 1), fam,
              engine ? "the accelerated engine ran the edited code"
                     : "the interpreter ran the edited code");

        // Refused while running; the paused view still shows the edit.
        uint64_t c = post(dbg, Command::Kind::Continue);
        waitFor(dbg, [&](const Snapshot& x) { return x.acked >= c && !x.stopped; });
        id = poke(dbg, kBp, {0x52, 0x81});
        s = ackedBy(dbg, id);
        check(s && !s->stopped && !s->message.empty(), fam,
              "an edit posted while running is refused");
        id = setReg(dbg, Reg::D0, 0);
        s = ackedBy(dbg, id);
        check(s && !s->message.empty(), fam, "a register edit while running too");
        c = post(dbg, Command::Kind::Pause);
        s = waitFor(dbg, [&](const Snapshot& x) {
            return x.acked >= c && x.stopped && x.reason == StopReason::Pause;
        });
        Command view;
        view.kind = Command::Kind::ViewMemory;
        view.addr = kBp;
        view.length = 2;
        id = dbg.post(view);
        s = ackedBy(dbg, id);
        check(s && s->memory.bytes.size() == 2 && s->memory.bytes[0] == 0x54, fam,
              "the refused write changed nothing");
        id = poke(dbg, kBp, {0x52, 0x81});            // restore the program
        s = ackedBy(dbg, id);
        check(s && s->message.empty(), fam, "the program is restored while paused");
    }

    // ── Access and exception stops ─────────────────────────────────────
    if (s && s->stopped) {
        const uint32_t vbr = s->regs.vbr;
        bool placed = true;
        for (auto [addr, bytes] : {std::pair{kProg2, kProgram2},
                                   std::pair{kTrapHandler, kHandlers},
                                   std::pair{vbr + 0x80, be32(kTrapHandler)},
                                   std::pair{vbr + 0x28, be32(kALineHandler)}}) {
            id = poke(dbg, addr, bytes);
            s = ackedBy(dbg, id);
            placed = placed && s && s->message.empty();
        }
        setReg(dbg, Reg(int(Reg::A0) + 7), 0x8000);
        setReg(dbg, Reg::D0, 5);
        id = setReg(dbg, Reg::PC, kProg2);
        s = ackedBy(dbg, id);
        check(placed && s && s->regs.pc == kProg2 && s->regs.a[7] == 0x8000, fam,
              "the access/exception program and its vectors are in place");
        auto runToStop = [&](StopReason want) {
            const uint64_t g = dbg.snapshot()->generation;
            const uint64_t c = post(dbg, Command::Kind::Continue);
            return waitFor(dbg, [&](const Snapshot& x) {
                return x.generation > g && x.acked >= c && x.stopped &&
                       x.reason == want;
            });
        };

        addWatch(dbg, kData, 4, Access::Write);
        s = runToStop(StopReason::Watchpoint);
        check(s && s->regs.pc == kRead && s->detail.instructionPc == kProg2 &&
                  s->detail.accessWrite && s->detail.accessAddr == kData &&
                  s->detail.accessSize == 4,
              fam, "a write watchpoint stops after the writing instruction");
        check(s && s->effectiveEngine == 0 && s->watchpoints.size() == 1, fam,
              "an armed watchpoint runs on the interpreter and is listed");
        Command view;
        view.kind = Command::Kind::ViewMemory;
        view.addr = kData;
        view.length = 4;
        id = dbg.post(view);
        s = ackedBy(dbg, id);
        check(s && s->memory.bytes == be32(5), fam,
              "the write has retired when the watchpoint stops");

        post(dbg, Command::Kind::ClearWatchpoints);
        addWatch(dbg, kData + 2, 1, Access::Read);
        s = runToStop(StopReason::Watchpoint);
        check(s && s->regs.pc == kTrap && s->detail.instructionPc == kRead &&
                  !s->detail.accessWrite && s->detail.accessAddr == kData &&
                  s->regs.d[2] == 5,
              fam, "a one-byte read watchpoint inside a long read stops after it");

        post(dbg, Command::Kind::ClearWatchpoints);
        catchCmd(dbg, Command::Kind::AddCatch, 32);
        s = runToStop(StopReason::Exception);
        check(s && s->regs.pc == kTrapHandler && s->detail.vector == 32 &&
                  s->detail.stackedPc == kALine && s->supervisor,
              fam, "TRAP #0 stops at its handler with the stacked PC");
        check(s && s->catches.size() == 1 && s->effectiveEngine == 0, fam,
              "an armed exception stop is listed and runs on the interpreter");

        // A Toolbox filter that does not match, and a watched opcode word:
        // neither stops; the breakpoint after the A-line does.
        catchCmd(dbg, Command::Kind::RemoveCatch, 32);
        catchCmd(dbg, Command::Kind::AddCatch, 10, 0xA9F4);
        addWatch(dbg, kALine, 2, Access::ReadWrite);   // prefetched by the RTE
        post(dbg, Command::Kind::AddBreakpoint, kAfterALine);
        s = runToStop(StopReason::Breakpoint);
        check(s && s->regs.pc == kAfterALine, fam,
              "a non-matching trap filter and a watched opcode fetch do not stop");
        post(dbg, Command::Kind::ClearWatchpoints);
        post(dbg, Command::Kind::RemoveBreakpoint, kAfterALine);
        catchCmd(dbg, Command::Kind::ClearCatches, 0);
        catchCmd(dbg, Command::Kind::AddCatch, 10, 0xA11E);
        s = runToStop(StopReason::Exception);
        check(s && s->regs.pc == kALineHandler && s->detail.vector == 10 &&
                  s->detail.stackedPc == kALine && s->detail.trapWord == 0xA31E,
              fam, "an OS trap filter matches its flag variants ($A11E stops $A31E)");
        id = catchCmd(dbg, Command::Kind::AddCatch, 10, 0x1234);
        s = ackedBy(dbg, id);
        check(s && !s->message.empty() && s->catches.size() == 1, fam,
              "a trap filter that is not an A-line word is refused");
        id = addWatch(dbg, kData, 17, Access::Write);
        s = ackedBy(dbg, id);
        check(s && !s->message.empty() && s->watchpoints.empty(), fam,
              "a watchpoint longer than 16 bytes is refused");

        catchCmd(dbg, Command::Kind::ClearCatches, 0);
        id = setReg(dbg, Reg::PC, kLoop);
        s = ackedBy(dbg, id);
        uint64_t c = post(dbg, Command::Kind::Continue);
        s = waitFor(dbg, [&](const Snapshot& x) { return x.acked >= c && !x.stopped; });
        check(s && s->effectiveEngine == engine && s->catches.empty(), fam,
              "with every stop removed the user's engine runs again");
        c = post(dbg, Command::Kind::Pause);
        s = waitFor(dbg, [&](const Snapshot& x) {
            return x.acked >= c && x.stopped && x.reason == StopReason::Pause;
        });
    }

    // ── Step over / step out ───────────────────────────────────────────
    if (s && s->stopped) {
        id = poke(dbg, kRec, kRecursive);
        s = ackedBy(dbg, id);
        check(s && s->message.empty(), fam, "the recursive program is in place");
        setReg(dbg, Reg(int(Reg::A0) + 7), 0x8000);
        setReg(dbg, Reg(int(Reg::D0) + 1), 0);
        id = setReg(dbg, Reg::PC, kRec);
        s = ackedBy(dbg, id);
        auto stepCmd = [&](Command::Kind k, StopReason want) {
            const uint64_t g = dbg.snapshot()->generation;
            const uint64_t c = post(dbg, k);
            return waitFor(dbg, [&](const Snapshot& x) {
                return x.generation > g && x.acked >= c && x.stopped &&
                       x.reason == want;
            });
        };
        auto runToBp = [&](uint32_t at) {
            post(dbg, Command::Kind::AddBreakpoint, at);
            const uint64_t g = dbg.snapshot()->generation;
            const uint64_t c = post(dbg, Command::Kind::Continue);
            SnapPtr r = waitFor(dbg, [&](const Snapshot& x) {
                return x.generation > g && x.acked >= c && x.stopped &&
                       x.reason == StopReason::Breakpoint;
            });
            post(dbg, Command::Kind::RemoveBreakpoint, at);
            return r;
        };

        // Depth 1 is about to recurse (D0 = 1).
        s = runToBp(kSubRecurse);
        check(s && s->regs.pc == kSubRecurse && s->regs.d[0] == 1, fam,
              "stopped on the recursive BSR at depth 1");
        const uint32_t sp1 = s ? s->regs.a[7] : 0;
        s = stepCmd(Command::Kind::StepOver, StopReason::Step);
        check(s && s->regs.pc == kSubRts && s->regs.a[7] == sp1 && s->regs.d[0] == 3,
              fam, "step over a recursive BSR stops at its own return, not a deeper one");
        s = stepCmd(Command::Kind::StepOver, StopReason::Step);
        check(s && s->regs.pc == kRecAfter && s->regs.a[7] == sp1 + 4, fam,
              "step over an RTS is one step");

        // Depth 2, mid-routine: step out ignores depth 3's RTS.
        s = runToBp(kSubCmp);
        s = s && s->regs.d[0] == 2 ? s : runToBp(kSubCmp);
        check(s && s->regs.pc == kSubCmp && s->regs.d[0] == 2, fam,
              "stopped at depth 2");
        const uint32_t sp2 = s ? s->regs.a[7] : 0;
        s = stepCmd(Command::Kind::StepOut, StopReason::Step);
        check(s && s->regs.pc == kSubRts && s->regs.a[7] == sp2 + 4 && s->regs.d[0] == 3,
              fam, "step out returns from depth 2 only (A7 back above it)");

        // A plain instruction, TRAP and an A-line, through their handlers.
        id = setReg(dbg, Reg::PC, kRec);
        s = stepCmd(Command::Kind::StepOver, StopReason::Step);
        check(s && s->regs.pc == kRecCall && s->regs.d[0] == 0, fam,
              "step over a plain instruction is one step");
        const uint32_t sp0 = s ? s->regs.a[7] : 0;
        id = setReg(dbg, Reg::PC, kTrap);
        s = stepCmd(Command::Kind::StepOver, StopReason::Step);
        check(s && s->regs.pc == kALine && s->regs.a[7] == sp0, fam,
              "step over TRAP #0 runs its handler and stops after it");
        s = stepCmd(Command::Kind::StepOver, StopReason::Step);
        check(s && s->regs.pc == kAfterALine && s->regs.a[7] == sp0, fam,
              "step over an A-line runs the trap and stops after it");

        // A breakpoint inside the call wins; no stray step stop follows.
        id = setReg(dbg, Reg::PC, kRecCall);
        s = ackedBy(dbg, id);
        post(dbg, Command::Kind::AddBreakpoint, kSubCmp);
        s = stepCmd(Command::Kind::StepOver, StopReason::Breakpoint);
        check(s && s->regs.pc == kSubCmp, fam,
              "a breakpoint inside a stepped-over call stops there");
        post(dbg, Command::Kind::RemoveBreakpoint, kSubCmp);
        auto runsFree = [&] {
            const uint64_t c0 = post(dbg, Command::Kind::Continue);
            waitFor(dbg, [&](const Snapshot& x) { return x.acked >= c0 && !x.stopped; });
            const long q0 = host.quanta.load();
            waitFor(dbg, [&](const Snapshot&) { return host.quanta.load() > q0 + 2; });
            const SnapPtr r = dbg.snapshot();
            const bool free = !r->stopped && r->effectiveEngine == engine;
            const uint64_t p0 = post(dbg, Command::Kind::Pause);
            waitFor(dbg, [&](const Snapshot& x) {
                return x.acked >= p0 && x.stopped && x.reason == StopReason::Pause;
            });
            return free;
        };
        s = runToBp(kRecAfter);
        check(s && s->regs.pc == kRecAfter, fam,
              "the next breakpoint is the next stop: the abandoned step over left none");

        // A step out that never returns (the top-level loop) is cancelled
        // by Pause, and the cancelled run leaves no stop behind.
        id = setReg(dbg, Reg::PC, kRec);
        id = setReg(dbg, Reg(int(Reg::A0) + 7), 0x8000);
        s = ackedBy(dbg, id);
        uint64_t c = post(dbg, Command::Kind::StepOut);
        waitFor(dbg, [&](const Snapshot& x) { return x.acked >= c && !x.stopped; });
        const long q = host.quanta.load();
        waitFor(dbg, [&](const Snapshot&) { return host.quanta.load() > q + 2; });
        check(!dbg.snapshot()->stopped, fam, "a step out with no return keeps running");
        c = post(dbg, Command::Kind::Pause);
        s = waitFor(dbg, [&](const Snapshot& x) {
            return x.acked >= c && x.stopped && x.reason == StopReason::Pause;
        });
        check(s != nullptr, fam, "Pause cancels it");
        check(runsFree(), fam,
              "after the cancelled run the machine runs free on the user's engine");
        id = setReg(dbg, Reg::PC, kLoop);
        s = ackedBy(dbg, id);
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

// Moira-level: Debugger::reset re-arms CHECK_CP like CHECK_BP/WP, so an
// exception stop survives a CPU reset (it used to stay listed, dead).
struct ExceptionHook final : pom68k::dbg::StopHook {
    int traps = 0;
    void cpuStopped(bool, moira::u32) override {}
    void cpuException(moira::u8 v) override { traps += v == 32; }
};

void catchSurvivesReset(MacMemory& mem, Cpu68k& cpu) {
    const char* fam = "68000 reset";
    ExceptionHook hook;
    cpu.setDebugStopHook(&hook);
    cpu.debugger.catchpoints.setAt(32);
    cpu.hardReset();
    mem.write8(0xEFE7FE, 0xFF);
    mem.write8(0xEFE3FE, 0x00);                // overlay off, RAM at 0
    uint32_t len = 0;
    uint8_t* p = mem.dataSpan(kProg2, len, true);
    std::memcpy(p, kProgram2.data(), kProgram2.size());
    p = mem.dataSpan(kTrapHandler, len, true);
    std::memcpy(p, kHandlers.data(), kHandlers.size());
    p = mem.dataSpan(0x80, len, true);
    std::memcpy(p, be32(kTrapHandler).data(), 4);
    p = mem.dataSpan(0x28, len, true);
    std::memcpy(p, be32(kALineHandler).data(), 4);
    cpu.setSR(0x2700);
    cpu.setA(7, 0x8000);
    cpu.debugger.jump(kProg2);
    cpu.setPC0(kProg2);
    cpu.runCycles(2000);
    check(hook.traps > 0, fam, "a catchpoint armed before a reset still fires");
    // Moira calls catchpointMatches() unconditionally, so the trap above
    // fires either way; what the reset used to lose was CHECK_CP itself —
    // the flag that keeps the accelerated engine out while a stop is armed.
    check(!cpu.pomJitIdle(), fam, "and still keeps the accelerated engine out");
    cpu.debugger.catchpoints.removeAll();
    cpu.setDebugStopHook(nullptr);
}

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
        scenario("68000", mem, cpu, 0, 0xEFE1FE, [&] {
            mem.write8(0xEFE7FE, 0xFF);        // VIA DDRA: PA out
            mem.write8(0xEFE3FE, 0x00);        // VIA ORA: PA4 low, overlay off
        });
        catchSurvivesReset(mem, cpu);
    }
    {
        static MacIIMemory mem(cfg);
        static Cpu020 cpu(mem, jit::defaultResolvedConfig(), cfg.cpu);
        mem.setCpu(&cpu);
        scenario("68020", mem, cpu, 0, 0x50000000, [&] {
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
        scenario(fam, m, *cpu, engine, 0x50F00000, [&] { (void)m.read8(0xA00000); });
    };
    lcii("68030", 0);
    lcii("68030 accelerated", 1);
    auto q605 = [&](const char* fam, int engine) {
        auto mem = std::make_unique<Q605Memory>(cfg, 8u << 20);
        auto cpu = std::make_unique<Cpu040>(*mem, jit::defaultResolvedConfig(),
                                            cfg.cpu, cfg.diagnostics);
        mem->setCpu(cpu.get());
        Q605Memory& m = *mem;
        if (fam) scenario(fam, m, *cpu, engine, 0x50F00000,
                          [&] { (void)m.read8(0x40000000); });
        else saveStateIsolation(m, *cpu);
    };
    q605("68040", 0);
    q605("68040 accelerated", 1);
    q605(nullptr, 0);
    std::printf("%s\n", gFails ? "debug_session_test: FAILED"
                               : "debug_session_test: all checks passed");
    return gFails ? 1 : 0;
}
