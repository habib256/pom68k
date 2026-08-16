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
#include <algorithm>
#include <cstring>

// ── FIFO ────────────────────────────────────────────────────────────────
void Ncr53c96::acceptDataOutByte_(uint8_t v) {
    // Shared DATA OUT gather for dmaWrite (pseudo-DMA window) and fifoPush
    // (polled R_FIFO). Ends the active Transfer Info with I_BUS when the
    // programmed count drains or the whole write payload is gathered
    // (ncr53c90.cpp:686 bus_complete / INIT_XFR_BUS_COMPLETE).
    dataOut_.push_back(v);
    if (tcounter_) {
        tcounter_--;
        // #41 — S_TC0 is strictly "DMA transfer counter reached zero": MAME's
        // decrement_tcounter early-outs on !dma_command and latches S_TC0
        // only at tcounter==0 (ncr53c90.cpp:1234-1251). The non-DMA tcounter
        // reload (7.5.5 HAL deviation, docs/LLE_VS_HLE.md §1.5) keeps the
        // I_BUS completion trigger below but must not forge the flag.
        if (dmaCommand_ && tcounter_ == 0) status_ |= S_TC0;
    }
    // A defect-list header carries the real length of what follows it, so the
    // gather target can grow once the first four bytes are in (FORMAT UNIT /
    // REASSIGN BLOCKS — ScsiDisk::extendDataOut).
    if (disk_ && !cmd_.empty())
        dataOutExpected_ = disk_->extendDataOut(cmd_.data(), int(cmd_.size()),
                                                dataOut_, dataOutExpected_);
    if (tcounter_ == 0 || dataOut_.size() >= dataOutExpected_) {
        if (dataOut_.size() >= dataOutExpected_)
            advanceToStatus();
        raiseIrq(I_BUS);
    }
    updateDrq();
}

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
            // Arbitration ran when the SELECT was issued (before the driver
            // streamed the CDB), so only the CDB bytes' bus time remains.
            if (wasSelWait)
                raiseIrqDeferred(I_BUS | I_FUNCTION, xferDelayCpu_(uint32_t(cmd_.size())));
        }
        updateDrq();
        return;
    }
    // Polled DATA OUT (CI_XFER $10): the System 7.5.5 SCSI Manager 4.3 HAL
    // feeds write payloads through R_FIFO, not the PrimeTime pseudo-DMA
    // window. Mirror dmaWrite so the gather + bus_complete path matches
    // (without this, WRITE(10) hangs the async wait — Q605 × 7.5.5/7.6).
    if (phase_ == DATA_OUT && dataXfer_) {
        acceptDataOutByte_(v);
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
void Ncr53c96::raiseIrq(uint8_t bits) {
    // A live event absorbs a scheduled one for the same cause. MAME's
    // bus_complete fires once — when the transfer counter is exhausted AND
    // the FIFO has drained (ncr53c90.cpp:672-692); our buffered model splits
    // that into a scheduled "bytes fetched" deferral (transferInfo) and an
    // instant drain-completion (dmaRead / R_FIFO / acceptDataOutByte_). When
    // the CPU out-drains the schedule the instant event supersedes the
    // pending one — without this the stale deferral fired AFTER the driver
    // had already consumed the completion, injecting a phantom bus-service
    // interrupt into the Mac OS 8.1 async SIM's ISR (boot wedged in
    // timeout/retry, q605_boot_etalon red at 2416 SCSI commands / 5 G).
    pendBits_ &= ~bits;
    if (!pendBits_) pendDelay_ = 0;
    istatus_ |= bits;
    irq_ = istatus_ != 0;
}

// ── MAME-derived delay model (LLE step 9, docs/LLE_VS_HLE.md §5.9) ──
// The 53C96 sequencer schedules every step on its own clock: delay(n) costs
// n × clock-conversion-factor SCSI clocks, delay_cycles(n) costs n raw clocks
// (ncr53c90.cpp:802-810). The Q605 clocks the chip at 40 MHz
// (macquadra605.cpp:202, 40_MHz_XTAL) against the 25 MHz CPU, so
// cpuCycles = scsiClocks × 25/40 = ×5/8 (round up — a partial clock still
// holds the line).
int Ncr53c96::scsiClocksToCpu_(int clocks) const {
    return (clocks * 5 + 7) / 8;
}

// Selection with ATN, arbitration through the last CDB byte
// (ncr53c90.cpp:336-460 + send_byte): arbitrate() delay(11), ARB_COMPLETE
// delay(6), ARB_ASSERT_SEL delay_cycles(4), ARB_SET_DEST delay(2),
// ARB_RELEASE_BUSY delay_cycles(2) = 19×conv + 6 clocks of bus setup, then
// each outgoing byte (IDENTIFY message + CDB) costs sync_period clocks
// (SEND path, delay_cycles(sync_period) :762). conv 0 encodes 8
// (ncr53c90.cpp:804).
int Ncr53c96::selectionDelayCpu_(int bytes) const {
    const int conv = clockConv_ ? clockConv_ : 8;
    const int per  = syncPeriod_ ? syncPeriod_ : 5;
    return scsiClocksToCpu_(19 * conv + 6 + bytes * per);
}

// Transfer Information: sync_period clocks per byte moved off/onto the bus
// (RECV_WAIT_REQ_1 → delay_cycles(sync_period) :462, send path :762) plus a
// 2-clock settle for the closing REQ/ACK turnaround.
int Ncr53c96::xferDelayCpu_(uint32_t bytes) const {
    const int per = syncPeriod_ ? syncPeriod_ : 5;
    return scsiClocksToCpu_(int(bytes) * per + 2);
}

// Bus-service latency (Q6.5b → step 9): hold the interrupt back — the time
// the real sequencer needs — so software that polls right after issuing a
// command does not see an instant completion. tick() applies the held bits.
// latency_ = 0 → identical to raiseIrq (unit-test default); > 0 → flat
// override (POM68K_SCSI_LAT); < 0 → the call site's MAME-derived cost.
void Ncr53c96::raiseIrqDeferred(uint8_t bits, int modelCycles) {
    const int d = latency_ > 0 ? latency_ : (latency_ < 0 ? modelCycles : 0);
    if (d <= 0) { raiseIrq(bits); return; }
    pendBits_ |= bits;
    pendDelay_ = d;
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
        // Key the read-payload DRQ on dataInPos_ rather than phase_==DATA_IN:
        // the last DMA Transfer Info pre-advances phase_ to STATUS (Q6.6b) while
        // buffered payload is still being drained through the pseudo-DMA window,
        // and DRQ must stay asserted until the CPU has emptied it (else
        // scsiDmaRead_ raises a spurious /BERR).
        if (dataInPos_ < dataIn_.size() && (phase_ == DATA_IN || phase_ == STATUS))
            d = true;
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
// DATA OUT sizing belongs to the target — see ScsiDisk::writeByteCount.
// What this controller kept locally was WRITE(6)/(10) and nothing else, so
// MODE SELECT and FORMAT UNIT with a parameter list hung the bus on every
// 53C96 machine while working on the Plus.
int Ncr53c96::writeByteCount(const std::vector<uint8_t>& cdb) const {
    if (!disk_ || cdb.empty()) return 0;
    return disk_->writeByteCount(cdb.data(), int(cdb.size()));
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
    // reset_disconnect (ncr53c90.cpp:275-282): the command queue dies too.
    cmdQueuePos_ = 0; cmdQueue_[0] = cmdQueue_[1] = 0;
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
                if (tcounter_) {
                    tcounter_--;
                    // #41 — S_TC0 latches only when a DMA command's counter
                    // hits zero (decrement_tcounter, ncr53c90.cpp:1234-1251).
                    // This port DOES drain DMA-variant chunks on PIO-mode
                    // devices (Q6.6b), so the flag stays honest for them; a
                    // polled ($10) drain never sets it.
                    if (dmaCommand_ && tcounter_ == 0) status_ |= S_TC0;
                }
                if (dataInPos_ >= dataIn_.size()) {
                    advanceToStatus(); raiseIrq(I_BUS);
                }
                updateDrq();
            } else {
                v = fifoPos_ ? fifoPop() : 0;
            }
            break;
        case R_COMMAND: v = lastCmd; break;
        // 53c90a status_r (ncr53c90.cpp:1288-1296): S_INTERRUPT | latched |
        // phase — and, with an interrupt pending, the READ itself drops the
        // three sticky error bits (the same set the ISTAT read clears).
        //
        // Audit § 2.7 re-check (2026-08-06), ALIGNED. The audit filed this
        // side effect as cosmetic on the premise that S_GROSS_ERROR /
        // S_PARITY / S_TCC are never raised. That premise is now STALE:
        // wave 1 gave S_GROSS_ERROR a real producer — a third command write
        // onto the 2-deep queue (commandWrite, ncr53c90.cpp:890-893) — so the
        // divergence became reachable, and two STATUS reads with an interrupt
        // pending would keep reporting an error MAME had already dropped.
        // Modelled rather than re-documented: it is provably inert on every
        // boot path (all three bits are 0 there, so the mask clears nothing),
        // and S_TC0 is deliberately OUTSIDE the mask — MAME clears that one
        // only in load_tcounter (:922-925). S_PARITY/S_TCC still have no
        // producer here (no parity model, no target-mode terminal count).
        case R_STATUS:
            v = (irq_ ? S_INTERRUPT : 0) | (status_ & (S_TC0 | S_PARITY | S_GROSS_ERROR))
                | phaseStatusBits();
            if (irq_) status_ &= uint8_t(~(S_GROSS_ERROR | S_PARITY | S_TCC));
            break;
        // Reading istatus clears the interrupt (ncr53c90.cpp:1103-1122).
        case R_ISTAT:
            v = istatus_;
            if (irq_) {
                status_ &= ~(S_GROSS_ERROR | S_PARITY | S_TCC);
                istatus_ = 0; seq_ = 0; irq_ = false;
            }
            // A non-zero interrupt status pops the finished command off the
            // queue and starts the one waiting behind it (ncr53c90.cpp:
            // 1116-1117 istatus_r → command_pop_and_chain). A read that
            // returns 0 (deferred IRQ still pending) pops nothing.
            if (v) commandPopAndChain();
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
            // Q6.6b — key the pending-payload count on dataInPos_, not
            // phase_==DATA_IN: the final DMA Transfer Info pre-advances phase_
            // to STATUS while the ROM's pseudo-DMA read loop ($408D1FAC
            // btst #4,($70,A3)) is still draining and gates its 16-byte bursts
            // on FLAGS bit4 (≥16 ready). Reporting min(remaining,16) as long as
            // bytes remain keeps that loop draining after the pre-advance; once
            // the payload is drained it falls back to the real FIFO count.
            uint32_t cnt = (dataXfer_ && dataInPos_ < dataIn_.size())
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
        case R_COMMAND: commandWrite(v); break;
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

// ── Command queue (ncr53c90.cpp:886-916 command_w / command_pop_and_chain) ─
// The chip holds the executing command in slot 0 and queues ONE more behind
// it; a third write sets S_GROSS_ERROR and is dropped — a status flag only,
// no interrupt (ncr53c90.cpp:890-893: check_irq() with istatus unchanged).
// RESET / RESET BUS jump the queue and execute at once (:895-900).
void Ncr53c96::commandWrite(uint8_t c) {
    if (cmdQueuePos_ == 2) {
        status_ |= S_GROSS_ERROR;
        return;
    }
    uint8_t op = c & 0x7F;
    if (op == CM_RESET || op == CM_RESET_BUS) cmdQueuePos_ = 0;
    cmdQueue_[cmdQueuePos_++] = c;
    if (cmdQueuePos_ == 1) startCommand(c);
}

// Pop the finished command; if one is queued behind it, start it now
// (ncr53c90.cpp:905-914). Called from the interrupt-status read and from
// the commands that complete without an interrupt.
void Ncr53c96::commandPopAndChain() {
    if (cmdQueuePos_) {
        cmdQueuePos_--;
        if (cmdQueuePos_) {
            cmdQueue_[0] = cmdQueue_[1];
            startCommand(cmdQueue_[0]);
        }
    }
}

// #40 — ncr53c90a_device::check_valid_command (ncr53c90.cpp:1298-1308 — the
// 53c94/96 inherit it, no override) with the functional model's mode mapping:
// MODE_I = connected as the initiator (a target is selected and the bus is
// not free), MODE_D = disconnected. MODE_T never arises — this model is
// initiator-only (docs/LLE_VS_HLE.md §1.5), so the target group always fails.
bool Ncr53c96::checkValidCommand_(uint8_t op) const {
    const bool modeI = disk_ != nullptr && phase_ != BUS_FREE;
    const int subcmd = op & 15;
    switch ((op >> 4) & 7) {
        case 0: return subcmd <= 3;              // misc group, any mode
        case 4: return !modeI && subcmd <= 6;    // disconnected group (MODE_D)
        case 1: return modeI && (subcmd <= 2 || subcmd == 8
                                 || subcmd == 10 || subcmd == 11); // initiator
        default: return false;                   // target group needs MODE_T
    }
}

// ── Command dispatch (ncr53c90.cpp:927 start_command) ───────────────────
void Ncr53c96::startCommand(uint8_t c) {
    lastCmd = c;
    uint8_t op = c & 0x7F;
    // #40 — mode/command validation (start_command, ncr53c90.cpp:928-935):
    // an invalid command latches I_ILLEGAL at once and executes nothing —
    // no dma flag, no tcounter reload — and it stays in queue slot 0 until
    // the interrupt-status read pops it (istatus_r → command_pop_and_chain).
    if (!checkValidCommand_(op)) {
        raiseIrq(I_ILLEGAL);
        return;
    }
    dmaCommand_ = (c & CMD_DMA) != 0;
    if (dmaCommand_) { tcounter_ = tcount_ ? tcount_ : 0x10000; status_ &= ~S_TC0; }
    else             { tcounter_ = 0; }

    switch (op) {
        case CM_NOP:
            commandPopAndChain();             // ncr53c90.cpp:949-952
            break;

        case CM_FLUSH_FIFO:
            fifoPos_ = 0; updateDrq();
            commandPopAndChain();             // ncr53c90.cpp:954-958
            break;

        case CM_RESET:
            reset();                          // clears the queue too (:279)
            break;

        case CM_RESET_BUS:
            // ncr53c90.cpp:965-969 + BUSRESET_WAIT_INT :324-334. S_RST resets
            // every target: the in-flight session buffers die with the bus
            // (a stale dataIn_ residue otherwise ghosts into the next
            // transaction's FIFO reads), the aborted command's scheduled
            // completion must not fire, and reset_disconnect() (:275-282)
            // drops the command queue. The chip FIFO itself is NOT flushed —
            // only CM_RESET's device_reset does that.
            phase_ = BUS_FREE; disk_ = nullptr;
            cmd_.clear(); dataIn_.clear(); dataInPos_ = 0;
            dataOut_.clear(); dataOutExpected_ = 0;
            targetStatus_ = 0; msgInLeft_ = 0;
            selCdbWait_ = false; dataXfer_ = false;
            pendBits_ = 0; pendDelay_ = 0;
            cmdQueuePos_ = 0;
            updateDrq();
            // I_SCSI_RESET unless config bit 6 masks it, after the bus-settle
            // delay (:968 delay(130) — 130 × clock-conversion SCSI clocks).
            if (!(config1_ & 0x40))
                raiseIrqDeferred(I_SCSI_RESET,
                    scsiClocksToCpu_(130 * (clockConv_ ? clockConv_ : 8)));
            break;

        case CD_SELECT:      selectTarget(false); break;
        case CD_SELECT_ATN:  selectTarget(true); break;
        case CD_SELECT_ATN_STOP:
            // Select-with-ATN-and-STOP halts after the single MSG OUT byte,
            // leaving the CDB in the FIFO for a later Transfer Information —
            // that is the window a driver uses to inject an SDTR before the
            // command goes out. Routing it into plain SELECT_ATN drained the
            // FIFO as the CDB and executed the command first, reporting
            // seq_step 4 where the chip reports 2 (ncr53c90.cpp:535-542).
            selectTarget(true, /*stopAfterMsg=*/true);
            break;

        case CD_ENABLE_SEL:
            // Enable selection/reselection: MAME just command_pop_and_chain()
            // — NO interrupt (ncr53c90.cpp:989-992). The Mac OS 8 SCSI Manager
            // issues this to arm reselection handling after an async command;
            // our old spurious I_FUNCTION mis-sequenced its interrupt-driven
            // completion wait (Q6.5c). Chain silently.
            commandPopAndChain();
            break;
        case CD_DISABLE_SEL:
            // Disable selection/reselection: function_complete() (I_FUNCTION)
            // then chain (ncr53c90.cpp:996-1000).
            raiseIrq(I_FUNCTION);
            commandPopAndChain();
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

        case CI_PAD: {
            // Transfer Pad moves filler bytes until the transfer counter
            // exhausts — function_complete(), I_FUNCTION with S_TC0 latched
            // (ncr53c90.cpp:707-714 INIT_XFR_SEND_PAD / :729-737
            // INIT_XFR_RECV_PAD) — or until the target changes phase first —
            // bus_complete(), I_BUS, and the command queue is dropped
            // (:699 / :721 `command_pos = 0`).
            bool tcHit = false;
            if (phase_ == DATA_OUT) {
                // Send pad: zeros go out to the target (send_byte drives 0x00
                // when padding, ncr53c90.cpp:749-757) until TC0 or the
                // expected write payload completes (= the phase change).
                while (dataOut_.size() < dataOutExpected_) {
                    dataOut_.push_back(0);
                    if (tcounter_ && --tcounter_ == 0) { tcHit = true; break; }
                }
                if (dataOut_.size() >= dataOutExpected_)
                    advanceToStatus();        // padded write executes → STATUS
            } else {
                // Receive pad DISCARDS bytes off the bus. A bare interrupt
                // left the DATA-IN residue live, and the next CI_COMPLETE
                // latches STATUS into the FIFO without clearing it — so
                // read(R_FIFO), which keys on residue rather than phase,
                // returned stale block data as the SCSI status byte.
                size_t left = dataIn_.size() - dataInPos_;
                size_t n = tcounter_ ? std::min<size_t>(tcounter_, left) : left;
                dataInPos_ += n;
                if (tcounter_) {
                    tcounter_ -= uint32_t(n);
                    tcHit = tcounter_ == 0;
                }
                if (dataInPos_ >= dataIn_.size() && phase_ == DATA_IN)
                    advanceToStatus();
            }
            if (tcHit) {                      // TC exhausted first
                status_ |= S_TC0;
                raiseIrq(I_FUNCTION);         // function_complete (:713/:736)
            } else {                          // target changed phase first
                cmdQueuePos_ = 0;             // :699/:721 command_pos = 0
                raiseIrq(I_BUS);              // bus_complete
            }
            updateDrq();
            break;
        }

        case CI_SET_ATN:
        case CI_RESET_ATN:
            // ATN line: no functional effect; chains without an interrupt
            // (ncr53c90.cpp:1042-1052).
            commandPopAndChain();
            break;

        default:
            // Valid per check_valid_command but unimplemented here
            // (CD_RESELECT / CD_SELECT_ATN3 — the model is initiator-only,
            // docs/LLE_VS_HLE.md §1.5) → illegal-command interrupt.
            raiseIrq(I_ILLEGAL);
            break;
    }
}

// ── Selection ───────────────────────────────────────────────────────────
// Arbitrate + select the destination (busId_). If ATN, the FIFO's first byte
// is the IDENTIFY message (consumed as MSG OUT); the rest is the CDB, drained
// in the COMMAND phase. Ends I_BUS|I_FUNCTION, seq_step=4 (whole CDB sent).
void Ncr53c96::selectTarget(bool withAtn, bool stopAfterMsg) {
    selects++;
    seq_ = 0;
    ScsiTarget* t = (busId_ >= 0 && busId_ < 7) ? targets_[busId_] : nullptr;
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
    int wireBytes = 0;                        // bytes clocked onto the bus
    if (withAtn && fifoPos_ > 0) {
        // Consume the IDENTIFY message byte (msg bytes have bit7 or are 0x80+).
        (void)fifoPop();                      // IDENTIFY (LUN select) — ignored
        seq_ = 2;                             // one MSG OUT byte sent
        wireBytes++;
    }
    if (stopAfterMsg) {
        // Stop here: the CDB stays in the FIFO and arrives through the normal
        // COMMAND path (fifoPush/dmaWrite -> runTarget) once the driver has
        // done whatever it stopped for.
        phase_ = COMMAND;
        seq_ = 2;
        selCdbWait_ = true;
        raiseIrqDeferred(I_BUS | I_FUNCTION, selectionDelayCpu_(wireBytes));
        updateDrq();
        return;
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
        // DISC_SEL_WAIT_REQ phase change, :544-556). The interrupt arrives
        // only after the arbitrate/assert/settle chain + the CDB bytes'
        // bus time (step 9 delay model).
        seq_ = 4;
        runTarget();
        raiseIrqDeferred(I_BUS | I_FUNCTION,
                         selectionDelayCpu_(wireBytes + int(cmd_.size())));
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
    // Clear the PREVIOUS command's read payload up front: the WRITE branch
    // below returns early, and read(R_FIFO)/read(R_FLAGS)/dmaRead() all key on
    // dataInPos_ < dataIn_.size() rather than the phase, so stale bytes leaked
    // into the next transaction and shadowed its STATUS.
    dataIn_.clear(); dataInPos_ = 0; dataXfer_ = false;
    int wbytes = writeByteCount(cmd_);
    if (wbytes > 0) {                         // WRITE: gather DATA OUT first
        phase_ = DATA_OUT; dataOut_.clear(); dataOutExpected_ = size_t(wbytes);
        return;
    }
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
    // No S_TC0 here (#41): the flag belongs to the DMA transfer counter alone
    // (MAME decrement_tcounter, ncr53c90.cpp:1234-1251) — the drain/gather
    // sites latch it when tcounter reaches zero on a DMA command.
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
                // Bytes this Transfer Info moves off the bus — the chunk the
                // step 9 delay model charges at sync_period clocks each. A
                // DMA variant moves tcounter_; a polled (non-DMA) one moves
                // ONE byte — MAME's non-DMA IN raises bus_complete per byte
                // (INIT_XFR_WAIT_REQ, ncr53c90.cpp:652 `fifo_pos == 1`), and
                // startCommand cleared tcounter_ for non-DMA. Charging the
                // whole remainder here armed a ~75 ms deferral per polled
                // byte-tail XFER and wedged the OS 8.1 boot in a retry loop.
                const uint32_t remaining = uint32_t(dataIn_.size() - dataInPos_);
                const uint32_t chunk =
                    tcounter_ ? std::min<uint32_t>(tcounter_, remaining) : 1;
                // #41 — S_TC0 strictly tracks the DMA transfer counter (MAME
                // decrement_tcounter early-outs on !dma_command and sets the
                // flag only at tcounter==0, ncr53c90.cpp:1234-1251). The
                // buffered model pre-announces it here — at Transfer Info time
                // the bus side has already fetched the chunk, and the OS 8.1
                // driver polls S_TC0 BEFORE bulk-reading ($408D1F7C →
                // $408D1FA2) — but only when the programmed count is fully
                // consumable: a short chunk (the target changes phase before
                // TC reaches 0) or a polled $10 must NOT set it.
                if (dmaCommand_ && remaining >= tcounter_)
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
                raiseIrqDeferred(I_BUS, xferDelayCpu_(chunk));
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
                //
                // Q6.6b — this MUST also fire for the DMA variant ($90). A
                // whole-block multi-block READ (e.g. READ(10) $49E21, 22 blocks
                // = 11264 B) is drained by the OS 8.1 SCSI Manager in a
                // "3 PIO bytes + a TC=509 DMA chunk" pattern per 512-byte block
                // ($0011B7CE/$0011B89A). Because the payload ends exactly on a
                // block boundary, the FINAL Transfer Info of the transaction is
                // that DMA chunk ($90) — and on a PIO-mode device (select $42,
                // no pseudo-DMA window) it is drained through the register FIFO
                // port (reg2), NOT dmaRead(). So dmaRead() never drives the
                // STATUS transition; only the last R_FIFO byte does, AFTER the
                // per-CI_XFER completion service ($0011E686) has already read
                // reg4 and seen DATA_IN → the SIM data-complete flag ($5e,A3
                // bit4) never sets and the sequencer ($00122A78, state $d) spins
                // forever. Pre-advancing here (bytes considered moved off the
                // bus at the Transfer Info, as on real hardware) lets that
                // service see STATUS. dmaRead()/updateDrq() key on dataInPos_
                // (not the phase) so a genuine pseudo-DMA-window drain still
                // works after the pre-advance.
                if (dataInPos_ + (tcounter_ ? tcounter_ : 1) >= dataIn_.size())
                    advanceToStatus();
            }
            updateDrq();
            break;

        case DATA_OUT:
            // Arm the gather path. DMA ($90) drains through dmaWrite; polled
            // ($10) drains through R_FIFO → fifoPush. startCommand clears
            // tcounter_ for non-DMA, so re-load from tcount_ (or the remaining
            // payload) here — otherwise fifoPush never sees a completion count
            // and the 7.5.5 HAL's WRITE(10) spins forever waiting for I_BUS.
            dataXfer_ = true;
            if (!dmaCommand_) {
                // ── Non-DMA: the FIFO is the budget, not the DMA count ───
                // The transfer counter is a DMA register (MAME's
                // decrement_tcounter early-outs on !dma_command), so a
                // polled Transfer Info moves what the FIFO holds and ends
                // there — the driver refills and issues another. Reading a
                // STALE tcount_ instead is what hung Mac OS 8.1's CD driver
                // on its 28-byte MODE SELECT: it preloaded 12 bytes, issued
                // $10, and waited on R_STATUS for a completion the chip
                // owed after those 12 — while this counted down from a 16
                // left over from an earlier command and waited for four
                // bytes the driver had no reason to send. Both sides
                // waiting, ~600 R_STATUS reads a frame and nothing else,
                // for the rest of the run.
                // An EMPTY FIFO keeps the old fallback, and that is the
                // 7.5.5 SCSI Manager HAL's shape (arm first, then stream
                // the payload through R_FIFO — docs/LLE_VS_HLE.md § 1.5):
                // there is no preload to size the transfer by.
                const uint32_t remain = dataOutExpected_ > dataOut_.size()
                    ? uint32_t(dataOutExpected_ - dataOut_.size()) : 0;
                tcounter_ = fifoPos_ ? uint32_t(fifoPos_)
                                     : (tcount_ ? tcount_ : remain);
                status_ &= ~S_TC0;
            }
            // ── The FIFO is the path to the bus, not a side pocket ───────
            // A driver may PRELOAD the payload's first bytes into the FIFO
            // and then program a transfer count covering the whole thing,
            // those bytes included: the chip streams the FIFO out first and
            // DMA refills it behind (ncr53c90.cpp DATA OUT drains m_fifo
            // before fetching). Here they were landing in `fifo_` and
            // STAYING there, because fifoPush's payload branch needs
            // `dataXfer_`, which only this command sets.
            // Mac OS 8.1's CD driver does exactly that on the MODE SELECT it
            // issues while adopting a disc: CDB `15 00 00 00 08 00`, two
            // parameter bytes pushed to the FIFO, then `$90` with a count of
            // 6 and four more bytes through the pseudo-DMA window. The two
            // vanished, the chip's counter stopped two short of the
            // driver's, no I_BUS ever came, and the machine sat polling
            // R_STATUS and R_FLAGS forever — 1333/667 reads and not one
            // further CDB. That is the CD gates' "no Finder", and it is NOT
            // a CD-ROM defect: it needs a target that asks for a parameter
            // list at all, which is why it appeared the day MODE SELECT
            // stopped being answered with an empty one (a355561,
            // 2026-08-08) and why bisecting the gate landed there.
            if (fifoPos_ > 0) {
                uint8_t pre[16];
                const int n = fifoPos_ < 16 ? fifoPos_ : 16;
                std::memcpy(pre, fifo_, size_t(n));
                fifoPos_ = 0;
                for (int i = 0; i < n; i++) acceptDataOutByte_(pre[i]);
            }
            updateDrq();
            break;

        case COMMAND: {
            // Continue draining a partial CDB from the FIFO (rare — the ROM
            // usually sends the whole CDB with SELECT_ATN).
            uint32_t sent = 0;
            while (fifoPos_ > 0) {
                uint8_t b = fifoPop();
                cmd_.push_back(b);
                sent++;
                if (int(cmd_.size()) >= cdbLength(cmd_[0])) { runTarget(); break; }
            }
            raiseIrqDeferred(I_BUS, xferDelayCpu_(sent ? sent : 1));
            break;
        }

        case STATUS:
        case MSG_IN:
            raiseIrqDeferred(I_BUS, xferDelayCpu_(1));   // one byte's handshake
            break;

        default:
            raiseIrqDeferred(I_BUS, xferDelayCpu_(1));
            break;
    }
}

// ── Pseudo-DMA data port (Q605 PrimeTime + $10100) ──────────────────────
uint8_t Ncr53c96::dmaRead() {
    // Keyed on dataInPos_ (not phase_==DATA_IN): the final DMA Transfer Info
    // pre-advances phase_ to STATUS (Q6.6b) while buffered payload remains, so
    // the window drain must continue in STATUS phase too.
    if (dataInPos_ < dataIn_.size()) {
        dmaBytes++;
        uint8_t b = dataIn_[dataInPos_++];
        if (tcounter_) {
            tcounter_--;
            // #41 — S_TC0 latches only when the DMA counter reaches zero
            // (decrement_tcounter, ncr53c90.cpp:1234-1251); a payload that
            // ends short (target phase change first) completes with I_BUS
            // alone and leaves the residual count readable in R_TC*.
            if (dmaCommand_ && tcounter_ == 0) status_ |= S_TC0;
        }
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
        // Q6.6: per-chunk completion, mirroring dmaRead (Q6.3). The OS 8.1
        // SCSI Manager writes a multi-block payload block-by-block: it programs
        // TC = one block ($200), DMA-bursts that many bytes through the window,
        // then waits for S_INTERRUPT (I_BUS + S_TC0) before setting up the next
        // chunk. Signal bus-service when the programmed count (tcounter_) drains
        // — NOT only when the whole payload (dataOutExpected_) is gathered.
        dmaBytes++;
        acceptDataOutByte_(v);
        return;
    }
    fifoPush(v);
}
