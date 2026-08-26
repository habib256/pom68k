// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── 68030 CPU (Moira wrapper, Mac LC II) ──
// Moira Model::M68030 at 15.6672 MHz on the V8 bus — functional accuracy
// by design (16-bit V8 data path, CLAUDE.md); the fuzzed O4/O5 core
// provides the MMU (24-bit mode runs through PMMU tables) and bus-fault
// frames. No RAM/video contention model: the only timing adjustments are
// the VIA1 E-clock sync and the +5-cycle SWIM penalty, both applied by
// V8Memory through stall(). External /BERR (unmapped I/O, SCSI pseudo-DMA
// timeout) raises through Moira::extBusError (O6 slice 1).
// The 68882 FPU socket is empty by default, like a stock LC II
// (maclc.cpp:325-330); pass withFpu to populate it.
// Gate: tests/v8_ramsize.cpp (BERR through the map), lcii boot etalon (O6.8).

#pragma once
#include "CoreConfig.h"
#include "MoiraSnapshot.h"
#include "jit/JitEngine.h"
#include <cstdint>
#include <string>
#include <vector>

class V8Memory;

class Cpu030 : public MoiraSnapshot {
public:
    // as020 = Macintosh LC profile: same V8 bus, Moira Model::M68020
    // (MAME maclc.cpp:342 M68020HMMU — the HMMU 24-bit remap is subsumed
    // by the V8's own A31+A23-A0 decode).
    explicit Cpu030(V8Memory& mem, const jit::ResolvedConfig& jitConfig,
                    const pom68k::CoreCpuConfig& cpuConfig,
                    bool withFpu = false, bool as020 = false);

    void hardReset();                       // V8 overlay + CPU reset
    // ── JIT engine (src/jit/POM68K_JIT.md, 030 extension 2026-07-28) ───
    // The same engine that drives the four 68040 machines: the 030 seam is
    // mmuFetchWord (the single choke point every instruction-stream fetch
    // goes through) plus the read-only ATC probe, so nothing here knows
    // more than the Cpu040 wrapper does. Off by default. The LC's as020
    // profile constructs it too and the window DOES arm there — through
    // pomJitProbeCode's identity branch and pomJitFetch020. (This comment
    // used to say the probe refused a 68020; it was stale from the day it
    // was written, and jit_lc_boot_etalon had been proving it stale.
    // Corrected 2026-08-06, when the same seam was wired to the Mac II.)
    jit::Engine& jit() { return jit_; }
    const jit::Engine& jit() const { return jit_; }
    int  engine() const { return jit_.enabled() ? 1 : 0; }
    void setEngine(int e) { jit_.setEnabled(e != 0); pomJitDisarm(); }
    void runCycles(moira::i64 n);
    void runUntil(moira::i64 clockTarget);
    void updateIpl();                       // from the V8 priority resolver
    void stall(int cycles);                 // E-clock sync / SWIM wait states
                                            // — in REAL machine cycles
    void flushTicks();                      // run peripherals up to `clock`

    struct PeriphTracePoint {
        uint32_t pc = 0;
        moira::i64 clock = 0;
        moira::i64 machine = 0;
        moira::i64 deadline = 0;
        moira::i64 remainder = 0;
        moira::i64 target = 0;
        int phase = 0;             // 0 before executeUntil, 1 after, 2 flush
        int delivered = 0;
        int nextEvent = 0;
        // Which door the flush came through: 0 = sync (syncCycles is the
        // charge — 0 for the jit's due callout, >0 for the interpreter's
        // per-instruction CYCLES), 1 = stall (mid-instruction bus wait),
        // 2 = the runCycles chunk boundary. The peripheral-phase class
        // lives in the DIFFERENCE between these doors across engines.
        int src = 0;
        int syncCycles = 0;
        moira::i64 oldDeadline = 0;
        uint64_t deviceHash = 0;
        // The i-cache overlay's counters AT the delivery: an intra-step
        // charge skew wanders between engines and re-heals by the step
        // boundary, so only a per-delivery capture can name the window
        // that first mis-charged (2026-08-19 retained-cache bisect).
        moira::i64 icFetches = 0, icHits = 0, icMisses = 0;
    };
    using PeriphTraceFn = void (*)(void*, const PeriphTracePoint&);
    void setPeriphTrace(void* opaque, PeriphTraceFn fn) {
        periphTraceOpaque_ = opaque;
        periphTraceFn_ = fn;
    }

    // The periph trace's sibling: one point per execInterrupt, capturing
    // the pc the handler will RTE back to. Two arms can agree on every
    // delivery (clock, devices, i-cache) and still part later because the
    // interrupt LANDED one instruction apart — only this capture names
    // where (docs/JIT_BRINGUP.md § C.4sexies forensic).
    struct IrqTracePoint {
        uint32_t pc = 0;            // return pc — reg.pc at execInterrupt
        moira::i64 clock = 0;
        int level = 0;
        int kind = 1;               // 0 = pin change (updateIpl), 1 = take
    };
    using IrqTraceFn = void (*)(void*, const IrqTracePoint&);
    void setIrqTrace(void* opaque, IrqTraceFn fn) {
        irqTraceOpaque_ = opaque;
        irqTraceFn_ = fn;
    }

    // The core clock runs at boost_× machine rate — boost_ is cacheBoost_
    // normally and 1 while the floppy motor runs (the gate below). Bus
    // models (E-clock alignment, wait states) must work in machine cycles:
    // on real silicon the i-cache accelerates instruction fetch, never a
    // VIA bus cycle. machineBase_/clockBase_ keep this continuous and
    // monotonic across gate switches.
    moira::i64 machineClock() const {
        return machineBase_ + (clock - clockBase_) / boost_;
    }

    // Diagnostic Line-F logger (SimCity-2000 "coprocesseur absent" crash,
    // TODO § O6). When enabled, runCycles single-steps and keeps a ring of
    // recent PCs; the first Line-F exception dumps the ring + the full
    // integer/FPU register set to `path`, and every Line-F appends a one
    // line summary. Off (zero cost) unless enabled. See main.cpp
    // POM68K_FPU_LOG.
    void enableFpuLog(const std::string& path, size_t ringSize = 4096);

    // 68030 i-cache diagnostics (hit/miss counters of the timing overlay,
    // which lives inline in Moira — Moira.h § PomIcache).
    struct ICacheStats { moira::i64 hits = 0, misses = 0, fetches = 0; };
    ICacheStats icacheStats() const {
        return { pomIcache.hits, pomIcache.misses, pomIcache.fetches };
    }
    void resetICacheStats() {
        pomIcache.fetches = pomIcache.hits = pomIcache.misses = 0;
    }
    // The overlay's CONTENT (tags + valid bits), for the 030 lockstep gate:
    // counters can agree while the cached lines have parted, and the state
    // skew then reads as a timing drift thousands of steps later.
    const moira::Moira::PomIcache& icacheState() const { return pomIcache; }
    bool icacheEnabled() const { return (getCACR() & 0x1) != 0; }

    // ── Save states (chunk "CPU ") ──────────────────────────────────────
    // The architectural half comes from MoiraSnapshot; what is added here is
    // the wrapper's own bus bookkeeping — how much peripheral time this CPU
    // has already accounted for. Getting that wrong would not corrupt the
    // guest, it would silently shift the VIA/ASC/SWIM cadence after every
    // restore, which is the failure the LC II soak gate exists to catch.
    // cacheBoost_ / icacheMiss_ are tuning knobs owned by the environment,
    // not guest state, so they stay out.
    template <class Ar> void visit(Ar& ar) {
        visitCpuCommon(ar);
        ar(lastPeriphClock_, periphAccum_, periphDeadline_);
        // The machineClock() continuity bases ARE run state: every gate
        // epoch (floppy streaming at boost 1) leaves a permanent offset
        // between clock/cacheBoost_ and true machine time; dropping it on
        // restore would shift every device syncTo() stamp by the total
        // gated time. boost_ itself is re-derived (env knob + motor state
        // at the first flushTicks after the restore).
        ar(machineBase_, clockBase_);
        if constexpr (Ar::loading) boost_ = cacheBoost_;
    }

private:
    int cacrFlushPolicy_ = -1;
    moira::u8  read8(moira::u32 addr) const override;
    moira::u16 read16(moira::u32 addr) const override;
    // Moira's disassembler falls back to read16() unless this is overridden,
    // which sent every disassembly read through the LIVE bus: device registers
    // with read side effects (SCC status latches, IWM state lines) and, on
    // unmapped I/O, a busError() that mutates An/MMU fault state and throws.
    // peek8() is the side-effect-free path the tracers already use.
    moira::u16 read16Dasm(moira::u32 addr) const override;
    void write8(moira::u32 addr, moira::u8 v) const override;
    void write16(moira::u32 addr, moira::u16 v) const override;
    void sync(int cycles) override;
    void willExecute(moira::M68kException exc, moira::u16 vector) override;
    void willInterrupt(moira::u8 level) override;
    void didChangeCACR(moira::u32 value) override;              // cache clear/enable
    void catchUp();
    void dumpFpuLog(moira::u16 vector);

    V8Memory& mem_;
    moira::i64 lastPeriphClock_ = 0;

    // Instruction-throughput model. Moira emulates the 68030 on its
    // Core::C68020 cycle model: 68020 per-instruction cycle counts (advisory
    // placeholders — POM68K_VENDOR.md), NO i-cache, NO d-cache, less pipeline
    // overlap than a real 030. The real LC II 68030 at 15.67 MHz has a
    // 256-byte i-cache (enabled by the System via CACR) and runs tight loops
    // from cache with no instruction-fetch bus cycles, so it executes far
    // more instructions per unit of machine time than Moira charges — most on
    // hot loops (SimCity's redraw measured 95% cache-resident), which is why a
    // single global multiplier can't fit both (a heavy per-VBL redraw needs a
    // big boost that would over-speed cold code, and under-boosting it lets
    // the VBL task re-enter itself → the "coprocesseur absent" livelock).
    //
    // Model: run the core at cacheBoost_× (the resident-code CEILING ≈ the
    // real 030's cached throughput), and charge icacheMiss_ boosted cycles per
    // instruction-cache MISS in willFetchInstr() — the fetch bus access the
    // real chip pays only on a miss. flushTicks() scales elapsed cycles back
    // down by cacheBoost_ so the VBL / VIA / ASC cadence stays real. Net:
    // cache-resident code runs near the ceiling (redraw finishes its frame,
    // no livelock), miss-heavy cold code is throttled toward real speed — the
    // per-code-path behaviour of the real cache, not a flat fudge. One fixed
    // ceiling → uniform tempo. History: retired a flat constant boost (couldn't
    // fit both), itself retired from an ADAPTIVE 2→24 spike (wobbled the sound
    // tempo). Both knobs tunable live: POM68K_CACHE_BOOST (ceiling),
    // POM68K_ICACHE_MISS (penalty). Long-term: a fuller Moira cache model.
    jit::Engine jit_;
    int cacheBoost_ = 4;           // resident-code ceiling ratio
    int icacheMiss_ = 4;           // boosted cycles charged per i-cache miss
    moira::i64 periphAccum_ = 0;   // sub-ratio remainder for exact scaling
    moira::i64 periphDeadline_ = 0;
    void* periphTraceOpaque_ = nullptr;
    PeriphTraceFn periphTraceFn_ = nullptr;
    void* irqTraceOpaque_ = nullptr;
    IrqTraceFn irqTraceFn_ = nullptr;
    int traceSrc_ = 0;             // trace only: flush door + charge
    int traceSyncCycles_ = 0;
    moira::i64 traceOldDeadline_ = 0;
    void emitPeriphTrace(int phase, int delivered, moira::i64 target = 0);
    void schedulePeriphDeadline();

    // ── Floppy boost gate (CHANGELOG 2026-08-05 (eighth)) ───────────────
    // Apple's 6-and-2 denibble inner path is hand-timed to JUST clear the
    // IWM's 14-tick post-read hold (~1.79 µs) at the real clock; the boost
    // compresses it below the window, the next poll re-reads the SAME
    // latched nibble (silicon-conformant — MAME iwm.cpp:284 re-arms the
    // hold on every access), and every 512-byte field checksums wrong
    // (badDCksum → "unreadable — Initialize?"). While the floppy motor
    // runs, boost_ freezes to 1: boost 1 IS the real machine clock, i.e.
    // Apple's timing restored. Switched only in flushTicks(), rebasing
    // machineBase_/clockBase_ so machineClock() never jumps.
    // POM68K_FLOPPY_BOOST_GATE=0 disables (reproduces the refusal for
    // lcii_sony_trace).
    int boost_ = 4;                // effective ratio (== cacheBoost_ or 1)
    bool floppyGate_ = true;
    moira::i64 machineBase_ = 0, clockBase_ = 0;
    void pollBoostGate();

    // The 68030 on-chip i-cache model itself (MC68030UM §6: 256 bytes = 16
    // lines × 4 longwords, logical, direct-mapped, non-burst fill) lives
    // INLINE in Moira's mmuFetchWord (Moira.h § PomIcache): a virtual
    // per-fetch hook cost ~11% of the emulator in call overhead. The
    // constructor arms it and hands it icacheMiss_; this class keeps the
    // knobs, the CACR clear strobes and the stats accessors.

    // Line-F logging state
    bool fpuLog_ = false;
    std::string fpuLogPath_;
    std::vector<uint32_t> pcRing_;             // every executed PC (fine)
    size_t pcRingPos_ = 0;
    struct Jump { uint32_t from, to; };        // control transfers (coarse,
    std::vector<Jump> jumpRing_;               // reaches far further back)
    size_t jumpRingPos_ = 0;
    uint32_t lastPc_ = 0;
    struct A5Chg { uint32_t pc, from, to; };   // A5 (app-globals ptr) writes
    std::vector<A5Chg> a5Ring_;                // — the crash is a corrupt A5
    size_t a5RingPos_ = 0;
    uint32_t lastA5_ = 0;
    long flineSeen_ = 0;
    bool fpuDumped_ = false;

    // Fixed batching was replaced on V8 by a device-derived deadline. With
    // firmware LLE the 68HC05 clock bridge binds; the HLE fallback retains
    // the historical 128-cycle cadence because it is explicitly non-conformant.
};
