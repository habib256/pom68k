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
        if (int(cmd_.size()) >= cdbLength(cmd_[0])) {
            // Whole CDB now streamed in: the target flips phase and the chip
            // reports the select's function_bus_complete (I_BUS|I_FUNCTION,
            // ncr53c90.cpp DISC_SEL_WAIT_REQ phase change). This is the FIRST
            // interrupt of the transaction the async SIM waits for after it
            // saw DRQ and installed its continuation (Q6.5b).
            bool wasSelWait = selCdbWait_;
            selCdbWait_ = false;
            seq_ = 4;
            runTarget();
            if (wasSelWait) raiseIrq(I_BUS | I_FUNCTION);
        }
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

// Bus-service latency (Q6.5b): hold the interrupt back for latency_ cycles —
// the delay a real target/bus needs — so software that polls right after
// issuing a Transfer Info does not see an instant completion. tick() applies
// the held bits. latency_=0 → identical to raiseIrq (unit-test default).
void Ncr53c96::raiseIrqDeferred(uint8_t bits) {
    if (latency_ <= 0) { raiseIrq(bits); return; }
    pendBits_ |= bits;
    pendDelay_ = latency_;
}

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
        // A DMA SELECT ($C1) whose CDB has not landed yet raises DRQ to fetch
        // it (MAME dma_set(DMA_OUT) at CD_SELECT + check_drq in
        // DISC_SEL_ARBITRATION: DMA_OUT with fifo<16, no S_TC0 → DRQ true,
        // ncr53c90.cpp:987/500-510/1207). The Mac OS 8 async SCSI SIM polls
        // this DRQ (pseudo-VIA2 IFR bit0) to detect an async transaction and
        // install its completion continuation (Q6.5b) — without it the SIM
        // took the sync path and later jumped through a NULL handler.
        else if (selCdbWait_ && phase_ == COMMAND) d = true;
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
    pendDelay_ = 0; pendBits_ = 0;       // latency_ itself survives (wiring)
    selCdbWait_ = false; dataXfer_ = false;
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
            // Drain the read payload through the FIFO port whenever bytes are
            // still pending — keyed on dataInPos_, NOT the phase: a polled
            // Transfer Info that drains the tail pre-advances phase_ to STATUS
            // at command time (Q6.6), but the driver still reads the staged
            // byte(s) here afterwards.
            if (dataInPos_ < dataIn_.size()) {
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
        // gates its 16-byte bulk read on bit4 = "≥16 bytes ready", $408D1FAC) —
        // but ONLY once a DATA-phase CI_XFER has actually fetched bytes into the
        // FIFO (dataXfer_). Before that (right after the SELECT, CDB already
        // drained) the physical FIFO is empty, so report 0; else the OS 8.1 SCSI
        // Manager's post-select check ($0011ADD4) sees phantom residue and
        // routes the whole read to its DISCARD engine (Q6.5d dsBadPatch).
        case R_FLAGS: {
            uint32_t cnt = (phase_ == DATA_IN)
                ? (dataXfer_ ? std::min<size_t>(dataIn_.size() - dataInPos_, 16) : 0u)
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
            // Enable selection/reselection: MAME just command_pop_and_chain()
            // — NO interrupt (ncr53c90.cpp:841). The Mac OS 8 SCSI Manager
            // issues this to arm reselection handling after an async command;
            // our old spurious I_FUNCTION mis-sequenced its interrupt-driven
            // completion wait (Q6.5c). Chain silently.
            break;
        case CD_DISABLE_SEL:
            // Disable selection/reselection: function_complete() (I_FUNCTION)
            // then chain (ncr53c90.cpp:845).
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
    if (expected && int(cmd_.size()) >= expected) {
        // Whole CDB is in — run it and set up the resulting phase.
        // seq_step 4 = command fully transferred; select completes with
        // I_BUS | I_FUNCTION (ncr53c90.cpp function_bus_complete via
        // DISC_SEL_WAIT_REQ phase change, :544-556).
        seq_ = 4;
        runTarget();
        raiseIrq(I_BUS | I_FUNCTION);
        updateDrq();
        return;
    }
    // CDB not fully loaded yet: stay in COMMAND and WAIT for it — with NO
    // interrupt (MAME DISC_SEL_ARBITRATION, ncr53c90.cpp:500-510: empty FIFO
    // → `seq = 1; check_drq(); break;`). For the DMA select variant ($C1 =
    // CMD_DMA|CD_SELECT — both the ROM polled driver and the Mac OS 8 async
    // SCSI SIM use it) the chip raises DRQ to fetch the CDB: the SIM's
    // select handler POLLS that DRQ through pseudo-VIA2 IFR bit 0 and only
    // arms its ASYNC continuation (trampoline install at ctx+$F0) when it
    // sees the line — our old instant I_BUS|I_FUNCTION made it take the
    // never-exercised sync path whose later "service interrupt" message
    // jumped through the never-installed continuation (Q6.5b crash). The
    // completion IRQ now fires when the CDB lands (fifoPush/dmaWrite →
    // runTarget) — see selCdbWait_.
    phase_ = COMMAND;
    seq_ = 1;
    selCdbWait_ = true;
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
    if (!dataIn_.empty()) { phase_ = DATA_IN; dataInPos_ = 0; dataXfer_ = false; }
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
            if (dataInPos_ < dataIn_.size()) {
                dataXfer_ = true;         // FIFO now holds fetched payload (R_FLAGS)
                status_ |= S_TC0;
                // A completed Transfer Info raises the bus-service interrupt
                // (ncr53c90.cpp:686 bus_complete). This fires for BOTH the DMA
                // variant ($90 — the driver bursts the window then waits on
                // Status bit7 at $408D1FC0/$408D2352) AND the polled variant
                // ($10 — Q6.3: the Mac OS 8.1 multi-block read's byte-tail
                // loop at $408D237A issues CI_XFER $10 with TC=1, waits for
                // S_INTERRUPT, then reads ONE byte from the FIFO port
                // ($408D2388 move.b ($20,A3),(A2)+)). Without the polled
                // signal the byte-tail wait ($40899704) spun forever.
                // Deferred (Q6.5b): held back by the bus-service latency.
                raiseIrqDeferred(I_BUS);
                // Q6.6 — polled Transfer Info ($10, non-DMA) that drains the
                // last of the payload: the target ends the DATA phase and
                // switches to STATUS as part of THIS command. Reflect that
                // phase change now, at command time — before the driver's
                // per-byte interrupt service ($0011E616 → $0011E686, called
                // right after the CI_XFER write and BEFORE the FIFO byte read)
                // — so the polled service sees STATUS and finalizes the
                // command. Otherwise (advancing only on the FIFO read) the
                // service always saw DATA_IN and the completion flag was never
                // set: the OS 8.1 SCSI-Manager bus-scan INQUIRY discard loop
                // ($0011B2EE) drained all 36 bytes then spun in its sequencer
                // ($00121Axx/$00122Dxx) forever. The FIFO-port read below still
                // drains the staged bytes (it keys on dataInPos_, not phase).
                // DMA ($90) is unaffected — dmaRead() drives its own STATUS
                // transition when the pseudo-DMA window empties the payload.
                if (!dmaCommand_ &&
                    dataInPos_ + (tcounter_ ? tcounter_ : 1) >= dataIn_.size())
                    advanceToStatus();
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
            raiseIrqDeferred(I_BUS);
            break;

        case STATUS:
        case MSG_IN:
            raiseIrqDeferred(I_BUS);
            break;

        default:
            raiseIrqDeferred(I_BUS);
            break;
    }
}

// ── Pseudo-DMA data port (Q605 PrimeTime + $10100) ──────────────────────
uint8_t Ncr53c96::dmaRead() {
    if (phase_ == DATA_IN && dataInPos_ < dataIn_.size()) {
        dmaBytes++;
        uint8_t b = dataIn_[dataInPos_++];
        if (tcounter_) tcounter_--;
        // Q6.3: the DMA Transfer Info moves exactly the programmed transfer
        // count (TC) per command, then raises the bus-service interrupt
        // (ncr53c90.cpp bus_complete, S_TC0). The Mac OS 8.1 driver's
        // multi-block read ($408D22C6) programs TC = one chunk, DMA-bursts
        // TC bytes from the pseudo-DMA window, then waits for S_INTERRUPT
        // ($40899704) before setting up the next chunk. Signal per-chunk
        // completion when TC hits 0 — NOT only when the whole payload is
        // drained (that stalled multi-block reads: dataIn_ held all 56
        // blocks, so the first-chunk interrupt never re-armed). Advance to
        // STATUS only once the last byte of the payload has left.
        if (tcounter_ == 0 || dataInPos_ >= dataIn_.size()) {
            status_ |= S_TC0;
            if (dataInPos_ >= dataIn_.size())
                advanceToStatus();            // whole payload done → STATUS
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
        // Q6.6: per-chunk completion, mirroring dmaRead (Q6.3). The OS 8.1
        // SCSI Manager writes a multi-block payload block-by-block: it programs
        // TC = one block ($200), DMA-bursts that many bytes through the window,
        // then waits for S_INTERRUPT (I_BUS + S_TC0) before setting up the next
        // chunk. Signal bus-service when the programmed count (tcounter_) drains
        // — NOT only when the whole payload (dataOutExpected_) is gathered: a
        // 2+-block WRITE (e.g. WRITE(10) LBA $8A1, 2 blocks) otherwise stalls
        // after the first 512 bytes (the chunk interrupt never re-arms) → the
        // async write never completes → the SCSI Manager spins on ioResult in
        // its deferred-task loop ($00123BA6/$0011CD2C) forever. Commit the WRITE
        // to the target and advance to STATUS only once the last byte lands.
        if (tcounter_ == 0 || dataOut_.size() >= dataOutExpected_) {
            status_ |= S_TC0;
            if (dataOut_.size() >= dataOutExpected_)
                advanceToStatus();            // whole payload gathered → commit WRITE
            raiseIrq(I_BUS);                  // bus_complete (ncr53c90.cpp:686)
        }
        updateDrq();
        return;
    }
    fifoPush(v);
}
