// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── 68030 CPU (Moira wrapper, PowerBook Duo 210/230/250) ──
// The VaspCpu pattern on the MSC bus: functional accuracy, PMMU,
// external /BERR through Moira::extBusError (the SCSI pseudo-DMA
// timeout, macpwrbkmsc.cpp scsi_berr_w), the Cpu030 i-cache throughput
// overlay. Duo 210 @ 25.175 MHz, Duo 230/250 @ 33.33 MHz — no FPU on
// any of the three (the 270c is the first Duo with one).
// Shared wrapper plumbing (bus forwarders, hooks, sync): MoiraCpu.h.
// Blueprint: docs/DUO_BRINGUP.md. Gate (milestone 3+):
// tests/duo230_boot_etalon.cpp.

#pragma once
#include "CoreConfig.h"
#include "MoiraCpu.h"
#include "MscMemory.h"
#include <cstdint>

class MscCpu : public pom68k::MoiraCpu<MscCpu, MscMemory> {
public:
    explicit MscCpu(MscMemory& mem, const jit::ResolvedConfig& jitConfig,
                    const pom68k::CoreCpuConfig& cpuConfig,
                    bool withFpu = false);

    void hardReset();
    void setEngine(int e) { jit_.setEnabled(e != 0); pomJitDisarm(); }
    void runCycles(moira::i64 n);
    void stall(int cycles);          // REAL machine cycles (bus time)
    void flushTicks();

    moira::i64 machineClock() const { return clock / cacheBoost_; }

    template <class Ar> void visit(Ar& ar) {
        visitCpuCommon(ar);
        // periphDeadline_ is scheduling state, not a cache — TODO § 4's
        // contract names this line, and savestate_030_test catches it.
        ar(lastPeriphClock_, periphAccum_, periphDeadline_);
    }

private:
    friend pom68k::MoiraCpu<MscCpu, MscMemory>;
    bool eventDriven_ = false;
    void didChangeCACR(moira::u32 value) override;
    void catchUp();
    void schedulePeriphDeadline();

    int cacheBoost_ = 4;
    int icacheMiss_ = 4;
    moira::i64 periphAccum_ = 0;
    moira::i64 periphDeadline_ = 0;
    static constexpr moira::i64 kPeriphBatch = 128;
};
