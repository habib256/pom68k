// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "JitBackendThreaded.h"

#include "Moira.h"

namespace jit {

namespace {

// The threaded backend's compiled artefact is the recorded block itself:
// there is nothing to generate. Keeping the type distinct anyway is what
// lets a code-generating backend be dropped in without the engine noticing.
class ThreadedCompiled : public Compiled {
};

class ThreadedBackend : public Backend {
public:
    const char* name() const override { return "threaded"; }
    const char* description() const override {
        return "portable — replays through Moira, no code generation";
    }

    // No executable memory, no CPU feature, no kernel policy involved.
    bool usable() const override { return true; }

    BackendCaps caps() const override {
        BackendCaps c;
        c.nativeCode = false;
        // It delegates to Moira, so anything the block classifier considers
        // safe is by definition within reach.
        c.aluReg = c.aluMem = c.moves = c.addrModes = true;
        c.branches = false;              // blocks are straight lines here
        c.maxBlockInstrs = 64;
        // Every guest, and for a structural reason rather than by testing:
        // the replay runs Moira's OWN handlers, so whatever the model does
        // ((An)+ ordering, restartable writes, prefetch refill) it does here
        // too. This is the backend that "cannot possibly be wrong about
        // semantics" this file's header claims — and the reason `auto` has a
        // correct floor for the 68000/020/030 machines.
        c.guestFamilies = kGuestAny;
        return c;
    }

    Compiled* compile(const BlockIr&, const Context&) override {
        return new ThreadedCompiled();
    }
    void release(Compiled* c) override { delete c; }
    void flushAll() override {}

    RunResult run(Compiled* c, Context& ctx) override {
        moira::Moira& cpu = *ctx.cpu;
        const BlockIr& ir = *c->ir;
        RunResult r;

        for (const Instr& in : ir.instrs) {

            // Four guards, all cheap, all mandatory. Together they are
            // invariant 2 of src/jit/POM68K_JIT.md: whatever happens, the
            // block is left at an instruction boundary with exact state.
            if (cpu.getClock() >= ctx.clockTarget) { r.exit = Exit::ClockBudget; return r; }

            // A peripheral ticked inside the previous instruction's sync()
            // and raised an interrupt; or a trace/stop/breakpoint armed.
            if (!cpu.pomJitIdle()) { r.exit = Exit::CpuFlags; return r; }

            // Execution is not where the trace said it would be. This is
            // the catch-all that makes recorded blocks safe: an opcode that
            // took a trap we did not predict, a fault that redirected the
            // pc, code rewritten under us — all land here.
            if (cpu.getPC() != in.pc) { r.exit = Exit::WindowLost; return r; }

            // Left the validated page (or a write tripped the guard and the
            // engine disarmed us): let the engine re-validate.
            if (!cpu.pomJitCovers(in.pc)) { r.exit = Exit::WindowLost; return r; }

            if (!cpu.pomJitExecOne()) { r.instrs++; r.slowInstrs++; r.exit = Exit::Fault; return r; }
            r.instrs++;
            r.slowInstrs++;   // this backend has no other kind
        }

        r.exit = Exit::BlockEnd;
        return r;
    }
};

}  // namespace

Backend* threadedBackend() {
    static ThreadedBackend backend;
    return &backend;
}

}  // namespace jit
