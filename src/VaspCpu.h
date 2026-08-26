// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── 68030 CPU (Moira wrapper, Mac IIvx / IIvi) ──
// The SonoraCpu pattern on the VASP bus: functional accuracy, PMMU,
// external /BERR through Moira::extBusError (the SCSI pseudo-DMA
// timeout), the Cpu030 i-cache throughput overlay. The IIvx runs at
// 31.3344 MHz (C32M), the IIvi at 15.6672 MHz (C15M); both ship with the
// 68882 socket populated on the vx (maciivx.cpp FPU config switch) —
// pass withFpu accordingly.
// Gate: tests/iivx_boot_etalon.cpp.

#pragma once
#include "CoreConfig.h"
#include "MoiraSnapshot.h"
#include "jit/JitEngine.h"
#include <cstdint>

class VaspMemory;

class VaspCpu : public MoiraSnapshot {
public:
    explicit VaspCpu(VaspMemory& mem, const jit::ResolvedConfig& jitConfig,
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
    void stall(int cycles);          // REAL machine cycles (bus time)
    void flushTicks();

    // Bus time is not accelerated by the i-cache — see SonoraCpu.h.
    // boost_ is cacheBoost_ normally, 1 while the floppy motor runs (the
    // Cpu030 floppy boost gate); the bases keep this continuous.
    moira::i64 machineClock() const {
        return machineBase_ + (clock - clockBase_) / boost_;
    }

    // ── Save states (chunk "CPU ") — the Cpu030 wrapper pattern ─────────
    // cacheBoost_/icacheMiss_ are environment tuning, not guest state; the
    // machineClock() bases ARE run state (see Cpu030.h).
    template <class Ar> void visit(Ar& ar) {
        visitCpuCommon(ar);
        ar(lastPeriphClock_, periphAccum_, periphDeadline_);
        ar(machineBase_, clockBase_);
        if constexpr (Ar::loading) boost_ = cacheBoost_;
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

    VaspMemory& mem_;
    moira::i64 lastPeriphClock_ = 0;

    // Cpu030's i-cache throughput model (knobs + rationale there).
    jit::Engine jit_;
    int cacheBoost_ = 4;
    int icacheMiss_ = 4;
    moira::i64 periphAccum_ = 0;
    moira::i64 periphDeadline_ = 0;
    static constexpr moira::i64 kPeriphBatch = 128;

    // Floppy boost gate — the Cpu030 pattern (rationale + invariants in
    // Cpu030.h; CHANGELOG 2026-08-05 (eighth)).
    int boost_ = 4;
    bool floppyGate_ = true;
    moira::i64 machineBase_ = 0, clockBase_ = 0;
    void pollBoostGate();
};
