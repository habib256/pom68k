// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── The debugger's seam in the CPU wrappers ──
// Moira already carries the debugger primitives (MoiraDebugger.h): the
// breakpoint list and the soft stop are evaluated at the end of execute(),
// AFTER an instruction has retired and before the next one starts, and a
// match calls a virtual delegate. This class is the one place every POM68K
// wrapper (the MoiraCpu family and Cpu68k alike) turns that delegate into a
// call on a host-supplied hook, and the one place a disassembly can be told
// where its opcode words come from.
//
// Two contracts, both deliberately narrow:
//
//   - The stop hook runs ON THE MACHINE THREAD, inside execute(), at an
//     instruction boundary. It may block (DebugSession.h serves the GUI
//     while it does); it must not run guest code or touch the bus.
//     Nothing calls it unless State::CHECK_BP is set, which only the
//     debugger's own breakpoint list and soft stop do — a session without
//     a debugger pays nothing.
//   - `disassembleWith` routes Moira's read16Dasm through a caller-supplied
//     fetch for the duration of one call. Moira's own read16Dasm reads the
//     PHYSICAL live bus (Cpu68k) or a physical peek8 (MoiraCpu); the
//     debugger needs logical, translated, side-effect-free words instead.
//
// Gate: tests/debug_session_test.cpp, tests/debug_inspection_test.cpp.

#pragma once
#include "Moira.h"

namespace pom68k::dbg {

// Implemented by the host-side adapter (DebugCpuTarget.h). `soft` is a
// single-step stop; otherwise a breakpoint matched. `pc` is the address of
// the instruction about to execute.
class StopHook {
public:
    virtual void cpuStopped(bool soft, moira::u32 pc) = 0;

protected:
    ~StopHook() = default;
};

} // namespace pom68k::dbg

class MoiraDebugSeam : public moira::Moira {
public:
    void setDebugStopHook(pom68k::dbg::StopHook* hook) { debugHook_ = hook; }

    // One disassembly whose opcode words come from `fetch(ctx, addr)`.
    // Returns Moira's instruction length in bytes.
    using DasmFetch = moira::u16 (*)(void* ctx, moira::u32 addr);
    int disassembleWith(char* out, moira::u32 addr, DasmFetch fetch,
                        void* ctx) const {
        dasmFetch_ = fetch;
        dasmCtx_ = ctx;
        const int n = disassemble(out, addr);
        dasmFetch_ = nullptr;
        dasmCtx_ = nullptr;
        return n;
    }

protected:
    void didReachSoftstop(moira::u32 addr) override {
        if (debugHook_) debugHook_->cpuStopped(true, addr);
    }
    void didReachBreakpoint(moira::u32 addr) override {
        if (debugHook_) debugHook_->cpuStopped(false, addr);
    }

    // Wrappers that override read16Dasm call this first.
    bool dasmOverride(moira::u32 addr, moira::u16& word) const {
        if (!dasmFetch_) return false;
        word = dasmFetch_(dasmCtx_, addr);
        return true;
    }
    moira::u16 read16Dasm(moira::u32 addr) const override {
        moira::u16 word = 0;
        if (dasmOverride(addr, word)) return word;
        return moira::Moira::read16Dasm(addr);
    }

private:
    pom68k::dbg::StopHook* debugHook_ = nullptr;
    mutable DasmFetch dasmFetch_ = nullptr;
    mutable void* dasmCtx_ = nullptr;
};
