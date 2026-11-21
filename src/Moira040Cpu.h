// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── 68040 wrapper base (CRTP over MoiraCpu) ──
// The four 68040 boards (MEMCjr: Cpu040; djMEMC: CentrisCpu; Spike/Eclipse:
// Q700Cpu; F108: Q630Cpu) each carried a private copy of the same pacing:
// cacheBoost_ scaling, the event-derived peripheral deadline, stall(),
// flushTicks(), CACR → JIT flush, the engine lock and the "CPU " save-state
// chunk. The copies had drifted — the Centris never clamped a zero boost
// (a division by zero) and three of the four left a stale deadline across
// hardReset() — and none of the drift was a platform decision. This class
// holds that pacing ONCE; what stays in each board's file is what makes it
// a platform: its CPU identity defaults, its configuration knobs and, for
// Cpu040, the lockstep and peripheral-statistics taps.
//
// Hooks a derived class may shadow (all inline to nothing here):
//   onPeriphCatchUp()        — every catchUp(), before the deadline test;
//   onPeriphFlushed(int m)   — after a flush served m machine cycles and
//                              re-armed the deadline;
//   onJitDeadline()          — the JIT's inline deadline test fired.
// Gates: q605_boot_etalon, centris650_boot_etalon, q700_boot_etalon,
// q630_boot_etalon (fingerprints unchanged by the merge, 2026-11-21).

#pragma once
#include "LleSession.h"
#include "MoiraCpu.h"
#include <algorithm>
#include <optional>

namespace pom68k {

template <class Derived, class Memory>
class Moira040Cpu : public MoiraCpu<Derived, Memory> {
    using Base = MoiraCpu<Derived, Memory>;

public:
    void hardReset() {
        this->mem_.reset();
        this->lastPeriphClock_ = this->getClock();
        periphAccum_ = 0;
        // A reset mid-session keeps the clock running: the deadline of the
        // previous run would otherwise stand until the clock reached it,
        // past whatever the freshly reset devices now ask for.
        schedulePeriphDeadline();
        this->jit_.flushAll();
        this->reset();                       // SSP/PC from $0 (ROM overlay)
    }

    void runCycles(moira::i64 n) {
        // The Egret/Cuda firmware asked for a host reset (RESET_SYSTEM $11,
        // the Finder's "Restart"). Apply it HERE, at a run boundary, never
        // from inside the memory callback that raised it — same contract as
        // the Duo's PMU wake (MscCpu::runCycles). The board has already
        // re-armed its ROM overlay, so this fetch takes the reset vectors
        // out of ROM. A board without such a latch has no consumeRestart().
        if constexpr (requires(Memory& m) { m.consumeRestart(); }) {
            if (this->mem_.consumeRestart()) this->reset();
        }
        // n is a peripheral (machine) cycle budget; run cacheBoost_× more
        // Moira cycles so hot i-cache-resident code keeps up with a real 040
        // without derailing ASC/VIA pacing (same contract as Cpu030).
        // The one and only switch point between the two engines.
        const moira::i64 target = this->getClock() + n * cacheBoost_;
        if (this->jit_.enabled()) this->jit_.executeUntil(target);
        else this->executeUntil(target);
        flushTicks();
    }

    // ── JIT engine (src/jit/POM68K_JIT.md) ─────────────────────────────
    // The second execution engine, ON by default for the 68040. setEngine()
    // is the only switch; the GUI routes it through the machine thread's
    // command queue so it always lands between two instructions.
    void setEngine(int e) {
        // This machine's registry, not the process's: the engine lock is
        // part of the promise made by THIS session (2026-08-27).
        if (!lle::engineChangeAllowed(this->mem_.lleRegistry(), e)) return;
        // setEnabled() already flushes everything; the explicit disarm is
        // belt and braces — a code window left armed while the INTERPRETER
        // runs would have it fetching from a host pointer nobody maintains.
        this->jit_.setEnabled(e != 0);
        this->pomJitDisarm();
    }

    // Wait states are specified in machine cycles (VIA E-clock, SWIM +5).
    // Scale into Moira time so flushTicks() still yields `cycles` of
    // peripheral time under cacheBoost_ > 1.
    void stall(int cycles) {
        if (cycles <= 0) return;
        this->clock += moira::i64(cycles) * cacheBoost_;
        catchUp();
    }

    // Run the peripherals up to `clock`.
    void flushTicks() {
        const moira::i64 d = this->clock - this->lastPeriphClock_;
        if (d <= 0) return;
        this->lastPeriphClock_ = this->clock;
        // Scale Moira cycles down to machine cycles. A shift at the
        // power-of-two boosts (the default 4): the 64-bit divide by a
        // runtime divisor was 5.8 % of the Quadra 605 JIT bench at 23 M
        // flushes per 50 s of guest time (2026-11-07). periphAccum_ is never
        // negative, so the shift and the divide agree.
        periphAccum_ += d;
        const int m = boostShift_ >= 0 ? int(periphAccum_ >> boostShift_)
                                       : int(periphAccum_ / cacheBoost_);
        periphAccum_ -= moira::i64(m) * cacheBoost_;
        if (m) this->mem_.tick(m);
        schedulePeriphDeadline();
        this->self().onPeriphFlushed(m);
    }

    // Re-arm the peripheral deadline. The board calls one of these after a
    // device access: a register access that arms a countdown (a SCSI
    // command or the last PIO byte, a VIA T2 load, a DAFB interrupt enable)
    // changes the bound the current deadline was computed from, and without
    // this the event would be served at the previously scheduled flush —
    // late by up to one slice (2026-11-07).
    //   schedulePeriphDeadline  — from mem_.cyclesToNextEvent(), every
    //                             device, measured from lastPeriphClock_,
    //                             the devices' common time: a running
    //                             countdown is in device cycles, and the
    //                             devices stand where the last flush left
    //                             them, not at `clock`;
    //   tightenPeriphDeadline   — one device's countdown armed by the access
    //                             itself, in machine cycles from now; only
    //                             ever brings the deadline forward. The
    //                             other countdowns are already in the
    //                             deadline, so this costs one bound instead
    //                             of eight — the 53C96 status poll runs
    //                             320 000 accesses a second.
    void schedulePeriphDeadline() {
        periphDeadline_ = periphDeadlineFor(this->mem_.cyclesToNextEvent());
    }
    void tightenPeriphDeadline(int machineCycles) {
        // Measured from `clock`, not from the last flush: the countdown was
        // armed by the access just served, so it starts now. A countdown
        // that was already running is already in the deadline (the min
        // keeps it), and measuring a fresh one from the last flush would
        // fire it early by the unflushed span and buy an extra flush each
        // time — +16 % flushes on the Quadra 605 bench when tried
        // (2026-11-07).
        if (machineCycles >= 0x7fffffff) return;
        const moira::i64 machine = std::max(1, machineCycles);
        periphDeadline_ =
            std::min(periphDeadline_, this->clock + machine * cacheBoost_);
    }

    int cacheBoost() const { return cacheBoost_; }
    // Bus/wire time (E-clock, wait states, the PIC1654S co-step) must be
    // measured here, not on the boosted core clock — see Moira030Cpu.h.
    moira::i64 machineClock() const { return this->clock / cacheBoost_; }

    // ── Save states (chunk "CPU ") — the Cpu030 wrapper pattern ─────────
    // cacheBoost_ is environment tuning, not guest state.
    template <class Ar> void visit(Ar& ar) {
        this->visitCpuCommon(ar);
        ar(this->lastPeriphClock_, periphAccum_, periphDeadline_);
    }

protected:
    friend Base;

    Moira040Cpu(Memory& mem, const jit::ResolvedConfig& jitConfig)
        : Base(mem, jit::kGuest68040, jitConfig) {
        // The JIT's generated code makes the peripheral catch-up test inline
        // rather than calling sync() on every instruction.
        this->jit_.setPeriphDeadline(&periphDeadline_, [](moira::Moira* cpu) {
            static_cast<Derived*>(cpu)->onJitDeadline();
        });
    }

    // The CPU identity: the full 68040 with its integrated FPU, or the
    // 68LC040 with either Moira's soft 68882 attached (a combination no
    // real board has, kept as the Finder-usable default of the LC040
    // profiles until TODO.md § Moteur › Justesse 68040 decides it) or no FPU
    // at all (architectural vector-11 format-$4 F-line — the silicon).
    void selectIdentity(bool lc040, bool bareFpu, bool mmu040Walk) {
        if (lc040) {
            this->setModel(moira::Model::M68LC040);
            this->setFPUModel(bareFpu ? moira::FPUModel::NONE
                                      : moira::FPUModel::M68882);
        } else {
            this->setModel(moira::Model::M68040);
            this->setFPUModel(moira::FPUModel::M68040);
        }
        // Q8: walk-per-access comparison mode (disables the I/D ATC overlay).
        if (mmu040Walk) this->setMmu040AtcArmed(false);
    }

    // Throughput ceiling (the Cpu030 pattern). Was pinned at 1 for a long
    // time because "boost 2+ → SCSI=0" on q605_boot_etalon; re-measured
    // 2026-07-25 and that ceiling is GONE — every 040 machine etalon boots
    // at 2, 4 and 8. Default 4, matching the 030 family. Stall / viaSync /
    // syncSwimFromCpu are boost-invariant.
    void setCacheBoost(const std::optional<int>& boost) {
        if (boost) cacheBoost_ = *boost;
        if (cacheBoost_ < 1) cacheBoost_ = 1;
        boostShift_ = (cacheBoost_ & (cacheBoost_ - 1)) == 0
                    ? __builtin_ctz(unsigned(cacheBoost_)) : -1;
    }

    // CINV/CPUSH is the guest announcing that it just wrote code.
    void didChangeCACR(moira::u32) override { this->jit_.flushAll(); }

    void catchUp() {
        this->self().onPeriphCatchUp();
        if (this->clock < periphDeadline_) return;
        flushTicks();
    }

    void onPeriphCatchUp() {}
    void onPeriphFlushed(int) {}
    void onJitDeadline() { flushTicks(); }

    moira::i64 periphDeadlineFor(int machineCycles) const {
        const moira::i64 machine = std::max(1, machineCycles);
        moira::i64 d = machine * cacheBoost_ - periphAccum_;
        if (d < 1) d = 1;
        return this->lastPeriphClock_ + d;
    }

    int cacheBoost_ = 4;
    int boostShift_ = 2;                    // log2(cacheBoost_), −1 if not a power of two
    moira::i64 periphAccum_ = 0;
    moira::i64 periphDeadline_ = 0;
};

}  // namespace pom68k
