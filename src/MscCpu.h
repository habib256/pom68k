// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── 68030 CPU (Moira wrapper, PowerBook Duo 210/230/250) ──
// The VaspCpu pattern on the MSC bus: functional accuracy, PMMU,
// external /BERR through Moira::extBusError (the SCSI pseudo-DMA
// timeout, macpwrbkmsc.cpp scsi_berr_w), the Cpu030 i-cache throughput
// overlay. Duo 210 @ 25.175 MHz, Duo 230/250 @ 33.33 MHz — no FPU on
// any of the three (the 270c is the first Duo with one).
// Blueprint: docs/DUO_BRINGUP.md. Gate (milestone 3+):
// tests/duo230_boot_etalon.cpp.

#pragma once
#include "CoreConfig.h"
#include "MoiraSnapshot.h"
#include "jit/JitEngine.h"
#include <cstdint>

class MscMemory;

class MscCpu : public MoiraSnapshot {
public:
    explicit MscCpu(MscMemory& mem, const jit::ResolvedConfig& jitConfig,
                    const pom68k::CoreCpuConfig& cpuConfig,
                    bool withFpu = false);

    void hardReset();
    jit::Engine& jit() { return jit_; }
    const jit::Engine& jit() const { return jit_; }
    int  engine() const { return jit_.enabled() ? 1 : 0; }
    void setEngine(int e) { jit_.setEnabled(e != 0); pomJitDisarm(); }
    void runCycles(moira::i64 n);
    void runUntil(moira::i64 clockTarget);
    void updateIpl();
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
    bool eventDriven_ = false;
    moira::u8  read8(moira::u32 addr) const override;
    moira::u16 read16(moira::u32 addr) const override;
    moira::u16 read16Dasm(moira::u32 addr) const override;   // peek8 path
    void write8(moira::u32 addr, moira::u8 v) const override;
    void write16(moira::u32 addr, moira::u16 v) const override;
    void sync(int cycles) override;
    void didChangeCACR(moira::u32 value) override;
    void catchUp();

    MscMemory& mem_;
    moira::i64 lastPeriphClock_ = 0;

    jit::Engine jit_;
    int cacheBoost_ = 4;
    int icacheMiss_ = 4;
    moira::i64 periphAccum_ = 0;
    moira::i64 periphDeadline_ = 0;
    void schedulePeriphDeadline();
    static constexpr moira::i64 kPeriphBatch = 128;
};
