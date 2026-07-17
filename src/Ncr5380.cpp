// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Register-write-driven phase engine, modelled on pce's macplus/scsi.c and
// cross-checked against MAME ncr5380.cpp bit layouts — see DEV.md § SCSI.
// The Plus SCSI is polled: no interrupt is ever raised. Each register write
// advances the bus phase; reads reflect the live bus. One direct-access
// target (ScsiDisk) answers at SCSI ID 0 (internal-drive convention).

#include "Ncr5380.h"
#include "ScsiDisk.h"

namespace { constexpr int kTargetId = 0; }

void Ncr5380::reset() {
    odr_ = icr_ = mode_ = tcr_ = selEnable_ = 0;
    phase_ = BUS_FREE;
    req_ = false;
    irq_ = false;
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
void Ncr5380::enterCommand() { phase_ = COMMAND; cmd_.clear(); cmdLen_ = 0; req_ = true; }
void Ncr5380::enterBusFree() { if (mode_ & MODE_DMA) irq_ = true;
                               phase_ = BUS_FREE; req_ = false; }
void Ncr5380::enterStatus()  { if (mode_ & MODE_DMA) irq_ = true;
                               phase_ = STATUS;  dataPos_ = 0; req_ = true; }
void Ncr5380::enterMsgIn()   { if (mode_ & MODE_DMA) irq_ = true;
                               phase_ = MSG_IN;  req_ = true; }

// Expected DATA OUT byte count for WRITE(6)/WRITE(10), else 0.
int Ncr5380::writeByteCount(const std::vector<uint8_t>& cdb) {
    if (cdb.empty()) return 0;
    if (cdb[0] == 0x0A && cdb.size() >= 5)                     // WRITE(6)
        return (cdb[4] ? cdb[4] : 256) * 512;
    if (cdb[0] == 0x2A && cdb.size() >= 9)                     // WRITE(10)
        return ((cdb[7] << 8) | cdb[8]) * 512;
    return 0;
}

void Ncr5380::execute() {
    commands++; lastCmd = cmd_.empty() ? 0 : cmd_[0];
    if (onCommand) onCommand(cmd_);
    int wbytes = writeByteCount(cmd_);
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
    if (disk_ && disk_->present() && (targets & (1 << kTargetId)))
        enterCommand();                           // device answers → COMMAND
    else
        enterBusFree();                           // selection timeout (no device)
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
            if (int(cmd_.size()) >= cmdLen_) execute();
            else req_ = true;
            break;
        case DATA_IN:
            if (dataPos_ >= dataIn_.size()) enterStatus();
            else req_ = true;
            break;
        case DATA_OUT:
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
    if (phase_ == BUS_FREE) return 0;
    if (phase_ == ARBITRATION || phase_ == SELECTION) return CBS_BSY;
    uint8_t v = CBS_BSY | phaseSignals();         // target holds BSY
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
            return (phase_ == DATA_IN && dataPos_ < dataIn_.size()) ? dataIn_[dataPos_]
                 : (phase_ == STATUS) ? status_
                 : (phase_ == MSG_IN) ? 0x00 : odr_;
        case R_ICR:  return uint8_t(icr_ | (phase_ == ARBITRATION ? ICR_AIP : 0));
        case R_MODE: return mode_;
        case R_TCR:  return tcr_;
        case R_CSR:  return liveBusStatus();
        case R_BSR:  return busAndStatus();
        case R_IDR:  return (phase_ == DATA_IN && dataPos_ < dataIn_.size()) ? dataIn_[dataPos_] : 0;
        case R_RPI:  irq_ = false; return 0;       // reset parity/interrupt
    }
    return 0xFF;
}

void Ncr5380::write(int reg, uint8_t v) {
    writes++;
    if (onAccess) onAccess(reg, true, v);
    switch (reg) {
        case R_DATA: odr_ = v; break;
        case R_ICR: {
            uint8_t old = icr_; icr_ = v; uint8_t dif = old ^ v;
            if (v & ICR_RST) { reset(); break; }
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
        case R_MODE:
            mode_ = v;
            if ((v & MODE_ARBITRATE) && phase_ == BUS_FREE) phase_ = ARBITRATION;
            break;
        case R_TCR:  tcr_ = v; break;
        case R_CSR:  selEnable_ = v; break;
        case R_BSR: case R_IDR: case R_RPI: break; // start-DMA triggers: no-op (polled)
    }
}

// Pseudo-DMA: each access auto-handshakes one byte (A9/DACK path).
uint8_t Ncr5380::dmaRead() {
    if (phase_ == DATA_IN) {
        dmaBytes++;
        uint8_t b = dataPos_ < dataIn_.size() ? dataIn_[dataPos_] : 0;
        dataPos_++;
        if (dataPos_ >= dataIn_.size()) enterStatus();
        return b;
    }
    if (phase_ == STATUS) { uint8_t s = status_; enterMsgIn(); return s; }
    if (phase_ == MSG_IN) { enterBusFree(); return 0x00; }
    return 0;
}

void Ncr5380::dmaWrite(uint8_t v) {
    if (phase_ == COMMAND) {
        cmd_.push_back(v);
        if (cmd_.size() == 1) cmdLen_ = cdbLength(cmd_[0]);
        if (int(cmd_.size()) >= cmdLen_) execute();
    } else if (phase_ == DATA_OUT) {
        dataOut_.push_back(v);
        if (dataOut_.size() >= dataOutExpected_) finishWrite();
    }
}

// DRQ policy: kept permissive (any REQ under MODE_DMA) — the Plus ROM
// pulls STATUS/MESSAGE through the pseudo-DMA port without touching
// TCR. The LC II SCSI Manager detects end-of-transfer through the IRQ
// latch (phase change under DMA, see enterStatus) rather than DRQ.
bool Ncr5380::drqActive() const { return req_ && (mode_ & MODE_DMA); }
