// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Q8 gate — PrimeTime/IOSB ASC ($BB) stereo FIFO and level IRQ semantics.
// Reference: MAME asc_iosb_device + ASCTester v4 results from a real LC 475.

#include "Asc.h"
#include "Q605Memory.h"

#include <cstdio>

namespace {
int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-62s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}
} // namespace

int main() {
    std::printf("q605_asc_test — PrimeTime IOSB ASC stereo (Q8)\n");

    AscIosb asc;
    bool irq = false;
    asc.onIrq = [&](bool s) { irq = s; };
    asc.reset();

    check(asc.read(0x800) == 0xBB, "IOSB ASC version is $BB");
    check(asc.read(0x801) == 1, "reset selects FIFO mode");
    check(asc.read(0x802) == 0 && asc.read(0x803) == 0, "control/FIFO-mode read zero");
    check(asc.read(0x804) == 0x0E, "ASCTester idle FIFO status is $0E");
    check(asc.read(0x809) == 1 && asc.read(0x829) == 1,
          "FIFO A/B interrupts reset disabled");

    asc.write(0x80A, 1);                        // record bit: expose A occupancy
    for (int i = 0; i < 0x200; i++) {
        asc.write(uint32_t(i & 0x3FF), 0x90);   // left = +16
        asc.write(0x400u + uint32_t(i & 0x3FF), 0x70); // right = -16
    }
    check(asc.fifoCap(0) == 0x200 && asc.fifoCap(1) == 0x200,
          "both 1 KB FIFOs accept independent data");
    check((asc.read(0x804) & 0x0F) == 0, "half-full stereo FIFOs clear status");

    asc.write(0x829, 0);                        // enable aggregate stereo IRQ
    asc.tick(704 * 2);
    check(asc.fifoCap(0) < 0x200 && asc.fifoCap(1) < 0x200,
          "drain consumes both channels in lockstep");
    check((asc.read(0x804) & 0x04) != 0 && irq,
          "either FIFO below half asserts the level IRQ");
    (void)asc.read(0x804);
    check(irq, "status read cannot clear an active half-empty level");

    int16_t left = 0, right = 0;
    check(asc.popStereo(left, right) && left > 0 && right < 0,
          "stereo output preserves distinct left/right samples");

    while (asc.fifoCap(0) < 0x200) asc.write(0, 0x90);
    while (asc.fifoCap(1) < 0x200) asc.write(0x400, 0x70);
    (void)asc.read(0x804);
    check(!irq, "refill plus status read clears the inactive IRQ");

    AscIosb mode;
    mode.reset();
    mode.write(0x801, 0);
    mode.tick(704);
    check((mode.read(0x804) & 0x04) != 0, "disabled chip reports half-empty");

    // Integration: pseudo-VIA2 IFR acknowledgement must re-latch a live ASC
    // level, matching PrimeTime's asc_irq_w wiring.
    Q605Memory mem(1u << 20);
    mem.reset();
    mem.asc().write(0x829, 0);
    mem.asc().tick(704);
    check((mem.via2Ifr() & 0x10) != 0, "IOSB ASC raises pseudo-VIA2 bit 4");
    mem.write8(0x50003A00, 0x10);               // VIA2 IFR reg 13 acknowledge
    check((mem.via2Ifr() & 0x10) != 0, "IFR ack re-latches the live ASC level");

    std::printf("%s\n", gFails ? "FAILED" : "PASSED");
    return gFails ? 1 : 0;
}
