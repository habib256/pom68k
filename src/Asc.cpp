// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "Asc.h"
#include <cstring>

void AscV8::classicResetFifos() {
    rd_ = wr_ = rdB_ = wrB_ = 0;
    cap_ = capB_ = 0;
    emptyCycleSamples_ = 0;
}

void AscV8::reset() {
    classicResetFifos();
    for (auto& r : regs_) r = 0;
    std::memset(fifo_, 0, sizeof fifo_);
    std::memset(fifoB_, 0, sizeof fifoB_);
    // Classic ASC idle status is $00 (ASCTester on IIci); V8 starts empty.
    fifoStat_ = classic() ? 0 : uint8_t(STAT_EMPTY_OR_FULL_A);
    drainAcc_ = 0;
    outRd_ = outWr_ = 0;
    setIrq(false);
}

// asc_v8_device::read (asc.cpp:845-882) + classic asc_base_device::read
uint8_t AscV8::read(uint32_t offset) {
    offset &= 0xFFF;
    uint8_t v = readReg(offset);
    if (onRead) onRead(offset, v);           // diagnostic tap (off by default)
    return v;
}

uint8_t AscV8::readReg(uint32_t offset) {
    // Classic ASC (Mac II): FIFO windows are memory-mapped (MAME asc_base
    // read). V8 returns 0 for FIFO space — ROM POST never peeks them.
    if (offset < 0x400) {
        if (!classic()) return 0;
        return fifo_[offset & 0x3FF];
    }
    if (offset < 0x800) {
        if (!classic()) return 0;
        return fifoB_[(offset - 0x400) & 0x3FF];
    }

    switch (offset - 0x800) {
    case 0x00: return version_;              // version (get_version)
    case 0x01:                               // mode
        return classic() ? regs_[0x01] : 1;
    case 0x02:                               // control
        return classic() ? regs_[0x02] : 1;
    case 0x03:                               // fifo mode
        return classic() ? regs_[0x03] : 1;
    case 0x05:                               // wavetable control
    case 0x07:                               // clock
    case 0x08:                               // "batman" control
        return classic() ? regs_[offset - 0x800] : 0;
    case 0x04: {                             // FIFO status
        uint8_t rv = fifoStat_;
        if (classic()) {
            // Original ASC: reading clears all status bits and the IRQ
            // (MAME asc_base_device::read R_FIFOSTAT). V8 is level-sticky.
            fifoStat_ = 0;
            setIrq(false);
        } else if (!(fifoStat_ & STAT_HALF_A)) {
            setIrq(false);
        }
        return rv;
    }
    default:
        if (offset - 0x800 < 0x20) return regs_[offset - 0x800];
        return 0;
    }
}

// asc_v8_device::write (asc.cpp:884-903) + classic asc_device::write
void AscV8::write(uint32_t offset, uint8_t v) {
    offset &= 0xFFF;
    if (onWrite) onWrite(offset, v);                 // diagnostic tap (off by default)

    if (offset >= 0x400 && offset < 0x800) {
        // FIFO B: V8 mono ignores; classic ASC is stereo / wavetable RAM.
        if (!classic()) return;
        if ((regs_[0x01] & 3) == 1) {
            fifoB_[wrB_ & 0x3FF] = v;
            wrB_ = (wrB_ + 1) & 0x3FF;
            if (capB_ < 0x400) capB_++;
            if (capB_ >= 0x200) {
                fifoStat_ &= uint8_t(~STAT_HALF_B);
                if (capB_ >= 0x3FF) {
                    fifoStat_ |= STAT_EMPTY_OR_FULL_B;
                    setIrq(true);
                }
            } else if (capB_ > 0) {
                fifoStat_ &= uint8_t(~STAT_EMPTY_OR_FULL_B);
            }
        } else {
            fifoB_[(offset - 0x400) & 0x3FF] = v;
        }
        return;
    }

    if (offset < 0x400) {                    // FIFO A
        if (classic() && (regs_[0x01] & 3) != 1) {
            // Wavetable / idle: addressed poke into FIFO RAM (MAME/QEMU).
            fifo_[offset & 0x3FF] = v;
            return;
        }
        if (cap_ < 0x400) {
            fifo_[wr_] = v;
            wr_ = (wr_ + 1) & 0x3FF;
            cap_++;
        }
        if (cap_ >= 0x200) {
            fifoStat_ &= uint8_t(~STAT_HALF_A);
            if (cap_ >= 0x3FF) {
                fifoStat_ |= STAT_EMPTY_OR_FULL_A;
                if (classic()) setIrq(true);
            }
        } else if (cap_ > 0) {
            fifoStat_ &= uint8_t(~STAT_EMPTY_OR_FULL_A);
        }
        if (!classic() && fifoStat_ == 0) setIrq(false);   // V8 quirk
        emptyCycleSamples_ = 0;
        return;
    }

    const uint32_t reg = offset - 0x800;
    switch (reg) {
    case 0x01: {                             // MODE
        if (!classic()) return;              // V8: read-only / forced FIFO
        v &= 3;                              // only bits 0–1 writable (MAME)
        if (v != regs_[0x01]) {
            classicResetFifos();
            setIrq(false);
            if (v == 0) fifoStat_ = 0;       // idle chip: ASCTester 804Idle=$00
        }
        regs_[0x01] = v;
        return;
    }
    case 0x02:                               // CONTROL
        if (!classic()) return;
        regs_[0x02] = v;
        return;
    case 0x03:                               // FIFO MODE
        if (!classic()) return;
        if (v & 0x80) {
            classicResetFifos();
            fifoStat_ |= STAT_EMPTY_OR_FULL_A | STAT_EMPTY_OR_FULL_B;
            setIrq(false);
        }
        regs_[0x03] = v;
        return;
    case 0x05: case 0x07: case 0x08:
        if (!classic()) return;
        if (reg < 0x20) regs_[reg] = v;
        return;
    default:
        if (offset == 0xE00) {               // test hook: force status + IRQ
            fifoStat_ |= 0x0F;
            setIrq(true);
            return;
        }
        if (reg < 0x20) regs_[reg] = v;
        return;
    }
}

// sound_stream_update (asc.cpp): one sample every 704 CPU cycles
// (22 257 Hz nominal — Bresenham keeps the exact ratio)
void AscV8::tick(int cpuCycles) {
    drainAcc_ += int64_t(cpuCycles) * kSampleRate;
    while (drainAcc_ >= kCpuHz) {
        drainAcc_ -= kCpuHz;

        if (classic()) {
            const uint8_t mode = regs_[0x01] & 3;
            if (mode != 1) {
                // Off or wavetable: no FIFO half-empty IRQ. Wavetable audio
                // is still a functional stub (silence out) — Sys7 uses FIFO.
                if (((outWr_ - outRd_) & (kOutSize - 1)) < kOutSize - 1)
                    out_[outWr_++ & (kOutSize - 1)] = 0;
                continue;
            }

            const int capA = cap_;
            const int capB = capB_;
            const bool stereo = (regs_[0x02] & 1) != 0;

            const int8_t smplL = int8_t(fifo_[rd_] ^ 0x80);
            const int8_t smplR = stereo ? int8_t(fifoB_[rdB_] ^ 0x80) : smplL;
            if (cap_) {
                rd_ = (rd_ + 1) & 0x3FF;
                cap_--;
            }
            if (stereo && capB_) {
                rdB_ = (rdB_ + 1) & 0x3FF;
                capB_--;
            }

            // Edge at half-crossing (MAME: pre-decrement cap == $1FF).
            if (capA == 0x1FF) {
                fifoStat_ |= STAT_HALF_A;
                fifoStat_ &= uint8_t(~STAT_EMPTY_OR_FULL_A);
                setIrq(true);
            }
            if (stereo && capB == 0x1FF) {
                fifoStat_ |= STAT_HALF_B;
                fifoStat_ &= uint8_t(~STAT_EMPTY_OR_FULL_B);
                setIrq(true);
            }
            if (cap_ == 0) fifoStat_ |= STAT_EMPTY_OR_FULL_A;
            if (!stereo || capB_ == 0) {
                if (stereo) fifoStat_ |= STAT_EMPTY_OR_FULL_B;
            }

            // QEMU empty-cycle: FIFO mode left running with no data still
            // needs a periodic IRQ or Sound Manager freezes post-Welcome.
            if (cap_ == 0 && (!stereo || capB_ == 0)) {
                if (++emptyCycleSamples_ >= 0x400) {
                    emptyCycleSamples_ = 0;
                    fifoStat_ |= STAT_HALF_A | STAT_EMPTY_OR_FULL_A;
                    if (stereo)
                        fifoStat_ |= STAT_HALF_B | STAT_EMPTY_OR_FULL_B;
                    setIrq(true);
                }
            } else {
                emptyCycleSamples_ = 0;
            }

            if (((outWr_ - outRd_) & (kOutSize - 1)) < kOutSize - 1)
                out_[outWr_++ & (kOutSize - 1)] = int16_t(int(smplL + smplR) << 7);
            continue;
        }

        // ── V8 (LC II): level half-empty IRQ ──────────────────────────
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

// ── IOSB / PrimeTime ASC ($BB) ─────────────────────────────────────────

void AscIosb::clearFifos() {
    rd_[0] = rd_[1] = wr_[0] = wr_[1] = 0;
    cap_[0] = cap_[1] = 0;
}

void AscIosb::reset() {
    clearFifos();
    for (auto& channel : fifo_)
        for (uint8_t& sample : channel) sample = 0;
    mode_ = 1;
    fifoStat_ = 0x0E;                 // ASCTester LC 475 idle value
    playRec_ = 0;
    fifoIrqEn_[0] = fifoIrqEn_[1] = 1;
    lastL_ = lastR_ = 0;
    drainAcc_ = 0;
    outRd_ = outWr_ = 0;
    setIrq(false);
}

uint8_t AscIosb::read(uint32_t offset) {
    offset &= 0xFFF;
    uint8_t v = 0;
    if (offset >= 0x800) {
        switch (offset) {
        case 0x800: v = 0xBB; break;
        case 0x801: v = mode_; break;
        case 0x802: case 0x803: case 0x805: case 0x807: case 0x808:
            v = 0; break;
        case 0x804:
            v = fifoStat_;
            if (!(fifoStat_ & STAT_HALF_B)) setIrq(false);
            break;
        case 0x809: v = fifoIrqEn_[0]; break;
        case 0x80A: v = playRec_; break;
        case 0x829: v = fifoIrqEn_[1]; break;
        case 0xF0E: case 0xF2E: v = 0x2C; break;
        default: v = 0; break;
        }
    }
    if (onRead) onRead(offset, v);
    return v;
}

void AscIosb::push(int channel, uint8_t v) {
    if (cap_[channel] < 0x400) {
        fifo_[channel][wr_[channel]] = v;
        wr_[channel] = (wr_[channel] + 1) & 0x3FF;
        cap_[channel]++;
    }

    uint8_t half = channel ? STAT_HALF_B : STAT_HALF_A;
    uint8_t edge = channel ? STAT_EMPTY_OR_FULL_B : STAT_EMPTY_OR_FULL_A;
    if (cap_[channel] >= 0x200) {
        fifoStat_ &= uint8_t(~half);
        if (cap_[channel] >= 0x3FF) fifoStat_ |= edge;
    } else if (cap_[channel] > 0) {
        fifoStat_ &= uint8_t(~edge);
    }
    // IOSB playback mode reports FIFO A empty even while software feeds it.
    if (channel == 0 && !(playRec_ & 1)) fifoStat_ |= STAT_EMPTY_OR_FULL_A;
}

void AscIosb::write(uint32_t offset, uint8_t v) {
    offset &= 0xFFF;
    if (onWrite) onWrite(offset, v);

    if (offset < 0x400) { push(0, v); return; }
    if (offset < 0x800) { push(1, v); return; }

    switch (offset) {
    case 0x801:
        v &= 1;
        if (v != mode_) clearFifos();
        mode_ = v;
        fifoStat_ |= STAT_EMPTY_OR_FULL_B;
        return;
    case 0x802: case 0x805: case 0x807: case 0x808:
        return;                                 // read-only / no-op on IOSB
    case 0x803:
        if (v & 0x80) {
            clearFifos();
            fifoStat_ |= STAT_EMPTY_OR_FULL_A | STAT_EMPTY_OR_FULL_B;
        }
        return;
    case 0x809:
        if (!(v & 1) && (fifoIrqEn_[0] & 1) && (fifoStat_ & STAT_HALF_A))
            setIrq(true);
        fifoIrqEn_[0] = v & 1;
        return;
    case 0x80A:
        playRec_ = v;
        return;
    case 0x829:
        if (!(v & 1) && (fifoIrqEn_[1] & 1) && (fifoStat_ & STAT_HALF_B))
            setIrq(true);
        fifoIrqEn_[1] = v & 1;
        return;
    case 0xE00:
        fifoStat_ |= 0x0F;
        setIrq(true);
        return;
    default:
        return;
    }
}

void AscIosb::tick(int cpuCycles) {
    drainAcc_ += int64_t(cpuCycles) * kSampleRate;
    while (drainAcc_ >= kCpuHz) {
        drainAcc_ -= kCpuHz;

        if (mode_ == 0) {
            fifoStat_ |= STAT_HALF_B;
        } else {
            if (!(playRec_ & 1)) {
                fifoStat_ |= STAT_EMPTY_OR_FULL_A;
                if (!(fifoIrqEn_[0] & 1)) setIrq(true);
            }

            lastL_ = int8_t(fifo_[0][rd_[0]] ^ 0x80);
            lastR_ = int8_t(fifo_[1][rd_[1]] ^ 0x80);
            for (int ch = 0; ch < 2; ch++) {
                if (cap_[ch]) {
                    rd_[ch] = (rd_[ch] + 1) & 0x3FF;
                    cap_[ch]--;
                }
            }

            if (cap_[0] < 0x200 || cap_[1] < 0x200) {
                fifoStat_ |= STAT_HALF_B;
                if (!(fifoIrqEn_[1] & 1)) setIrq(true);
            } else {
                fifoStat_ &= uint8_t(~STAT_HALF_B);
            }
            if (cap_[0] == 0 || cap_[1] == 0)
                fifoStat_ |= STAT_EMPTY_OR_FULL_B;
            else
                fifoStat_ &= uint8_t(~STAT_EMPTY_OR_FULL_B);
        }

        if (((outWr_ - outRd_) & (kOutSize - 1)) < kOutSize - 1) {
            uint32_t i = outWr_++ & (kOutSize - 1);
            outL_[i] = int16_t(int(lastL_) * 256);
            outR_[i] = int16_t(int(lastR_) * 256);
        }
    }
}
