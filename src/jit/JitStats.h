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

// Why a backend declined to publish a compiled block. This is deliberately
// coarser than the per-opcode fallback census: it attributes the WHOLE
// compile attempt and its host cost, while RuntimeFallbackReason explains
// individual instructions inside a block that was accepted.
enum class CompileReject : int {
    None = 0,
    Context,         // missing/empty IR, unsupported live CPU mode
    Emit,            // assembler/fixup or traced-contract mismatch
    Coverage,        // native share below POM68K_JIT_MIN_NATIVE
    CodeCapacity,    // bump allocator full; a cache flush makes this retryable
    CodeMemory,      // reserve or W^X transition failed; retrying cannot help
    Count
};

inline const char* compileRejectName(CompileReject r) {
    switch (r) {
        case CompileReject::None:       return "accepted";
        case CompileReject::Context:    return "context/IR";
        case CompileReject::Emit:       return "emit/fixup";
        case CompileReject::Coverage:   return "coverage";
        case CompileReject::CodeCapacity: return "code cache full";
        case CompileReject::CodeMemory: return "code memory/W^X";
        default:                        return "?";
    }
}

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

// Why the WHOLE cache was dropped. A flush is not an error either, but on a
// 68030 it is the difference between a code generator and a warm-up loop:
// every flush throws away compiled code that must then be re-recorded and
// re-visited `hot` times before it is native again. A total tells you the
// cost; only the cause tells you what to do about it.
enum class Flush : int {
    External = 0,     // a CPU wrapper asked: hard reset, or an SMC hint
                      // (68030 `didChangeCACR`, 68040 CINV/CPUSH). Resets
                      // are a handful per run, so this is the hint.
    MmuGen,           // the translation moved (`pomJitMapMoved`); a block is
                      // a script of LOGICAL addresses and cannot survive it
    Guard,            // a write into translated code the precise evictor
                      // could not localise (`CodeGuard::mustFlush`)
    Deferred,         // a flush requested while the backend was mid-replay
    CodeCapacity,     // generated-code bump allocator filled; recycle cache
    Count
};

inline const char* flushName(Flush f) {
    switch (f) {
        case Flush::External:  return "wrapper hint/reset";
        case Flush::MmuGen:    return "translation moved";
        case Flush::Guard:     return "write into code";
        case Flush::Deferred:  return "deferred";
        case Flush::CodeCapacity: return "code cache full";
        default:               return "?";
    }
}

// Why an instruction did NOT run as generated code. Exit reasons explain
// how a block STOPPED; these explain why one never started, which on the
// 68030 is where the throughput went: measured 2026-08-18, 82 % of retired
// instructions never entered a block at all, while 81 % of the ones that
// did were already native. Counted in instructions, not events, so the
// shares add up against `retired`.
enum class Miss : int {
    CpuFlags = 0,     // Moira had a flag up: IRQ, trace, STOP, breakpoint
    ArmFailed,        // armWindow() refused this pc — not plain memory, or
                      // a translation the probe could not confirm
    ArmBackoff,       // …and the 32-instruction backoff after such a refusal
    Trace,            // being recorded: tracing IS execution, so a block's
                      // first pass is always interpreted
    NotHot,           // a recorded block below its visit threshold
    NotProfitable,    // below visits × potentially-native instruction score
    Rejected,         // the backend declined to generate code for it
    CacheLine,        // 68040 architectural i-cache: block bytes not resident
    GuardReplay,      // retired interpreted after a guard-tripped block exit,
                      // so a store that trips WITHOUT evicting (32-byte mask,
                      // byte-exact eviction) cannot re-run natively forever
    Count
};

inline const char* missName(Miss m) {
    switch (m) {
        case Miss::CpuFlags:   return "cpu flags";
        case Miss::ArmFailed:  return "window refused";
        case Miss::ArmBackoff: return "arm backoff";
        case Miss::Trace:      return "tracing";
        case Miss::NotHot:     return "not hot yet";
        case Miss::NotProfitable: return "profit score";
        case Miss::Rejected:   return "backend declined";
        case Miss::CacheLine:  return "040 line absent";
        case Miss::GuardReplay: return "guard replay";
        default:               return "?";
    }
}

// Why `armWindow()` refused a pc. `Miss::ArmFailed` counts the refusals in
// INSTRUCTIONS; this says which exit took them, which is the
// difference between "the JIT is off here" and a mechanism. It exists
// because the 68030 lockstep under the x86-64 generator refuses 100 % of
// its arms (TODO § 1, 2026-08-28) and the aggregate cannot say why.
// `windowArmed` = accepted + the sum of these.
enum class ArmFail : int {
    Probe = 0,        // pomJitProbeCode(): no ATC entry yet, a supervisor
                      // page seen from user mode, or walk-per-access mode
    NotRam,           // codeSpan() has no host pointer, or a span shorter
                      // than one window: I/O, VRAM, a hole, the boot overlay
    TooShort,         // the page ∩ host span bound is itself shorter than
                      // one window — a DEGENERATE page, not a geometry
                      // accident: on the 68030 `pageLen` comes from TC's
                      // page-size field, which is 0 until the OS programs
                      // it, so an unprogrammed TC yields a 1-byte page
    PcAtEnd,          // the bound is usable but pc sits in its last bytes
    Pipe,             // the post-PMOVE fetch pipe is live: its fetches
                      // bypass the i-cache counters, which only the
                      // interpreter reproduces (Moira pomMmuPipeLive)
    Uncovered,        // armed, and pomJitCovers() STILL says no — the 040
                      // architectural i-cache knowing a translation before
                      // the line exists. Uncounted before 2026-08-28, which
                      // is why `armed` and `failed` could not be reconciled.
    Count
};

inline const char* armFailName(ArmFail a) {
    switch (a) {
        case ArmFail::Probe:     return "probe";
        case ArmFail::NotRam:    return "not plain memory";
        case ArmFail::TooShort:  return "degenerate page";
        case ArmFail::PcAtEnd:   return "pc at page end";
        case ArmFail::Pipe:      return "pmove pipe live";
        case ArmFail::Uncovered: return "uncovered after arm";
        default:                 return "?";
    }
}

struct Stats {
    std::atomic<uint64_t> instrs{0};        // guest instructions run by the JIT
    std::atomic<uint64_t> interpInstrs{0};  // …run by the interpreter fallback
    std::atomic<uint64_t> compileAttempts{0}; // backend compile() calls
    std::atomic<uint64_t> compileRejected{0}; // …that published no code
    std::atomic<uint64_t> compileNanos{0};    // host time spent in compile()
    std::atomic<uint64_t> compileRejects[int(CompileReject::Count)] = {};
    std::atomic<uint64_t> compileRejectNanos[int(CompileReject::Count)] = {};
    std::atomic<uint64_t> blocksCompiled{0};
    std::atomic<uint64_t> blocksRun{0};
    std::atomic<uint64_t> blocksLive{0};
    std::atomic<uint64_t> flushes{0};
    std::atomic<uint64_t> invalidations{0}; // writes that hit a live code window
    std::atomic<uint64_t> windowArmed{0};   // code-window arm attempts
    std::atomic<uint64_t> windowFailed{0};  // …that could not be validated
    std::atomic<uint64_t> armFails[int(ArmFail::Count)] = {};  // …by exit
    std::atomic<uint64_t> dtlbFills{0};     // data-TLB entries created
    std::atomic<uint64_t> dtlbRefused{0};   // …refused: not plain memory
    std::atomic<uint64_t> slowInstrs{0};    // ran through a block's fallback
    std::atomic<uint64_t> windowInstrs{0};  // ran on the fetch-window path
    // Retired BY THE TRACER (its first pass executes as it records). They
    // land in `instrs` but in neither windowInstrs nor slowInstrs, so until
    // 2026-08-18 the report's `native = instrs - slow - window` silently
    // absorbed them: the 68030's "14.4 % native residency" was ~9.3 % host
    // code + 5.1 % tracer interpretation, and JIT_BRINGUP's "unexplained
    // 82 % window share" closed exactly once this was counted apart.
    std::atomic<uint64_t> traceInstrs{0};
    std::atomic<uint64_t> evictions{0};     // blocks dropped by a precise evict
    std::atomic<uint64_t> exits[int(Exit::Count)] = {};
    std::atomic<uint64_t> flushCauses[int(Flush::Count)] = {};
    std::atomic<uint64_t> misses[int(Miss::Count)] = {};   // in INSTRUCTIONS

    void bump(Exit e) { exits[int(e)].fetch_add(1, std::memory_order_relaxed); }
    void bump(Flush f) { flushCauses[int(f)].fetch_add(1, std::memory_order_relaxed); }
    void bump(ArmFail a) { armFails[int(a)].fetch_add(1, std::memory_order_relaxed); }
    void miss(Miss m, uint64_t n = 1) {
        misses[int(m)].fetch_add(n, std::memory_order_relaxed);
    }
    void add(std::atomic<uint64_t>& c, uint64_t n = 1) {
        c.fetch_add(n, std::memory_order_relaxed);
    }

    void reset() {
        instrs = 0; interpInstrs = 0;
        compileAttempts = 0; compileRejected = 0; compileNanos = 0;
        blocksCompiled = 0; blocksRun = 0; blocksLive = 0;
        flushes = 0; invalidations = 0; windowArmed = 0; windowFailed = 0;
        dtlbFills = 0; dtlbRefused = 0; slowInstrs = 0; windowInstrs = 0;
        traceInstrs = 0;
        evictions = 0;
        for (auto& e : exits) e = 0;
        for (auto& f : flushCauses) f = 0;
        for (auto& m : misses) m = 0;
        for (auto& a : armFails) a = 0;
        for (auto& r : compileRejects) r = 0;
        for (auto& r : compileRejectNanos) r = 0;
    }

    // Snapshot for the GUI: a plain POD copy, no atomics to drag around.
    struct Snapshot {
        uint64_t compileAttempts, compileRejected, compileNanos;
        uint64_t compileRejects[int(CompileReject::Count)];
        uint64_t compileRejectNanos[int(CompileReject::Count)];
        uint64_t instrs, interpInstrs, blocksCompiled, blocksRun, blocksLive;
        uint64_t flushes, invalidations, windowArmed, windowFailed;
        uint64_t dtlbFills, dtlbRefused, slowInstrs, windowInstrs, evictions;
        uint64_t traceInstrs;
        uint64_t exits[int(Exit::Count)];
        uint64_t flushCauses[int(Flush::Count)];
        uint64_t misses[int(Miss::Count)];
        uint64_t armFails[int(ArmFail::Count)];
    };
    Snapshot snapshot() const {
        Snapshot s{};
        s.instrs = instrs.load(std::memory_order_relaxed);
        s.interpInstrs = interpInstrs.load(std::memory_order_relaxed);
        s.compileAttempts = compileAttempts.load(std::memory_order_relaxed);
        s.compileRejected = compileRejected.load(std::memory_order_relaxed);
        s.compileNanos = compileNanos.load(std::memory_order_relaxed);
        for (int i = 0; i < int(CompileReject::Count); i++) {
            s.compileRejects[i] =
                compileRejects[i].load(std::memory_order_relaxed);
            s.compileRejectNanos[i] =
                compileRejectNanos[i].load(std::memory_order_relaxed);
        }
        s.blocksCompiled = blocksCompiled.load(std::memory_order_relaxed);
        s.blocksRun = blocksRun.load(std::memory_order_relaxed);
        s.blocksLive = blocksLive.load(std::memory_order_relaxed);
        s.flushes = flushes.load(std::memory_order_relaxed);
        s.invalidations = invalidations.load(std::memory_order_relaxed);
        s.windowArmed = windowArmed.load(std::memory_order_relaxed);
        s.windowFailed = windowFailed.load(std::memory_order_relaxed);
        s.dtlbFills = dtlbFills.load(std::memory_order_relaxed);
        s.dtlbRefused = dtlbRefused.load(std::memory_order_relaxed);
        s.slowInstrs = slowInstrs.load(std::memory_order_relaxed);
        s.windowInstrs = windowInstrs.load(std::memory_order_relaxed);
        s.traceInstrs = traceInstrs.load(std::memory_order_relaxed);
        s.evictions = evictions.load(std::memory_order_relaxed);
        for (int i = 0; i < int(Exit::Count); i++)
            s.exits[i] = exits[i].load(std::memory_order_relaxed);
        for (int i = 0; i < int(Flush::Count); i++)
            s.flushCauses[i] = flushCauses[i].load(std::memory_order_relaxed);
        for (int i = 0; i < int(Miss::Count); i++)
            s.misses[i] = misses[i].load(std::memory_order_relaxed);
        for (int i = 0; i < int(ArmFail::Count); i++)
            s.armFails[i] = armFails[i].load(std::memory_order_relaxed);
        return s;
    }
};

}  // namespace jit
