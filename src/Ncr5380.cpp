// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Register-write-driven phase engine, modelled on pce's macplus/scsi.c and
// cross-checked against MAME ncr5380.cpp bit layouts — see DEV.md § SCSI.
// The Plus SCSI is polled: no interrupt is ever raised. Each register write
// advances the bus phase; reads reflect the live bus. Direct-access targets
// (ScsiDisk) answer at SCSI IDs 0-6; the boot drive sits at ID 0
// (internal-drive convention).

#include "Ncr5380.h"
#include "ScsiDisk.h"

void Ncr5380::reset() {
    odr_ = icr_ = mode_ = tcr_ = selEnable_ = 0;
    phase_ = BUS_FREE;
    req_ = false;
    reqGap_ = false;
    irq_ = false;
    disk_ = nullptr;                 // drop the selected session, keep targets
    cmd_.clear(); dataIn_.clear(); dataPos_ = 0; cmdLen_ = 0; status_ = 0;
}

// SCSI-1 CDB length by operation-code group (top 3 bits of byte 0).
static int cdbLength(uint8_t op) {
    switch (op >> 5) {
        case 0: return 6;
        case 1: case 2: return 10;
        case 5: return 12;
        default: return 6;
    }
}

// Phase MSG/C/D/I/O signals folded into Current-Bus-Status bits.
uint8_t Ncr5380::phaseSignals() const {
    switch (phase_) {
        case COMMAND:  return CBS_CD;                         // C/D
        case DATA_IN:  return CBS_IO;                         // I/O
        case DATA_OUT: return 0;
        case STATUS:   return CBS_CD | CBS_IO;                // C/D + I/O
        case MSG_IN:   return CBS_CD | CBS_IO | CBS_MSG;      // C/D + I/O + MSG
        case MSG_OUT:  return CBS_CD | CBS_MSG;
        default:       return 0;
    }
}

bool Ncr5380::targetPhase() const {
    return phase_ == COMMAND || phase_ == DATA_IN || phase_ == DATA_OUT ||
           phase_ == STATUS || phase_ == MSG_IN;
}

// ── Phase transitions ───────────────────────────────────────────────────
void Ncr5380::enterCommand() {
    // New CDB phase: drop a stale DMA-complete IRQ. Mac II SCSI Manager
    // polls BSR.IRQ to end blind PDMA; a latch left set from the previous
    // command aborts the next READ after one block and spins on PHASE_MATCH
    // (seen at cmds=235, PC=$1E090, 512 of 1536 bytes).
    // DELIBERATE HLE DIVERGENCE (audit #38): on silicon only an RPI read
    // clears the latch (MAME rpi_r, machine/ncr5380.cpp:511-518; sdir_w
    // and selection never touch it). Our phase engine completes a whole
    // transfer inside the guest's polled loop, so the DATA→STATUS mismatch
    // IRQ is latched before the guest's own clearing read lands where it
    // would on real timing. Reopening condition: macii_boot_etalon green
    // with both this line and the write(R_RPI) clear removed.
    irq_ = false;
    phase_ = COMMAND; cmd_.clear(); cmdLen_ = 0; req_ = true;
}
void Ncr5380::enterBusFree() {
    // Bus-free after MSG_IN: do not re-raise IRQ (one edge on DATA→STATUS).
    phase_ = BUS_FREE; req_ = false;
}
void Ncr5380::enterStatus()  {
    // Phase change out of DATA under MODE_DMA → IRQ (MAME phase mismatch).
    if (mode_ & MODE_DMA) irq_ = true;
    phase_ = STATUS;  dataPos_ = 0; req_ = true;
}
void Ncr5380::enterMsgIn()   {
    // Keep the STATUS-time IRQ; do not pulse again on STATUS→MSG.
    phase_ = MSG_IN;  req_ = true;
}

// Expected DATA OUT byte count for the commands that carry one, else 0.
// Dropping a parameter phase and jumping straight to STATUS desynchronizes
// the initiator's handshake (it keeps ACKing bytes the target never takes).
// How many DATA OUT bytes to handshake before the target can act, and how
// to grow that count once a defect-list header has landed: both are the
// TARGET's business (ScsiDisk::writeByteCount / ::extendDataOut). This
// controller used to carry its own partial copy of the table, which is how
// MODE SELECT ended up supported here and not on the 53C96.
void Ncr5380::extendDataOut() {
    if (!disk_ || cmd_.empty()) return;
    dataOutExpected_ = disk_->extendDataOut(cmd_.data(), int(cmd_.size()),
                                            dataOut_, dataOutExpected_);
}

void Ncr5380::execute() {
    commands++; lastCmd = cmd_.empty() ? 0 : cmd_[0];
    if (onCommand) onCommand(cmd_);
    int wbytes = disk_ ? disk_->writeByteCount(cmd_.data(), int(cmd_.size())) : 0;
    if (wbytes > 0) {                                          // WRITE: collect DATA OUT first
        phase_ = DATA_OUT; dataOut_.clear(); dataOutExpected_ = size_t(wbytes);
        req_ = true;
        return;
    }
    dataIn_.clear(); dataPos_ = 0;
    std::vector<uint8_t> none;
    status_ = disk_ ? disk_->command(cmd_.data(), int(cmd_.size()), dataIn_, none) : 0x02;
    if (!dataIn_.empty()) { phase_ = DATA_IN; dataPos_ = 0; req_ = true; }
    else enterStatus();
}

void Ncr5380::finishWrite() {
    std::vector<uint8_t> readback;
    status_ = disk_ ? disk_->command(cmd_.data(), int(cmd_.size()), readback, dataOut_) : 0x02;
    enterStatus();
}

// Selection: the initiator asserts SEL with the target's ID bit on the data
// bus and BSY released. If our disk owns that ID, it grabs the bus.
void Ncr5380::trySelect() {
    uint8_t targets = odr_ & 0x7F;                // exclude the initiator's ID 7
    selects++;
    for (int id = 0; id < 7; id++) {
        if ((targets & (1 << id)) && targets_[id] && targets_[id]->present()) {
            disk_ = targets_[id];                 // device answers → COMMAND
            enterCommand();
            return;
        }
    }
    enterBusFree();                               // selection timeout (no device)
}

// ── ACK handshake (one byte per REQ/ACK cycle) ──────────────────────────
void Ncr5380::ackRising() {
    if (phase_ == COMMAND) {
        cmd_.push_back(odr_);
        if (cmd_.size() == 1) cmdLen_ = cdbLength(cmd_[0]);
    } else if (phase_ == DATA_OUT) {
        dataOut_.push_back(odr_);                  // write phase: byte from ODR
    } else {
        dataPos_++;                               // read phases: byte consumed
    }
    req_ = false;
}

void Ncr5380::ackFalling() {
    switch (phase_) {
        case COMMAND:
            // cmdLen_ == 0 means enterCommand() has not seen a CDB opcode yet:
            // firing execute() on an empty cmd_ passed nullptr + size 0 into
            // ScsiDisk::command(), whose first act is switch (cdb[0]) — a null
            // dereference reachable from three guest register writes.
            if (cmdLen_ > 0 && int(cmd_.size()) >= cmdLen_) execute();
            else req_ = true;
            break;
        case DATA_IN:
            if (dataPos_ >= dataIn_.size()) enterStatus();
            else req_ = true;
            break;
        case DATA_OUT:
            extendDataOut();
            if (dataOut_.size() >= dataOutExpected_) finishWrite();
            else req_ = true;
            break;
        case STATUS:  enterMsgIn(); break;
        case MSG_IN:  enterBusFree(); break;
        default: break;
    }
}

// ── Register interface ──────────────────────────────────────────────────
uint8_t Ncr5380::liveBusStatus() const {
    // Our own RST assertion reads back on the live bus (MAME csstat_r).
    const uint8_t rst = (icr_ & ICR_RST) ? CBS_RST : 0;
    if (phase_ == BUS_FREE) return rst;
    if (phase_ == ARBITRATION || phase_ == SELECTION) return rst | CBS_BSY;
    uint8_t v = rst | CBS_BSY | phaseSignals();   // target holds BSY
    if (req_) v |= CBS_REQ;
    return v;
}

uint8_t Ncr5380::busAndStatus() const {
    uint8_t v = 0;
    if (phaseMatch()) v |= BSR_PHASE;             // PHASE_MATCH (live)
    if (drqActive()) v |= BSR_DRQ;                // DRQ for pseudo-DMA
    if (irq_) v |= BSR_IRQ;                       // latched (phase mismatch)
    if (icr_ & ICR_ACK) v |= BSR_ACK;
    if (icr_ & ICR_ATN) v |= BSR_ATN;
    return v;
}

// TCR-programmed phase vs the live bus phase (5380 datasheet: DMA
// requests stop and the IRQ latch sets when the target changes phase)
//
// Audit § 2.6(c), DOCUMENT-SKIP — deliberate divergence at BUS FREE. MAME
// compares the raw signal lines with no phase-validity guard: `bas_r`
// (machine/ncr5380.cpp:475-486) returns BAS_PHASEMATCH whenever
// `(ctrl & S_PHASE_MASK) == (m_tcmd & TC_PHASE)`, so with the bus idle (all
// of MSG/C-D/I-O deasserted) and a TCR programmed to 0 = DATA OUT, PHASE
// MATCH reads SET even though no target is on the bus. That is faithful to
// the silicon comparator — and it is exactly why we do not copy it: this
// engine reports PHASE MATCH only while a target actually holds the bus
// (`targetPhase()`). The bit is polled on the hot path of every SCSI machine
// we ship (the SCSI Manager's scLoop wait and the Plus's blind PDMA loops
// both spin on BSR bit 3), so flipping its bus-free reading from 0 to 1
// changes an observable on the boot path of eleven platforms to satisfy a
// case no driver programs — TCR is written to the expected transfer phase
// immediately before MODE_DMA, never left at 0 across a bus-free window.
// Zero benefit, non-zero blast radius: kept. Reopening condition: a guest
// found waiting for PHASE MATCH *before* selecting a target.
bool Ncr5380::phaseMatch() const {
    return targetPhase()
        && (phaseSignals() & 0x1C) == ((tcr_ & 0x07) << 2 & 0x1C);
}

uint8_t Ncr5380::read(int reg) {
    reads++;
    if (onAccess) {
        uint8_t v = 0;
        switch (reg) {
            case R_CSR: v = liveBusStatus(); break;
            case R_BSR: v = busAndStatus(); break;
            default: break;
        }
        onAccess(reg, false, v);
    }
    switch (reg) {
        case R_DATA:
            // Current SCSI Data is a transparent bus latch: reading it has
            // NO side effect, MODE_DMA or not (MAME csdata_r, machine/
            // ncr5380.cpp:273-279 — only a DACK access advances a transfer).
            // Every platform decodes the pseudo-DMA aliases in its memory
            // map (e.g. macii.cpp:306-333 word offsets $130/$100) and routes
            // them to dmaRead/dmaWrite, exactly like MAME's scsi_helper.
            return (phase_ == DATA_IN && dataPos_ < dataIn_.size()) ? dataIn_[dataPos_]
                 : (phase_ == STATUS) ? status_
                 : (phase_ == MSG_IN) ? 0x00 : odr_;
        case R_ICR:  return uint8_t(icr_ | (phase_ == ARBITRATION ? ICR_AIP : 0));
        case R_MODE: return mode_;
        case R_TCR:  return tcr_;
        case R_CSR: {
            // One-shot REQ gap after DACK: this read returns REQ clear, then
            // re-arms so the Plus (polls REQ between PDMA bytes) and Mac II
            // scLoop wait ($1E088) both see a falling edge without sticking.
            const uint8_t v = liveBusStatus();
            if (reqGap_) {
                reqGap_ = false;
                if (phase_ == DATA_IN && dataPos_ < dataIn_.size()) req_ = true;
                else if (phase_ == DATA_OUT && dataOut_.size() < dataOutExpected_)
                    req_ = true;
            }
            return v;
        }
        case R_BSR:  return busAndStatus();
        case R_IDR:
            // Input Data reads back side-effect-free (MAME idata_r,
            // machine/ncr5380.cpp:501-506); DACK is the only consuming
            // access and the platform maps route it to dmaRead. The byte
            // currently on the bus stands in for MAME's latched m_idata.
            return (phase_ == DATA_IN && dataPos_ < dataIn_.size()) ? dataIn_[dataPos_] : 0;
        // Reset Parity/Interrupt. MAME's rpi_r also drops BAS_PARITYERROR and
        // BAS_BUSYERROR (machine/ncr5380.cpp:521-529); neither has a producer
        // in either model — no parity line on the bus (§ 2.6(b), Ncr5380.h)
        // and no MONBSY busy-error path (LLE_VS_HLE § 1.5).
        case R_RPI:  irq_ = false; return 0;
    }
    return 0xFF;
}

void Ncr5380::write(int reg, uint8_t v) {
    writes++;
    if (onAccess) onAccess(reg, true, v);
    switch (reg) {
        case R_DATA:
            // ODR is a plain latch on write, MODE_DMA or not (MAME odata_w,
            // machine/ncr5380.cpp:281-290) — the pseudo-DMA write aliases
            // are decoded by the platform maps (macii.cpp scsi_w offset
            // $100 / scsi_drq_w) and routed to dmaWrite.
            odr_ = v;
            break;
        case R_ICR: {
            // Only the low 0x9F is a latch: bits 6/5 are the read-only
            // AIP / LOST-ARBITRATION status, and the write-side bit 6 is the
            // chip's TEST mode (MAME icmd_w, machine/ncr5380.cpp:355 —
            // `m_icmd = (m_icmd & ~IC_WRITE) | (data & IC_WRITE)`, IC_WRITE =
            // 0x9f). Without the mask a guest that wrote 0x40/0x20 read its
            // own bit back as a phantom "arbitration in progress".
            v &= ICR_WRITE;
            uint8_t old = icr_; icr_ = v; uint8_t dif = old ^ v;
            if (v & ICR_RST) {
                // Bus reset is EDGE-triggered and interrupts the initiator
                // itself (MAME machine/ncr5380.cpp:330-355, 449-463): chip
                // reset + IRQ latch on the rising edge only; RST stays
                // readable in ICR and CSR until the initiator releases the
                // bit. The Mac II SCSIReset needs that IRQ edge on VIA2.
                if (!(old & ICR_RST)) { reset(); irq_ = true; }
                icr_ = v;                 // reset() cleared icr_ — re-latch
                break;
            }
            // Selection: SEL asserted + BSY released while the bus is idle.
            if ((v & ICR_SEL) && !(v & ICR_BSY) &&
                (phase_ == BUS_FREE || phase_ == ARBITRATION))
                trySelect();
            if (targetPhase()) {
                if (dif & v & ICR_ACK) ackRising();
                if (dif & ~v & ICR_ACK) ackFalling();
            }
            break;
        }
        case R_MODE: {
            uint8_t old = mode_;
            mode_ = v;
            if ((v & MODE_ARBITRATE) && phase_ == BUS_FREE) phase_ = ARBITRATION;
            // Clearing MODE_ARBITRATE cancels a pending arbitration (MAME
            // machine/ncr5380.cpp:389-405: stop arbitration → IDLE, AIP/LA
            // drop) — without it CSR reads BSY forever and the bus is
            // wedged. AIP is synthesized from phase_ in read(R_ICR).
            if ((old & ~v & MODE_ARBITRATE) && phase_ == ARBITRATION)
                phase_ = BUS_FREE;
            break;
        }
        case R_TCR:  tcr_ = v; break;
        case R_CSR:  selEnable_ = v; break;
        case R_BSR:  // Start DMA Send (initiator DATA OUT) — polled PDMA
        case R_IDR:  // Start DMA Target Receive — unused (we are initiator)
            break;
        case R_RPI:
            // Start DMA Initiator Receive (MAME sdir_w). Arms the IRQ-on-
            // phase-change path; clear any stale latch so the upcoming
            // PDMA loop is not cut short by BSR.IRQ from the last CDB.
            if ((mode_ & MODE_DMA) && !(mode_ & MODE_TARGET))
                irq_ = false;
            break;
    }
}

// Pseudo-DMA: each access auto-handshakes one byte (A9/DACK path).
uint8_t Ncr5380::dmaRead() {
    if (phase_ == DATA_IN) {
        dmaBytes++;
        uint8_t b = dataPos_ < dataIn_.size() ? dataIn_[dataPos_] : 0;
        dataPos_++;
        if (dataPos_ >= dataIn_.size()) {
            enterStatus();
        } else {
            // REQ/ACK: after DACK, REQ falls for one CSR sample (reqGap_),
            // then re-arms on the next CSR read. Mac II SCSIMgr scLoop
            // ($1E088) needs that clear between TIB chunks; the Plus polls
            // REQ between PDMA bytes and needs the re-arm.
            req_ = false;
            reqGap_ = true;
        }
        return b;
    }
    if (phase_ == STATUS) {
        // No DRQ (receive mismatch, MAME machine/ncr5380.cpp:227-245): a
        // stray DACK read must not eat the status byte — return the bus
        // latch without handshaking.
        if (!drqActive()) return status_;
        uint8_t s = status_; enterMsgIn(); return s;
    }
    if (phase_ == MSG_IN) {
        if (!drqActive()) return 0x00;
        enterBusFree(); return 0x00;
    }
    return 0;
}

void Ncr5380::dmaWrite(uint8_t v) {
    if (phase_ == COMMAND) {
        cmd_.push_back(v);
        if (cmd_.size() == 1) cmdLen_ = cdbLength(cmd_[0]);
        if (cmdLen_ > 0 && int(cmd_.size()) >= cmdLen_) execute();
    } else if (phase_ == DATA_OUT) {
        dataOut_.push_back(v);
        extendDataOut();
        if (dataOut_.size() >= dataOutExpected_) finishWrite();
        else { req_ = false; reqGap_ = true; }
    }
}

// DRQ: asserted while MODE_DMA and a byte remains to move. Independent of
// the CSR.REQ gap so blind PDMA can still see BSR.DRQ after ACK.
// Outside the data phases, a phase mismatch is DIRECTIONAL (MAME
// machine/ncr5380.cpp:227-245): after a SEND the already-requested byte
// survives the mismatch — the host cycles DACK once more so ACK can
// release; after a RECEIVE the last byte was already DACKed, so DRQ stays
// low (a spurious raise feeds pseudo-DMA read loops the status byte as
// data). TCR bit 0 = expected I/O, set = receive. A TCR match (programmed
// STATUS/MSG/COMMAND transfer) keeps the normal per-REQ DRQ.
bool Ncr5380::drqActive() const {
    if (!(mode_ & MODE_DMA)) return false;
    if (phase_ == DATA_IN) return dataPos_ < dataIn_.size();
    if (phase_ == DATA_OUT) return dataOut_.size() < dataOutExpected_;
    if (phase_ != STATUS && phase_ != MSG_IN && phase_ != COMMAND) return false;
    return req_ && (phaseMatch() || !(tcr_ & 0x01));
}
