// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "Swim2.h"
#include "SonyDrive.h"

#include <algorithm>

void Swim2::reset() {
    mode_ = 0x40;
    setup_ = phases_ = 0;
    for (uint8_t& p : params_) p = 0;
    paramIdx_ = 0;
    fifo_[0] = fifo_[1] = 0;
    fifoPos_ = 0;
    error_ = 0;
    cellPhase_ = 0;
    driveSel_ = 0;
    lstrb_ = false;
    crc_ = 0xCDB4;
    sr_ = 0;
    tssSr_ = tssOutput_ = 0;
    mfmSyncCounter_ = 0;
    currentBit_ = -1;
    halfWait_ = 0;
    writeHalfPos_ = 0;
    writeStartCell_ = 0;
    writeActive_ = false;
    writeTransitions_.clear();
    if (drive_[0]) drive_[0]->reset();
    if (drive_[1]) drive_[1]->reset();
}

void Swim2::attachDrive(SonyDrive* internal, SonyDrive* external) {
    drive_[0] = internal;
    drive_[1] = external;
    if (drive_[0]) drive_[0]->setSuperDrive(true);
    if (drive_[1]) drive_[1]->setSuperDrive(true);
}

SonyDrive* Swim2::selectedDrive() const {
    // MAME swim2.cpp:301 — motor bit gates the soft-select; bits 1:2 pick A/B.
    if (!(mode_ & 0x80)) return nullptr;
    const int sel = (mode_ >> 1) & 3;
    if (sel == 1) return drive_[0];
    if (sel == 2) return drive_[1];
    if (sel == 3) return drive_[driveSel_];      // both — last explicit
    return nullptr;
}

bool Swim2::isWriteProtected() const {
    SonyDrive* d = selectedDrive();
    return !d || !d->hasDisk() || d->isWriteProtected();
}

int Swim2::senseAddr() const {
    // mac_floppy_device::seek_phase_w/wpt_r: direct phase register with
    // HDSEL as bit 3 (floppy.cpp:3227,3303).
    return (phases_ & 7) | (side1() ? 8 : 0);
}

void Swim2::updateDevsel() {
    // soft-select mirror (swim2.cpp:300-301); remember A/B for "both"
    const int sel = (mode_ >> 1) & 3;
    if (sel == 1) driveSel_ = 0;
    else if (sel == 2) driveSel_ = 1;
}

void Swim2::applyPhases(uint8_t value) {
    // applefdintf phases → mac_floppy seek_phase_w: bits 0-2 choose the
    // direct drive register; bit 3 is LSTRB.
    const bool lstrb = (value & 0x08) != 0;
    const bool rising = lstrb && !lstrb_;
    phases_ = value;
    lstrb_ = lstrb;
    if (rising) {
        if (SonyDrive* d = selectedDrive()) d->commandSwim(senseAddr());
    }
}

void Swim2::fifoClear() {
    fifoPos_ = 0;
    crcClear();                                  // MAME fifo_clear()
}

bool Swim2::fifoPush(uint16_t value) {
    if (fifoPos_ == 2) return true;
    fifo_[fifoPos_++] = value;
    return false;
}

uint16_t Swim2::fifoPop() {
    if (!fifoPos_) return 0xFFFF;
    uint16_t value = fifo_[0];
    fifo_[0] = fifo_[1];
    fifoPos_--;
    return value;
}

uint8_t Swim2::read(int reg) {
    switch (reg & 7) {
    case 0: {                                    // data, marks are errors
        uint16_t value = fifoPop();
        if (!error_) {
            if (value == 0xFFFF) error_ |= 0x04;
            else if (value & MARK) error_ |= 0x02;
        }
        return uint8_t(value);
    }
    case 1: {                                    // mark/data, accepts either
        uint16_t value = fifoPop();
        if (!error_ && value == 0xFFFF) error_ |= 0x04;
        return uint8_t(value);
    }
    case 2: {                                    // error, clear on read
        uint8_t value = error_;
        error_ = 0;
        return value;
    }
    case 3: {                                    // four rotating timing params
        uint8_t value = params_[paramIdx_];
        paramIdx_ = (paramIdx_ + 1) & 3;
        return value;
    }
    case 4: return phases_;
    case 5: return setup_;
    case 6: return mode_;
    case 7: {                                    // FIFO handshake (swim2.cpp:188-214)
        uint8_t value = 0;
        if (fifoPos_) {
            if (fifo_[fifoPos_ - 1] & MARK) value |= 0x01;
            if (!(fifo_[fifoPos_ - 1] & CRC0)) value |= 0x02;
        }
        // Sense multiplex on the classic "WPRT" line (mac_floppy::wpt_r).
        SonyDrive* d = selectedDrive();
        bool senseHigh = !d || d->senseSwim(senseAddr());
        if (senseHigh) value |= 0x08;
        if (error_) value |= 0x20;
        if (mode_ & 0x10) {                      // write: report available room
            if (!fifoPos_) value |= 0xC0;
            else if (fifoPos_ == 1) value |= 0x80;
        } else {                                 // read: report queued bytes
            if (fifoPos_ == 2) value |= 0xC0;
            else if (fifoPos_ == 1) value |= 0x80;
        }
        return value;
    }
    }
    return 0xFF;
}

void Swim2::write(int reg, uint8_t value) {
    uint8_t previousMode = mode_;
    switch (reg & 7) {
    case 0:
        if (fifoPush(value) && !error_) error_ |= 0x04;
        break;
    case 1:
        if (fifoPush(MARK | value) && !error_) error_ |= 0x04;
        break;
    case 2:
        if (fifoPush(CRC) && !error_) error_ |= 0x04;
        break;
    case 3:
        params_[paramIdx_] = value;
        paramIdx_ = (paramIdx_ + 1) & 3;
        break;
    case 4:
        applyPhases(value);
        break;
    case 5:
        setup_ = value;
        // setup bit 2 = GCR read clocking; bit 6 = GCR write (swim2.cpp:265-275).
        // Reflect MFM/GCR preference onto the selected SuperDrive when present.
        if (SonyDrive* d = selectedDrive()) {
            // setup.2 = GCR (set) / MFM (clear) on the read path — swim2.cpp:273
            if (d->isSuperDrive()) d->setMfmMode((value & 0x04) == 0);
        }
        break;
    case 6:                                     // mode clear
        mode_ &= uint8_t(~value);
        mode_ |= 0x40;
        paramIdx_ = 0;
        break;
    case 7:                                     // mode set
        mode_ |= value;
        break;
    }
    if (mode_ & 0x01) fifoClear();
    if ((mode_ ^ previousMode) & 0x86) updateDevsel();

    // ACTION edge handling — swim2.cpp:305-340. Mode bit 7 enables
    // soft-selection; motor state itself is phase commands 2/6 (MFD-75W).
    if ((mode_ & 0x18) == 0x18 && (previousMode & 0x18) != 0x18) {
        startWrite();                            // entering write mode
    } else if ((previousMode & 0x18) == 0x18 && (mode_ & 0x18) != 0x18) {
        finishWrite();                           // exiting write mode
        currentBit_ = -1;
        halfWait_ = 0;
    }
    if ((mode_ & 0x18) == 0x08 && (previousMode & 0x18) != 0x08) {
        // Entering read mode: reset shifter + PLL (here: cell divider),
        // land the head at the current rotation angle.
        currentBit_ = 0;
        sr_ = 0;
        mfmSyncCounter_ = 0;
        cellPhase_ = 0;
        if (SonyDrive* d = selectedDrive()) d->syncCellsToRotation(side1());
    } else if ((previousMode & 0x18) == 0x08 && (mode_ & 0x18) != 0x08) {
        currentBit_ = -1;
        cellPhase_ = 0;
    }
}

// setup[3:2] → controller clocks per raw cell (swim2.cpp:329):
// {MFM fclk, GCR fclk, MFM fclk/2, GCR fclk/2}
int Swim2::cellCycles() const {
    static constexpr int kCyclesPerCell[4] = { 16, 31, 31, 63 };
    return kCyclesPerCell[(setup_ >> 2) & 3];
}

void Swim2::startWrite() {
    currentBit_ = 0;
    tssSr_ = 0;
    tssOutput_ = 0;
    halfWait_ = 0;
    writeHalfPos_ = 0;
    writeTransitions_.clear();
    writeActive_ = true;
    SonyDrive* d = selectedDrive();
    writeStartCell_ = d ? d->startWriteCells(side1()) : 0;
}

// Convert the captured transitions into raw cells and hand them to the
// drive. MAME emits attotime flux that floppy.cpp re-clocks; our discrete
// equivalent reconstructs cell gaps like a PLL would: per-gap rounding
// (never cumulative) so the 31/63-half write spacing (swim2.cpp:432-436)
// can't drift against the 16/31-cycle read cells.
void Swim2::finishWrite() {
    if (!writeActive_) return;
    writeActive_ = false;
    SonyDrive* d = selectedDrive();
    if (!d || writeTransitions_.empty()) { writeTransitions_.clear(); return; }

    const uint64_t div = uint64_t(cellCycles()) * 2;   // half-cycles per cell
    std::vector<int64_t> cellsAt;
    cellsAt.reserve(writeTransitions_.size());
    uint64_t prevHalf = 0;
    int64_t prevCell = -1;
    for (uint64_t h : writeTransitions_) {
        int64_t cell;
        if (prevCell < 0) {
            cell = int64_t((h + div / 2) / div);
        } else {
            int64_t gap = int64_t((h - prevHalf + div / 2) / div);
            cell = prevCell + std::max<int64_t>(1, gap);
        }
        cellsAt.push_back(cell);
        prevHalf = h;
        prevCell = cell;
    }
    // Span length in the same per-gap metric (an absolute divide would
    // drift against the 31-half write spacing and clip the CRC tail).
    const int64_t totalCells =
        prevCell + std::max<int64_t>(1, int64_t((writeHalfPos_ - prevHalf + div / 2) / div));
    d->commitCells(writeStartCell_, totalCells, cellsAt, !(setup_ & 0x40));
    writeTransitions_.clear();
}

void Swim2::tick(int controllerCycles) {
    if (!(mode_ & 0x08)) return;                 // ACTION off
    if (mode_ & 0x10) tickWrite(controllerCycles);
    else              tickRead(controllerCycles);
}

int Swim2::cyclesToNextEvent() const {
    if (!(mode_ & 0x08)) return 0x7fffffff;       // ACTION off
    if (mode_ & 0x10) {
        if (halfWait_) return int((halfWait_ + 1) / 2);
        return currentBit_ >= 0 ? 1 : 0x7fffffff;
    }
    const int left = cellCycles() - cellPhase_;
    return left > 0 ? left : 1;
}

// Read engine — swim2.cpp:482-547 verbatim, cells instead of PLL flux.
void Swim2::tickRead(int cycles) {
    const int cell = cellCycles();
    SonyDrive* d = selectedDrive();
    cellPhase_ += cycles;
    while (cellPhase_ >= cell) {
        cellPhase_ -= cell;
        // No media / stopped spindle: no transitions, the PLL free-runs 0s.
        const int bit = (d && d->hasDisk() && d->motorOn())
                            ? d->nextCell(side1()) : 0;
        if (setup_ & 0x04) {
            // GCR mode: high bit frames the nibble (swim2.cpp:486-497)
            sr_ = uint16_t(((sr_ << 1) | bit) & 0xFF);
            if (sr_ & 0x80) {
                if (fifoPush(sr_) && !error_) error_ |= 0x01;
                sr_ = 0;
            }
        } else {
            // MFM mode (swim2.cpp:499-546): hunt >=64 alternating cells,
            // then decode 16-cell windows; odd cells are data, a 0001 raw
            // pattern on an even cell is a missing clock -> MARK.
            if (mfmSyncCounter_ < 64) {
                if (bit != (mfmSyncCounter_ & 1)) mfmSyncCounter_++;
                else mfmSyncCounter_ = 0;
            } else {
                if (mfmSyncCounter_ == 64 && bit)
                    mfmSyncCounter_--;
                else {
                    if (mfmSyncCounter_ == 65 || mfmSyncCounter_ == 81) {
                        tssSr_ = 0xFF;
                        sr_ = 0;
                    }
                    if (mfmSyncCounter_ & 1) {
                        sr_ |= uint16_t(bit << (((96 - mfmSyncCounter_) >> 1) & 7));
                        crcUpdate(bit);
                    }
                    tssSr_ = uint8_t((tssSr_ << 1) | bit);
                    if ((tssSr_ & 0xF) == 1 && !(mfmSyncCounter_ & 1))
                        sr_ |= MARK;
                    mfmSyncCounter_++;
                    if (mfmSyncCounter_ == 80) {
                        if (!(sr_ & MARK)) mfmSyncCounter_ = 0;
                        else {
                            crcClear();
                            if (fifoPush(sr_) && !error_) error_ |= 0x01;
                        }
                    } else if (mfmSyncCounter_ == 96) {
                        mfmSyncCounter_ -= 16;
                        if (sr_ & MARK) crcClear();
                        else if (!crc_) sr_ |= CRC0;
                        if (fifoPush(sr_) && !error_) error_ |= 0x01;
                    }
                }
            }
        }
    }
}

// Write engine — swim2.cpp:402-481 verbatim. Counts half-cycles; the TSS
// turns data bits into transition spacings (63 halves after a flux, 31/63
// for an empty cell), marks drop the clock via the 0xC entry.
void Swim2::tickWrite(int cycles) {
    uint32_t halves = uint32_t(cycles) << 1;
    while (halves) {
        if (halfWait_) {
            const uint32_t step = std::min(halfWait_, halves);
            halfWait_ -= step;
            halves -= step;
            writeHalfPos_ += step;
            if (halfWait_) break;
        }

        if (tssOutput_ & 0xC) {
            bool bit;
            if (tssOutput_ & 8) {
                bit = (tssOutput_ >> 1) & 1;
                tssOutput_ &= uint8_t(~0xA);
            } else {
                bit = tssOutput_ & 1;
                tssOutput_ = 0;
            }
            if (bit) {
                writeTransitions_.push_back(writeHalfPos_);
                halfWait_ = 63;
            } else
                halfWait_ = (setup_ & 0x40) ? 63 : 31;
            if (setup_ & 8) halfWait_ <<= 1;
            continue;
        }

        if (currentBit_ < 0) break;              // sequence break (MAME fatal)

        if (currentBit_ == 0) {
            if (sr_ & CRC)
                sr_ = uint16_t(crc_ >> 8);       // 2nd CRC byte (shifted low)
            else {
                uint16_t r = fifoPop();
                if (r == 0xFFFF) {
                    if (!error_) error_ |= 0x01; // underrun ends ACTION
                    finishWrite();
                    currentBit_ = -1;
                    halfWait_ = 0;
                    mode_ &= uint8_t(~0x08);
                    break;
                }
                if (r & CRC) sr_ = uint16_t(CRC | (crc_ >> 8));
                else         sr_ = r & (MARK | CRC | 0xFF);
            }
            currentBit_ = 8;
            if (sr_ & MARK) crcClear();
        }
        currentBit_--;
        const int bit = (sr_ >> currentBit_) & 1;
        if (!(sr_ & MARK)) crcUpdate(bit);
        tssSr_ = uint8_t((tssSr_ << 1) | bit);
        if (setup_ & 0x40)
            tssOutput_ = uint8_t(4 | bit);
        else {
            static constexpr uint8_t kTss[4] = { 5, 0xD, 4, 5 };
            if ((sr_ & MARK) && ((tssSr_ & 0xF) == 8)) tssOutput_ = 0xC;
            else tssOutput_ = kTss[tssSr_ & 3];
        }
    }
}
