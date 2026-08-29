// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Moira M68030 @ 40 MHz on the Mac IIfx map (functional accuracy) — the
// Cpu020 wrapper pattern on platform #12 (docs/IOP_BRINGUP.md M3). No
// i-cache throughput overlay: the core clock IS machine time.
// Shared wrapper plumbing (bus forwarders, hooks, sync): MoiraCpu.h.

#pragma once
#include "IIfxMemory.h"
#include "MoiraCpu.h"
#include <cstdint>

class IIfxCpu : public pom68k::MoiraCpu<IIfxCpu, IIfxMemory> {
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
    void setEngine(int e) { jit_.setEnabled(e != 0); pomJitDisarm(); }

    void runCycles(moira::i64 n);
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
    friend pom68k::MoiraCpu<IIfxCpu, IIfxMemory>;
    moira::u16 read16OnReset(moira::u32 addr) const override;
    void didChangeCACR(moira::u32 value) override;
    // Fixed-batch pacing, not a device-derived deadline (TODO.md § 4) —
    // shadows the base's deadline-flavoured dispatch.
    void catchUp();

    static constexpr int kPeriphBatch = 64;
};
