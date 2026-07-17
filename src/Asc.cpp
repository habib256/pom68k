// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "Asc.h"

void AscV8::reset() {
    rd_ = wr_ = 0;
    cap_ = 0;
    for (auto& r : regs_) r = 0;
    fifoStat_ = STAT_EMPTY_OR_FULL_A;        // FIFO A empty (asc.cpp:755-761)
    drainAcc_ = 0;
    outRd_ = outWr_ = 0;
    setIrq(false);
}

// asc_v8_device::read (asc.cpp:845-882)
uint8_t AscV8::read(uint32_t offset) {
    offset &= 0xFFF;
    uint8_t v = readReg(offset);
    if (onRead) onRead(offset, v);           // diagnostic tap (off by default)
    return v;
}

uint8_t AscV8::readReg(uint32_t offset) {
    if (offset < 0x800) return 0;            // FIFO space reads nothing useful

    switch (offset - 0x800) {
    case 0x00: return 0xE8;                  // version (get_version)
    case 0x01:                               // mode: forced 1 (FIFO)
    case 0x02:                               // control
    case 0x03:                               // fifo mode
        return 1;
    case 0x05:                               // wavetable control
    case 0x07:                               // clock
    case 0x08:                               // "batman" control
        return 0;
    case 0x04:                               // FIFO status
        // reading clears the IRQ, but only when no longer half-empty —
        // the LEVEL behaviour System 7 depends on (asc.cpp:858-866)
        if (!(fifoStat_ & STAT_HALF_A)) setIrq(false);
        return fifoStat_;
    default:
        if (offset - 0x800 < 0x20) return regs_[offset - 0x800];
        return 0;
    }
}

// asc_v8_device::write (asc.cpp:884-903) + base FIFO A path (:369-431)
void AscV8::write(uint32_t offset, uint8_t v) {
    offset &= 0xFFF;
    if (onWrite) onWrite(offset, v);                 // diagnostic tap (off by default)

    if (offset >= 0x400 && offset < 0x800) return;   // FIFO B: mono, ignored

    if (offset < 0x400) {                    // FIFO A push
        if (cap_ < 0x400) {                  // drop writes past FULL (don't
            fifo_[wr_] = v;                  // overwrite the oldest unread byte
            wr_ = (wr_ + 1) & 0x3FF;         // + stall wr_/cap_, like MAME)
            cap_++;
        }
        if (cap_ >= 0x200) {
            fifoStat_ &= uint8_t(~STAT_HALF_A);
            if (cap_ >= 0x3FF) fifoStat_ |= STAT_EMPTY_OR_FULL_A;
        } else if (cap_ > 0) {
            fifoStat_ &= uint8_t(~STAT_EMPTY_OR_FULL_A);
        }
        if (fifoStat_ == 0) setIrq(false);   // V8 quirk (asc.cpp:899-902)
        return;
    }

    switch (offset - 0x800) {
    case 0x01: case 0x02: case 0x05: case 0x07: case 0x08:
        return;                              // read-only / no-op on V8
    default:
        if (offset == 0xE00) {               // test hook: force status + IRQ
            fifoStat_ |= 0x0F;
            setIrq(true);
            return;
        }
        if (offset - 0x800 < 0x20) regs_[offset - 0x800] = v;
        return;
    }
}

// sound_stream_update (asc.cpp:806-843): one sample every 704 CPU cycles
// (22 257 Hz nominal — Bresenham keeps the exact ratio)
void AscV8::tick(int cpuCycles) {
    drainAcc_ += int64_t(cpuCycles) * kSampleRate;
    while (drainAcc_ >= kCpuHz) {
        drainAcc_ -= kCpuHz;

        const int8_t smpl = int8_t(fifo_[rd_] ^ 0x80);
        if (cap_) {
            rd_ = (rd_ + 1) & 0x3FF;
            cap_--;
        }
        if (((outWr_ - outRd_) & (kOutSize - 1)) < kOutSize - 1)
            out_[outWr_++ & (kOutSize - 1)] = int16_t(smpl << 8);

        if (cap_ < 0x200) {
            fifoStat_ |= STAT_HALF_A;
            setIrq(true);
        } else {
            fifoStat_ &= uint8_t(~STAT_HALF_A);
        }
        if (cap_ == 0) fifoStat_ |= STAT_EMPTY_OR_FULL_A;
        else           fifoStat_ &= uint8_t(~STAT_EMPTY_OR_FULL_A);
    }
}
