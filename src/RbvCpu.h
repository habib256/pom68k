// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── 68030 CPU (Moira wrapper, Mac IIsi) ──
// The SonoraCpu pattern on the RBV bus at 20 MHz: functional accuracy,
// PMMU for the 24-bit mode, external /BERR through Moira::extBusError
// (the SCSI pseudo-DMA timeout), the Cpu030 i-cache throughput overlay.
// The IIsi socket takes an optional 68882 on the adapter card (MAME
// maciici.cpp:514 defaults to "No FPU"); pass withFpu to populate it,
// as the target System 7.5 images expect (the LC II precedent).
// Shared wrapper plumbing (bus forwarders, hooks, sync): MoiraCpu.h.
// Gate: tests/iisi_boot_etalon.cpp.

#pragma once
#include "CoreConfig.h"
#include "MoiraCpu.h"
#include "RbvMemory.h"
#include <cstdint>

class RbvCpu : public pom68k::MoiraCpu<RbvCpu, RbvMemory> {
public:
    explicit RbvCpu(RbvMemory& mem, const jit::ResolvedConfig& jitConfig,
                    const pom68k::CoreCpuConfig& cpuConfig,
                    bool withFpu = false);

    void hardReset();
    // ── JIT engine (030 extension 2026-07-28, the Cpu030 pattern) ──────
    void setEngine(int e) { jit_.setEnabled(e != 0); pomJitDisarm(); }
    void runCycles(moira::i64 n);
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
    friend pom68k::MoiraCpu<RbvCpu, RbvMemory>;
    void didChangeCACR(moira::u32 value) override;
    void catchUp();
    void schedulePeriphDeadline();

    // Cpu030's i-cache throughput model (knobs + rationale there).
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
