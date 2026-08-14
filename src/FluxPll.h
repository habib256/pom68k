// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Analog data-separator PLL for the floppy controllers ──
// A faithful port of MAME's `fdc_pll_t` (`machine/fdc_pll.cpp`, Olivier
// Galibert, BSD-3) — the phase-locked loop every real floppy controller
// puts between the head amplifier and its shift register. It is handed
// **flux transition times** and produces **cells**: a 1 when a transition
// landed inside the current window, a 0 when none did, adjusting its phase
// and, over a run of same-sign errors, its period.
//
// Why POM68K needs one. `SonyDrive` stores a track as discrete cells at the
// exact rate the controller programmed, so the controllers used to read a
// stream that was pre-aligned by construction: an ideal separator, no
// jitter, nothing for a PLL to do — the "no flux layer" entry of
// `docs/LLE_VS_HLE.md` § 1.3. That simplification is also what left
// `Swim1`'s LS-pair correction machinery (`swim1.cpp:965-1140`) as dead
// code: it exists to discriminate real-world jitter that ideal cells do not
// have.
//
// ── Time unit ────────────────────────────────────────────────────────────
// MAME works in `attotime`. POM68K works in **flux ticks**: `kSubCell`
// subdivisions of one *nominal* cell, as int64. A whole revolution is
// therefore `cellsPerRev × kSubCell` ticks, which is ~2×10^8 for MFM at
// 300 RPM — comfortably inside int64, and integer throughout so a snapshot
// restores bit-identically (no double rounding in the hot path).
//
// The one place MAME uses floating point is the period trim
// (`period_adjust_base × delta / period`); that is exact in integers here,
// which is why `kSubCell` is 1024 rather than something smaller: it keeps
// the trim's resolution meaningful (±25 ticks out of 1024) instead of
// quantising it to nothing.
//
// Gate: tests/flux_pll_test.cpp.

#pragma once
#include "SaveState.h"
#include <cstdint>

class FluxPll {
public:
    /// Subdivisions of one nominal cell. Positions handed to this class —
    /// `readReset`, transition edges, limits — are all in these units.
    static constexpr int64_t kSubCell = 1024;
    /// "No transition" / "no limit", MAME's `attotime::never`.
    static constexpr int64_t kNever = INT64_MAX;

    /// `fdc_pll_t::set_clock` — the nominal cell period, in flux ticks.
    /// The 0.75/1.25 clamps and the 5 % trim base are MAME's.
    void setClock(int64_t period) {
        period_ = period;
        adjustBase_ = period * 5 / 100;
        minPeriod_ = period * 75 / 100;
        maxPeriod_ = period * 125 / 100;
    }

    /// `fdc_pll_t::read_reset` — park the window at `when` and forget the
    /// accumulated phase/frequency history. Note it does NOT restore the
    /// nominal period: a PLL that has pulled to a fast-written track stays
    /// pulled, which is the behaviour that makes a re-read consistent.
    void readReset(int64_t when) {
        ctime_ = when;
        phaseAdjust_ = 0;
        freqHist_ = 0;
    }

    int64_t ctime() const { return ctime_; }
    int64_t period() const { return period_; }
    /// End of the window the next feedReadData() call would close — the
    /// bound an event scheduler needs for "ticks until the next recovered
    /// cell". Exact, not conservative: it is the same `ctime_ + period_ +
    /// phaseAdjust_` the read side computes.
    int64_t nextWindowEnd() const { return ctime_ + period_ + phaseAdjust_; }

    /// `fdc_pll_t::feed_read_data`. `edge` is the first flux transition at
    /// or after `ctime()` (`kNever` if the track is blank ahead); `limit`
    /// caps how far the caller may advance this call.
    ///
    /// Returns the recovered cell: 1 = transition in the window, 0 = none,
    /// **-1 = the window would cross `limit`** — in which case nothing is
    /// consumed and the caller must come back with more time. On 0 or 1,
    /// `ctime()` has advanced to the end of the window just closed.
    int feedReadData(int64_t edge, int64_t limit = kNever) {
        const int64_t next = ctime_ + period_ + phaseAdjust_;
        if (next > limit) return -1;
        ctime_ = next;

        if (edge == kNever || edge > next) {
            // Nothing in the window: a 0 cell, and the PLL free-runs.
            phaseAdjust_ = 0;
            return 0;
        }

        // A transition: a 1 cell, and the loop pulls toward its centre.
        const int64_t delta = edge - (next - period_ / 2);
        // MAME writes the two signs separately to keep the truncation
        // symmetric about zero; C++ integer division already truncates
        // toward zero, so one expression is the same arithmetic.
        phaseAdjust_ = delta * 65 / 100;

        if (delta < 0)       freqHist_ = freqHist_ < 0 ? freqHist_ - 1 : -1;
        else if (delta > 0)  freqHist_ = freqHist_ > 0 ? freqHist_ + 1 : 1;
        else                 freqHist_ = 0;

        // Two or more errors of the SAME sign in a row means the data rate
        // itself differs from ours — trim the period, not just the phase.
        const int afh = freqHist_ < 0 ? -freqHist_ : freqHist_;
        if (afh > 1) {
            period_ += adjustBase_ * delta / period_;
            if (period_ < minPeriod_) period_ = minPeriod_;
            else if (period_ > maxPeriod_) period_ = maxPeriod_;
        }
        return 1;
    }

    // ── Write side (`fdc_pll_t::write_next_bit`) ─────────────────────────
    // A written 1 becomes a transition at the CENTRE of its cell window,
    // which is what makes a write-then-read round trip land the edge where
    // the reader's PLL expects it.
    void startWriting(int64_t tm) { writeStart_ = tm; writeCount_ = 0; }
    bool writing() const { return writeStart_ != kNever; }
    int64_t writeStart() const { return writeStart_; }
    int writeCount() const { return writeCount_; }
    int64_t writeAt(int i) const { return writeBuf_[i]; }

    /// Returns true when the window would cross `limit` (nothing emitted).
    bool writeNextBit(bool bit, int64_t limit = kNever) {
        if (writeStart_ == kNever) { writeStart_ = ctime_; writeCount_ = 0; }
        const int64_t etime = ctime_ + period_;
        if (etime > limit) return true;
        if (bit && writeCount_ < kWriteBuf)
            writeBuf_[writeCount_++] = ctime_ + period_ / 2;
        ctime_ = etime;
        return false;
    }
    void commitWrite(int64_t tm) { writeStart_ = tm; writeCount_ = 0; }

    // Save states: the whole loop is live machine state — a snapshot taken
    // mid-sector must resume with the same phase and the same pulled
    // period, or the very next cell can differ from the un-snapshotted run.
    template <class Ar> void visit(Ar& ar) {
        ar(period_, adjustBase_, minPeriod_, maxPeriod_,
           ctime_, phaseAdjust_, freqHist_, writeStart_, writeCount_);
        for (int i = 0; i < kWriteBuf; i++) ar(writeBuf_[i]);
    }

private:
    static constexpr int kWriteBuf = 32;      // MAME's write_buffer size

    int64_t period_ = kSubCell;
    int64_t adjustBase_ = kSubCell * 5 / 100;
    int64_t minPeriod_ = kSubCell * 75 / 100;
    int64_t maxPeriod_ = kSubCell * 125 / 100;

    int64_t ctime_ = 0;
    int64_t phaseAdjust_ = 0;
    int freqHist_ = 0;

    int64_t writeStart_ = kNever;
    int writeCount_ = 0;
    int64_t writeBuf_[kWriteBuf] = {};
};
