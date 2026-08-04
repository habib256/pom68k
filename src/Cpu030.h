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
    explicit Cpu030(V8Memory& mem, bool withFpu = false, bool as020 = false);

    void hardReset();                       // V8 overlay + CPU reset
    // ── JIT engine (src/jit/POM68K_JIT.md, 030 extension 2026-07-28) ───
    // The same engine that drives the four 68040 machines: the 030 seam is
    // mmuFetchWord (the single choke point every instruction-stream fetch
    // goes through) plus the read-only ATC probe, so nothing here knows
    // more than the Cpu040 wrapper does. Off by default; the LC's as020
    // profile constructs it too but the probe refuses a 68020, so the
    // window simply never arms there.
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

    // The core clock runs at cacheBoost_× machine rate. Bus models (E-clock
    // alignment, wait states) must work in machine cycles: on real silicon
    // the i-cache accelerates instruction fetch, never a VIA bus cycle.
    moira::i64 machineClock() const { return clock / cacheBoost_; }

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
    }

private:
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
    void schedulePeriphDeadline();

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
