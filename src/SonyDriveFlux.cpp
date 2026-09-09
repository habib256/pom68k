// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

// Flux-to-cell reconstruction for SonyDrive's offline write-back decoders.

#include "SonyDrive.h"

#include <algorithm>

// The store, read back through a real separator — the offline write-back
// decoders' view of the medium. Quantizing on the nominal grid instead
// would defeat the store's whole purpose: a track the guest wrote at its
// own rate would decode to garbage here and commit nothing, where a real
// controller's PLL reads it back without noticing.
void SonyDrive::rebuildCellsFromFlux() {
    cellsDirty_ = false;
    cells_.clear();
    decodeWriteCellBegin_ = decodeWriteCellEnd_ = size_t(-1);
    if (flux_.empty() || fluxRev_ <= 0) return;
    const int64_t nominalT = fluxCellTicks();
    const int64_t cellT = decodeCellTicks_ > 0 ? decodeCellTicks_ : nominalT;
    const bool split = decodeSpanTicks_ > 0 && decodeStartTick_ > 0 &&
                       decodeStartTick_ + decodeSpanTicks_ < fluxRev_;
    const int64_t writeFrom = decodeStartTick_;
    const int64_t writeTo = writeFrom + decodeSpanTicks_;
    FluxPll pll;
    pll.setClock(split ? nominalT : cellT);
    pll.readReset(0);
    size_t idx = 0;
    int stage = 0;                               // nominal / write / nominal
    size_t writeCellBegin = 0, writeCellEnd = 0;
    cells_.reserve(size_t(fluxRev_ / std::min(cellT, nominalT)) + 8);
    while (pll.ctime() < fluxRev_) {
        const int64_t boundary = !split ? fluxRev_
                               : stage == 0 ? writeFrom
                               : stage == 1 ? writeTo : fluxRev_;
        while (idx < flux_.size() && flux_[idx] < pll.ctime()) idx++;
        const int64_t edge = idx < flux_.size() ? flux_[idx] : FluxPll::kNever;
        const int cell = pll.feedReadData(edge, boundary);
        if (cell < 0) {
            if (!split || stage == 2) break;
            stage++;
            if (stage == 1) writeCellBegin = cells_.size();
            else            writeCellEnd = cells_.size();
            pll.setClock(stage == 1 ? cellT : nominalT);
            if (stage == 1) {
                // The SWIM lays its first transition at ACTION time. Put it
                // at the centre of the verifier's first programmed-rate cell.
                pll.readReset(writeFrom - cellT / 2);
                idx = size_t(std::lower_bound(flux_.begin(), flux_.end(), writeFrom)
                           - flux_.begin());
            } else {
                // Resume on the canonical medium's original cell grid. A
                // write splice normally closes part-way through one of
                // those cells; making that arbitrary instant a new cell
                // boundary phase-shifts the untouched rest of the track.
                const int64_t resume = ((writeTo + nominalT - 1) / nominalT)
                                     * nominalT;
                pll.readReset(resume);
                idx = size_t(std::lower_bound(flux_.begin(), flux_.end(), resume)
                           - flux_.begin());
            }
            continue;
        }
        cells_.push_back(uint8_t(cell));
        if (cell) idx++;                         // that edge is consumed
    }
    if (split) {
        decodeWriteCellBegin_ = writeCellBegin;
        decodeWriteCellEnd_ = writeCellEnd;
    }
}
