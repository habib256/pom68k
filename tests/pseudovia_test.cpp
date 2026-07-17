// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// O6.2 gate — pseudo-VIA (V8 flavour) IFR/IER/slot semantics against the
// table pinned in docs/LCII_HARDWARE.md § Pseudo-VIA (MAME pseudovia.cpp
// hardware-tested on a real LC II). Exit 0 = pass, 1 = fail.

#include "PseudoVia.h"

#include <cstdio>

namespace {
int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}
} // namespace

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

    std::printf("%s\n", gFails ? "FAILED" : "PASSED");
    return gFails ? 1 : 0;
}
