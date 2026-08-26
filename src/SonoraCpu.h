// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── 68030 CPU (Moira wrapper, Mac LC III) ──
// The Cpu030 (LC II) pattern on the Sonora bus at 25 MHz: functional
// accuracy, PMMU for the 24-bit mode, external /BERR through
// Moira::extBusError (the SCSI pseudo-DMA timeout). Keeps the Cpu030
// i-cache throughput overlay (cacheBoost ceiling + per-miss charge,
// rationale in Cpu030.h) — the real LC III leans on its 030 caches just
// as hard. The 68882 socket is empty on a stock LC III (maclc3.cpp:338
// set_fpu_enable(false)); pass withFpu to populate it, as the target
// System 7.5 images expect (the LC II precedent).
// Gate: tests/lc3_boot_etalon.cpp.

#pragma once
#include "CoreConfig.h"
#include "MoiraSnapshot.h"
#include "jit/JitEngine.h"
#include <cstdint>

class SonoraMemory;

class SonoraCpu : public MoiraSnapshot {
public:
    explicit SonoraCpu(SonoraMemory& mem, const jit::ResolvedConfig& jitConfig,
                       const pom68k::CoreCpuConfig& cpuConfig,
                       bool withFpu = false);

    void hardReset();
    // ── JIT engine (030 extension 2026-07-28, the Cpu030 pattern) ──────
    jit::Engine& jit() { return jit_; }
    const jit::Engine& jit() const { return jit_; }
    int  engine() const { return jit_.enabled() ? 1 : 0; }
    void setEngine(int e) { jit_.setEnabled(e != 0); pomJitDisarm(); }
    void runCycles(moira::i64 n);
    void runUntil(moira::i64 clockTarget);
    void updateIpl();
    // `cycles` is REAL machine cycles (bus/wait-state time), not boosted
    // core cycles — see the machineClock() note below.
    void stall(int cycles);
    void flushTicks();

    // The core clock runs at cacheBoost_× machine rate (i-cache throughput
    // overlay). Anything that models the BUS — E-clock alignment, device
    // wait states — must work in machine cycles: on real silicon the
    // i-cache accelerates instruction fetch, never a VIA bus cycle.
    moira::i64 machineClock() const { return clock / cacheBoost_; }

    // ── Save states (chunk "CPU ") — the Cpu030 wrapper pattern ─────────
    // cacheBoost_/icacheMiss_ are environment tuning, not guest state.
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
    void didChangeCACR(moira::u32 value) override;
    void catchUp();
    void schedulePeriphDeadline();

    SonoraMemory& mem_;
    moira::i64 lastPeriphClock_ = 0;

    // Cpu030's i-cache throughput model (knobs + rationale there).
    jit::Engine jit_;
    int cacheBoost_ = 4;
    int icacheMiss_ = 4;
    moira::i64 periphAccum_ = 0;
    moira::i64 periphDeadline_ = 0;
    static constexpr moira::i64 kPeriphBatch = 128;
};
