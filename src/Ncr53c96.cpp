// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Command-driven phase engine for the NCR 53C96, modelled on MAME
// ncr53c90.cpp (the 53c90/94/96 device) and reusing the ScsiDisk SCSI-1
// target. See Ncr53c96.h for the abstraction rationale and the MAME
// file:line map.
//
// Programming model followed (what the Mac ROM/OS-8.1 driver does, per the
// MAME transfer FSM ncr53c90.cpp:601-692 and start_command:927):
//   1. CM_FLUSH_FIFO, then the CDB bytes are written to the FIFO (R_FIFO).
//   2. R_STATUS write = destination bus id (bus_id_w, ncr53c90.cpp:1097).
//   3. CD_SELECT_ATN / CD_SELECT: arbitrate + select; if ATN, one IDENTIFY
//      message byte goes out first, then the CDB drains from the FIFO in the
//      COMMAND phase. Terminates with I_BUS|I_FUNCTION and seq_step=4 when the
//      whole CDB left the FIFO (ncr53c90.cpp:544-570 DISC_SEL_WAIT_REQ/seq).
//   4. CI_XFER (Transfer Information), usually the DMA variant ($90), with the
//      transfer counter loaded: moves one phase's worth of bytes through the
//      FIFO/DMA port. DATA IN drains the target's read payload; DATA OUT
//      gathers the write payload. Ends with I_BUS (ncr53c90.cpp:686 bus_complete).
//   5. CI_COMPLETE (Initiator Command Complete, $11): the controller reads the
//      STATUS byte and the COMMAND-COMPLETE message in one shot, latching them
//      into the FIFO, and raises I_FUNCTION (ncr53c90.cpp:1011-1016,
//      INIT_CPT_* + function_complete). The driver then reads the two FIFO
//      bytes and issues CI_MSG_ACCEPT to reach BUS FREE (I_DISCONNECT).

#include "Ncr53c96.h"
#include "ScsiDisk.h"
#include <cstring>

// ── FIFO ────────────────────────────────────────────────────────────────
void Ncr53c96::fifoPush(uint8_t v) {
    // In COMMAND phase after a select, FIFO writes ARE the command descriptor
    // block. On real hardware the SELECT sequence streams the FIFO to the
    // target (ncr53c90.cpp DISC_SEL_WAIT_REQ/SEND_BYTE, :544-570) and the
    // target flips the phase once the whole CDB is in — the driver polls that
    // phase change ($408D1A84) rather than issuing a Transfer Info. Model that
    // functionally: accumulate the CDB and run the target as soon as it is
    // complete, which advances phase_ out of COMMAND.
    if (phase_ == COMMAND && disk_) {
        cmd_.push_back(v);
        if (int(cmd_.size()) >= cdbLength(cmd_[0])) runTarget();
        updateDrq();
        return;
    }
    if (fifoPos_ < 16) fifo_[fifoPos_++] = v;
    updateDrq();
}
uint8_t Ncr53c96::fifoPop() {
    uint8_t r = fifo_[0];
    if (fifoPos_) { fifoPos_--; std::memmove(fifo_, fifo_ + 1, fifoPos_); }
    updateDrq();
    return r;
}

// ── IRQ / DRQ lines ─────────────────────────────────────────────────────
// The 53c90a+ latches interrupt cause into istatus and asserts IRQ while it
// is non-zero (ncr53c90.cpp:1079-1086 check_irq). Reading R_ISTAT clears it.
void Ncr53c96::raiseIrq(uint8_t bits) { istatus_ |= bits; irq_ = istatus_ != 0; }

// DRQ policy (ncr53c90.cpp:1207-1232 + c94 check_drq:1374). We assert DRQ
// whenever a DMA transfer-info command is active and the FIFO can move a byte
// in the required direction. DRQ tracks CPU-side data availability and is
// independent of S_TC0 (the SCSI-bus transfer count) — the driver polls S_TC0
// to know a chunk has landed, then drains it through the window while DRQ
// holds. The integrator uses DRQ to hold off /DTACK on the pseudo-DMA window.
void Ncr53c96::updateDrq() {
    bool d = false;
    if (dmaCommand_) {
        if (phase_ == DATA_IN)  d = dataInPos_ < dataIn_.size();
        else if (phase_ == DATA_OUT) d = dataOut_.size() < dataOutExpected_;
    }
    drq_ = d;
}

// ── CDB geometry (SCSI-1 group code = top 3 bits) ───────────────────────
int Ncr53c96::cdbLength(uint8_t op) {
    switch (op >> 5) {
        case 0: return 6;
        case 1: case 2: return 10;
        case 5: return 12;
        default: return 6;
    }
}
int Ncr53c96::writeByteCount(const std::vector<uint8_t>& cdb) {
    if (cdb.empty()) return 0;
    if (cdb[0] == 0x0A && cdb.size() >= 5)                    // WRITE(6)
        return (cdb[4] ? cdb[4] : 256) * 512;
    if (cdb[0] == 0x2A && cdb.size() >= 9)                    // WRITE(10)
        return ((cdb[7] << 8) | cdb[8]) * 512;
    return 0;
}

// Low 3 status bits reflect the live target phase (I/O, C/D, MSG).
uint8_t Ncr53c96::phaseStatusBits() const {
    switch (phase_) {
        case COMMAND:  return S_CD;                          // C/D
        case DATA_IN:  return S_IO;                          // I/O
        case DATA_OUT: return 0;
        case STATUS:   return S_CD | S_IO;                   // C/D + I/O
        case MSG_IN:   return S_CD | S_IO | S_MSG;
        case MSG_OUT:  return S_CD | S_MSG;
        default:       return 0;
    }
}

// ── Reset ───────────────────────────────────────────────────────────────
void Ncr53c96::reset() {
    // ncr53c90.cpp:246 device_reset — clockConv 2, syncPeriod 5, syncOffset 0,
    // config keeps low 3 bits, status/istatus cleared, counter cleared.
    fifoPos_ = 0; std::memset(fifo_, 0, sizeof(fifo_));
    clockConv_ = 2; syncPeriod_ = 5; syncOffset_ = 0; seq_ = 0;
    config1_ &= 7; config2_ = 0; config3_ = 0;
    status_ = 0; istatus_ = 0; irq_ = false; drq_ = false; testMode_ = false;
    tcount_ = 0; tcounter_ = 0;
    phase_ = BUS_FREE; disk_ = nullptr;
    cmd_.clear(); dataIn_.clear(); dataInPos_ = 0;
    dataOut_.clear(); dataOutExpected_ = 0;
    targetStatus_ = 0; msgInLeft_ = 0; dmaCommand_ = false;
}

// ── Register interface ──────────────────────────────────────────────────
uint8_t Ncr53c96::read(int reg) {
    reads++;
    uint8_t v = 0xFF;
    switch (reg & 0xF) {
        case R_TCLOW:  v = uint8_t(tcounter_); break;
        case R_TCMID:  v = uint8_t(tcounter_ >> 8); break;
        case R_TCHIGH: v = uint8_t(tcounter_ >> 16); break;
        // In DATA IN the FIFO port is the drain path for the payload (polled
        // reads). Everything else reads the real FIFO (STATUS/MSG bytes that
        // CI_COMPLETE latched, or a staged CDB echo).
        case R_FIFO:
            if (phase_ == DATA_IN && dataInPos_ < dataIn_.size()) {
                v = dataIn_[dataInPos_++];
                if (tcounter_) tcounter_--;
                if (dataInPos_ >= dataIn_.size()) {
                    status_ |= S_TC0; advanceToStatus(); raiseIrq(I_BUS);
                }
                updateDrq();
            } else {
                v = fifoPos_ ? fifoPop() : 0;
            }
            break;
        case R_COMMAND: v = lastCmd; break;
        // 53c90a status_r (ncr53c90.cpp:1288): S_INTERRUPT | latched | phase.
        case R_STATUS:
            v = (irq_ ? S_INTERRUPT : 0) | (status_ & (S_TC0 | S_PARITY | S_GROSS_ERROR))
                | phaseStatusBits();
            break;
        // Reading istatus clears the interrupt (ncr53c90.cpp:1103-1122).
        case R_ISTAT:
            v = istatus_;
            if (irq_) {
                status_ &= ~(S_GROSS_ERROR | S_PARITY);
                istatus_ = 0; seq_ = 0; irq_ = false;
            }
            break;
        case R_SEQ:    v = seq_; break;                       // sequence step
        // FIFO flags: low 5 bits = byte count, top 3 = seq step. In DATA IN the
        // payload is drained through dataIn_ (window/FIFO port), not the real
        // FIFO array, so report the pending count there (the driver's DMA loop
        // gates its 16-byte bulk read on bit4 = "≥16 bytes ready", $408D1FAC).
        case R_FLAGS: {
            uint32_t cnt = (phase_ == DATA_IN)
                ? std::min<size_t>(dataIn_.size() - dataInPos_, 16)
                : uint32_t(fifoPos_);
            v = uint8_t(cnt & 0x1F) | (uint8_t(seq_) << 5);
            break;
        }
        case R_CONFIG1: v = config1_; break;
        case R_CONFIG2: v = config2_; break;
        case R_CONFIG3: v = config3_; break;
        default: v = 0xFF; break;
    }
    if (onAccess) onAccess(reg & 0xF, false, v);
    return v;
}

void Ncr53c96::write(int reg, uint8_t v) {
    writes++;
    if (onAccess) onAccess(reg & 0xF, true, v);
    switch (reg & 0xF) {
        case R_TCLOW:  tcount_ = (tcount_ & ~uint32_t(0x0000FF)) | v; break;
        case R_TCMID:  tcount_ = (tcount_ & ~uint32_t(0x00FF00)) | (uint32_t(v) << 8); break;
        case R_TCHIGH: tcount_ = (tcount_ & ~uint32_t(0xFF0000)) | (uint32_t(v) << 16); break;
        case R_FIFO:   fifoPush(v); break;
        case R_COMMAND: startCommand(v); break;
        case R_STATUS:  busId_ = v & 7; break;                // bus_id_w
        case R_ISTAT:   selectTimeout_ = v; break;            // timeout_w
        case R_SEQ:     syncPeriod_ = v & 0x1F; break;        // sync_period_w
        case R_FLAGS:   syncOffset_ = v & 0x0F; break;        // sync_offset_w
        case R_CONFIG1: config1_ = v; scsiId_ = v & 7; if (v & 8) testMode_ = true; break;
        case R_CLOCK:   clockConv_ = v & 7; break;
        case R_TEST:    break;                                // test mode: no-op
        case R_CONFIG2: config2_ = v; break;
        case R_CONFIG3: config3_ = v; break;
        default: break;
    }
}

// ── Command dispatch (ncr53c90.cpp:927 start_command) ───────────────────
void Ncr53c96::startCommand(uint8_t c) {
    lastCmd = c;
    uint8_t op = c & 0x7F;
    dmaCommand_ = (c & CMD_DMA) != 0;
    if (dmaCommand_) { tcounter_ = tcount_ ? tcount_ : 0x10000; status_ &= ~S_TC0; }
    else             { tcounter_ = 0; }

    switch (op) {
        case CM_NOP: break;

        case CM_FLUSH_FIFO:
            fifoPos_ = 0; updateDrq();
            break;

        case CM_RESET:
            reset();
            break;

        case CM_RESET_BUS:
            // ncr53c90.cpp:965 — raises I_SCSI_RESET unless config bit 6 masks it.
            phase_ = BUS_FREE; disk_ = nullptr;
            if (!(config1_ & 0x40)) raiseIrq(I_SCSI_RESET);
            break;

        case CD_SELECT:      selectTarget(false); break;
        case CD_SELECT_ATN:
        case CD_SELECT_ATN_STOP: selectTarget(true); break;

        case CD_ENABLE_SEL:
        case CD_DISABLE_SEL:
            // Target-mode selection enable — never used as initiator.
            raiseIrq(I_FUNCTION);
            break;

        case CI_XFER:        transferInfo(); break;

        case CI_COMPLETE:
            // Initiator Command Complete: latch STATUS + COMMAND-COMPLETE
            // message into the FIFO, raise I_FUNCTION (ncr53c90.cpp:1011).
            fifoPos_ = 0;
            if (phase_ == STATUS || phase_ == DATA_IN || phase_ == DATA_OUT || phase_ == COMMAND) {
                fifoPush(targetStatus_);      // STATUS byte
                fifoPush(0x00);               // COMMAND COMPLETE message
                phase_ = MSG_IN; msgInLeft_ = 0;
            }
            seq_ = 0;
            raiseIrq(I_FUNCTION);
            break;

        case CI_MSG_ACCEPT:
            // Accept the last message → target disconnects → BUS FREE
            // (ncr53c90.cpp:1018 + the MODE_I !S_BSY disconnect at :314).
            phase_ = BUS_FREE; disk_ = nullptr;
            raiseIrq(I_DISCONNECT);
            break;

        case CI_PAD:
            raiseIrq(I_BUS);
            break;

        case CI_SET_ATN:
        case CI_RESET_ATN:
            break;                            // ATN line: no functional effect

        default:
            // Unknown/unsupported command → illegal-command interrupt.
            raiseIrq(I_ILLEGAL);
            break;
    }
}

// ── Selection ───────────────────────────────────────────────────────────
// Arbitrate + select the destination (busId_). If ATN, the FIFO's first byte
// is the IDENTIFY message (consumed as MSG OUT); the rest is the CDB, drained
// in the COMMAND phase. Ends I_BUS|I_FUNCTION, seq_step=4 (whole CDB sent).
void Ncr53c96::selectTarget(bool withAtn) {
    selects++;
    seq_ = 0;
    ScsiDisk* t = (busId_ >= 0 && busId_ < 7) ? targets_[busId_] : nullptr;
    if (!t || !t->present()) {
        // Selection timeout: the target never asserted BSY → disconnect.
        phase_ = BUS_FREE; disk_ = nullptr;
        raiseIrq(I_DISCONNECT);
        return;
    }
    disk_ = t;

    // The FIFO holds [IDENTIFY msg?] + CDB. Drain it: first byte(s) are the
    // MSG OUT (IDENTIFY / message), remainder is the command descriptor block.
    cmd_.clear();
    if (withAtn && fifoPos_ > 0) {
        // Consume the IDENTIFY message byte (msg bytes have bit7 or are 0x80+).
        (void)fifoPop();                      // IDENTIFY (LUN select) — ignored
        seq_ = 2;                             // one MSG OUT byte sent
    }
    // Remaining FIFO bytes are the CDB.
    int expected = 0;
    while (fifoPos_ > 0) {
        uint8_t b = fifoPop();
        cmd_.push_back(b);
        if (cmd_.size() == 1) expected = cdbLength(b);
        if (int(cmd_.size()) >= expected) break;
    }
    // seq_step counts the phase progression; 4 = command fully transferred.
    seq_ = 4;

    if (expected && int(cmd_.size()) >= expected) {
        // Whole CDB is in — run it and set up the resulting phase.
        runTarget();
    } else {
        // CDB not fully loaded yet: stay in COMMAND (driver may XFER more).
        phase_ = COMMAND;
    }
    // Select-with-ATN completes with I_BUS | I_FUNCTION (ncr53c90.cpp:772/537).
    raiseIrq(I_BUS | I_FUNCTION);
    updateDrq();
}

// Execute the accumulated CDB against the target and choose the next phase.
void Ncr53c96::runTarget() {
    commands++;
    if (onCommand) onCommand(cmd_);
    int wbytes = writeByteCount(cmd_);
    if (wbytes > 0) {                         // WRITE: gather DATA OUT first
        phase_ = DATA_OUT; dataOut_.clear(); dataOutExpected_ = size_t(wbytes);
        return;
    }
    dataIn_.clear(); dataInPos_ = 0;
    std::vector<uint8_t> none;
    targetStatus_ = disk_ ? disk_->command(cmd_.data(), int(cmd_.size()), dataIn_, none) : 0x02;
    if (!dataIn_.empty()) { phase_ = DATA_IN; dataInPos_ = 0; }
    else phase_ = STATUS;
}

// Move DATA→STATUS once the payload is drained/gathered.
void Ncr53c96::advanceToStatus() {
    if (phase_ == DATA_OUT) {                 // finish a WRITE
        std::vector<uint8_t> readback, none;
        targetStatus_ = disk_ ? disk_->command(cmd_.data(), int(cmd_.size()), readback, dataOut_)
                              : 0x02;
    }
    phase_ = STATUS;
    status_ |= S_TC0;
    updateDrq();
}

// ── Transfer Information (CI_XFER) ──────────────────────────────────────
// One "phase's worth" of the current bus phase. In DATA IN the payload is
// already queued (dataIn_); the driver reads it via the FIFO or the DMA port.
// We flag the transfer as complete for the polled (non-DMA) path by pre-
// loading the FIFO with a first byte and letting FIFO reads / dmaRead drain
// the rest; DRQ tracks availability for the DMA path.
void Ncr53c96::transferInfo() {
    seq_ = 0;
    switch (phase_) {
        case DATA_IN:
            // Payload sits in dataIn_. Both the polled path (R_FIFO reads) and
            // the DMA path (dmaRead) drain it directly. For a DMA transfer the
            // chip first buffers `tcounter_` bytes off the SCSI bus and then
            // lets the CPU drain them through the pseudo-DMA window; the Mac OS
            // 8.1 driver polls S_TC0 (Status bit4) to know the requested count
            // has landed BEFORE it bulk-reads 16 bytes at a time ($408D1F7C →
            // $408D1FA2). Signal that here so the polled path unblocks.
            if (dmaCommand_ && dataInPos_ < dataIn_.size()) {
                status_ |= S_TC0;
                // A completed DMA Transfer Info also raises the bus-service
                // interrupt (ncr53c90.cpp:686 bus_complete): the driver drains
                // the 16-byte window burst then waits on Status bit7 ($408D1FC0)
                // and reads R_ISTAT ($408D1FC8) before the next chunk.
                raiseIrq(I_BUS);
            }
            updateDrq();
            break;

        case DATA_OUT:
            updateDrq();                      // driver feeds bytes via dmaWrite/FIFO
            break;

        case COMMAND:
            // Continue draining a partial CDB from the FIFO (rare — the ROM
            // usually sends the whole CDB with SELECT_ATN).
            while (fifoPos_ > 0) {
                uint8_t b = fifoPop();
                cmd_.push_back(b);
                if (int(cmd_.size()) >= cdbLength(cmd_[0])) { runTarget(); break; }
            }
            raiseIrq(I_BUS);
            break;

        case STATUS:
        case MSG_IN:
            raiseIrq(I_BUS);
            break;

        default:
            raiseIrq(I_BUS);
            break;
    }
}

// ── Pseudo-DMA data port (Q605 PrimeTime + $10100) ──────────────────────
uint8_t Ncr53c96::dmaRead() {
    if (phase_ == DATA_IN && dataInPos_ < dataIn_.size()) {
        dmaBytes++;
        uint8_t b = dataIn_[dataInPos_++];
        if (tcounter_) tcounter_--;
        if (dataInPos_ >= dataIn_.size()) {   // payload drained → transfer done
            status_ |= S_TC0;
            advanceToStatus();
            raiseIrq(I_BUS);                  // bus_complete (ncr53c90.cpp:686)
        }
        updateDrq();
        return b;
    }
    // Also allow reading residual FIFO (STATUS/MSG bytes latched by CI_COMPLETE).
    if (fifoPos_) return fifoPop();
    return 0;
}

void Ncr53c96::dmaWrite(uint8_t v) {
    if (phase_ == DATA_OUT) {
        dmaBytes++;
        dataOut_.push_back(v);
        if (tcounter_) tcounter_--;
        if (dataOut_.size() >= dataOutExpected_) {
            status_ |= S_TC0;
            advanceToStatus();                // commit the WRITE to the target
            raiseIrq(I_BUS);
        }
        updateDrq();
        return;
    }
    fifoPush(v);
}
