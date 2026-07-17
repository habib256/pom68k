// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "PseudoVia.h"

void PseudoVia::reset() {
    for (auto& r : regs_) r = 0;
    regs_[2] = 0x7F;                         // no slot IRQ pending (active low)
    regs_[3] = 0x1B;                         // pseudovia.cpp:93-97
    irq_ = false;
}

// pseudovia.cpp:220-248 (base read — the V8 flavour only overrides write)
uint8_t PseudoVia::read(uint32_t offset) {
    offset &= 0x13;                          // A0, A1, A4 decoded
    uint8_t data = regs_[offset];

    if (offset == 0x00) data = 0;            // port B: no inputs wired on LC II
    if (offset == 0x01 && onConfigRead) data = onConfigRead();
    if (offset == 0x10) {
        data &= uint8_t(~0x38);
        if (onVideoRead) data |= onVideoRead();
    }
    // bit 7 of the IERs always reads back 0 on V8 (pseudovia.cpp:20,241-247)
    if (offset == 0x12 || offset == 0x13) data &= uint8_t(~0x80);

    return data;
}

// v8_pseudovia_device::write (pseudovia.cpp:329-388)
void PseudoVia::write(uint32_t offset, uint8_t v) {
    if ((offset >> 9) == 1) {                // $200-$3FF: port A
        if (onPortA) onPortA(v);
        return;
    }

    offset &= 0x1F;
    switch (offset) {
    case 0x00:
        if (onPortB) onPortB(v);
        break;
    case 0x01:
        if (onConfigWrite) onConfigWrite(v);
        break;
    case 0x02:                               // slot IFR: 1 arms/acks VBL only
        regs_[2] |= uint8_t(v & 0x40);
        recalcIrqs();
        break;
    case 0x03:                               // IFR: write 1 to ack…
        v &= uint8_t(~0x10);                 // …except ASC, level-triggered (NOP)
        regs_[3] &= uint8_t(~(v & 0x7F));
        recalcIrqs();
        break;
    case 0x10:
        regs_[0x10] = v;
        if (onVideoWrite) onVideoWrite(v);
        break;
    case 0x12:                               // slot IER, bit-7 selector
    case 0x13:                               // IER, bit-7 selector
        if (v & 0x80) regs_[offset] |= uint8_t(v & 0x7F);
        else          regs_[offset] &= uint8_t(~(v & 0x7F));
        recalcIrqs();
        break;
    }
}

void PseudoVia::slotIrq(uint8_t mask, bool state) {
    if (state) regs_[2] &= uint8_t(~mask);   // asserted = bit LOW
    else       regs_[2] |= mask;
    recalcIrqs();
}

// V8: level-triggered — mirrors the line into IFR bit 4 both ways
// (pseudovia.cpp:315-327; the base RBV device only latches the rise)
void PseudoVia::ascIrq(bool state) {
    if (state) regs_[3] |= uint8_t(ASC);
    else       regs_[3] &= uint8_t(~ASC);
    recalcIrqs();
}

void PseudoVia::scsiIrq(bool state) {
    if (state) regs_[3] |= uint8_t(SCSI_IRQ);
    else       regs_[3] &= uint8_t(~SCSI_IRQ);
    recalcIrqs();
}

void PseudoVia::scsiDrq(bool state) {
    if (state) regs_[3] |= uint8_t(SCSI_DRQ);
    else       regs_[3] &= uint8_t(~SCSI_DRQ);
    recalcIrqs();
}

// pseudovia.cpp:190-218 — slot lines bubble into IFR bit 1; an enabled,
// pending IFR REPLACES reg 3 with (ifr | $80) — non-enabled pending bits
// are dropped, faithfully to MAME (and the hardware tests behind it)
void PseudoVia::recalcIrqs() {
    uint8_t slotIrqs = uint8_t(~regs_[2] & 0x78);
    slotIrqs &= uint8_t(regs_[0x12] & 0x78);

    if (slotIrqs) regs_[3] |= uint8_t(ANY_SLOT);
    else          regs_[3] &= uint8_t(~ANY_SLOT);

    const uint8_t ifr = uint8_t(regs_[3] & regs_[0x13] & 0x1B);
    if (ifr) {
        regs_[3] = uint8_t(ifr | 0x80);
        irq_ = true;
    } else {
        regs_[3] &= uint8_t(~0x80);
        irq_ = false;
    }
}
