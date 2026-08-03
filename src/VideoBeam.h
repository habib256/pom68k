// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── The raster beam ──
// Every platform already accumulates CPU cycles into the current frame to
// generate its VBL (`framePos_` in `V8Memory`, `RbvMemory`, `SonoraMemory`,
// `Dafb`, `Valkyrie`, `TobyVideo`…). This class turns that ONE accumulator
// into a beam position, and drives a row-granular decode from it.
//
// It deliberately owns no clock. `setPos()` adopts the platform's own
// accumulator, so there is exactly one source of frame time and the VBL
// edges — which are load-bearing and have bitten this project before —
// are untouched by anything here.
//
// Two jobs:
//   1. `line()` / `inVBlank()` — the position, for registers that expose one.
//   2. `pumpRows()` — the visible rows the beam has newly crossed, so a
//      decoder can render each row ONCE, at the moment it is scanned out.
//      That is the whole point: a decode taken at an arbitrary later moment
//      shows rows the guest has since overwritten (tearing) and register
//      state the beam never had (a mid-frame palette split lands on the
//      wrong lines). Same total work per frame as one whole-frame decode —
//      each row is still decoded exactly once — only correctly placed.
//
// Not serialized: the row cursor is a pure cache, re-derivable from the
// frame accumulator and the framebuffer (SaveState.h's rule).

#pragma once
#include <algorithm>
#include <cstdint>

class VideoBeam {
public:
    // `frameCycles` = a whole frame in CPU cycles, `activeCycles` = the
    // part of it the beam spends on visible lines (the platform's
    // `vblStart_`), `totalLines` includes blanking, `visibleLines` is the
    // decoded height. Deriving the row from `activeCycles` rather than from
    // `line()` keeps the two independent: some platforms model a modeline
    // whose total-line count does not match the resolution the guest asked
    // the decoder for, and every visible row must still be emitted once.
    void setGeometry(int64_t frameCycles, int64_t activeCycles,
                     int totalLines, int visibleLines) {
        if (frameCycles <= 0 || totalLines <= 0 || visibleLines <= 0) {
            valid_ = false;
            return;
        }
        // A changed geometry invalidates the rows already emitted — they
        // were placed against the old one. Restart the frame.
        if (frameCycles != frameCycles_ || activeCycles != activeCycles_ ||
            totalLines != totalLines_ || visibleLines != visibleLines_) {
            emitted_ = 0;
            pos_ = 0;
        }
        frameCycles_ = frameCycles;
        activeCycles_ = std::clamp<int64_t>(activeCycles, 1, frameCycles);
        totalLines_ = totalLines;
        visibleLines_ = visibleLines;
        valid_ = true;
    }

    bool valid() const { return valid_; }
    int visibleLines() const { return visibleLines_; }

    // Adopt the platform's frame accumulator. `frameSeq` counts completed
    // frames: it is what tells a caller that samples ONCE per frame, at a
    // fixed phase, that a whole frame went by — the position alone is
    // modulo, so that case is indistinguishable from no time at all and
    // the screen would never update again. A decrease in position is also
    // a wrap, which covers a caller that has no counter to offer (pass 0).
    void setPos(int64_t framePos, uint64_t frameSeq = 0) {
        if (!valid_) return;
        if (framePos < 0) framePos = 0;
        framePos %= frameCycles_;
        wrapped_ = framePos < pos_ || frameSeq != seq_;
        pos_ = framePos;
        seq_ = frameSeq;
    }

    // Scan position, for the registers that expose one.
    int line() const {
        return valid_ ? int(pos_ * totalLines_ / frameCycles_) : 0;
    }
    bool inVBlank() const { return valid_ && pos_ >= activeCycles_; }

    // The visible row the beam is on, or `visibleLines` while blanking —
    // i.e. the exclusive end of what has been scanned out this frame.
    int scannedRows() const {
        if (!valid_) return 0;
        if (pos_ >= activeCycles_) return visibleLines_;
        return int(std::min<int64_t>(pos_ * visibleLines_ / activeCycles_,
                                     visibleLines_));
    }

    // Emit the visible rows newly crossed since the last call, as half-open
    // [y0, y1) runs — at most twice: the tail of the frame that just ended,
    // then the head of the new one. `emit` is called only for non-empty
    // runs, and every row is emitted exactly once per frame.
    template <class F> void pumpRows(F&& emit) {
        if (!valid_) return;
        if (wrapped_) {
            wrapped_ = false;
            if (emitted_ < visibleLines_) {      // finish the old frame
                emit(emitted_, visibleLines_);
            }
            emitted_ = 0;
        }
        const int target = scannedRows();
        if (target > emitted_) {
            emit(emitted_, target);
            emitted_ = target;
        }
    }

    // Force the next pump to redraw the whole frame — for a decoder whose
    // surface was resized or whose geometry the caller changed underneath.
    void restartFrame() { emitted_ = 0; wrapped_ = false; }

private:
    int64_t frameCycles_ = 0;
    int64_t activeCycles_ = 0;
    int64_t pos_ = 0;
    uint64_t seq_ = 0;           // completed frames, as the platform counts them
    int     totalLines_ = 0;
    int     visibleLines_ = 0;
    int     emitted_ = 0;        // visible rows already scanned out this frame
    bool    wrapped_ = false;
    bool    valid_ = false;
};
