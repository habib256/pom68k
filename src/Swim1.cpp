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

// KNOWN MAME DIVERGENCE, deliberately kept (parity audit § 2.4, cosmetic).
// MAME's swim1_device::device_reset (swim1.cpp:95-126) clears only the ISM
// *register* file — m_ism_mode/m_ism_setup/m_ism_param_idx/m_ism_param — plus
// the whole IWM half; it leaves the ISM *engine* alone: m_ism_fifo,
// m_ism_fifo_pos, m_ism_error, m_ism_crc, m_ism_sr, m_ism_current_bit,
// m_ism_tss_*, m_ism_mfm_sync_counter all survive a reset. (Contrast
// swim2.cpp:52-80, which does clear them and seeds m_crc = $FFFF.) That is an
// omission in the master, not modelled behaviour: nothing can observe stale
// ISM state on real silicon's power-up path, and replicating it here would
// leak a half-decoded byte, a latched error bit and a live FIFO across a
// machine reset — and make a post-reset save state non-deterministic. So we
// clear everything, and crc_ starts at the crcClear() seed rather than at
// Swim2's MAME-exact $FFFF, because SWIM1 has no MAME reset constant to match.
// Reopen only if a driver is found that reads the ISM FIFO before its first
// write to the ISM register file.
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
    ismClock_ = lastSync_ = latestEdge_ = 0;
    prevLs_ = 0x5;
    csmState_ = CsmInit;
    csmErr_[0] = csmErr_[1] = 0;
    csmPairSide_ = csmMinCount_ = 0;
    correction_[0] = correction_[1] = 0;
    tsmOut_ = tsmBits_ = 0;
    tsmMark_ = false;
    driveSel_ = 0;
    lstrb_ = false;
    crc_ = 0xCDB4;
    sr_ = 0;
    tssSr_ = tssOutput_ = 0;
    currentBit_ = -1;
    halfWait_ = 0;
    writeHalfPos_ = 0;
    writeStartTick_ = 0;
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

// IWM-personality access watcher (MAME swim1.cpp:554-581): four writes to
// offset 0xf whose bit 6 goes 1,0,1,1 switch the chip to ISM. The check
// runs on EVERY IWM access — any access that is not the expected next
// pattern step resets the counter (:578-579). Reads reach iwm_control with
// data 0x00 (:153-154), so a READ of offset 0xf stands in for a 0 step.
void Swim1::iwmModeWatch(int reg, uint8_t v) {
    const int prev = iwmToIsm_;
    if (reg == 15) {
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
    if (!ismMode_) {
        iwmModeWatch(reg, 0x00);                 // reads carry data 0
        return iwm_.read(reg);
    }
    return ismRead(reg & 7);                     // reads alias &7 (swim1.cpp:184)
}

void Swim1::write(int reg, uint8_t v) {
    if (!ismMode_) {
        // The watcher sees every IWM access, exactly like MAME's hook in
        // iwm_control (before the IWM consumes it).
        iwmModeWatch(reg, v);
        iwm_.write(reg, v);
        return;
    }
    // MAME swim1.cpp:269-337: the ISM WRITE decode uses the full offset —
    // 8-15 fall to the logerror default and are ignored, never aliased.
    if (reg < 8) ismWrite(reg, v);
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
        // No mid-read reclocking needed here: the ISM read thresholds are
        // pure parameter-RAM arithmetic (swim1.cpp:1003-1080), so a setup
        // write changes nothing the engine caches.
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
        // Entering read mode — swim1.cpp:388-399 verbatim: land the edge
        // clock at the drive's rotation angle, arm the CSM (GCR starts
        // SYNCHRONIZED — only MFM calibrates), forget the calibration.
        // MAME leaves the TSM assembly registers alone here, so we do too.
        currentBit_ = 0;
        sr_ = 0;
        SonyDrive* d = selectedDrive();
        const int64_t t0 = d ? d->fluxAngleTicks(side1())
                                   / (FluxPll::kSubCell / 2)
                             : 0;
        ismClock_ = lastSync_ = latestEdge_ = t0;
        prevLs_ = 0x5;                           // (1<<2)|1
        csmState_ = (setup_ & 0x04) ? CsmSynchronized : CsmInit;
        csmErr_[0] = csmErr_[1] = 0;
        correction_[0] = correction_[1] = 0;
        csmPairSide_ = 0;
        csmMinCount_ = 0;
    } else if ((previousMode & 0x18) == 0x08 && (mode_ & 0x18) != 0x08) {
        currentBit_ = -1;
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
    writeStartTick_ = d ? d->startWriteFlux(side1()) : 0;
}

// Same flux hand-off as Swim2::finishWrite. It matters more here: the ISM
// write spacing comes from the parameter RAM (P_TIME0/1), so a guest that
// programs its own timings genuinely writes at its own rate — and since
// § 1.3 step 5 the medium keeps that rate instead of snapping it onto the
// read grid cellCycles() defines.
void Swim1::finishWrite() {
    if (!writeActive_) return;
    writeActive_ = false;
    SonyDrive* d = selectedDrive();
    // Same deliberate divergence as Swim2::finishWrite: MAME's flush_write
    // erases a transition-less span (write_flux with count 0), we leave the
    // cells alone. See the long note there.
    if (!d || writeTransitions_.empty()) { writeTransitions_.clear(); return; }

    const int64_t halfTick = FluxPll::kSubCell / 2;
    std::vector<int64_t> atTicks;
    atTicks.reserve(writeTransitions_.size());
    for (uint64_t h : writeTransitions_)
        atTicks.push_back(int64_t(h) * halfTick);
    d->commitFlux(writeStartTick_, int64_t(writeHalfPos_) * halfTick, atTicks,
                  !(setup_ & 0x40),
                  int64_t(cellCycles()) * FluxPll::kSubCell);
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

int Swim1::cyclesToNextEvent() const {
    // The legacy IWM personality has no event API yet; retain the historical
    // scheduler cadence for it.  ISM uses the same cell/TSS engine as SWIM2.
    // (256 is one GCR byte time on a C15M board, which looks alarming next
    // to the step-6 cell engine and is not: every register access flushes
    // the chip forward first — measured, `Iwm.h`.)
    if (!ismMode_) return 256;
    if (!(mode_ & 0x08)) return 0x7fffffff;
    if (mode_ & 0x10) {
        if (halfWait_) return int((halfWait_ + 1) / 2);
        return currentBit_ >= 0 ? 1 : 0x7fffffff;
    }
    // ISM read: the next observable transition (a FIFO byte, an error
    // latch) needs at least one more inter-transition gap, and the
    // shortest legal gap is P_MINCT+6 halves. A smaller bound is merely
    // slow (the deadline contract), so return the historical 16-cycle
    // cadence capped by that floor.
    const int minGap = (params_[P_MINCT] + 6) / 2;
    return minGap > 0 ? std::min(minGap, 16) : 16;
}

// ── ISM read engine — MAME swim1.cpp:885-1233, ported verbatim ──────────
// Not a window separator: the engine measures INTER-TRANSITION times (in
// half-cycles), classifies each gap under a Short and a Long hypothesis
// against cumulative parameter-RAM thresholds scaled by the running
// correction factor, resolves marginal pairs against the previous gap,
// and feeds resolved cell counts to two machines — the Correction State
// Machine (64 minimum cells calibrate `correction_[pair side]`) and the
// Trans-Space Machine (bytes into the FIFO: MFM via the nb/bb tables with
// missing-clock mark detection, GCR as gap→bits). Edges come from
// SonyDrive's flux view, converted FluxPll ticks → halves.
void Swim1::tickRead(int cycles) {
    constexpr int64_t kHalf = FluxPll::kSubCell / 2;   // ticks per half-cycle
    SonyDrive* d = selectedDrive();
    ismClock_ += int64_t(cycles) * 2;
    const int64_t nextSync = ismClock_;
    const bool live = d && d->hasDisk() && d->motorOn();

    while (lastSync_ < nextSync) {
        // Find when in the future the next edge happens (swim1.cpp:983-999)
        int64_t cyclesToNext;
        bool willHitEdge;
        const int64_t edge =
            live ? d->nextFluxAfter((latestEdge_ + 2) * kHalf, side1())
                 : FluxPll::kNever;
        if (edge == FluxPll::kNever || edge / kHalf > nextSync) {
            cyclesToNext = nextSync - latestEdge_;
            willHitEdge = false;
        } else {
            cyclesToNext = edge / kHalf - latestEdge_;
            willHitEdge = true;
        }

        // Pick up the current rescaling factor (swim1.cpp:1002-1004)
        int scale = correction_[csmPairSide_];
        if (scale < 192) scale |= 256;

        // Count the cells in the S and L hypotheses (swim1.cpp:1006-1080):
        // cumulative thresholds MINCT+6, +s1+4, +s2+4, +RPT+4, all scaled.
        auto count = [&](uint8_t s1, uint8_t s2) -> uint32_t {
            int64_t t = params_[P_MINCT] + 3 * 2;
            if (cyclesToNext <= (scale * t) >> 8) return 0;
            t += s1 + 2 * 2;
            if (cyclesToNext <= (scale * t) >> 8) return 1;
            t += s2 + 2 * 2;
            if (cyclesToNext <= (scale * t) >> 8) return 2;
            t += params_[P_RPT] + 2 * 2;
            if (cyclesToNext <= (scale * t) >> 8) return 3;
            return 4;
        };
        uint32_t sct, lct;
        if (prevLs_ == 0x5) {                    // previous was a short
            sct = count(params_[P_SSS], params_[P_SLS]);
            lct = count(params_[P_SSL], params_[P_SLL]);
        } else if (prevLs_ == 0x6 || prevLs_ == 0x7 ||
                   prevLs_ == 0x9 || prevLs_ == 0xd) {   // previous marginal
            sct = count(params_[P_LSS], params_[P_CSLS]);
            lct = count(params_[P_LSL], params_[P_CSLS]);
        } else {                                 // previous was a long
            sct = count(params_[P_LSS], params_[P_LLS]);
            lct = count(params_[P_LSL], params_[P_LLL]);
        }

        // Resolve cell lengths (swim1.cpp:1083-1125)
        int resolvedCount = 0;
        uint32_t resolvedType[2] = { 0, 0 };
        if ((sct == 4 || lct == 4) && !error_)
            error_ |= 0x20;

        if (willHitEdge) {
            if (sct == 0) {
                // No short-cell error: write splices trigger it and the
                // physical media probably doesn't allow for it (MAME note).
                sct = lct = 1;
            }
            if (sct == 4) sct = 3;
            if (lct == 4) lct = 3;

            const bool previousMarginal =
                prevLs_ == 0x6 || prevLs_ == 0x7 ||
                prevLs_ == 0x9 || prevLs_ == 0xd;
            const bool currentMarginal =
                (sct == 1 && lct > 1) || (lct == 1 && sct > 1);

            if (previousMarginal && currentMarginal) {
                if (!error_) error_ |= 0x40;
                resolvedCount = 2;
                resolvedType[0] = (prevLs_ >> 2) & 3;
                resolvedType[1] = lct;
            } else {
                if (previousMarginal) {
                    if (sct == 1)
                        resolvedType[resolvedCount++] = prevLs_ & 3;
                    else
                        resolvedType[resolvedCount++] = (prevLs_ >> 2) & 3;
                }
                if (!currentMarginal) {
                    if (sct == 1)
                        resolvedType[resolvedCount++] = sct;
                    else
                        resolvedType[resolvedCount++] = lct;
                }
            }
            prevLs_ = uint8_t((lct << 2) | sct);
        }

        // Run the CSM and the TSM on the resolved cells (swim1.cpp:1128-1220)
        for (int r = 0; r != resolvedCount; r++) {
            const uint32_t type = resolvedType[r];
            bool dropOneBit = false;
            switch (csmState_) {
            case CsmInit:
                csmErr_[0] = csmErr_[1] = 0;
                csmPairSide_ = 0;
                csmMinCount_ = 0;
                csmState_ = CsmCountMin;
                break;

            case CsmCountMin:
                if (type != 1) {
                    csmState_ = CsmInit;
                    break;
                }
                csmErr_[csmPairSide_] +=
                    uint32_t(params_[P_MULT]) * uint32_t(cyclesToNext >> 1);
                csmMinCount_++;
                if (csmMinCount_ == 64) {
                    for (int i = 0; i != 2; i++) {
                        correction_[i] = uint8_t(csmErr_[i] >> 8);
                        if (!error_ && (csmErr_[i] < 0xc000 ||
                                        csmErr_[i] >= 0x1c000))
                            error_ |= 0x08;
                    }
                    csmState_ = CsmWaitNonMin;
                }
                break;

            case CsmWaitNonMin:
                if (type == 1)
                    break;
                csmState_ = CsmCheckMark;
                tsmOut_ = 0;
                tsmMark_ = false;
                tsmBits_ = 0;
                crcClear();
                dropOneBit = true;
                [[fallthrough]];

            case CsmCheckMark:
            case CsmSynchronized:
                if (setup_ & 0x04) {
                    // GCR: a gap of N cells is N-1 zeros then a one; the
                    // high bit frames the byte (swim1.cpp:1166-1175).
                    for (uint32_t i = 0; i != type; i++) {
                        const int bit = (i + 1 == type) ? 1 : 0;
                        tsmOut_ = uint8_t((tsmOut_ << 1) | bit);
                        if (tsmOut_ & 0x80) {
                            if (fifoPush(tsmOut_) && !error_) error_ |= 0x01;
                            tsmOut_ = 0;
                        }
                    }
                } else {
                    // MFM: gap type + current polarity → emitted bits;
                    // idx 5 (long-long on a one) is the missing clock of
                    // the A1/C2 marks (swim1.cpp:1176-1215).
                    static constexpr uint32_t nb[6] = { 1, 1, 2, 1, 2, 2 };
                    static constexpr uint32_t bb[6] = { 1, 0, 1, 0, 1, 0 };
                    const int idx = (tsmOut_ & 1 ? 0 : 3) + int(type) - 1;
                    uint32_t nbc = nb[idx];
                    const uint32_t bbc = bb[idx];
                    if (dropOneBit) {
                        nbc--;
                        dropOneBit = false;
                    }
                    if (idx == 5)
                        tsmMark_ = true;
                    for (uint32_t i = 0; i != nbc; i++) {
                        const int bit = int((bbc >> (nbc - 1 - i)) & 1);
                        tsmOut_ = uint8_t((tsmOut_ << 1) | bit);
                        tsmBits_++;
                        crcUpdate(bit);

                        if (tsmBits_ == 8) {
                            if (csmState_ == CsmCheckMark) {
                                if (!tsmMark_) {
                                    csmState_ = CsmInit;
                                    break;
                                }
                                csmState_ = CsmSynchronized;
                            }
                            uint16_t val = tsmOut_;
                            if (tsmMark_) {
                                tsmMark_ = false;
                                val |= MARK;
                                crcClear();
                            }
                            if (!crc_)
                                val |= CRC0;
                            if (fifoPush(val) && !error_) error_ |= 0x01;
                            tsmBits_ = 0;
                        }
                    }
                }
                break;
            }

            csmPairSide_ = uint8_t(!csmPairSide_);
        }

        // Go to the next sync point (swim1.cpp:1224-1229)
        if (willHitEdge) {
            latestEdge_ += cyclesToNext;
            lastSync_ = latestEdge_;
        } else
            lastSync_ = nextSync;
    }
    lastSync_ = nextSync;
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
                // Same deliberate divergence as Swim2::tickWrite: MAME's
                // swim1.cpp:944 gates the termination on `&& !m_ism_error`, so
                // an underrun with an error already latched writes endless $FF
                // mark bytes instead of stopping. See the long note there.
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
