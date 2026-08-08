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
// Only the initiator side; up to 7 targets (ScsiDisk, IDs 0-6 — ID 7 is
// the Mac) selected by the ID bit the initiator drives on the data bus.
// One disk at ID 0 is enough for the ROM to read the driver + boot blocks
// and launch System 6; extra IDs let the GUI mount secondary volumes.
// Source of truth: MAME ncr5380.cpp; NCR 5380 datasheet; DEV.md § SCSI.
// Gate: tests/scsi_boot_etalon.cpp.
//
// PHASE ENGINE: the arbitration/selection/phase-handshake state machine is
// filled from the M7 research report (pending). Until then the bus reads as
// idle/no-device, so the ROM falls through to the floppy — no regression.

#pragma once
#include "SaveState.h"
#include "ScsiTarget.h"
#include <cstdint>
#include <functional>
#include <vector>
#include <string>

class Ncr5380 {
public:
    void reset();
    // Attach a target at a SCSI ID (0-6). The historical single-disk call
    // sites (Plus, tests) keep the default ID 0. Any ScsiTarget answers
    // here, not just a disk — see ScsiTarget.h.
    void attach(ScsiTarget* disk, int id = 0) {
        if (id >= 0 && id < 7) targets_[id] = disk;
    }

    // reg = (addr>>4)&7. Pseudo-DMA (A9) handled by the dma* entry points.
    uint8_t read(int reg);
    void write(int reg, uint8_t v);
    uint8_t dmaRead();
    void dmaWrite(uint8_t v);
    bool drqActive() const;          // A9 path routes to dma* only when set
    // Mac II: IRQ → VIA2 CB2 (active low into the 6522). Plus ignores it.
    bool irqAsserted() const { return irq_; }

    long reads = 0, writes = 0, selects = 0, commands = 0;   // debug counters
    long dmaBytes = 0;                       // data bytes handshaked out/in
    uint8_t lastCmd = 0;
    // Debug hooks: full CDB of each executed command; every register
    // access (reg, isWrite, value)
    std::function<void(const std::vector<uint8_t>&)> onCommand;
    std::function<void(int, bool, uint8_t)> onAccess;

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
        // Audit § 2.6(d), ALIGNED: bits 6/5 are READ-ONLY status (arbitration
        // in progress / lost arbitration). A write only lands in the low
        // 0x9F — MAME `m_icmd = (m_icmd & ~IC_WRITE) | (data & IC_WRITE)`
        // with IC_WRITE = 0x9f (machine/ncr5380.h:83-84, icmd_w :355); bit 6
        // on the WRITE side is the chip's TEST mode, which latches nothing
        // readable. AIP is synthesized from `phase_` in read(R_ICR); LA never
        // sets — this engine has one initiator and cannot lose arbitration.
        ICR_WRITE = 0x9F,
    };
    // Mode Register bits.
    //
    // Audit § 2.6(a), DOCUMENT-SKIP — MODE_EOP_IE and BSR_ENDDMA below are
    // stored/reported as 0 and never act. On silicon (and in MAME) the
    // End-Of-DMA flag has exactly ONE producer: the /EOP pin, driven by an
    // external DMA controller when its byte count expires — MAME
    // `ncr5380_device::eop_w` (machine/ncr5380.cpp:762-778) sets BAS_ENDOFDMA
    // and, with MODE_EOPIRQ, interrupts. There is no /EOP source anywhere in
    // POM68K: every platform here is PSEUDO-DMA (the CPU moves each byte
    // through the DACK window), and the only Mac with a real SCSI DMA engine
    // — the IIfx — is modelled at MAME's own M3 subset (bare 53C80 + soft
    // handshake, `IIfxMemory::scsiDmaRead/Write`), the full engine being
    // A/UX-only (`apple/scsidma.cpp:12`; MAME calls eop_w only from
    // scsidma.cpp:393-394). Modelling the flag would add a bit no code can
    // ever set. Reopening condition: the IIfx SCSIDMA engine grows its
    // count-driven transfer path (`docs/IOP_BRINGUP.md`), which is the moment
    // an /EOP edge becomes expressible.
    enum Mode {
        MODE_ARBITRATE = 0x01, MODE_DMA = 0x02, MODE_MONBSY = 0x04,
        MODE_EOP_IE = 0x08, MODE_PARITY_IE = 0x10, MODE_PARITY_CHK = 0x20,
        MODE_TARGET = 0x40, MODE_BLOCK_DMA = 0x80,
    };
    // Current SCSI Bus Status (R4) bits — live bus signals.
    //
    // Audit § 2.6(b), DOCUMENT-SKIP: CBS_DBP (data-bus parity) and BSR_PARITY
    // always read 0, and MODE_PARITY_CHK / MODE_PARITY_IE are stored inert.
    // This is not a divergence — it is MAME parity. The nscsi bus carries no
    // parity line, so `csstat_r` never sets ST_DBP (machine/ncr5380.cpp:
    // 445-459: the value is assembled from S_RST/BSY/REQ/MSG/CTL/INP/SEL and
    // nothing else) and BAS_PARITYERROR has no producer either — `rpi_r`
    // (:521-529) clears a bit that is never raised. Both models therefore
    // report "parity always good", which is also what a healthy bus reports.
    enum Cbs {
        CBS_DBP = 0x01, CBS_SEL = 0x02, CBS_IO = 0x04, CBS_CD = 0x08,
        CBS_MSG = 0x10, CBS_REQ = 0x20, CBS_BSY = 0x40, CBS_RST = 0x80,
    };
    // Bus and Status Register (R5) bits. BSR_BUSERR (MONBSY busy-error) and
    // BSR_ENDDMA are inert here — see the MODE_MONBSY simplification in
    // docs/LLE_VS_HLE.md § 1.5 and the § 2.6(a) note above.
    enum Bsr {
        BSR_ACK = 0x01, BSR_ATN = 0x02, BSR_BUSERR = 0x04, BSR_PHASE = 0x08,
        BSR_IRQ = 0x10, BSR_PARITY = 0x20, BSR_DRQ = 0x40, BSR_ENDDMA = 0x80,
    };

    // ── Save states (SaveState.h) ───────────────────────────────────────
    // Register file, bus phase and the in-flight command buffers — a
    // snapshot can be taken with a READ half-handshaked, and the ROM's
    // polled loop resumes expecting the same byte position.
    //
    // `disk_` is a POINTER into `targets_[]`, which the machine owns and
    // re-attaches on restore, so it is carried as an ID and re-resolved.
    // That is the general rule for this codebase: cross-device pointers
    // become indices, never raw addresses — a restored pointer would either
    // dangle or, worse, silently address the previous session's object.
    template <class Ar> void visit(Ar& ar) {
        ar(odr_, icr_, mode_, tcr_, selEnable_,
           phase_, req_, reqGap_, irq_,
           cmd_, dataIn_, dataOut_, dataPos_, dataOutExpected_, cmdLen_,
           status_, reads, writes, selects, commands, dmaBytes, lastCmd);

        std::int8_t sel = -1;
        if constexpr (!Ar::loading) {
            for (int i = 0; i < 7; i++)
                if (disk_ && targets_[i] == disk_) { sel = std::int8_t(i); break; }
        }
        ar(sel);
        if constexpr (Ar::loading)
            disk_ = (sel >= 0 && sel < 7) ? targets_[sel] : nullptr;
    }

private:
    ScsiTarget* targets_[7] = {};    // by SCSI ID (7 = initiator, never used)
    ScsiTarget* disk_ = nullptr;     // target selected by the current session

    // Register file (as written by the initiator)
    uint8_t odr_ = 0, icr_ = 0, mode_ = 0, tcr_ = 0, selEnable_ = 0;

    // SCSI bus phase
    enum Phase { BUS_FREE, ARBITRATION, SELECTION, COMMAND, DATA_IN, DATA_OUT,
                 STATUS, MSG_IN, MSG_OUT } phase_ = BUS_FREE;
    bool req_ = false;               // target asserting REQ
    bool reqGap_ = false;            // one CSR read sees REQ low after DACK
    bool irq_ = false;               // 5380 IRQ latch (phase mismatch)

    // Transfer buffers for the current command
    std::vector<uint8_t> cmd_, dataIn_, dataOut_;
    size_t dataPos_ = 0, dataOutExpected_ = 0;
    int cmdLen_ = 0;
    uint8_t status_ = 0;

    bool phaseMatch() const;
    void trySelect();
    void enterCommand();
    void enterStatus();
    void enterMsgIn();
    void enterBusFree();
    void execute();
    void finishWrite();
    void extendDataOut();            // defect-list header → real DATA OUT size
    void ackRising();
    void ackFalling();
    bool targetPhase() const;
    uint8_t phaseSignals() const;
    uint8_t liveBusStatus() const;   // R4 value from the current phase
    uint8_t busAndStatus() const;    // R5 value from the current phase
};
