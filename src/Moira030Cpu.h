// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── 68030 wrapper base (CRTP over MoiraCpu) ──
// RbvCpu (IIsi, IIci), VaspCpu (IIvx, IIvi) and SonoraCpu (LC III family)
// held the same code three times: the Cpu030 i-cache throughput boost, a
// capped event-derived peripheral deadline, the CACR SMC hint behind the
// board's own store inventory and the "CPU " save-state chunk. This class
// holds it once; each board keeps its constructor (FPU population, history)
// in its own file.
// FloppyGate selects the one real difference. true (RBV, VASP): the floppy
// boost gate with its continuous machineClock() bases, the slice delivered
// in 4096-cycle chunks, the bases saved. false (Sonora): a fixed boost, the
// slice in one executeUntil, no bases in the chunk — that board's pacing
// and save-state layout, kept as they were.
// Gates: iisi_boot_etalon, iici_boot_etalon, iivx_boot_etalon,
// iivi_boot_etalon (fingerprints unchanged by the merge, 2026-11-21), the
// Sonora etalons (lc3_boot_etalon and its siblings, unchanged by the
// FloppyGate = false fold).

#pragma once
#include "MoiraCpu.h"
#include <algorithm>
#include <optional>

namespace pom68k {

template <class Derived, class Memory, bool FloppyGate = true>
class Moira030Cpu : public MoiraCpu<Derived, Memory> {
    using Base = MoiraCpu<Derived, Memory>;

public:
    void hardReset() {
        this->mem_.reset();
        this->lastPeriphClock_ = this->getClock();
        this->pomIcache.reset();
        this->jit_.flushAll();
        this->reset();                       // SSP/PC from $0 (ROM via overlay)
    }

    // ── JIT engine (030 extension 2026-07-28, the Cpu030 pattern) ──────
    void setEngine(int e) { this->jit_.setEnabled(e != 0); this->pomJitDisarm(); }

    void runCycles(moira::i64 n) {
        // The Egret/Cuda firmware asked for a host reset (RESET_SYSTEM $11,
        // the Finder's "Restart"). Apply it HERE, at a run boundary, never
        // from inside the memory callback that raised it — same contract as
        // the Duo's PMU wake (MscCpu::runCycles). The machine has already
        // re-armed its ROM overlay, so this fetch takes the reset vectors
        // out of ROM.
        if (this->mem_.consumeRestart()) this->reset();

        if constexpr (!FloppyGate) {
            const moira::i64 target = this->getClock() + n * boost_;
            if (this->jit_.enabled()) this->jit_.executeUntil(target);
            else this->executeUntil(target);
            flushTicks();
            return;
        }

        // Deliver the machine-cycle budget in bounded chunks: the floppy
        // gate can flip boost_ mid-slice, and a single core-clock target
        // computed with the old ratio would mis-deliver machine time
        // (Cpu030 pattern).
        const moira::i64 end = machineClock() + n;
        while (machineClock() < end && !this->isHalted()) {
            const moira::i64 chunk =
                std::min<moira::i64>(end - machineClock(), 4096);
            const moira::i64 target = this->getClock() + chunk * boost_;
            if (this->jit_.enabled()) this->jit_.executeUntil(target);
            else this->executeUntil(target);
            flushTicks();
        }
        flushTicks();
    }

    void stall(int cycles) {         // REAL machine cycles (bus time)
        if (cycles <= 0) return;
        this->clock += moira::i64(cycles) * boost_;   // machine cycles → core clock
        catchUp();
    }

    void flushTicks() {
        const moira::i64 d = this->clock - this->lastPeriphClock_;
        if (d <= 0) return;
        this->lastPeriphClock_ = this->clock;
        periphAccum_ += d;
        const int m = int(periphAccum_ / boost_);
        periphAccum_ -= moira::i64(m) * boost_;
        if (m) this->mem_.tick(m);
        if constexpr (FloppyGate) pollBoostGate();
        schedulePeriphDeadline();
    }

    // The core clock runs at boost_× machine rate (i-cache throughput
    // overlay). Anything that models the BUS — E-clock alignment, device
    // wait states — works in machine cycles: on real silicon the i-cache
    // accelerates instruction fetch, never a VIA bus cycle.
    // boost_ is cacheBoost_ normally, 1 while the floppy motor runs (the
    // Cpu030 floppy boost gate); the bases keep this continuous.
    moira::i64 machineClock() const {
        if constexpr (!FloppyGate) return this->clock / boost_;
        return machineBase_ + (this->clock - clockBase_) / boost_;
    }

    // ── Save states (chunk "CPU ") — the Cpu030 wrapper pattern ─────────
    // cacheBoost_/icacheMiss_ are environment tuning, not guest state; the
    // machineClock() bases ARE run state (see Cpu030.h).
    template <class Ar> void visit(Ar& ar) {
        this->visitCpuCommon(ar);
        ar(this->lastPeriphClock_, periphAccum_, periphDeadline_);
        if constexpr (FloppyGate) {
            ar(machineBase_, clockBase_);
            if constexpr (Ar::loading) boost_ = cacheBoost_;
        }
    }

protected:
    friend Base;

    Moira030Cpu(Memory& mem, const jit::ResolvedConfig& jitConfig,
                     const CoreCpuConfig& cpuConfig, bool withFpu)
        : Base(mem, jit::kGuest68030, jitConfig) {
        this->setModel(moira::Model::M68030);
        this->setFPUModel(withFpu ? moira::FPUModel::M68882
                                  : moira::FPUModel::NONE);
        if (cpuConfig.cacheBoost) cacheBoost_ = *cpuConfig.cacheBoost;
        if (cpuConfig.icacheMiss) icacheMiss_ = *cpuConfig.icacheMiss;
        boost_ = cacheBoost_;
        if (FloppyGate && cpuConfig.floppyBoostGate)
            floppyGate_ = *cpuConfig.floppyBoostGate;
        if (cpuConfig.cacr030Flush)
            cacrFlushPolicy_ = *cpuConfig.cacr030Flush ? 1 : 0;
        this->armIcacheOverlay(icacheMiss_);
    }

    void didChangeCACR(moira::u32 value) override {
        this->pomInvalidateIcache030(value);   // CI whole / CEI selected longword
        // The CACR SMC hint, retired behind the board's own audited store
        // inventory (Memory::kJitStoreInventoryComplete, pinned by
        // store_inventory_test): every RAM store already crosses
        // CodeGuard::note(), so the drop-all flush protected nothing.
        // POM68K_JIT_030_CACR_FLUSH keeps its three values as on the V8:
        // unset = the board's own answer, 1 = force the hint back on (the
        // measurement instrument), 0 = force it off.
        const bool flush = cacrFlushPolicy_ < 0
            ? !Memory::kJitStoreInventoryComplete : cacrFlushPolicy_ != 0;
        if ((value & 0x0C) && flush) this->jit_.flushAll();
    }

    void catchUp() {
        if (this->clock < periphDeadline_) return;
        flushTicks();
    }

    // The Cpu030 floppy boost gate: re-evaluated at a settled point,
    // rebased so machineClock() never jumps (rationale in Cpu030.h/.cpp).
    void pollBoostGate() {
        const int want =
            (floppyGate_ && this->mem_.floppyStreaming()) ? 1 : cacheBoost_;
        if (want == boost_) return;
        machineBase_ += (this->clock - clockBase_) / boost_;
        clockBase_ = this->clock;
        periphAccum_ = periphAccum_ * want / boost_;
        boost_ = want;
    }

    void schedulePeriphDeadline() {
        // min(next observable machine-cycle bound, the historical batch):
        // the cap keeps every transport without a deadline API (ADB PIC,
        // IOPs) at exactly its former cadence, so exactness can only
        // refine, never coarsen (TODO.md § Fidélité matérielle et LLE,
        // inventory 2026-08-04).
        moira::i64 machine = this->mem_.cyclesToNextEvent();
        if (machine < 1) machine = 1;
        moira::i64 d = machine * boost_ - periphAccum_;
        if (d > kPeriphBatch) d = kPeriphBatch;
        if (d < 1) d = 1;
        periphDeadline_ = this->clock + d;
    }

    // POM68K_JIT_030_CACR_FLUSH: -1 = board default (retired here), 0/1 =
    // forced by the knob — Cpu030's exact contract.
    int cacrFlushPolicy_ = -1;

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
};

}  // namespace pom68k
