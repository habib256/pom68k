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
    writing_ = wrPending_ = wrUnderrun_ = false;
    wrPhase_ = 0;
    selDelay_ = 0;
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
            // Commands only reach a SELECTED drive (MAME iwm.cpp:243-247:
            // devsel drops to "none" when the controller is idle).
            if (rising && driveSelected() && selectedDrive())
                selectedDrive()->command(senseAddr());
            break;
        }
        case 4: {                                 // drive ENABLE
            const bool was = enable_;
            enable_ = set;
            // MAME iwm.cpp:224-239: dropping ENABLE with mode bit 2 set
            // ($1F on every Mac) deselects immediately; with the motor-off
            // timer enabled (bit 2 clear, the reset default) the selection
            // lingers MODE_DELAY = cycles_to_time(8388608) ≈ 1 s.
            if (set) selDelay_ = 0;
            else if (was && !(mode_ & 0x04)) selDelay_ = 8388608LL * clockScale_;
            break;
        }
        case 5: driveSel_ = set; break;           // 0 = internal, 1 = external
        case 6: q6_ = set; break;
        case 7: q7_ = set; break;
    }
    updateRw();
    return readRegister();
}

// Read/write mode tracking (MAME iwm.cpp control(), :198-221): q7 while
// enabled enters write mode — the access that sets q7 can carry the first
// data byte; clearing q7 (or ENABLE) leaves it and flushes the write-back.
void Iwm::updateRw() {
    const bool wantWrite = enable_ && q7_;
    if (wantWrite && !writing_) {
        writing_ = true;
        wrUnderrun_ = false;
        wrPending_ = false;
        wrPhase_ = 7;                             // first load: S_IDLE + 7
    } else if (!wantWrite && writing_) {
        writing_ = false;
        wrPending_ = false;
        if (selectedDrive()) selectedDrive()->flushWrite(sel_);
    }
}

uint8_t Iwm::read(int reg) {
    readCount[reg]++;
    uint8_t v = access(reg);
    if (!q6_ && !q7_) { dataReads++; if (v & 0x80) dataHits++; }
    return v;
}

void Iwm::write(int reg, uint8_t v) {
    access(reg);
    // MAME iwm.cpp control(): mode/data writes reach the chip only through
    // the odd (line-set) addresses — an even access just toggles its line.
    if ((reg & 1) && q6_ && q7_) {
        if (!enable_) mode_ = v & 0x1F;           // mode register ($1F on Mac)
        else if (writing_) {                      // write-data register
            wrData_ = v;
            wrPending_ = true;                    // latched: handshake b7 low
        }
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
            // 14 ticks of the IWM'S OWN clock (MAME `m_last_sync + 14`,
            // iwm.cpp:285, where last_sync counts clock() ticks) — so the
            // hold is ~1.79 us of wall time whatever the host. It must be
            // scaled onto the tick unit exactly like kCyclesPerNibble is,
            // or a C15M host holds the byte for half as long as silicon.
            // KNOWN MAME DIVERGENCE, deliberately pinned (parity audit
            // #24): MAME re-arms the clear on EVERY access (iwm.cpp:284-285
            // runs in control(); expiry applied lazily at :461-465), so the
            // byte dies 14 ticks after the LAST access, not the first read.
            // Re-arming here would move the clear later and shrink the
            // anti-duplicate margin of Apple's denibble loop (tst.b poll +
            // move.b consume) by the tst->move gap — the exact badDCksum
            // failure mode fixed 2026-08-05. Do not align without first
            // measuring reReads under lcii_sony_trace at boost 1.
            clearCountdown_ = 14 * clockScale_;
            consumed[consumedPos] = v; consumedPos = (consumedPos + 1) & 511;
        } else if (v & 0x80) {
            // Re-read of a byte already latched: on silicon the clear runs
            // in continuous time, so a faster CPU simply sees the same
            // valid byte for the rest of the 1.79 us. Counted because our
            // clear is quantized to tick() — a driver that takes each
            // MSB-set read as a NEW nibble would decode duplicates, and
            // the count is how a boosted CPU could change decode outcomes.
            reReads++;
        }
        return v;
    }
    if (q6_ && !q7_) {                            // STATUS register
        const_cast<Iwm*>(this)->senseCount[senseAddr()]++;
        // Deselected drive = pull-up on the sense line (MAME iwm.cpp:129
        // reads high when m_floppy is null, i.e. devsel none).
        bool sense = (driveSelected() && selectedDrive())
                         ? selectedDrive()->sense(senseAddr()) : true;
        return uint8_t((sense ? 0x80 : 0x00) | (enable_ ? 0x20 : 0x00) | (mode_ & 0x1F));
    }
    if (!q6_ && q7_) {                            // write HANDSHAKE (m_whd)
        // b7 = register empty (ready for the next byte), b6 = write mode
        // healthy; low bits ride high like MAME's 0xBF reset value.
        return uint8_t((wrPending_ ? 0x00 : 0x80) |
                       (writing_ && !wrUnderrun_ ? 0x40 : 0x00) | 0x3F);
    }
    return 0xFF;                                  // (Q6,Q7) = (1,1)
}

// Nibble pacing: mode $1F = 2 µs bit cells → one GCR byte every 16 µs
// ≈ 128 C7M clocks — clockScale_ maps that onto the platform's tick unit.
void Iwm::tick(int cpuCycles) {
    if (clearCountdown_ > 0) {
        clearCountdown_ -= cpuCycles;
        if (clearCountdown_ <= 0) { clearCountdown_ = 0; dataReg_ = 0; }
    }
    if (selDelay_ > 0) {                          // MODE_DELAY deselect timer
        selDelay_ -= cpuCycles;
        if (selDelay_ < 0) selDelay_ = 0;
    }
    if (!enable_ || !selectedDrive() || !selectedDrive()->hasDisk()) return;
    const int kCyclesPerNibble = 128 * clockScale_;
    if (writing_) {
        // Write shifter (MAME MODE_WRITE): consume the pending byte every
        // 8 bit windows; loading with nothing pending is an underrun that
        // halts the engine (SW_UNDERRUN) and flushes what was written.
        if (wrUnderrun_) return;
        wrPhase_ -= cpuCycles;
        while (wrPhase_ <= 0) {
            if (!wrPending_) {
                wrUnderrun_ = true;
                selectedDrive()->flushWrite(sel_);
                wrPhase_ = 0;
                break;
            }
            selectedDrive()->writeNibble(wrData_);
            written++;
            wrPending_ = false;
            wrPhase_ += kCyclesPerNibble;
        }
        return;
    }
    // MAME iwm.cpp:398-405: the read shifter advances on flux transitions,
    // and floppy.cpp:1175-1178 get_next_transition() returns `never` while
    // the spindle is stopped (m_mon) — Mac drives gate the motor by command,
    // not by ENABLE (mon_w override, floppy.cpp:3417-3420). No spin, no
    // nibbles. (The write engine above stays ungated: the Sony driver never
    // writes motor-off, and iwm_write_test drives it without a motor.)
    if (!selectedDrive()->motorOn()) { cellPhase_ = 0; return; }
    cellPhase_ += cpuCycles;
    while (cellPhase_ >= kCyclesPerNibble) {
        cellPhase_ -= kCyclesPerNibble;
        if (dataReg_ & 0x80) overwritten++;
        dataReg_ = selectedDrive()->nextNibble(sel_);
        clearCountdown_ = 0;
    }
}
