// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// SWIM1: IWM personality delegated to `Iwm`; ISM personality ported from
// MAME swim1.cpp (register file :175-360, switch :555-579, write engine
// :888-965, read reduced from the LS-pair CSM :965-1140 to the ideal-cell
// shifter — see Swim1.h). Engine structure mirrors Swim2.cpp so the two
// SWIM generations stay diffable.

#include "Swim1.h"
#include "SonyDrive.h"

#include <algorithm>

void Swim1::reset() {
    iwm_.reset();
    ismMode_ = false;
    iwmToIsm_ = 0;
    mode_ = 0;
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
    if (onDat1Byte) onDat1Byte(false);           // swim1.cpp:109
    if (drive_[0]) drive_[0]->reset();
    if (drive_[1]) drive_[1]->reset();
}

void Swim1::attachDrive(SonyDrive* internal, SonyDrive* external) {
    drive_[0] = internal;
    drive_[1] = external;
    iwm_.attachDrive(internal, external);
    // The LC II ships a SuperDrive (MFD-75W) as its internal mechanism.
    if (drive_[0]) drive_[0]->setSuperDrive(true);
    if (drive_[1]) drive_[1]->setSuperDrive(true);
}

SonyDrive* Swim1::selectedDrive() const {
    if (!(mode_ & 0x80)) return nullptr;         // motor/soft-select gate
    const int sel = (mode_ >> 1) & 3;
    if (sel == 1) return drive_[0];
    if (sel == 2) return drive_[1];
    if (sel == 3) return drive_[driveSel_];
    return nullptr;
}

int Swim1::senseAddr() const {
    return (phases_ & 7) | (side1() ? 8 : 0);
}

void Swim1::updateDevsel() {
    const int sel = (mode_ >> 1) & 3;
    if (sel == 1) driveSel_ = 0;
    else if (sel == 2) driveSel_ = 1;
}

void Swim1::applyPhases(uint8_t value) {
    // applefdintf phases → mac_floppy seek_phase_w: bits 0-2 choose the
    // direct drive register; bit 3 is LSTRB (upper output-enable nibble
    // is not modelled — the driver always enables all four).
    const bool lstrb = (value & 0x08) != 0;
    const bool rising = lstrb && !lstrb_;
    phases_ = value;
    lstrb_ = lstrb;
    if (rising) {
        if (SonyDrive* d = selectedDrive()) d->commandSwim(senseAddr());
    }
}

// swim1.cpp:1226 ism_update_dat1byte — mode bit 4 is the write direction.
void Swim1::updateDat1Byte() {
    if (!onDat1Byte) return;
    onDat1Byte((mode_ & 0x10) ? fifoPos_ < 2 : fifoPos_ > 0);
}

void Swim1::fifoClear() {
    fifoPos_ = 0;
    updateDat1Byte();
    crcClear();
}

bool Swim1::fifoPush(uint16_t value) {
    if (fifoPos_ == 2) return true;
    fifo_[fifoPos_++] = value;
    updateDat1Byte();
    return false;
}

uint16_t Swim1::fifoPop() {
    if (!fifoPos_) return 0xFFFF;
    uint16_t value = fifo_[0];
    fifo_[0] = fifo_[1];
    fifoPos_--;
    updateDat1Byte();
    return value;
}

// IWM mode-register write watcher (swim1.cpp:555-579): four writes to the
// q7-set odd address whose bit 6 goes 1,0,1,1 switch the chip to ISM. Any
// break in the pattern resets the counter.
void Swim1::iwmModeWatch(uint8_t v) {
    const int prev = iwmToIsm_;
    switch (iwmToIsm_) {
        case 0: case 2:
            if (v & 0x40) iwmToIsm_++;
            break;
        case 1:
            if (!(v & 0x40)) iwmToIsm_++;
            break;
        case 3:
            if (v & 0x40) enterIsm();
            break;
    }
    if (iwmToIsm_ != prev + 1) iwmToIsm_ = 0;
}

void Swim1::enterIsm() {
    ismMode_ = true;
    iwmToIsm_ = 0;
    mode_ |= 0x40;                               // ISM mode reg bit 6 = ISM
}

void Swim1::leaveIsm() {
    ismMode_ = false;
    iwmToIsm_ = 0;
    currentBit_ = -1;
    halfWait_ = 0;
}

uint8_t Swim1::read(int reg) {
    if (!ismMode_) return iwm_.read(reg);
    return ismRead(reg & 7);
}

void Swim1::write(int reg, uint8_t v) {
    if (!ismMode_) {
        // The watcher sees every write to the mode/data address, exactly
        // like MAME's offset-0xf hook (before the IWM consumes it).
        if (reg == 15) iwmModeWatch(v);
        iwm_.write(reg, v);
        return;
    }
    ismWrite(reg & 7, v);
}

uint8_t Swim1::ismRead(int reg) {
    switch (reg) {
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
    case 3: {                                    // 16-deep rotating param RAM
        uint8_t value = params_[paramIdx_];
        paramIdx_ = (paramIdx_ + 1) & 15;
        return value;
    }
    case 4: return phases_;
    case 5: return setup_;
    case 6: return mode_;
    case 7: {                                    // handshake (swim1.cpp:224-251)
        uint8_t value = 0;
        if (fifoPos_) {
            if (fifo_[fifoPos_ - 1] & MARK) value |= 0x01;
            if (!(fifo_[fifoPos_ - 1] & CRC0)) value |= 0x02;
        }
        // swim1.cpp:233 raises bits 2+3 together on the WPT/sense line.
        SonyDrive* d = selectedDrive();
        bool senseHigh = !d || d->senseSwim(senseAddr());
        if (senseHigh) value |= 0x0C;
        if (error_) value |= 0x20;
        if (mode_ & 0x10) {                      // write: available room
            if (!fifoPos_) value |= 0xC0;
            else if (fifoPos_ == 1) value |= 0x80;
        } else {                                 // read: queued bytes
            if (fifoPos_ == 2) value |= 0xC0;
            else if (fifoPos_ == 1) value |= 0x80;
        }
        return value;
    }
    }
    return 0xFF;
}

void Swim1::ismWrite(int reg, uint8_t value) {
    uint8_t previousMode = mode_;
    switch (reg) {
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
        paramIdx_ = (paramIdx_ + 1) & 15;
        break;
    case 4:
        applyPhases(value);
        break;
    case 5:
        setup_ = value;
        // setup.2 = GCR (set) / MFM (clear) — reflect onto the SuperDrive
        if (SonyDrive* d = selectedDrive()) {
            if (d->isSuperDrive()) d->setMfmMode((value & 0x04) == 0);
        }
        break;
    case 6:                                     // mode clear — bit 6 exits ISM
        mode_ &= uint8_t(~value);
        paramIdx_ = 0;
        updateDat1Byte();                       // the direction bit moved
        break;
    case 7:                                     // mode set
        mode_ |= value;
        updateDat1Byte();
        break;
    }
    if (mode_ & 0x01) fifoClear();
    if ((mode_ ^ previousMode) & 0x86) updateDevsel();

    // ACTION edges (swim1.cpp:355-383): write = mode bits 4+3, read = bit 3.
    if ((mode_ & 0x18) == 0x18 && (previousMode & 0x18) != 0x18) {
        startWrite();
    } else if ((previousMode & 0x18) == 0x18 && (mode_ & 0x18) != 0x18) {
        finishWrite();
        currentBit_ = -1;
        halfWait_ = 0;
    }
    if ((mode_ & 0x18) == 0x08 && (previousMode & 0x18) != 0x08) {
        currentBit_ = 0;
        sr_ = 0;
        mfmSyncCounter_ = 0;
        cellPhase_ = 0;
        if (SonyDrive* d = selectedDrive()) d->syncCellsToRotation(side1());
    } else if ((previousMode & 0x18) == 0x08 && (mode_ & 0x18) != 0x08) {
        currentBit_ = -1;
        cellPhase_ = 0;
    }

    if (!(mode_ & 0x40)) leaveIsm();             // swim1.cpp:322 "switch to iwm"
}

// setup[3:2] → fclk cycles per raw cell, same clocking table as SWIM2
// (setup.2 = GCR, setup.3 = fclk/2). SonyDrive builds its cell track at
// the same MFM-16 / GCR-31 rates in the LC II's 15.6672 MHz domain.
int Swim1::cellCycles() const {
    static constexpr int kCyclesPerCell[4] = { 16, 31, 31, 63 };
    return kCyclesPerCell[(setup_ >> 2) & 3];
}

void Swim1::startWrite() {
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

// Same per-gap reconstruction as Swim2::finishWrite — the write spacing
// comes from the param RAM (P_TIME0/1), the read cells from cellCycles();
// per-gap rounding keeps them from drifting apart.
void Swim1::finishWrite() {
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
    const int64_t totalCells =
        prevCell + std::max<int64_t>(1, int64_t((writeHalfPos_ - prevHalf + div / 2) / div));
    d->commitCells(writeStartCell_, totalCells, cellsAt, !(setup_ & 0x40));
    writeTransitions_.clear();
}

void Swim1::tick(int cycles) {
    if (!ismMode_) {
        iwm_.tick(cycles);
        return;
    }
    if (!(mode_ & 0x08)) return;                 // ACTION off
    if (mode_ & 0x10) tickWrite(cycles);
    else              tickRead(cycles);
}

// Read engine: the SWIM2/ideal-cell shifter (see Swim1.h — SWIM1's real
// LS-pair CSM discriminates flux jitter our discrete cells don't have).
void Swim1::tickRead(int cycles) {
    const int cell = cellCycles();
    SonyDrive* d = selectedDrive();
    cellPhase_ += cycles;
    while (cellPhase_ >= cell) {
        cellPhase_ -= cell;
        const int bit = (d && d->hasDisk() && d->motorOn())
                            ? d->nextCell(side1()) : 0;
        if (setup_ & 0x04) {
            // GCR: high bit frames the nibble
            sr_ = uint16_t(((sr_ << 1) | bit) & 0xFF);
            if (sr_ & 0x80) {
                if (fifoPush(sr_) && !error_) error_ |= 0x01;
                sr_ = 0;
            }
        } else {
            // MFM: hunt >=64 alternating cells, then 16-cell windows;
            // odd cells are data, a 0001 raw pattern on an even cell is
            // a missing clock -> MARK. (swim2.cpp:499-546 shape.)
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

// Write engine — swim1.cpp:888-965: the TSS turns data bits into
// transition spacings taken from the param RAM (P_TIME1 after a flux,
// P_TIME0 for an empty cell, both +2*2 halves, doubled by setup.3),
// marks drop the clock via the 0xC entry.
void Swim1::tickWrite(int cycles) {
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
                halfWait_ = uint32_t(params_[P_TIME1]) + 2 * 2;
            } else
                halfWait_ = uint32_t(params_[P_TIME0]) + 2 * 2;
            if (setup_ & 8) halfWait_ <<= 1;
            continue;
        }

        if (currentBit_ < 0) break;              // sequence break (MAME fatal)

        if (currentBit_ == 0) {
            if (sr_ & CRC)
                sr_ = uint16_t(crc_ >> 8);       // 2nd CRC byte
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
            tssOutput_ = uint8_t(4 | bit);       // GCR: one cell per bit
        else {
            static constexpr uint8_t kTss[4] = { 5, 0xD, 4, 5 };
            if ((sr_ & MARK) && ((tssSr_ & 0xF) == 8)) tssOutput_ = 0xC;
            else tssOutput_ = kTss[tssSr_ & 3];
        }
    }
}
