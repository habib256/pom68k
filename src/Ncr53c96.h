// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── NCR 53C96 SCSI controller (Quadra 605 / LC 475) ──
// The Q605's single genuinely new peripheral. Unlike the Mac Plus/LC II NCR
// 5380 — which the CPU drove signal by signal (REQ/ACK per byte, phases read
// off the live bus) — the 53C90/94/96 family is a *command-driven* controller:
// the CPU loads a CDB into a 16-byte FIFO, issues a "Select with ATN" command
// then "Transfer Information" commands, and reads the interrupt-status /
// sequence-step registers to follow the bus. A 24-bit transfer counter and a
// DMA data path (pseudo-DMA on the Quadra: the PrimeTime/IOSB holds off /DTACK
// while !DRQ) move the payload.
//
// This is a FUNCTIONAL model at the same abstraction as our Ncr5380: it does
// not simulate the nscsi_bus wire states MAME does, but it presents the exact
// register-level programming interface the Mac OS 8.1 driver and the ROM SCSI
// Manager use, and it reuses the existing ScsiDisk SCSI-1 target verbatim.
//
// Source of truth: MAME src/devices/machine/ncr53c90.cpp/.h (53C90/94/96
// family, Olivier Galibert) — command opcodes ncr53c90.h:160-189, register
// map ncr53c90.cpp:29-42, start_command ncr53c90.cpp:927, status/istatus
// ncr53c90.cpp:1088-1122 + 1288 (53c90a S_INTERRUPT bit), transfer-info FSM
// ncr53c90.cpp:601-692, DMA path ncr53c90.cpp:1182-1232 + dma16 1326-1372.
// Q605 wiring: MAME macquadra605.cpp:33/85/204-206 (ncr53c96, BUSMD_1, irq/drq
// to PrimeTime), iosb.cpp:58-59 (register map inside PrimeTime) + 482-591
// (turboscsi_* pseudo-DMA with /DTACK holdoff on !DRQ).
// Gate: tests/ncr53c96_test.cpp.

#pragma once
#include <cstdint>
#include <functional>
#include <vector>

class ScsiDisk;

class Ncr53c96 {
public:
    void reset();                        // hard reset (power-on / chip reset)

    // Attach a target at a SCSI ID (0-6). The initiator is ID 7 by default
    // (set through the CONFIG1 register at run time — bus_id_w selects the
    // destination for a Select command).
    void attach(ScsiDisk* disk, int id = 0) {
        if (id >= 0 && id < 7) targets_[id] = disk;
    }

    // ── Register file (offset = address bits, 0..15). On the Q605 the CPU
    // reaches these at PrimeTime + $10000, stride $10 (reg = (addr>>4)&0xf) —
    // see the integration notes. ──
    uint8_t read(int reg);
    void write(int reg, uint8_t v);

    // Pseudo-DMA data port (Q605 PrimeTime + $10100). Each access moves one
    // byte through the FIFO/target under the active Transfer Information
    // command. The integrator gates these behind the DRQ line (holds off
    // /DTACK while !drq()) — mirror of MAME turboscsi_dma_r/w.
    uint8_t dmaRead();
    void dmaWrite(uint8_t v);

    // Line outputs (level-sensitive, as MAME's m_irq_handler / m_drq_handler).
    bool irq() const { return irq_; }    // → PrimeTime scsi_irq_w → VIA2 / IPL
    bool drq() const { return drq_; }    // → PrimeTime scsi_drq_w → /DTACK gate

    // ── Bus-service latency model (Q6.5b) ──
    // A real target takes time between a Transfer Info command and the
    // bus-service interrupt (target processing + bus turnaround). Our instant
    // completion made the Mac OS 8.1 async SCSI SIM see S_INTERRUPT in the poll
    // right after its send-CDB handler, so it posted its "service interrupt"
    // message BEFORE the client installed the continuation routine at ctx+$F0
    // → wild jmp through NULL → the "illegal instruction" crash alert.
    // latency=0 keeps the historical instant behaviour (unit tests drive the
    // chip without a tick source); the Q605 machine enables it and pumps
    // tick() from Q605Memory::tick.
    void setLatency(int cycles) { latency_ = cycles; }
    void tick(int cycles) {
        if (pendDelay_ > 0) {
            pendDelay_ -= cycles;
            if (pendDelay_ <= 0 && pendBits_) { raiseIrq(pendBits_); pendBits_ = 0; }
        }
    }

    // ── 53C96 register indices (ncr53c90.cpp:29-42, +c94/c96 extensions) ──
    enum Reg {
        R_TCLOW  = 0x0,   // Transfer Count LSB   (R: current / W: reload)
        R_TCMID  = 0x1,   // Transfer Count MID
        R_FIFO   = 0x2,   // FIFO data
        R_COMMAND= 0x3,   // Command
        R_STATUS = 0x4,   // Status (R) / Destination Bus ID (W)
        R_ISTAT  = 0x5,   // Interrupt Status (R) / Select/reselect Timeout (W)
        R_SEQ    = 0x6,   // Sequence Step (R) / Synchronous Period (W)
        R_FLAGS  = 0x7,   // FIFO Flags / Sequence Step (R) / Sync Offset (W)
        R_CONFIG1= 0x8,   // Configuration 1 (own ID, features)
        R_CLOCK  = 0x9,   // Clock Conversion Factor (W)
        R_TEST   = 0xA,   // Test (W)
        R_CONFIG2= 0xB,   // Configuration 2 (53c90a+)
        R_CONFIG3= 0xC,   // Configuration 3 (53c94+)
        R_TCHIGH = 0xE,   // Transfer Count HIGH (53c94+, 24-bit counter)
        R_FIFO_ALIGN = 0xF,
    };

    // Command register opcodes (ncr53c90.h:160-189). Top bit = DMA variant.
    enum Cmd {
        CM_NOP        = 0x00, CM_FLUSH_FIFO = 0x01,
        CM_RESET      = 0x02, CM_RESET_BUS  = 0x03,
        CD_RESELECT   = 0x40, CD_SELECT     = 0x41,
        CD_SELECT_ATN = 0x42, CD_SELECT_ATN_STOP = 0x43,
        CD_ENABLE_SEL = 0x44, CD_DISABLE_SEL = 0x45,
        CI_XFER       = 0x10, CI_COMPLETE   = 0x11,
        CI_MSG_ACCEPT = 0x12, CI_PAD        = 0x18,
        CI_SET_ATN    = 0x1A, CI_RESET_ATN  = 0x1B,
        CMD_DMA       = 0x80,
    };

    // Status register bits (R4). Low 3 bits mirror the live bus phase
    // MSG/C_D/I_O — ncr53c90.cpp:1091.
    enum Status {
        S_IO = 0x01, S_CD = 0x02, S_MSG = 0x04,          // bus phase (I/O,C/D,MSG)
        S_TCC = 0x08,                                     // (unused here)
        S_TC0 = 0x10, S_PARITY = 0x20, S_GROSS_ERROR = 0x40,
        S_INTERRUPT = 0x80,                               // 53c90a+: IRQ pending
    };
    // Interrupt-status register bits (R5) — ncr53c90.h:151-158.
    enum IStatus {
        I_SELECTED   = 0x01, I_SELECT_ATN = 0x02, I_RESELECTED = 0x04,
        I_FUNCTION   = 0x08, I_BUS = 0x10, I_DISCONNECT = 0x20,
        I_ILLEGAL    = 0x40, I_SCSI_RESET = 0x80,
    };

    // ── SCSI bus phase (what the target is presenting) ──
    enum Phase { BUS_FREE, MSG_OUT, COMMAND, DATA_IN, DATA_OUT, STATUS, MSG_IN };

    // Debug counters / hooks (mirror Ncr5380's).
    long reads = 0, writes = 0, selects = 0, commands = 0, dmaBytes = 0;
    uint8_t lastCmd = 0;
    std::function<void(const std::vector<uint8_t>&)> onCommand;
    std::function<void(int, bool, uint8_t)> onAccess;

private:
    ScsiDisk* targets_[7] = {};
    ScsiDisk* disk_ = nullptr;           // target selected this session

    // Register file
    uint8_t config1_ = 0, config2_ = 0, config3_ = 0;
    uint8_t clockConv_ = 2, syncPeriod_ = 5, syncOffset_ = 0;
    uint8_t busId_ = 0, selectTimeout_ = 0, seq_ = 0;
    uint8_t status_ = 0, istatus_ = 0;
    uint8_t scsiId_ = 7;                 // this initiator's ID (CONFIG1 low 3)
    bool irq_ = false, drq_ = false;
    int latency_ = 0;                    // bus-service IRQ latency (0 = instant)
    int pendDelay_ = 0;                  // countdown to the deferred raiseIrq
    uint8_t pendBits_ = 0;               // istatus bits held back by the latency
    bool selCdbWait_ = false;            // select left CDB incomplete: waiting
                                         // for the driver to stream it in (MAME
                                         // DISC_SEL_ARBITRATION empty-FIFO path)
    bool testMode_ = false;

    // 24-bit transfer counter (reload latch `tcount_`, live `tcounter_`).
    uint32_t tcount_ = 0, tcounter_ = 0;
    bool dmaCommand_ = false;            // last command was a DMA variant

    // 16-byte FIFO.
    uint8_t fifo_[16] = {};
    int fifoPos_ = 0;

    // Q6.5d: has a DATA-phase Transfer Info fetched bytes into the FIFO yet?
    // Our model short-circuits the payload through dataIn_ instead of the real
    // FIFO, so R_FLAGS (FIFO byte count) must NOT report the whole pending
    // payload before a CI_XFER has actually "moved" it: right after a
    // (DMA-)SELECT the physical FIFO is empty (the CDB drained out), so the OS
    // 8.1 SCSI Manager's post-select check `and.b reg7,#$1F; cmpi #1` at
    // $0011ADD4 must see 0 — else it treats the 16 phantom bytes as stray FIFO
    // residue and routes the read to its DISCARD engine (data never stored).
    bool dataXfer_ = false;               // a data-phase CI_XFER has run

    // Bus phase + per-session transfer buffers (functional target I/O).
    Phase phase_ = BUS_FREE;
    std::vector<uint8_t> cmd_;            // CDB accumulator (COMMAND phase)
    std::vector<uint8_t> dataIn_;         // DATA IN bytes queued from the target
    size_t dataInPos_ = 0;
    std::vector<uint8_t> dataOut_;        // DATA OUT bytes gathered for the target
    size_t dataOutExpected_ = 0;
    uint8_t targetStatus_ = 0;            // STATUS phase byte
    int msgInLeft_ = 0;                   // MESSAGE IN bytes still to deliver

    // Engine helpers
    void startCommand(uint8_t c);
    void selectTarget(bool withAtn);
    void transferInfo();                  // CI_XFER: move one phase's worth
    void raiseIrq(uint8_t istatusBits);
    void raiseIrqDeferred(uint8_t istatusBits);   // honors latency_ (tick-driven)
    void updateDrq();
    void fifoPush(uint8_t v);
    uint8_t fifoPop();
    void acceptDataOutByte_(uint8_t v);   // DATA OUT gather (FIFO or DMA)
    void runTarget();                     // execute the accumulated CDB
    void advanceToStatus();               // DATA/COMMAND done → STATUS
    uint8_t phaseStatusBits() const;      // low 3 bits of the STATUS register
    static int cdbLength(uint8_t op);
    static int writeByteCount(const std::vector<uint8_t>& cdb);
};
