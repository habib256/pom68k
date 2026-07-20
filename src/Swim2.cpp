// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "Swim2.h"
#include "SonyDrive.h"

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
    // IWM-compatible CA packing used by SonyDrive (DEV.md § Sony):
    // (CA2<<3)|(CA1<<2)|(CA0<<1)|SEL — SEL from HDSEL (mode bit 5).
    return ((phases_ & 4) ? 8 : 0) | ((phases_ & 2) ? 4 : 0)
         | ((phases_ & 1) ? 2 : 0) | (side1() ? 1 : 0);
}

void Swim2::updateDevsel() {
    // soft-select mirror (swim2.cpp:300-301); remember A/B for "both"
    const int sel = (mode_ >> 1) & 3;
    if (sel == 1) driveSel_ = 0;
    else if (sel == 2) driveSel_ = 1;
}

void Swim2::applyPhases(uint8_t value) {
    // applefdintf phases → mac_floppy seek_phase_w: bits 0-2 = CA0-2,
    // bit 3 = LSTRB. Rising LSTRB strobes the command (Iwm.cpp:32-36).
    const bool lstrb = (value & 0x08) != 0;
    const bool rising = lstrb && !lstrb_;
    phases_ = value;
    lstrb_ = lstrb;
    if (rising) {
        if (SonyDrive* d = selectedDrive()) d->command(senseAddr());
    }
}

void Swim2::fifoClear() {
    fifoPos_ = 0;
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
            // CRC0 is clear in this register-only / byte-stream slice.
            value |= 0x02;
        }
        // Sense multiplex on the classic "WPRT" line (mac_floppy::wpt_r).
        SonyDrive* d = selectedDrive();
        bool senseHigh = !d || d->sense(senseAddr());
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
    // Motor bit mirrors onto the selected drive (IWM ENABLE equivalent).
    if ((mode_ ^ previousMode) & 0x80) {
        updateDevsel();
        if (SonyDrive* d = selectedDrive()) d->setMotor((mode_ & 0x80) != 0);
        else {
            if (drive_[0]) drive_[0]->setMotor(false);
            if (drive_[1]) drive_[1]->setMotor(false);
        }
    }
    if ((mode_ & 0x18) != (previousMode & 0x18)) cellPhase_ = 0;
}

void Swim2::tick(int controllerCycles) {
    if (!(mode_ & 0x08)) return;               // ACTION off

    static constexpr int bitCycles[4] = { 16, 31, 31, 63 };
    int byteCycles = bitCycles[(setup_ >> 2) & 3] * 8;
    cellPhase_ += controllerCycles;
    while (cellPhase_ >= byteCycles) {
        cellPhase_ -= byteCycles;
        SonyDrive* d = selectedDrive();
        if (mode_ & 0x10) {                    // write: serialize queued bytes
            if (fifoPos_) {
                uint16_t v = fifoPop();
                if (d && d->hasDisk()) d->writeByte(v);
            } else {
                error_ |= 0x01;                // underrun ends ACTION
                mode_ &= uint8_t(~0x08);
                break;
            }
        } else {
            if (fifoPos_ == 2) break;
            if (d && d->hasDisk()) {
                // GCR or MFM byte stream from the drive (MARK bits for A1).
                fifoPush(d->nextByte(side1()));
            } else {
                // No media: PLL idles high → $FF (MAME swim2 read amp).
                fifoPush(0xFF);
            }
        }
    }
}
