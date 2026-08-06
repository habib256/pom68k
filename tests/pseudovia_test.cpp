// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// O6.2 gate — pseudo-VIA IFR/IER/slot semantics against the table pinned
// in docs/LCII_HARDWARE.md § Pseudo-VIA (MAME pseudovia.cpp, hardware-
// tested on a real LC II). Covers both flavours: Level (V8/Sonora,
// v8_pseudovia_device) and Base (RBV/VASP, pseudovia_device), which differ
// only in how IFR bit 4 (ASC) latches and clears.
// Exit 0 = pass, 1 = fail.

#include "PseudoVia.h"

#include <cstdio>

namespace {
int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}
} // namespace

// The base device (rbv.cpp / vasp.cpp) differs from the V8 one only around
// IFR bit 4: pseudovia.cpp:136-146 latches the 0→1 edge into a bit the
// guest can then clear, and the write path (:268-271) carries no ~$10 mask.
void baseFlavour() {
    std::printf("pseudovia_test — base (RBV/VASP) ASC edge semantics\n");

    PseudoVia pv{PseudoVia::Flavour::Base};
    pv.reset();
    pv.ascIrq(false);
    pv.scsiIrq(false);
    pv.scsiDrq(false);
    pv.write(0x13, 0x80 | 0x1B);                 // enable everything

    // Rising edge latches; the W1C ack sticks even with the line still high
    pv.ascIrq(true);
    check(pv.irqAsserted(), "base ASC: rising edge latches IFR bit 4");
    pv.write(3, 0x10);
    check(!pv.irqAsserted(), "base ASC: W1C ack clears it (no ~$10 mask)");
    check((pv.reg(3) & 0x10) == 0, "base ASC: the level does not re-latch it");

    // No new edge while the line stays high — the ack must stay honoured
    pv.ascIrq(true);
    check(!pv.irqAsserted(), "base ASC: no re-assert without a new 0→1 edge");

    // Drop and re-raise: that IS a new edge
    pv.ascIrq(false);
    pv.ascIrq(true);
    check(pv.irqAsserted(), "base ASC: a fresh edge latches again");
    pv.write(3, 0x10);
    check(!pv.irqAsserted(), "base ASC: and acks again");

    // A line that falls on its own leaves the pending bit alone (edge latch)
    pv.ascIrq(false);
    pv.ascIrq(true);
    pv.ascIrq(false);
    check(pv.irqAsserted(), "base ASC: falling line does not clear the latch");
    pv.write(3, 0x10);
    check(!pv.irqAsserted(), "base ASC: only the ack clears the latch");

    // Everything else is shared with the V8 flavour — spot-check the W1C
    // path that the ~$10 mask must not have widened.
    pv.scsiIrq(true);
    check(pv.irqAsserted(), "base SCSI: line asserts IRQ");
    pv.write(3, 0x08);
    check(!pv.irqAsserted(), "base SCSI: W1C ack clears it");
    pv.scsiIrq(false);

    // Base-only IER quirk (pseudovia.cpp:290-305): a set-write of exactly
    // $FF stores $1F — "the IIci ROM's POST demands it" (:295-298).
    pv.write(0x13, 0xFF);
    check(pv.read(0x13) == 0x1F, "base IER: write $FF stores $1F (IIci POST quirk)");
    pv.write(0x13, 0x80 | 0x7E);             // any other set-write is plain
    check(pv.read(0x13) == 0x7F, "base IER: $FE set-write stays a plain selector");
}

// Decode widths per flavour (pseudovia.cpp:222/:252 base, :337 V8 write,
// :413/:449 Sonora, :560 Msc): the narrow base decode creates mirrors, the
// Sonora read decode un-aliases regs 4/5, Msc NOPs outside its cases.
void decodeWidths() {
    std::printf("pseudovia_test — per-flavour decode widths\n");

    // Base: A0/A1/A4 decode on WRITES too — $0B mirrors the IFR ack ($03)
    PseudoVia base{PseudoVia::Flavour::Base};
    base.reset();
    base.write(0x13, 0x80 | 0x1B);
    base.scsiIrq(true);
    check(base.irqAsserted(), "decode base: SCSI IRQ pending");
    base.write(0x0B, 0x08);                  // $0B & $13 = $03 → IFR ack
    check(!base.irqAsserted(), "decode base: write at $0B mirrors the IFR ack");
    base.scsiIrq(false);

    // V8 (Level): writes decode $00-$1F — $0B is NOT an IFR mirror
    PseudoVia v8{PseudoVia::Flavour::Level};
    v8.reset();
    v8.write(0x13, 0x80 | 0x1B);
    v8.scsiIrq(true);
    v8.write(0x0B, 0x08);                    // falls out of the switch: NOP
    check(v8.irqAsserted(), "decode V8: write at $0B is a NOP, IRQ survives");
    v8.scsiIrq(false);
    v8.write(3, 0x08);

    // Sonora: reads decode $00-$1F — reg 4 is backing store, not port B
    PseudoVia son{PseudoVia::Flavour::Sonora};
    son.reset();
    check(son.read(0x04) == 0, "decode Sonora: reg 4 reads backing store (0)");
    son.write(0x13, 0x80 | 0x1B);            // IER = $1B
    check(son.read(0x17) == 0, "decode Sonora: $17 is its own reg, not an IER mirror");
    // and the Level ASC semantics are shared (ack is a NOP)
    son.write(0x13, 0x80 | 0x1B);
    son.ascIrq(true);
    son.write(3, 0x10);
    check(son.irqAsserted(), "decode Sonora: ASC ack stays a NOP (level)");
    son.ascIrq(false);

    // Msc: full decode, $30-$FF are NOPs — $33 must not alias onto IER $13
    PseudoVia msc{PseudoVia::Flavour::Msc};
    msc.reset();
    msc.write(0x33, 0x80 | 0x7F);            // master :560-617 has no case $33
    check(msc.read(0x13) == 0, "decode Msc: write at $33 NOPs (no IER alias)");
}

int main() {
    std::printf("pseudovia_test — V8 pseudo-VIA semantics (O6.2)\n");

    PseudoVia pv;
    uint8_t videoCfg = 0, portA = 0xEE;
    pv.onVideoRead = [] { return uint8_t((2 << 3) & 0x38); };   // 12" RGB
    pv.onVideoWrite = [&](uint8_t v) { videoCfg = v; };
    pv.onConfigRead = [] { return uint8_t(0x40 | 0x04); };
    pv.onPortA = [&](uint8_t v) { portA = v; };
    pv.reset();

    // Reset values (pseudovia.cpp:93-97)
    check(pv.read(2) == 0x7F, "reset: slot IFR (reg 2) = $7F, all inactive");
    check(pv.read(3) == 0x1B, "reset: reg 3 = $1B");
    check(pv.read(0x13) == 0x00, "reset: IER reads 0 (bit 7 masked)");
    check(!pv.irqAsserted(), "reset: no IRQ");

    // Device lines settle low after machine reset — the $1B reset value
    // is transient until the level lines are first driven (MAME order)
    pv.ascIrq(false);
    pv.scsiIrq(false);
    pv.scsiDrq(false);
    check(pv.read(3) == 0x00, "reset: reg 3 clears once device lines settle");

    // IER bit-7-selector writes
    pv.write(0x13, 0x80 | 0x1B);             // enable everything
    check(pv.read(0x13) == 0x1B, "IER: 1-bits-write-1s, bit 7 reads 0");
    pv.write(0x13, 0x02);                    // clear any-slot enable
    check(pv.read(0x13) == 0x19, "IER: 0-selector clears the given bits");
    pv.write(0x13, 0x80 | 0x02);             // re-enable

    // SCSI IRQ (IFR bit 3): assert, read, ack by writing 1
    pv.scsiIrq(true);
    check(pv.irqAsserted(), "SCSI IRQ: line asserts VIA2 IRQ");
    check(pv.read(3) == 0x88, "SCSI IRQ: reg 3 = ANY | bit 3");
    pv.write(3, 0x08);                       // write-1-to-ack
    check(!pv.irqAsserted(), "SCSI IRQ: W1C ack clears it");

    // ASC (IFR bit 4) is LEVEL-triggered on V8: ack write is a NOP
    pv.ascIrq(true);
    check(pv.irqAsserted(), "ASC: level asserts IRQ");
    pv.write(3, 0x10);
    check(pv.irqAsserted(), "ASC: writing 1 to IFR bit 4 is a NOP (level)");
    pv.ascIrq(false);
    check(!pv.irqAsserted(), "ASC: IRQ clears only when the line drops");

    // Slot interrupts: active-low latches in reg 2, bubble into IFR bit 1
    pv.write(0x12, 0x80 | 0x40);             // enable VBL in slot IER
    pv.slotIrq(PseudoVia::VBL, true);
    check((pv.read(2) & 0x40) == 0, "VBL: slot line clears reg 2 bit 6 (active low)");
    check(pv.irqAsserted(), "VBL: bubbles into IFR any-slot → IRQ");
    check((pv.read(3) & 0x82) == 0x82, "VBL: reg 3 shows ANY | any-slot");
    pv.slotIrq(PseudoVia::VBL, false);
    check(!pv.irqAsserted(), "VBL: line drop clears any-slot");

    // Slot IER gate: disabled slot lines never reach the IFR
    pv.write(0x12, 0x40);                    // disable VBL enable
    pv.slotIrq(PseudoVia::VBL, true);
    check(!pv.irqAsserted(), "slot IER: masked slot line raises nothing");
    pv.slotIrq(PseudoVia::VBL, false);

    // PDS slot $E (bit 5), enabled
    pv.write(0x12, 0x80 | 0x20);
    pv.slotIrq(PseudoVia::SLOT_E, true);
    check(pv.irqAsserted(), "PDS $E: enabled slot line raises any-slot IRQ");
    pv.slotIrq(PseudoVia::SLOT_E, false);

    // Video config reg $10: write = depth, read = monitor sense
    pv.write(0x10, 0x03);                    // 8 bpp
    check(videoCfg == 0x03, "video config: write reaches the machine hook");
    check((pv.read(0x10) & 0x38) == 0x10, "video config: read = sense bits (12\" RGB = 2)");
    check((pv.read(0x10) & 0x07) == 0x03, "video config: depth bits read back");

    // RAM config reg 1 reads through the machine hook (config | 0x04)
    check(pv.read(1) == 0x44, "RAM config: reads config | $04");

    // Port A write decode: (offset >> 9) == 1
    pv.write(0x200, 0x5A);
    check(portA == 0x5A, "port A: write at +$200 hits the port A hook");

    // The IER $FF⇒$1F quirk is base-only: V8's case 0x13
    // (pseudovia.cpp:376-386) keeps the plain bit-7 selector.
    pv.write(0x13, 0xFF);
    check(pv.read(0x13) == 0x7F, "IER: $FF stays a plain set on V8 (no $1F quirk)");

    baseFlavour();

    std::printf("%s\n", gFails ? "FAILED" : "PASSED");
    return gFails ? 1 : 0;
}
