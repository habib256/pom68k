// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── JIT gauges ──
// Plain counters, written by the machine thread and read by the GUI thread
// without a lock (relaxed atomics: a torn read only misdraws one frame of a
// statistics window). They exist so the user can SEE what the engine does —
// that is half the point of having a second engine at all.

#pragma once
#include <atomic>
#include <cstdint>

namespace jit {

// Why a block replay handed control back to the interpreter. Every exit is
// taken at an instruction boundary with exact guest state (invariant 2 of
// src/jit/POM68K_JIT.md), so an exit is never an error — but the mix tells
// us what the engine still cannot do.
enum class Exit : int {
    BlockEnd = 0,     // ran to the end of the compiled block, normally
    CpuFlags,         // Moira raised a flag (IRQ, trace, stop, breakpoint…)
    Fault,            // the instruction took an MMU/bus fault
    WindowLost,       // the code window went stale mid-block (SMC, remap)
    ClockBudget,      // the caller's cycle budget ran out
    NotCompilable,    // no block could be built at this pc
    Count
};

inline const char* exitName(Exit e) {
    switch (e) {
        case Exit::BlockEnd:       return "block end";
        case Exit::CpuFlags:       return "cpu flags";
        case Exit::Fault:          return "fault";
        case Exit::WindowLost:     return "window lost";
        case Exit::ClockBudget:    return "clock budget";
        case Exit::NotCompilable:  return "not compilable";
        default:                   return "?";
    }
}

struct Stats {
    std::atomic<uint64_t> instrs{0};        // guest instructions run by the JIT
    std::atomic<uint64_t> interpInstrs{0};  // …run by the interpreter fallback
    std::atomic<uint64_t> blocksCompiled{0};
    std::atomic<uint64_t> blocksRun{0};
    std::atomic<uint64_t> blocksLive{0};
    std::atomic<uint64_t> flushes{0};
    std::atomic<uint64_t> invalidations{0}; // writes that hit a live code window
    std::atomic<uint64_t> windowArmed{0};   // code-window arm attempts
    std::atomic<uint64_t> windowFailed{0};  // …that could not be validated
    std::atomic<uint64_t> exits[int(Exit::Count)] = {};

    void bump(Exit e) { exits[int(e)].fetch_add(1, std::memory_order_relaxed); }
    void add(std::atomic<uint64_t>& c, uint64_t n = 1) {
        c.fetch_add(n, std::memory_order_relaxed);
    }

    void reset() {
        instrs = 0; interpInstrs = 0;
        blocksCompiled = 0; blocksRun = 0; blocksLive = 0;
        flushes = 0; invalidations = 0; windowArmed = 0; windowFailed = 0;
        for (auto& e : exits) e = 0;
    }

    // Snapshot for the GUI: a plain POD copy, no atomics to drag around.
    struct Snapshot {
        uint64_t instrs, interpInstrs, blocksCompiled, blocksRun, blocksLive;
        uint64_t flushes, invalidations, windowArmed, windowFailed;
        uint64_t exits[int(Exit::Count)];
    };
    Snapshot snapshot() const {
        Snapshot s{};
        s.instrs = instrs.load(std::memory_order_relaxed);
        s.interpInstrs = interpInstrs.load(std::memory_order_relaxed);
        s.blocksCompiled = blocksCompiled.load(std::memory_order_relaxed);
        s.blocksRun = blocksRun.load(std::memory_order_relaxed);
        s.blocksLive = blocksLive.load(std::memory_order_relaxed);
        s.flushes = flushes.load(std::memory_order_relaxed);
        s.invalidations = invalidations.load(std::memory_order_relaxed);
        s.windowArmed = windowArmed.load(std::memory_order_relaxed);
        s.windowFailed = windowFailed.load(std::memory_order_relaxed);
        for (int i = 0; i < int(Exit::Count); i++)
            s.exits[i] = exits[i].load(std::memory_order_relaxed);
        return s;
    }
};

}  // namespace jit
