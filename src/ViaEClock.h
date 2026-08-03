// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── The VIA E clock ──
// A 6522's φ2 on a Mac is the E clock: a fixed **783.36 kHz** (C7M ÷ 10),
// generated on the board and ASYNCHRONOUS to the CPU. On machines whose CPU
// clock is an integer multiple of it the two coincide and an integer divisor
// is exact — Plus 7.8336 MHz ÷ 10, Mac II / IIfx 15.6672 MHz ÷ 20. On the
// 040 boards it is not: 25 MHz ÷ 783 360 = 31.914, 33.33 MHz ÷ 783 360 =
// 42.55. Rounding those to 32 and 43 ran the VIA 0.27 % slow and 1.0 % fast
// respectively (`LLE_VS_HLE.md` § 1.2, "the E-clock ratio wants a fractional
// accumulator like the ASC drain").
//
// This header owns that arithmetic so it exists ONCE. Two things need it and
// they must not drift apart:
//
//   1. the **rate** — how many E-clock cycles a span of machine cycles is
//      worth (`Ticker`, a fractional accumulator, no rounding drift);
//   2. the **phase grid** — where `viaSync()` stalls the CPU so a VIA access
//      lands on an E-clock edge. That one is load-bearing: mis-scaling it is
//      what wedged the IIsi and blacked out the LC III in 2026-07-25, and the
//      pinned lesson from that day is that bus time is charged in MACHINE
//      cycles, never the boosted core clock. Callers must pass a machine-cycle
//      count; this header cannot check that for them.

#pragma once
#include <cstdint>

namespace via_eclock {

inline constexpr int64_t kHz = 783360;          // C7M ÷ 10, every Mac

// E-clock cycles elapsed by machine cycle `c`.
inline int64_t cycleAt(int64_t c, int64_t cpuHz) {
    return cpuHz > 0 ? c * kHz / cpuHz : 0;
}

// The stall target `viaSync()` aims at: the middle of the NEXT E-clock cycle,
// plus one. Identical to the old integer `(viaCycle + 1) * D + D / 2 + 1`
// when D divides evenly, exact when it does not.
inline int64_t syncTarget(int64_t c, int64_t cpuHz) {
    if (cpuHz <= 0) return c;
    return ((2 * cycleAt(c, cpuHz) + 3) * cpuHz) / (2 * kHz) + 1;
}

// Machine cycles → E-clock cycles, carrying the remainder so the rate has no
// rounding drift (the pattern `SonoraMemory`/`RbvMemory`/`VaspMemory` already
// use for their VIA, and `Asc` for its drain).
struct Ticker {
    int64_t acc = 0;

    int advance(int machineCycles, int64_t cpuHz) {
        if (cpuHz <= 0) return 0;
        acc += int64_t(machineCycles) * kHz;
        const int n = int(acc / cpuHz);
        acc -= int64_t(n) * cpuHz;
        return n;
    }

    template <class Ar> void visit(Ar& ar) { ar(acc); }
};

} // namespace via_eclock
