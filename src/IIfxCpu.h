// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Moira M68030 @ 40 MHz on the Mac IIfx map (functional accuracy) — the
// Cpu020 wrapper pattern on platform #12 (docs/IOP_BRINGUP.md M3). No
// i-cache throughput overlay: the core clock IS machine time.

#pragma once
#include "MoiraSnapshot.h"
#include "jit/JitEngine.h"
#include <cstdint>

class IIfxMemory;

class IIfxCpu : public MoiraSnapshot {
public:
    explicit IIfxCpu(IIfxMemory& mem, const jit::ResolvedConfig& jitConfig,
                     bool withFpu = true);

    void hardReset();

    // ── JIT engine (src/jit/POM68K_JIT.md) ──────────────────────────────
    // The last wrapper to get one (2026-08-06), and the least eventful: the
    // IIfx is a 68030, so it takes the ATC probe and the mmuFetchWord choke
    // point the five other 030 families already use — no Moira work at all.
    // Its map is also the easiest the engine has met: 32-bit clean, no HMMU
    // and no GLUE remap, so the probe's address IS the bus address.
    jit::Engine& jit() { return jit_; }
    const jit::Engine& jit() const { return jit_; }
    int  engine() const { return jit_.enabled() ? 1 : 0; }
    void setEngine(int e) { jit_.setEnabled(e != 0); pomJitDisarm(); }

    void runCycles(moira::i64 n);
    void runUntil(moira::i64 clockTarget);
    void updateIpl();
    void stall(int cycles);
    void flushTicks();
    moira::i64 machineClock() const { return clock; }

    // ── Save states (chunk "CPU ") — the Cpu020 wrapper pattern ─────────
    template <class Ar> void visit(Ar& ar) {
        visitCpuCommon(ar);
        ar(lastPeriphClock_);
    }

private:
    moira::u8  read8(moira::u32 addr) const override;
    moira::u16 read16(moira::u32 addr) const override;
    // Side-effect-free disassembly reads (the Cpu020 lesson: the live bus
    // mutates SCC/SWIM latches and bus-errors on unmapped I/O).
    moira::u16 read16Dasm(moira::u32 addr) const override;
    moira::u16 read16OnReset(moira::u32 addr) const override;
    void write8(moira::u32 addr, moira::u8 v) const override;
    void write16(moira::u32 addr, moira::u16 v) const override;
    void sync(int cycles) override;
    void didChangeCACR(moira::u32 value) override;
    void catchUp();

    IIfxMemory& mem_;
    jit::Engine jit_;
    moira::i64 lastPeriphClock_ = 0;
    static constexpr int kPeriphBatch = 64;
};
