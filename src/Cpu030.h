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

private:
    moira::u8  read8(moira::u32 addr) const override;
    moira::u16 read16(moira::u32 addr) const override;
    void write8(moira::u32 addr, moira::u8 v) const override;
    void write16(moira::u32 addr, moira::u16 v) const override;
    void sync(int cycles) override;
    void willExecute(moira::M68kException exc, moira::u16 vector) override;
    void willInterrupt(moira::u8 level) override;   // counts IRQs for adaptive boost
    void catchUp();
    void dumpFpuLog(moira::u16 vector);

    V8Memory& mem_;
    moira::i64 lastPeriphClock_ = 0;

    // Instruction-cache throughput model. The real 68030 runs tight loops
    // from its 256-byte on-chip i-cache at ~1 cycle/word, so it executes
    // far more instructions per frame than Moira accounts for (Moira has no
    // i-cache model; the WinUAE co-sim measured 6.5 vs 16.9 cyc/instr, both
    // cacheless). Without this, SimCity 2000's per-VBL screen redraw
    // overruns its frame and re-enters itself — a livelock that never
    // restores A5 → the "coprocesseur absent" crash (TODO § O6). We run the
    // core cacheBoost_× more instructions per unit of peripheral (machine)
    // time; flushTicks() scales elapsed Moira cycles back down so the VBL /
    // VIA timer / ASC cadences stay real. Default 2 (the cache gives ~2-3×
    // on tight loops; 3 turned out to corrupt SC2K's display — too far from
    // the real CPU/peripheral balance). Overridable with POM68K_CACHE_BOOST
    // when a heavier redraw path still livelocks. Read once in the ctor.
    int cacheBoost_ = 2;
    moira::i64 periphAccum_ = 0;   // sub-boost remainder for exact scaling

    // Adaptive boost: a normal game frame takes only a handful of interrupts;
    // a heavy per-VBL redraw (SimCity's biggest cities at 640×480) takes many
    // dozens as the redraw handler re-enters. When last frame's IRQ count
    // crosses the threshold, run this frame at maxBoost_ so the redraw
    // finishes within its budget (no livelock/crash), then fall back to the
    // base boost — so normal play stays fast and only heavy redraws slow
    // down briefly. Ceiling raisable via POM68K_CACHE_BOOST (sets the base;
    // max tracks it). Disabled while single-stepping (fpuLog).
    static constexpr long kIrqBoostThreshold = 12;
    int maxBoost_ = 16;
    long irqThisFrame_ = 0, irqPrevFrame_ = 0;
    int boostHold_ = 0;            // hysteresis: stay boosted a few frames

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
