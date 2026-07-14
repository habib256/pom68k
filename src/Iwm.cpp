// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Register model ported from MAME iwm.cpp, cross-checked against pce iwm.c
// and Snow iwm.rs — see DEV.md § IWM.

#include "Iwm.h"
#include "SonyDrive.h"

void Iwm::reset() {
    for (bool& p : ph_) p = false;
    enable_ = driveSel_ = q6_ = q7_ = sel_ = false;
    mode_ = 0;
    dataReg_ = 0;
    cellPhase_ = 0;
}

// Sense/command address presented to the drive: CA2 CA1 CA0 = ph2 ph1 ph0,
// SEL from VIA PA5.
int Iwm::senseAddr() const {
    return (ph_[2] ? 8 : 0) | (ph_[1] ? 4 : 0) | (ph_[0] ? 2 : 0) | (sel_ ? 1 : 0);
}

// Every access (read or write) toggles one of the 8 state lines:
// reg = line*2 + (A9: 0 = clear, 1 = set).
uint8_t Iwm::access(int reg) {
    bool set = reg & 1;
    switch (reg >> 1) {
        case 0: case 1: case 2:
            ph_[reg >> 1] = set;
            break;
        case 3: {                                 // ph3 = LSTRB
            bool rising = set && !ph_[3];
            ph_[3] = set;
            if (rising && selectedDrive()) selectedDrive()->command(senseAddr());
            break;
        }
        case 4: enable_ = set; break;             // drive ENABLE
        case 5: driveSel_ = set; break;           // 0 = internal, 1 = external
        case 6: q6_ = set; break;
        case 7: q7_ = set; break;
    }
    return readRegister();
}

uint8_t Iwm::read(int reg) {
    readCount[reg]++;
    uint8_t v = access(reg);
    if (!q6_ && !q7_) { dataReads++; if (v & 0x80) dataHits++; }
    return v;
}

void Iwm::write(int reg, uint8_t v) {
    access(reg);
    if (q6_ && q7_) {
        if (!enable_) mode_ = v & 0x1F;           // mode register ($1F on Mac)
        // enabled: write-data register — write support is M5.1
    }
}

uint8_t Iwm::readRegister() {
    if (!q6_ && !q7_) {                           // DATA register
        if (!enable_) return 0xFF;
        uint8_t v = dataReg_;
        // Real IWM latched mode: the register clears ~14 IWM clocks AFTER a
        // read (MAME m_last_sync + 14). The ROM relies on it: `tst.b` polls
        // the MSB, then `move.b` re-reads the same nibble an instant later.
        if ((v & 0x80) && clearCountdown_ == 0) {
            clearCountdown_ = 14;
            consumed[consumedPos] = v; consumedPos = (consumedPos + 1) & 511;
        }
        return v;
    }
    if (q6_ && !q7_) {                            // STATUS register
        const_cast<Iwm*>(this)->senseCount[senseAddr()]++;
        bool sense = selectedDrive() ? selectedDrive()->sense(senseAddr()) : true;
        return uint8_t((sense ? 0x80 : 0x00) | (enable_ ? 0x20 : 0x00) | (mode_ & 0x1F));
    }
    if (!q6_ && q7_) return 0xC0;                 // write HANDSHAKE: ready, no underrun
    return 0xFF;                                  // (Q6,Q7) = (1,1)
}

// Nibble pacing: mode $1F = 2 µs bit cells → one GCR byte every 16 µs
// ≈ 128 CPU cycles at 7.8336 MHz.
void Iwm::tick(int cpuCycles) {
    if (clearCountdown_ > 0) {
        clearCountdown_ -= cpuCycles;
        if (clearCountdown_ <= 0) { clearCountdown_ = 0; dataReg_ = 0; }
    }
    if (!enable_ || !selectedDrive() || !selectedDrive()->hasDisk()) return;
    cellPhase_ += cpuCycles;
    constexpr int kCyclesPerNibble = 128;
    while (cellPhase_ >= kCyclesPerNibble) {
        cellPhase_ -= kCyclesPerNibble;
        if (dataReg_ & 0x80) overwritten++;
        dataReg_ = selectedDrive()->nextNibble(sel_);
        clearCountdown_ = 0;
    }
}
