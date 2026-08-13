// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "Asc.h"
#include <algorithm>
#include <cstring>

// The wavetable register window, $810-$82F (reg offsets $10-$2F). Each
// voice owns two 24-bit big-endian values three bytes apart in a 8-byte
// stride: phase at $x1-$x3, increment at $x5-$x7 (asc.cpp:474-560 on the
// write side, :344-357 on the read side). $810/$814/… are unmapped holes.
bool AscV8::wtRegister(uint32_t reg, int& voice, bool& isIncr, int& shift) {
    if (reg < 0x10 || reg > 0x2F) return false;
    const uint32_t rel = reg - 0x10;         // 0..0x1F
    voice = int(rel >> 3);                   // 8 bytes per voice
    const uint32_t in = rel & 7;             // 0=hole,1..3=phase,4=hole,5..7=incr
    if (in == 0 || in == 4) return false;
    isIncr = in >= 5;
    shift = 8 * (2 - int((in - (isIncr ? 5 : 1))));   // 16, 8, 0
    return true;
}

void AscV8::classicResetFifos() {
    rd_ = wr_ = rdB_ = wrB_ = 0;
    cap_ = capB_ = 0;
    emptyCycleSamples_ = 0;
}

void AscV8::reset() {
    classicResetFifos();
    for (auto& r : regs_) r = 0;
    for (int i = 0; i < 4; i++) wtPhase_[i] = wtIncr_[i] = 0;
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
    default: {
        // Wavetable phase/increment read back from the LIVE oscillators, not
        // from the register file — the phase advances every sample, so a
        // stored copy would be stale the moment playback started. MAME does
        // the same rebuild on any read in the window (asc.cpp:344-357).
        int voice, shift; bool isIncr;
        if (classic() && offset >= 0x800
            && wtRegister(offset - 0x800, voice, isIncr, shift))
            return uint8_t((isIncr ? wtIncr_[voice] : wtPhase_[voice]) >> shift);
        // FIFO IRQ-control registers live at bus offsets $F09/$F29, not
        // $809/$829. Classic reads them as 0: MAME master asc_device::read
        // (asc.cpp:657-667) overrides the base $01 (asc.cpp:335-337) to
        // match the real-IIci ASCTester dump "F09: 0 ($00)  F29: 0 ($00)"
        // (asc.cpp:621) — a recent MAME fix, adopted here (audit #42).
        // PIN — V8 reads 0 too: DELIBERATE divergence from MAME master, whose
        // asc_v8_device::read returns 1 (asc.cpp:867-869) against its own
        // in-file real-LC dump: hardware "F09: 0 ($00)" (asc.cpp:767) vs
        // MAME "F09: 0 ($01)" (asc.cpp:786). Do not "fix" toward MAME.
        if (offset == 0xF09 || offset == 0xF29) return 0;
        // Sparse register file: anything outside the $800-$81F block reads 0
        // where MAME's flat m_regs[0x800] echoes the written byte. Kept —
        // reasoning and reopening condition at `AscV8::regs_` in Asc.h
        // (audit § 2.8(b)).
        if (offset - 0x800 < 0x20) return regs_[offset - 0x800];
        return 0;
    }
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
            // CONTROL bit 1 (CONTROL_STEREO, a BIT() index) gates FIFO B in
            // FIFO mode: in mono the write is dropped entirely — no data, no
            // status edge, no full IRQ (MAME asc_device::write,
            // asc.cpp:704-739; audit #43).
            if (!(regs_[0x02] & 0x02)) return;
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
        // R_PLAYRECA ($80A) bit 0 = record mode freezes the FIFO A status
        // bits on V8: asc_base_device::write (asc.cpp:386-404), reached via
        // asc_v8_device::write (asc.cpp:878-902). The classic override
        // (asc_device::write, asc.cpp:669-703) carries NO such gate — the
        // audit #44 wording "V8/classic" over-reaches; only V8 is gated.
        if (classic() || !(regs_[0x0A] & 1)) {
            if (cap_ >= 0x200) {
                fifoStat_ &= uint8_t(~STAT_HALF_A);
                if (cap_ >= 0x3FF) {
                    fifoStat_ |= STAT_EMPTY_OR_FULL_A;
                    if (classic()) setIrq(true);
                }
            } else if (cap_ > 0) {
                fifoStat_ &= uint8_t(~STAT_EMPTY_OR_FULL_A);
            }
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
        // PIN — $807 CLOCK RATE is HONOURED here (classic ASC only): the
        // stored byte feeds drainHz(), which paces tick(). MAME documents the
        // register (sound/asc.cpp:30 "0 = Mac 22257 Hz, 1 = undefined,
        // 2 = 22050 Hz, 3 = 44100 Hz") and implements NOTHING — its
        // sound_stream_update always runs at the device clock. A parity diff
        // will therefore show POM68K "diverging"; it is the manual that is the
        // oracle for this register, not MAME. Do not drop the store. Rationale
        // and the host-DAC caveat: AscV8::drainHz in Asc.h.
        if (!classic()) return;
        if (reg < 0x20) regs_[reg] = v;
        return;
    default: {
        if (offset == 0xE00) {               // test hook: force status + IRQ
            fifoStat_ |= 0x0F;
            setIrq(true);
            return;
        }
        // Wavetable phase/increment (classic only): one byte of a 24-bit
        // value, straight into the live oscillator. Two of the four voices
        // live past `regs_`, which is exactly why the oscillators are their
        // own state rather than a view of the register file.
        int voice, shift; bool isIncr;
        if (classic() && wtRegister(reg, voice, isIncr, shift)) {
            uint32_t& dst = isIncr ? wtIncr_[voice] : wtPhase_[voice];
            dst = (dst & ~(0xFFu << shift)) | (uint32_t(v) << shift);
            return;
        }
        if (reg < 0x20) regs_[reg] = v;
        return;
    }
    }
}

// sound_stream_update (asc.cpp): one sample every 704 CPU cycles
// (22 257 Hz nominal — Bresenham keeps the exact ratio)
void AscV8::tick(int cpuCycles) {
    drainAcc_ += int64_t(cpuCycles) * drainHz();   // $807, see Asc.h
    while (drainAcc_ >= kCpuHz) {
        drainAcc_ -= kCpuHz;

        if (classic()) {
            const uint8_t mode = regs_[0x01] & 3;
            if (mode == 2) {
                // ── Four-voice wavetable (asc.cpp:248-281) ─────────────
                // Unique to the first-generation ASC. Each voice advances a
                // 24-bit phase by its increment and takes bits 23-15 as a
                // 512-byte table index; voices 0/1 read FIFO A, 2/3 FIFO B,
                // odd voices from the upper half of their RAM. Samples are
                // offset binary (XOR $80 → signed). No FIFO status and no
                // IRQ here: MAME's case 2 raises none, and the Sound
                // Manager drives wavetable playback open-loop.
                static constexpr uint32_t kTableOffset[2] = {0, 0x200};
                int mix = 0;
                for (int ch = 0; ch < 4; ch++) {
                    wtPhase_[ch] = (wtPhase_[ch] + wtIncr_[ch]) & 0xFFFFFF;
                    const uint8_t* table = ch < 2 ? fifo_ : fifoB_;
                    const uint32_t idx = ((wtPhase_[ch] >> 15) & 0x1FF)
                                       + kTableOffset[ch & 1];
                    mix += int(int8_t(table[idx & 0x3FF] ^ 0x80));
                }
                // MAME weights each voice at 64 in 16-bit terms (put_int
                // with a 32768*4 full scale over samples scaled by 256), so
                // four voices at full deflection land exactly at full
                // scale. Clamp rather than wrap: the sum's low bound is
                // -32768 and its high bound +32512, but a future gain
                // change must not silently alias.
                mix *= 64;
                if (mix > 32767) mix = 32767;
                if (mix < -32768) mix = -32768;
                if (((outWr_ - outRd_) & (kOutSize - 1)) < kOutSize - 1)
                    out_[outWr_++ & (kOutSize - 1)] = int16_t(mix);
                continue;
            }
            if (mode != 1) {
                // Chip off (mode 0) or the undefined mode 3: silence, and no
                // FIFO half-empty IRQ. (MAME holds the last sample as a DC
                // level instead of zeroing — an anti-click detail with no
                // guest observable, not adopted.)
                if (((outWr_ - outRd_) & (kOutSize - 1)) < kOutSize - 1)
                    out_[outWr_++ & (kOutSize - 1)] = 0;
                continue;
            }

            const int capA = cap_;
            const int capB = capB_;
            // CONTROL bit 1 = stereo/mono; bit 0 selects analog vs PWM output.
            // MAME's `CONTROL_STEREO = 1` is a BIT() index, not a mask.
            const bool stereo = (regs_[0x02] & 0x02) != 0;

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
    // PIN — hardware reset state, from the real LC 475 ASCTester dump MAME
    // carries in-file (sound/asc.cpp:1130-1136: "804Idle: $0E", "F09: 1 ($01)
    // F29: 1 ($01)"), NOT from a running MAME session: iosb.cpp:89 wires
    // ASC_EASC in place of asc_iosb, so MAME never executes its own $BB reset
    // path. A parity diff against MAME's LC 475 / Q605 will report the EASC's
    // values instead — do not adopt them. See the class comment in Asc.h.
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
            // Reading the status register drops the IRQ latch, ALWAYS; the
            // next 22 257 Hz sample re-raises it while the gated condition
            // still holds. MAME gates the clear on !(stat & HALF_B)
            // (asc.cpp:1263-1269), but tick() sets HALF_B whenever either
            // FIFO sits below half — precisely the state the handler is in
            // when it reads this register — so the gate makes the latch
            // un-clearable, and the pseudo-VIA2 level re-sample
            // (Q605Memory::via2Recalc) turns that into a permanent level-2
            // storm. The real LC 475 clears: MAME's own ASCTester
            // comparison (asc.cpp:1162-1166) annotates its column "still in
            // IFR after clear" as the MAME-side artefact. Same ruling as
            // AscSonora below (the "Bienvenue." freeze).
            setIrq(false);
            v = fifoStat_;
            break;
        // FIFO A/B interrupt control lives at $F09/$F29, not $809/$829:
        // MAME asc.h:52-74 (R_FIFOA_IRQCTRL = 0xf00-0x800 + 9,
        // R_FIFOB_IRQCTRL = 0xf20-0x800 + 9) decoded via `offset - 0x800`
        // in asc_iosb_device::read — the same block as the $F0E/$F2E pair
        // just below. Confirmed by ASCTester on a real LC 475: "F09: 1
        // ($01)  F29: 1 ($01)" (asc.cpp:1135). $809 is R_REG9 and $829 a
        // plain register-file byte; both read back 0.
        case 0xF09: v = fifoIrqEn_[0]; break;
        case 0x80A: v = playRec_; break;
        case 0xF29: v = fifoIrqEn_[1]; break;
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
    case 0xF09:                              // R_FIFOA_IRQCTRL (asc.h:61)
        if (!(v & 1) && (fifoIrqEn_[0] & 1) && (fifoStat_ & STAT_HALF_A))
            setIrq(true);                    // enable while A already pending
        fifoIrqEn_[0] = v & 1;
        // Disabling the last enabled source must drop the line: otherwise
        // Q605Memory::ascLine_ stays true and via2Recalc() re-latches IFR
        // bit 4 forever (MAME asc.cpp:1087-1090 does the same on Sonora).
        if ((fifoIrqEn_[0] & 1) && (fifoIrqEn_[1] & 1)) setIrq(false);
        return;
    case 0x80A:
        playRec_ = v;
        return;
    case 0xF29:                              // R_FIFOB_IRQCTRL (asc.h:74)
        if (!(v & 1) && (fifoIrqEn_[1] & 1) && (fifoStat_ & STAT_HALF_B))
            setIrq(true);
        fifoIrqEn_[1] = v & 1;
        if ((fifoIrqEn_[0] & 1) && (fifoIrqEn_[1] & 1)) setIrq(false);
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

// ── Sonora / Spice ASC ($BC) ───────────────────────────────────────────
// MAME asc_sonora_device (asc.cpp:910-1080, hardware-pinned by ASCTester
// on a real LC III) — see Asc.h for the status-bit folding.

void AscSonora::reset() {
    clearFifos();
    for (auto& channel : fifo_)
        for (uint8_t& sample : channel) sample = 0;
    for (auto& r : regs_) r = 0;
    for (auto& r : xtraRegs_) r = 0;
    regs_[0x01] = 1;                         // mode forced to FIFO
    // PIN — reset FIFOSTAT is $0A, both empty flags, not $02.
    // Both FIFOs are empty at reset, so BOTH empty/full flags are set:
    // MAME sets R_FIFOSTAT |= $0A for the "fifos A&B empty" idle state
    // (asc.cpp:465). Only latching A ($02) hung the all-in-one LC 520 /
    // LC 550, whose boot sound-init spins on $804 bit 3 (EMPTY_OR_FULL_B)
    // — the LC III / Color Classic path only tests FIFO A, so it was
    // unaffected either way.
    fifoStat_ = STAT_EMPTY_OR_FULL_A | STAT_EMPTY_OR_FULL_B;   // $0A
    fifoIrqEn_[0] = fifoIrqEn_[1] = 0;       // 0 = enabled (Sonora reset)
    drainAcc_ = 0;
    outRd_ = outWr_ = 0;
    setIrq(false);
}

uint8_t AscSonora::read(uint32_t offset) {
    offset &= 0xFFF;
    uint8_t v = 0;
    if (offset < 0x400)      v = fifo_[0][offset];
    else if (offset < 0x800) v = fifo_[1][offset - 0x400];
    else switch (offset) {
    case 0x800: v = 0xBC; break;             // get_version
    case 0x801: v = 1; break;                // MODE reads 1
    case 0x802: case 0x803: case 0x805: case 0x807: case 0x808:
        v = 0; break;                        // read-as-0 on Sonora
    case 0x804:
        // PIN — UNCONDITIONAL read-clear; MAME's `!(stat & HALF_B)` gate must
        // NOT be restored here (nor in AscIosb::read above, same ruling).
        // Reading clears the IRQ latch (never the status bits); the next
        // 22 257 Hz sample re-raises it while a gated condition holds.
        // MAME gates this clear on !(stat & HALF_B) (asc.cpp:1050-1055),
        // but the combined half flag is PERMANENTLY set at idle — with
        // the pseudo-VIA IER bit 4 enabled that reading makes the level
        // continuous and the CC/LC III boot lives inside the autovector
        // (RTE → immediate re-entry, TickCount frozen — bring-up
        // 2026-07-24). ASCTester on the real LC III counts ~50 000
        // DISTINCT idle IRQs (≈ one per sample), so the hardware latch
        // does drop on read and re-arms per sample: model that.
        setIrq(false);
        v = fifoStat_;
        break;
    // R_FIFOA_IRQCTRL / R_FIFOB_IRQCTRL are at $F09/$F29 (MAME asc.h:52-74,
    // decoded as `offset - 0x800` in asc_sonora_device) — NOT $809/$829,
    // which are register-file bytes. Not fatal here the way it is on IOSB,
    // because Sonora resets these to 0 = enabled, but it meant guest writes
    // landed in xtraRegs_ and the ASC interrupt could never be turned OFF.
    case 0xF09: v = fifoIrqEn_[0]; break;
    case 0xF29: v = fifoIrqEn_[1]; break;
    default:
        v = offset >= 0xF00 ? xtraRegs_[offset - 0xF00]
          : offset < 0x840  ? regs_[offset - 0x800] : 0;
        break;
    }
    if (onRead) onRead(offset, v);
    return v;
}

void AscSonora::write(uint32_t offset, uint8_t v) {
    offset &= 0xFFF;
    if (onWrite) onWrite(offset, v);

    if (offset == 0xE00) {                   // test hook (asc.cpp:374-378)
        fifoStat_ |= 0x0F;
        setIrq(true);
        return;
    }
    if (offset < 0x400) {                    // FIFO A (mode is always FIFO)
        if (cap_[0] < 0x400) {
            fifo_[0][wr_[0]] = v;
            wr_[0] = (wr_[0] + 1) & 0x3FF;
            cap_[0]++;
        }
        if (!(regs_[0x0A] & 1)) {            // asc_base write, PLAYRECA gate
            if (cap_[0] >= 0x200) {
                fifoStat_ &= uint8_t(~STAT_HALF_A);
                if (cap_[0] >= 0x3FF) fifoStat_ |= STAT_EMPTY_OR_FULL_A;
            } else if (cap_[0] > 0) {
                fifoStat_ &= uint8_t(~STAT_EMPTY_OR_FULL_A);
            }
            // Sonora override (MAME asc.cpp:1107-1116): in playback mode FIFO A
            // always reads back "empty" right after a write. AscIosb already
            // forces this; AscSonora cleared the bit and never re-set it.
            fifoStat_ |= STAT_EMPTY_OR_FULL_A;
        }
        return;
    }
    if (offset < 0x800) {                    // FIFO B
        if (cap_[1] < 0x400) {
            fifo_[1][wr_[1]] = v;
            wr_[1] = (wr_[1] + 1) & 0x3FF;
            cap_[1]++;
        }
        if (cap_[1] >= 0x200) {
            fifoStat_ &= uint8_t(~STAT_HALF_B);
            if (cap_[1] >= 0x3FF) fifoStat_ |= STAT_EMPTY_OR_FULL_B;
        } else if (cap_[1] > 0) {
            fifoStat_ &= uint8_t(~STAT_EMPTY_OR_FULL_B);
        }
        return;
    }

    switch (offset) {
    case 0x801: case 0x802: case 0x805: case 0x807: case 0x808:
        return;                              // read-only / no-op on Sonora
    case 0x803:                              // FIFOMODE: bit 7 = reset
        if (v & 0x80) {
            clearFifos();
            fifoStat_ |= STAT_EMPTY_OR_FULL_A | STAT_EMPTY_OR_FULL_B;
        }
        return;
    // asc_sonora_device::write (asc.cpp:1082-1104): enabling with the matching
    // half flag already up fires a dummy IRQ; disabling drops the line.
    case 0xF09:
        if (!(v & 1) && (fifoIrqEn_[0] & 1) && (fifoStat_ & STAT_HALF_A))
            setIrq(true);
        else if (v & 1) setIrq(false);
        fifoIrqEn_[0] = v & 1;
        return;
    case 0xF29:
        if (!(v & 1) && (fifoIrqEn_[1] & 1) && (fifoStat_ & STAT_HALF_B))
            setIrq(true);
        else if (v & 1) setIrq(false);
        fifoIrqEn_[1] = v & 1;
        return;
    default:
        if (offset >= 0xF00)     xtraRegs_[offset - 0xF00] = v;
        else if (offset < 0x840) regs_[offset - 0x800] = v;
        return;
    }
}

// sound_stream_update (asc.cpp:968-1035): stereo A/B drain, combined
// status on the B bits, playback mode reporting FIFO A empty per sample.
void AscSonora::tick(int cpuCycles) {
    drainAcc_ += int64_t(cpuCycles) * kSampleRate;
    while (drainAcc_ >= cpuHz_) {
        drainAcc_ -= cpuHz_;

        if (!(regs_[0x0A] & 1)) {            // playback: A reads empty
            fifoStat_ |= STAT_EMPTY_OR_FULL_A;
            if (!(fifoIrqEn_[0] & 1)) setIrq(true);
        }

        const int8_t smplL = int8_t(fifo_[0][rd_[0]] ^ 0x80);
        const int8_t smplR = int8_t(fifo_[1][rd_[1]] ^ 0x80);
        if (cap_[0]) { rd_[0] = (rd_[0] + 1) & 0x3FF; cap_[0]--; }
        if (cap_[1]) { rd_[1] = (rd_[1] + 1) & 0x3FF; cap_[1]--; }

        if (cap_[0] < 0x200 || cap_[1] < 0x200) {
            fifoStat_ |= STAT_HALF_B;
            if (!(fifoIrqEn_[1] & 1)) setIrq(true);
        } else {
            fifoStat_ &= uint8_t(~STAT_HALF_B);
        }
        if (cap_[0] == 0 || cap_[1] == 0) fifoStat_ |= STAT_EMPTY_OR_FULL_B;
        else                              fifoStat_ &= uint8_t(~STAT_EMPTY_OR_FULL_B);

        if (((outWr_ - outRd_) & (kOutSize - 1)) < kOutSize - 1) {
            uint32_t i = outWr_++ & (kOutSize - 1);
            outL_[i] = int16_t(int(smplL) * 256);
            outR_[i] = int16_t(int(smplR) * 256);
        }
    }
}

// ── Discrete EASC ($B0) — Quadra 700/900/950 ───────────────────────────
// MAME master asc_easc_device (sound/asc.cpp:1419-1771) — see Asc.h for
// the ASCTester hardware pin and the 44.1 kHz / host-decimation note.

void AscEasc::reset() {
    clearFifos();
    for (auto& channel : fifo_)
        for (uint8_t& sample : channel) sample = 0;
    for (auto& r : regs_) r = 0;
    for (auto& r : xtraRegs_) r = 0;
    // Base device_reset zeroes the register file (asc.cpp:148-160), FIFOSTAT
    // included: the ASCTester "804Idle: $0F" is produced by the running
    // drain, not latched at reset.
    fifoStat_ = 0;
    // asc.cpp:1753-1766 — EASC resets both FIFO IRQ enables to 1 = DISABLED
    // (the Sonora cell resets them enabled) and clears the CD-XA predictors.
    fifoIrqEn_[0] = fifoIrqEn_[1] = 1;
    srcStep_[0] = srcStep_[1] = 0;           // ctor state (asc.cpp:1420-1425)
    srcAccum_[0] = srcAccum_[1] = 0;
    for (int ch = 0; ch < 2; ch++) {
        xaS0_[ch] = xaS1_[ch] = 0;
        xaParam_[ch] = xaByte_[ch] = 0;
        xaPos_[ch] = xaSubpos_[ch] = 0;
    }
    lastL_ = lastR_ = 0;
    drainAcc_ = 0;
    outPhase_ = 0;
    outRd_ = outWr_ = 0;
    setIrq(false);
}

// asc_easc_device::read (asc.cpp:1650-1677) + asc_base_device::read defaults
uint8_t AscEasc::read(uint32_t offset) {
    offset &= 0xFFF;
    uint8_t v = 0;
    if (offset < 0x400)      v = fifo_[0][offset];
    else if (offset < 0x800) v = fifo_[1][offset - 0x400];
    else switch (offset) {
    case 0x800: v = 0xB0; break;             // get_version (asc.cpp:1768-1771)
    case 0x804:
        // asc.cpp:1653-1659: reading clears the IRQ latch (never the status
        // bits) only while FIFO B is NOT below half. The Sonora ruling that
        // forced an unconditional clear (the "Bienvenue." freeze) does not
        // transfer here: EASC's HALF_B is genuinely channel B (no combined
        // fold), the enables reset to DISABLED, and drivers re-edge the line
        // through the $F09/$F29 enables — the real-Q700 ASCTester dump and
        // MAME's agree line for line (asc.cpp:1428-1456).
        if (!(fifoStat_ & STAT_HALF_B)) setIrq(false);
        v = fifoStat_;
        break;
    case 0x807: v = 3; break;                // R_CLOCK: read-only, "44.1 kHz"
                                             // (asc.cpp:1661-1663)
    case 0xF09: v = fifoIrqEn_[0]; break;    // R_FIFOA_IRQCTRL (asc.h:60)
    case 0xF29: v = fifoIrqEn_[1]; break;    // R_FIFOB_IRQCTRL (asc.h:73)
    default:
        // asc_base_device::read fallthrough: the register backing store
        // (mode reads back its stored bit, SRC/VOL/CTRL/CD-XA bytes echo).
        v = offset >= 0xF00 ? xtraRegs_[offset - 0xF00]
          : offset < 0x840  ? regs_[offset - 0x800] : 0;
        break;
    }
    if (onRead) onRead(offset, v);
    return v;
}

// FIFO write, asc_base_device::write semantics (asc.cpp:474-527): channel A
// status updates gated on R_PLAYRECA bit 0, channel B ungated.
void AscEasc::pushFifo(int channel, uint8_t v) {
    if (cap_[channel] < 0x400) {
        fifo_[channel][wr_[channel]] = v;
        wr_[channel] = (wr_[channel] + 1) & 0x3FF;
        cap_[channel]++;
    }
    if (channel == 0 && (regs_[0x0A] & 1)) return;   // record mode: A frozen
    const uint8_t half = channel ? STAT_HALF_B : STAT_HALF_A;
    const uint8_t edge = channel ? STAT_EMPTY_OR_FULL_B : STAT_EMPTY_OR_FULL_A;
    if (cap_[channel] >= 0x200) {
        fifoStat_ &= uint8_t(~half);
        if (cap_[channel] >= 0x3FF) fifoStat_ |= edge;
    } else if (cap_[channel] > 0) {
        fifoStat_ &= uint8_t(~edge);
    }
}

// asc_easc_device::write (asc.cpp:1679-1735) + base defaults
void AscEasc::write(uint32_t offset, uint8_t v) {
    offset &= 0xFFF;
    if (onWrite) onWrite(offset, v);

    if (offset == 0xE00) {                   // test hook (asc.cpp:471-475)
        fifoStat_ |= 0x0F;
        setIrq(true);
        return;
    }
    if (offset < 0x400) { pushFifo(0, v); return; }
    if (offset < 0x800) { pushFifo(1, v); return; }

    switch (offset) {
    case 0x801:                              // R_MODE (asc.cpp:1683-1692)
        if ((v & 1) != regs_[0x01]) clearFifos();
        regs_[0x01] = v & 1;                 // only bit 0 can be written
        fifoStat_ |= STAT_EMPTY_OR_FULL_B;   // "signal playback FIFO empty"
        return;
    case 0x802:                              // R_CONTROL is a no-op on EASC
        return;
    case 0x803:                              // R_FIFOMODE (base, asc.cpp:543-551)
        if (v & 0x80) {
            clearFifos();
            fifoStat_ |= STAT_EMPTY_OR_FULL_A | STAT_EMPTY_OR_FULL_B;
        }
        regs_[0x03] = v;
        return;
    // asc.cpp:1697-1719 — enabling with the matching half flag already up
    // fires a dummy IRQ; disabling drops the line.
    case 0xF09:
        if (!(v & 1) && (fifoIrqEn_[0] & 1) && (fifoStat_ & STAT_HALF_A))
            setIrq(true);
        else if (v & 1) setIrq(false);
        fifoIrqEn_[0] = v & 1;
        return;
    case 0xF29:
        if (!(v & 1) && (fifoIrqEn_[1] & 1) && (fifoStat_ & STAT_HALF_B))
            setIrq(true);
        else if (v & 1) setIrq(false);
        fifoIrqEn_[1] = v & 1;
        return;
    // SRC step registers (asc.cpp:1721-1732): step = 16.16 phase increment,
    // programmed value + 1 (so $FFFF = 1:1 passthrough).
    case 0xF04: case 0xF05:                  // R_SRCA_H / R_SRCA_L
        xtraRegs_[offset - 0xF00] = v;
        srcStep_[0] = (uint32_t(xtraRegs_[0x04]) << 8 | xtraRegs_[0x05]) + 1;
        return;
    case 0xF24: case 0xF25:                  // R_SRCB_H / R_SRCB_L
        xtraRegs_[offset - 0xF00] = v;
        srcStep_[1] = (uint32_t(xtraRegs_[0x24]) << 8 | xtraRegs_[0x25]) + 1;
        return;
    default:                                 // base backing store (asc.cpp:594-597)
        if (offset >= 0xF00)     xtraRegs_[offset - 0xF00] = v;
        else if (offset < 0x840) regs_[offset - 0x800] = v;
        return;
    }
}

// asc_easc_device::pop_fifo (asc.cpp:1537-1587). The half/empty edges use
// the capacity from BEFORE the decrement, and an enabled channel below half
// asserts the IRQ. (MAME's m_fifo_clrptr bookkeeping is omitted: nothing
// reads it back.)
uint8_t AscEasc::popFifo(int channel) {
    const uint8_t half  = channel ? STAT_HALF_B : STAT_HALF_A;
    const uint8_t empty = channel ? STAT_EMPTY_OR_FULL_B : STAT_EMPTY_OR_FULL_A;
    const uint8_t sample = fifo_[channel][rd_[channel]];
    const int cap = cap_[channel];
    if (cap_[channel]) {
        rd_[channel] = (rd_[channel] + 1) & 0x3FF;
        cap_[channel]--;
    }
    if (cap <= 0x1FF) {
        fifoStat_ |= half;
        if (!(fifoIrqEn_[channel] & 1)) setIrq(true);
    } else {
        fifoStat_ &= uint8_t(~half);
    }
    if (cap == 0) fifoStat_ |= empty;
    else          fifoStat_ &= uint8_t(~empty);
    return sample;
}

// asc_easc_device::decode_cdxa (asc.cpp:1589-1648) — CD-XA ADPCM, 2/4/8-bit
// samples with a per-28-sample block header (filter[5:4], shift[3:0]) and
// the two-tap predictor whose K0/K1 coefficients the driver programs in the
// $F10 (A) / $F30 (B) register blocks.
int16_t AscEasc::decodeCdxa(int channel, uint8_t mode) {
    const int regBase = channel ? 0x30 : 0x10;       // R_CDXA_B / R_CDXA_A
    if (xaPos_[channel] == 0) {
        xaParam_[channel] = popFifo(channel);
        xaSubpos_[channel] = 0;
    }
    const int filterIdx = (xaParam_[channel] >> 4) & 3;
    const int shift = std::min<int>(xaParam_[channel] & 0xF, 12);
    const int8_t k0 = int8_t(xtraRegs_[regBase + filterIdx * 2 + 1]);
    const int8_t k1 = int8_t(xtraRegs_[regBase + filterIdx * 2]);

    int16_t raw = 0;
    switch (mode) {
    case 1:                                  // 8-bit ADPCM (2:1)
        raw = int16_t(uint16_t(popFifo(channel)) << 8);
        break;
    case 2:                                  // 4-bit ADPCM (4:1), low nibble first
        if (xaSubpos_[channel] == 0) {
            xaByte_[channel] = popFifo(channel);
            raw = int16_t(uint16_t(xaByte_[channel] & 0xF) << 12);
            xaSubpos_[channel] = 1;
        } else {
            raw = int16_t(uint16_t((xaByte_[channel] >> 4) & 0xF) << 12);
            xaSubpos_[channel] = 0;
        }
        break;
    case 3:                                  // 2-bit ADPCM (8:1), LSB first
        if (xaSubpos_[channel] == 0) xaByte_[channel] = popFifo(channel);
        raw = int16_t(uint16_t((xaByte_[channel] >> (xaSubpos_[channel] * 2)) & 0x3) << 14);
        xaSubpos_[channel] = (xaSubpos_[channel] + 1) & 3;
        break;
    }

    int32_t sample32 = int32_t(raw) >> shift;
    sample32 += (int32_t(k0) * xaS0_[channel] + int32_t(k1) * xaS1_[channel] + 32) >> 6;
    const int16_t sample = int16_t(std::clamp<int32_t>(sample32, -32768, 32767));
    xaS1_[channel] = xaS0_[channel];
    xaS0_[channel] = sample;
    if (++xaPos_[channel] >= 28) xaPos_[channel] = 0;
    return sample;
}

// asc_easc_device::sound_stream_update (asc.cpp:1457-1535): a 44.1 kHz
// drain; per channel the SRC accumulator decides how many source samples
// feed this output slot (0 = hold the previous one), each either a linear
// FIFO byte or a CD-XA decode. Host ring keeps every second sample (Asc.h).
void AscEasc::tick(int cpuCycles) {
    drainAcc_ += int64_t(cpuCycles) * kSampleRate;
    while (drainAcc_ >= cpuHz_) {
        drainAcc_ -= cpuHz_;

        if ((regs_[0x01] & 3) == 1) {        // FIFO mode
            for (int ch = 0; ch < 2; ch++) {
                const uint8_t ctrl = xtraRegs_[ch ? 0x28 : 0x08];  // R_FIFOx_CTRL
                int skip = 1;
                if (ctrl & 0x80) {           // SRC enabled
                    srcAccum_[ch] += srcStep_[ch];
                    skip = int(srcAccum_[ch] >> 16);
                    if (skip > 0) srcAccum_[ch] -= uint32_t(skip) << 16;
                }
                int16_t s = ch ? lastR_ : lastL_;
                for (int n = 0; n < skip; n++) {
                    s = (ctrl & 3) == 0
                      ? int16_t(int16_t(int8_t(popFifo(ch) ^ 0x80)) << 8)
                      : decodeCdxa(ch, ctrl & 3);
                }
                if (ch) lastR_ = s; else lastL_ = s;
            }
        } else {
            // Chip off: hold the last samples, signal half-empty B
            // (asc.cpp:1459-1469).
            fifoStat_ |= STAT_HALF_B;
        }

        outPhase_ ^= 1;
        if (outPhase_ && ((outWr_ - outRd_) & (kOutSize - 1)) < kOutSize - 1) {
            uint32_t i = outWr_++ & (kOutSize - 1);
            outL_[i] = lastL_;
            outR_[i] = lastR_;
        }
    }
}
