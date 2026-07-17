// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Apple pseudo-VIA, V8 flavour (LC II "VIA2") ──
// 2-port GPIO + interrupt controller with a 6522-ish register layout but
// no timers, no shift register, no DDRs. Inside the V8 gate array at
// $F26000, byte regs 0-3/$10-$13 (A0, A1, A4 decoded); a write with
// (offset >> 9) == 1 hits port A. IFR bit 4 (ASC) is LEVEL-triggered on
// V8: writing 1 to ack it is a NOP (System 7 sound hangs otherwise).
// Source of truth: MAME pseudovia.cpp (v8_pseudovia_device, master
// 2026-07-15) — read :220-248, V8 write :329-388, recalc :190-218,
// reset :93-97. Pinned in docs/LCII_HARDWARE.md § Pseudo-VIA.
// Gate: tests/pseudovia_test.cpp.

#pragma once
#include <cstdint>
#include <functional>

class PseudoVia {
public:
    // IFR (reg 3) bits — enabled mask is $1B (pseudovia.cpp:205)
    enum Irq { SCSI_DRQ = 0x01, ANY_SLOT = 0x02, SCSI_IRQ = 0x08,
               ASC = 0x10, ANY = 0x80 };
    // Slot IFR (reg 2) bits, ACTIVE-LOW latches (pseudovia.cpp:113-134)
    enum Slot { SLOT_C = 0x10, SLOT_E = 0x20, VBL = 0x40 };

    void reset();                            // regs[2]=$7F, regs[3]=$1B

    // Bus access, offset = byte offset inside the $F26000-$F27FFF window
    uint8_t read(uint32_t offset);
    void write(uint32_t offset, uint8_t v);

    // Device interrupt lines
    void slotIrq(uint8_t mask, bool state);  // reg 2, active low
    void ascIrq(bool state);                 // IFR bit 4, LEVEL (V8 quirk)
    void scsiIrq(bool state);                // IFR bit 3
    void scsiDrq(bool state);                // IFR bit 0
    bool irqAsserted() const { return irq_; }

    // Machine hooks (V8Memory): port B write (bit 3 = HMMU enable on the
    // 68020 LC — unused on the LC II), RAM config reg 1, video config $10
    std::function<void(uint8_t)> onPortB;
    std::function<uint8_t()> onConfigRead;   // reads back config | 0x04
    std::function<void(uint8_t)> onConfigWrite;
    std::function<uint8_t()> onVideoRead;    // monitor sense bits 3-5
    std::function<void(uint8_t)> onVideoWrite;
    std::function<void(uint8_t)> onPortA;

private:
    void recalcIrqs();                       // pseudovia.cpp:190-218

    uint8_t regs_[0x20] = {};
    bool irq_ = false;
};
