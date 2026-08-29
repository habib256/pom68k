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
// Shared wrapper plumbing (bus forwarders, hooks, sync): MoiraCpu.h.
// Gate: tests/lc3_boot_etalon.cpp.

#pragma once
#include "CoreConfig.h"
#include "MoiraCpu.h"
#include "SonoraMemory.h"
#include <cstdint>

class SonoraCpu : public pom68k::MoiraCpu<SonoraCpu, SonoraMemory> {
public:
    explicit SonoraCpu(SonoraMemory& mem, const jit::ResolvedConfig& jitConfig,
                       const pom68k::CoreCpuConfig& cpuConfig,
                       bool withFpu = false);

    void hardReset();
    // ── JIT engine (030 extension 2026-07-28, the Cpu030 pattern) ──────
    void setEngine(int e) { jit_.setEnabled(e != 0); pomJitDisarm(); }
    void runCycles(moira::i64 n);
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
    friend pom68k::MoiraCpu<SonoraCpu, SonoraMemory>;
    void didChangeCACR(moira::u32 value) override;
    void catchUp();
    void schedulePeriphDeadline();

    // Cpu030's i-cache throughput model (knobs + rationale there).
    int cacheBoost_ = 4;
    int icacheMiss_ = 4;
    moira::i64 periphAccum_ = 0;
    moira::i64 periphDeadline_ = 0;
    static constexpr moira::i64 kPeriphBatch = 128;
};
