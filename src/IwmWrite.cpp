// POM68K — IWM magnetic write serializer.
// Timing/state reference: MAME 0.285 iwm.cpp MODE_WRITE (lines 471–530).
#include "Iwm.h"
#include "SonyDrive.h"

void Iwm::beginWriteFlux() {
    wrElapsed_ = 0;
    wrEdges_.clear();
    wrStart_ = selectedDrive() ? selectedDrive()->startWriteFlux(sel_) : 0;
}

void Iwm::flushWriteFlux() {
    if (SonyDrive* d = selectedDrive(); d && d->motorOn())
        d->commitFlux(wrStart_, wrElapsed_, wrEdges_, false, 2 * halfWindowTicks());
    wrStart_ += wrElapsed_;
    wrElapsed_ = 0;
    wrEdges_.clear();
}

void Iwm::setSel(bool sel) {
    if (sel == sel_) return;
    if (writing_) flushWriteFlux();
    sel_ = sel;
    if (writing_) beginWriteFlux();
    readArmed_ = false;
}

// A PA4 change moves ENABLE1 to the other internal connector: the same
// hand-over as SEL or ENABLE2 — the magnetic arc so far belongs to the
// mechanism that was selected.
void Iwm::setInternalSelect(bool high) {
    if (high == intSel_) return;
    const bool moves = intSelWired_ && !driveSel_;
    if (moves && writing_) flushWriteFlux();
    intSel_ = high;
    if (!moves) return;
    if (writing_) beginWriteFlux();
    readArmed_ = false;
}

void Iwm::tickWrite(int64_t elapsed) {
    if (wrUnderrun_) return;
    while (elapsed >= wrPhase_) {
        elapsed -= wrPhase_;
        wrElapsed_ += wrPhase_;
        if (wrState_ == 0) {
            if (!wrPending_) {
                // Stop at the actual load deadline, independent of scheduler batch.
                wrUnderrun_ = true;
                flushWriteFlux();
                wrPhase_ = 0;
                return;
            }
            wrShift_ = wrData_;
            wrPending_ = false;
            wrState_ = 1;
            wrPhase_ = halfWindowTicks() - 7 * clockTick();
            written++;
        } else if (wrState_ == 1) {
            if (wrShift_ & 0x80) wrEdges_.push_back(wrElapsed_);
            wrShift_ <<= 1;
            wrState_ = 2;
            wrPhase_ = halfWindowTicks();
        } else {
            if (!isSync() && --wrBits_ == 0) {
                wrBits_ = 8;
                wrState_ = 0;
                wrPhase_ = 7 * clockTick();
            } else {
                wrState_ = 1;
                wrPhase_ = halfWindowTicks();
            }
            // Bound resident write arcs without changing the shifter phase.
            if (wrElapsed_ >= 15667200LL * FluxPll::kSubCell) flushWriteFlux();
        }
    }
    wrElapsed_ += elapsed;
    wrPhase_ -= elapsed;
}
