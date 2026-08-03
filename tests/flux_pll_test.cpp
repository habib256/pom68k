// POM68K — the floppy data-separator PLL (src/FluxPll.h), an integer port
// of MAME's `fdc_pll_t`. This is the piece POM68K never had: the drive
// stored cells already aligned to the controller's rate, so no separator
// was needed and none existed (`docs/LLE_VS_HLE.md` § 1.3).
//
// The properties that matter are not "it compiles" but:
//   1. on a PERFECT stream it recovers exactly the cells that were written;
//   2. on a JITTERED stream it still does — that is the whole point of a
//      PLL, and it is what an ideal cell array can never demonstrate;
//   3. on a stream written at a DIFFERENT rate it pulls its period toward
//      that rate instead of slipping;
//   4. it refuses to run past `limit` without consuming anything;
//   5. write-then-read round-trips through the same class.
//
// Every test drives `feedReadData` directly with synthesized edges — no
// drive, no controller — so a failure here is the PLL and nothing else.

#include "FluxPll.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {
int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-64s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}

constexpr int64_t kCell = FluxPll::kSubCell;      // 1024 ticks per cell

// Lay a bit pattern down as flux: a 1 puts a transition at the centre of
// its cell, a 0 puts none. `cellLen` lets a track be written at a rate
// other than the reader's nominal one.
std::vector<int64_t> layFlux(const std::vector<int>& bits, int64_t cellLen,
                             int64_t start = 0) {
    std::vector<int64_t> flux;
    for (size_t i = 0; i < bits.size(); i++)
        if (bits[i]) flux.push_back(start + int64_t(i) * cellLen + cellLen / 2);
    return flux;
}

// Read `n` cells back, feeding the PLL the next transition at or after its
// current position — exactly what `fdc_pll_t::get_next_bit` does with a
// floppy_image_device.
std::vector<int> readCells(FluxPll& pll, const std::vector<int64_t>& flux, int n) {
    std::vector<int> out;
    size_t fi = 0;
    for (int i = 0; i < n; i++) {
        while (fi < flux.size() && flux[fi] <= pll.ctime()) fi++;
        const int64_t edge = fi < flux.size() ? flux[fi] : FluxPll::kNever;
        const int bit = pll.feedReadData(edge);
        if (bit < 0) break;
        out.push_back(bit);
    }
    return out;
}

// A cheap deterministic jitter source — no <random>, so the vectors are
// identical on every platform and a failure is reproducible.
int64_t jitter(uint32_t& s, int64_t amplitude) {
    s = s * 1664525u + 1013904223u;
    return int64_t((s >> 16) % uint32_t(2 * amplitude + 1)) - amplitude;
}

// The MFM-ish pattern the tests read back: enough 1s to keep the loop
// locked and enough 0 runs to make a slipped window visible.
std::vector<int> pattern(int cells) {
    std::vector<int> b;
    const int seq[] = { 1, 0, 1, 1, 0, 0, 1, 0, 1, 0, 0, 1, 1, 0, 1, 0 };
    for (int i = 0; i < cells; i++) b.push_back(seq[i % 16]);
    return b;
}
}  // namespace

int main() {
    std::printf("flux_pll_test — the floppy data separator\n");

    // ── 1. Perfect stream ───────────────────────────────────────────────
    {
        const std::vector<int> bits = pattern(400);
        const std::vector<int64_t> flux = layFlux(bits, kCell);
        FluxPll pll;
        pll.setClock(kCell);
        pll.readReset(0);
        const std::vector<int> got = readCells(pll, flux, int(bits.size()));
        check(got.size() == bits.size(), "perfect stream: every cell recovered");
        check(got == bits, "perfect stream: cells match bit for bit");
        check(pll.period() == kCell, "perfect stream: the period never drifts");
    }

    // ── 2. Jittered stream — the property ideal cells cannot show ───────
    // ±12 % of a cell on every transition. A window-edge comparison with no
    // phase feedback would mis-read these; the PLL must not.
    {
        const std::vector<int> bits = pattern(2000);
        std::vector<int64_t> flux = layFlux(bits, kCell);
        uint32_t seed = 12345;
        for (int64_t& e : flux) e += jitter(seed, kCell * 12 / 100);
        FluxPll pll;
        pll.setClock(kCell);
        pll.readReset(0);
        const std::vector<int> got = readCells(pll, flux, int(bits.size()));
        check(got == bits, "±12 % jitter: cells still recovered exactly");
    }

    // ── 3. Off-rate track: the loop pulls its period ────────────────────
    // A track written 8 % slow (a drive spinning under speed when it was
    // formatted). Without the frequency term the reader slips within a few
    // hundred cells; with it, the period walks toward the medium's.
    {
        const std::vector<int> bits = pattern(3000);
        const int64_t slow = kCell * 108 / 100;
        const std::vector<int64_t> flux = layFlux(bits, slow);
        FluxPll pll;
        pll.setClock(kCell);
        pll.readReset(0);
        const std::vector<int> got = readCells(pll, flux, int(bits.size()));
        check(got == bits, "track written 8 % slow: cells recovered");
        check(pll.period() > kCell, "…and the PLL pulled its period UP toward the medium");
        check(pll.period() <= kCell * 125 / 100, "…without exceeding the +25 % clamp");
    }
    {
        // …and the same 8 % fast, to prove the trim is not one-sided.
        const std::vector<int> bits = pattern(3000);
        const std::vector<int64_t> flux = layFlux(bits, kCell * 92 / 100);
        FluxPll pll;
        pll.setClock(kCell);
        pll.readReset(0);
        const std::vector<int> got = readCells(pll, flux, int(bits.size()));
        check(got == bits, "track written 8 % fast: cells recovered");
        check(pll.period() < kCell, "…and the PLL pulled its period DOWN");
        check(pll.period() >= kCell * 75 / 100, "…without dropping below the -25 % clamp");
    }

    // ── 4. The gate bites — and the first attempt at this did NOT ──────
    // The obvious "prove a dumb decoder fails" case is the jittered stream
    // of test 2. It does not work: ±12 % of a cell never pushes a
    // transition out of its own fixed window, so a naive fixed-window
    // decoder reads it perfectly and test 2 would prove nothing about the
    // loop. Kept as a comment because the next person will try the same
    // thing.
    //
    // The unambiguous case is the OFF-RATE track of test 3: without a
    // frequency term, a fixed 1-cell window walks off a medium written 8 %
    // slow within a handful of cells and never recovers. That is exactly
    // what the PLL's `freq_hist` trim exists to prevent, and test 3 shows
    // the PLL reading the very same flux correctly.
    {
        const std::vector<int> bits = pattern(3000);
        const int64_t slow = kCell * 108 / 100;
        const std::vector<int64_t> flux = layFlux(bits, slow);
        std::vector<int> naive(bits.size(), 0);
        for (int64_t e : flux) {
            const int64_t idx = e / kCell;             // fixed nominal window
            if (idx >= 0 && idx < int64_t(naive.size())) naive[size_t(idx)] = 1;
        }
        check(naive != bits,
              "a fixed-window separator DOES slip on the 8 %-slow track");
        // …and it slips EARLY, not just eventually: locate the first cell
        // the naive decoder gets wrong, to prove the divergence is real
        // rather than a tail effect of the vector length.
        size_t firstBad = bits.size();
        for (size_t i = 0; i < bits.size(); i++)
            if (naive[i] != bits[i]) { firstBad = i; break; }
        check(firstBad < 32, "…within the first 32 cells (it is not a tail artefact)");
    }

    // ── 5. limit: nothing is consumed when the window would overrun ─────
    {
        FluxPll pll;
        pll.setClock(kCell);
        pll.readReset(0);
        const int64_t before = pll.ctime();
        const int r = pll.feedReadData(FluxPll::kNever, kCell / 2);
        check(r == -1, "limit: a window that would overrun returns -1");
        check(pll.ctime() == before, "limit: …and consumes nothing");
        check(pll.feedReadData(FluxPll::kNever, kCell * 4) == 0,
              "limit: the same call succeeds once there is room");
    }

    // ── 6. Write, then read it back through the same loop ───────────────
    {
        const std::vector<int> bits = pattern(256);
        FluxPll wr;
        wr.setClock(kCell);
        wr.readReset(0);
        wr.startWriting(0);
        std::vector<int64_t> flux;
        for (size_t i = 0; i < bits.size(); i++) {
            if (wr.writeCount() == 32) {           // drain the 32-slot buffer
                for (int k = 0; k < wr.writeCount(); k++) flux.push_back(wr.writeAt(k));
                wr.commitWrite(wr.ctime());
            }
            wr.writeNextBit(bits[i] != 0);
        }
        for (int k = 0; k < wr.writeCount(); k++) flux.push_back(wr.writeAt(k));
        check(flux.size() > 0, "write: transitions were emitted");

        FluxPll rd;
        rd.setClock(kCell);
        rd.readReset(0);
        const std::vector<int> got = readCells(rd, flux, int(bits.size()));
        check(got == bits, "write→read round-trips through the PLL");
    }

    if (gFails) {
        std::printf("flux_pll_test: %d failure(s)\n", gFails);
        return 1;
    }
    std::printf("flux_pll_test: OK\n");
    return 0;
}
