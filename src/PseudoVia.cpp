// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "PseudoVia.h"

void PseudoVia::reset() {
    for (auto& r : regs_) r = 0;
    regs_[2] = 0x7F;                         // no slot IRQ pending (active low)
    regs_[3] = 0x1B;                         // pseudovia.cpp:93-97
    irq_ = false;
    ascLine_ = false;
}

// pseudovia.cpp:220-248 (base read — the V8 flavour only overrides write)
uint8_t PseudoVia::read(uint32_t offset) {
    if (flavour_ == Flavour::Msc) {          // msc_pseudovia_device::read
        offset &= 0xFF;                      // full decode, 256-byte mirror
        if (offset >= 0x20 && offset <= 0x2F)
            return onMscRead ? onMscRead(int(offset & 0xF)) : 0;
        uint8_t data = offset < 0x20 ? regs_[offset] : 0;
        if (offset == 0x00) data = onPortBRead ? onPortBRead() : 0;
        if (offset == 0x01 && onConfigRead) data = onConfigRead();
        if (offset == 0x10) {
            data &= uint8_t(~0x38);
            if (onVideoRead) data |= onVideoRead();
        }
        if (offset == 0x12 || offset == 0x13) data &= uint8_t(~0x80);
        return data;
    }
    // Decode width per flavour: base and V8 read through the narrow A0/A1/A4
    // decode (pseudovia.cpp:222 — V8 only overrides write), Sonora reads the
    // full $00-$1F file (:413; regs 4/5 are distinct, not port-B mirrors).
    offset &= (flavour_ == Flavour::Sonora) ? 0x1Fu : 0x13u;
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

// v8_pseudovia_device::write (pseudovia.cpp:329-388). The base device's
// write (:250-307) differs in case 0x03 (no ~$10 ASC mask, :268-271) and
// in case 0x13, where a set-write of exactly $FF stores $1F — "the IIci
// ROM's POST demands it" (:290-305). That quirk is base-only: V8
// (:376-386), Sonora (:488-498) and Msc (:600-610) keep the plain bit-7
// selector.
void PseudoVia::write(uint32_t offset, uint8_t v) {
    if (flavour_ == Flavour::Msc) {
        // msc_pseudovia_device::write — no port-A window, full decode,
        // MSC block at $20-$2F; ASC ack is a NOP (level-triggered) like
        // the Level flavour, everything else matches the base cases.
        offset &= 0xFF;
        if (offset >= 0x20 && offset <= 0x2F) {
            if (onMscWrite) onMscWrite(int(offset & 0xF), v);
            return;
        }
    } else if ((offset >> 9) == 1) {         // $200-$3FF: port A
        if (onPortA) onPortA(v);
        return;
    }

    // Decode width per flavour: base writes through the narrow A0/A1/A4
    // decode (pseudovia.cpp:252 — so $0B mirrors the IFR ack), V8/Sonora
    // decode $00-$1F (:337/:449), Msc keeps the full offset (:560) — its
    // $14-$FF non-cases fall out of the switch as NOPs, like master's
    // (no write function has a default case).
    if (flavour_ == Flavour::Base)      offset &= 0x13;
    else if (flavour_ != Flavour::Msc)  offset &= 0x1F;
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
        // …except ASC on the Level and Msc flavours, where the ack is a
        // NOP (pseudovia.cpp:341/:576). The base device has no such mask
        // (:268-271): there the edge-latched bit 4 clears normally.
        if (flavour_ != Flavour::Base) v &= uint8_t(~0x10);
        regs_[3] &= uint8_t(~(v & 0x7F));
        recalcIrqs();
        break;
    case 0x10:
        regs_[0x10] = v;
        if (onVideoWrite) onVideoWrite(v);
        break;
    case 0x12:                               // slot IER, bit-7 selector
    case 0x13:                               // IER, bit-7 selector
        if (v & 0x80) {
            regs_[offset] |= uint8_t(v & 0x7F);
            // Base IER only: a $FF write stores $1F — "the IIci ROM's
            // POST demands it" (pseudovia.cpp:295-298; no other flavour
            // has the quirk).
            if (offset == 0x13 && flavour_ == Flavour::Base && v == 0xFF)
                regs_[offset] = 0x1F;
        } else {
            regs_[offset] &= uint8_t(~(v & 0x7F));
        }
        recalcIrqs();
        break;
    }
}

void PseudoVia::slotIrq(uint8_t mask, bool state) {
    if (state) regs_[2] &= uint8_t(~mask);   // asserted = bit LOW
    else       regs_[2] |= mask;
    recalcIrqs();
}

// Level (V8/Sonora, pseudovia.cpp:315-327 / :396-408): we remember the raw
// line level in ascLine_ and let recalcIrqs() reflect it into IFR bit 4
// every pass — so the level survives being masked while disabled and
// re-latches when the System later enables the ASC interrupt (IER bit 4)
// with the line already high (empty FIFO after the boot chime). Without
// this the Sound Manager enables the ASC IRQ but never sees it pending →
// its refill handler never runs → app sound is silent (SC2K, TODO §
// App-compat; traced 2026-07-17).
//
// Base (RBV/VASP, pseudovia.cpp:136-146): only the 0→1 transition sets IFR
// bit 4, ascLine_ is purely the edge detector (m_live_main_ints bit 4), and
// nothing re-applies the level afterwards — so the guest's W1C ack sticks.
void PseudoVia::ascIrq(bool state) {
    if (flavour_ == Flavour::Base) {
        if (state && !ascLine_) {
            regs_[3] |= uint8_t(ASC);
            recalcIrqs();
        }
        ascLine_ = state;
        return;
    }
    ascLine_ = state;
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

    // Level/Msc flavours: re-sample the ASC line every pass (like the slot
    // lines above) so it can't be lost across an enable/disable of IER
    // bit 4. On the base device bit 4 is a latch owned by ascIrq() and the
    // guest's ack — touching it here would make that ack unclearable.
    if (flavour_ != Flavour::Base) {
        if (ascLine_) regs_[3] |= uint8_t(ASC);
        else          regs_[3] &= uint8_t(~ASC);
    }

    const uint8_t ifr = uint8_t(regs_[3] & regs_[0x13] & 0x1B);
    if (ifr) {
        regs_[3] = uint8_t(ifr | 0x80);
        irq_ = true;
    } else {
        regs_[3] &= uint8_t(~0x80);
        irq_ = false;
    }
}
