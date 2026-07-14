// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── NCR 5380 SCSI controller ──
// The Mac Plus SCSI port at $580000. Register select = address bits A4-A6
// (reg = (addr>>4)&7); A0 = 0 read / 1 write; A9 = pseudo-DMA (DACK). The
// Plus has no real DMA — "pseudo-DMA" means the 5380 drives the REQ/ACK
// handshake while the CPU moves each byte through the DMA data ports.
// SCSI on the Plus is POLLED (no 68000 interrupt); the ROM reads the Bus
// and Status Register to follow bus phase.
// Only the initiator side, one target (a ScsiDisk) — enough for the ROM to
// read the driver + boot blocks and launch System 6.
// Source of truth: MAME ncr5380.cpp; NCR 5380 datasheet; DEV.md § SCSI.
// Gate: tests/scsi_boot_etalon.cpp.
//
// PHASE ENGINE: the arbitration/selection/phase-handshake state machine is
// filled from the M7 research report (pending). Until then the bus reads as
// idle/no-device, so the ROM falls through to the floppy — no regression.

#pragma once
#include <cstdint>
#include <vector>
#include <string>

class ScsiDisk;

class Ncr5380 {
public:
    void reset();
    void attach(ScsiDisk* disk) { disk_ = disk; }

    // reg = (addr>>4)&7. Pseudo-DMA (A9) handled by the dma* entry points.
    uint8_t read(int reg);
    void write(int reg, uint8_t v);
    uint8_t dmaRead();
    void dmaWrite(uint8_t v);
    bool drqActive() const;          // A9 path routes to dma* only when set

    long reads = 0, writes = 0, selects = 0, commands = 0;   // debug counters
    long dmaReads = 0; uint8_t lastCmd = 0;
    bool trace = false;
    std::vector<std::string> traceLog;

    // ── 5380 register indices ──
    enum Reg {
        R_DATA = 0,      // Current SCSI Data (R) / Output Data Register (W)
        R_ICR  = 1,      // Initiator Command Register
        R_MODE = 2,      // Mode Register
        R_TCR  = 3,      // Target Command Register
        R_CSR  = 4,      // Current SCSI Bus Status (R) / Select Enable (W)
        R_BSR  = 5,      // Bus and Status Register (R) / Start DMA Send (W)
        R_IDR  = 6,      // Input Data (R) / Start DMA Target Receive (W)
        R_RPI  = 7,      // Reset Parity/Interrupt (R) / Start DMA Init Recv (W)
    };
    // Initiator Command Register bits
    enum Icr {
        ICR_DBUS = 0x01, ICR_ATN = 0x02, ICR_SEL = 0x04, ICR_BSY = 0x08,
        ICR_ACK = 0x10, ICR_LA = 0x20, ICR_AIP = 0x40, ICR_RST = 0x80,
    };
    // Mode Register bits
    enum Mode {
        MODE_ARBITRATE = 0x01, MODE_DMA = 0x02, MODE_MONBSY = 0x04,
        MODE_EOP_IE = 0x08, MODE_PARITY_IE = 0x10, MODE_PARITY_CHK = 0x20,
        MODE_TARGET = 0x40, MODE_BLOCK_DMA = 0x80,
    };
    // Current SCSI Bus Status (R4) bits — live bus signals
    enum Cbs {
        CBS_DBP = 0x01, CBS_SEL = 0x02, CBS_IO = 0x04, CBS_CD = 0x08,
        CBS_MSG = 0x10, CBS_REQ = 0x20, CBS_BSY = 0x40, CBS_RST = 0x80,
    };
    // Bus and Status Register (R5) bits
    enum Bsr {
        BSR_ACK = 0x01, BSR_ATN = 0x02, BSR_BUSERR = 0x04, BSR_PHASE = 0x08,
        BSR_IRQ = 0x10, BSR_PARITY = 0x20, BSR_DRQ = 0x40, BSR_ENDDMA = 0x80,
    };

private:
    ScsiDisk* disk_ = nullptr;

    // Register file (as written by the initiator)
    uint8_t odr_ = 0, icr_ = 0, mode_ = 0, tcr_ = 0, selEnable_ = 0;

    // SCSI bus phase
    enum Phase { BUS_FREE, ARBITRATION, SELECTION, COMMAND, DATA_IN, DATA_OUT,
                 STATUS, MSG_IN, MSG_OUT } phase_ = BUS_FREE;
    bool req_ = false;               // target asserting REQ

    // Transfer buffers for the current command
    std::vector<uint8_t> cmd_, dataIn_;
    size_t dataPos_ = 0;
    int cmdLen_ = 0;
    uint8_t status_ = 0;

    void trySelect();
    void enterCommand();
    void enterStatus();
    void enterMsgIn();
    void enterBusFree();
    void execute();
    void ackRising();
    void ackFalling();
    bool targetPhase() const;
    uint8_t phaseSignals() const;
    uint8_t liveBusStatus() const;   // R4 value from the current phase
    uint8_t busAndStatus() const;    // R5 value from the current phase
};
