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
#include "Moira.h"
#include <cstdint>
#include <string>
#include <vector>

class V8Memory;

class Cpu030 : public moira::Moira {
public:
    explicit Cpu030(V8Memory& mem, bool withFpu = false);

    void hardReset();                       // V8 overlay + CPU reset
    void runCycles(moira::i64 n);
    void runUntil(moira::i64 clockTarget);
    void updateIpl();                       // from the V8 priority resolver
    void stall(int cycles);                 // E-clock sync / SWIM wait states
    void flushTicks();                      // run peripherals up to `clock`

    // Diagnostic Line-F logger (SimCity-2000 "coprocesseur absent" crash,
    // TODO § O6). When enabled, runCycles single-steps and keeps a ring of
    // recent PCs; the first Line-F exception dumps the ring + the full
    // integer/FPU register set to `path`, and every Line-F appends a one
    // line summary. Off (zero cost) unless enabled. See main.cpp
    // POM68K_FPU_LOG.
    void enableFpuLog(const std::string& path, size_t ringSize = 4096);

    // 68030 i-cache measurement (validation before the timing overlay is
    // wired). Counts instruction-fetch hits/misses against the modeled cache.
    struct ICacheStats { moira::i64 hits = 0, misses = 0, fetches = 0; };
    ICacheStats icacheStats() const { return icStats_; }
    void resetICacheStats() { icStats_ = {}; }
    bool icacheEnabled() const { return (getCACR() & 0x1) != 0; }

private:
    moira::u8  read8(moira::u32 addr) const override;
    moira::u16 read16(moira::u32 addr) const override;
    void write8(moira::u32 addr, moira::u8 v) const override;
    void write16(moira::u32 addr, moira::u16 v) const override;
    void sync(int cycles) override;
    void willExecute(moira::M68kException exc, moira::u16 vector) override;
    void willFetchInstr(moira::u32 addr, bool super) override;  // 68030 i-cache
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
    int cacheBoost_ = 4;           // resident-code ceiling ratio
    int icacheMiss_ = 4;           // boosted cycles charged per i-cache miss
    moira::i64 periphAccum_ = 0;   // sub-ratio remainder for exact scaling

    // 68030 on-chip instruction cache model (MC68030UM §6): 256 bytes = 16
    // lines × 4 longwords, LOGICAL, direct-mapped. Line = logical A[7:4],
    // longword = A[3:2], tag = A[31:8] + supervisor(FC2) bit. Each line has
    // 4 per-longword valid bits; a miss whose tag differs evicts the line
    // (direct-mapped). Non-burst fill (one longword per miss) — conservative
    // vs the real burst line-fill, refined later if needed. Fed by
    // willFetchInstr() (every instruction-word fetch), gated on CACR bit 0
    // (enable i-cache). The real chip runs tight cache-resident loops with no
    // instruction-fetch bus cycles — this model is what lets our timing
    // reflect that per code path instead of a global boost fudge.
    struct ICache {
        static constexpr int kLines = 16;
        moira::u32 tag_[kLines];
        moira::u8  valid_[kLines];
        void reset() { for (int i = 0; i < kLines; i++) { tag_[i] = 0xFFFFFFFF; valid_[i] = 0; } }
        bool access(moira::u32 addr, bool super) {          // returns true on hit
            moira::u32 t = (addr >> 8) | (super ? 0x80000000u : 0u);
            int line = int((addr >> 4) & (kLines - 1));
            int lw   = int((addr >> 2) & 3);
            if (tag_[line] == t && (valid_[line] & (1u << lw))) return true;
            if (tag_[line] != t) { tag_[line] = t; valid_[line] = 0; }   // evict
            valid_[line] |= moira::u8(1u << lw);
            return false;
        }
    };
    ICache icache_;
    ICacheStats icStats_;

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

    // Peripheral-tick batching: sync() fires on every bus access, but a
    // full V8Memory::tick sweep (VIA + Egret + ASC + SWIM + SCC + IRQ
    // resolve) every few cycles dominated the profile (~0.7× real time at
    // the Finder). Peripherals only need to be current at a device-space
    // access (V8Memory flushes first) — between those, batch up to this
    // many cycles (8 µs; IRQ latency jitter stays far below anything the
    // System's Time Manager can observe).
    static constexpr moira::i64 kPeriphBatch = 128;
};
