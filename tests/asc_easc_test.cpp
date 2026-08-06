// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Gate — discrete EASC ($B0) semantics against MAME master asc_easc_device
// (sound/asc.cpp:1419-1771), the cell of the Quadra 700/900/950
// (macquadra700.cpp:805). Pins the audit fix for MAME-parity finding #1:
// version $B0, R_CLOCK read-only 3, FIFO IRQ enables reset DISABLED
// ($F09/$F29 = 1 — the real-Q700 ASCTester dump in asc.cpp:1428-1441),
// the ASCTester "804Idle: $0F", the gated $804 IRQ clear, the 16.16 SRC
// pacing and one CD-XA ADPCM block.
// Exit 0 = pass, 1 = fail.

#include "Asc.h"

#include <cstdio>

namespace {
int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}
} // namespace

int main() {
    std::printf("asc_easc_test — EASC $B0 semantics (MAME asc.cpp:1419-1771)\n");

    // cpuHz = kSampleRate so tick(1) advances exactly one 44.1 kHz sample.
    AscEasc asc(AscEasc::kSampleRate);
    bool irqLine = false;
    asc.onIrq = [&](bool s) { irqLine = s; };
    asc.reset();

    // ── Reset register file (asc.cpp:1753-1771 + base :148-160) ─────────
    check(asc.read(0x800) == 0xB0, "version reads $B0");
    check(asc.read(0x807) == 3, "R_CLOCK read-only 3 (asc.cpp:1661-1663)");
    check(asc.read(0xF09) == 1 && asc.read(0xF29) == 1,
          "FIFO IRQ enables reset to 1 = disabled (F09/F29 dump)");
    check(asc.read(0x804) == 0x00, "reset FIFOSTAT is 0 (base device_reset)");
    check(asc.read(0x801) == 0, "mode reads 0 at reset");

    // ── Mode + FIFOMODE (asc.cpp:1683-1692, base :543-551) ──────────────
    asc.write(0x801, 0xFF);                       // only bit 0 writable
    check(asc.read(0x801) == 1, "mode write masks to bit 0");
    check(asc.read(0x804) & 0x08, "mode write signals FIFO B empty (bit 3)");
    asc.write(0x803, 0x80);                       // FIFO reset
    check((asc.read(0x804) & 0x0A) == 0x0A, "FIFOMODE bit 7 sets $0A");

    // ── Idle drain: ASCTester "804Idle: $0F", no IRQ while disabled ─────
    asc.tick(16);                                 // 16 samples, FIFOs empty
    check(asc.read(0x804) == 0x0F, "804 idle drain settles at $0F");
    check(!irqLine, "idle drain with enables disabled raises no IRQ");

    // ── Half-empty IRQ + the gated $804 clear (asc.cpp:1650-1659) ───────
    // A = $300, B = $400, enable A only: at pop #$102 A crosses below half
    // (HALF_A) while B is still above half (HALF_B clear) → $804 read DOES
    // clear. Later, once B also crosses, the read must NOT clear.
    asc.reset();
    asc.write(0x801, 1);
    for (int i = 0; i < 0x300; i++) asc.write(0x000, 0x80);
    for (int i = 0; i < 0x400; i++) asc.write(0x400, 0x80);
    asc.write(0xF09, 0);                          // enable FIFO A IRQ
    check(!irqLine, "enable with A above half: no dummy IRQ");
    asc.tick(0x102);                              // pre-pop capA reaches $1FF
    check(irqLine, "A below half with enable on asserts the IRQ");
    uint8_t st = asc.read(0x804);
    check((st & 0x01) && !(st & 0x04), "HALF_A set, HALF_B still clear");
    check(!irqLine, "804 read clears the latch while HALF_B is clear");
    asc.tick(0x100);                              // capA $FF, capB $1FF
    check(irqLine, "next below-half pop re-asserts");
    st = asc.read(0x804);
    check(st & 0x04, "B crossed below half too");
    check(irqLine, "804 read does NOT clear while HALF_B is set (the gate)");
    asc.write(0xF09, 1);                          // disable A
    check(!irqLine, "disabling the enable drops the line");
    asc.write(0xF09, 0);                          // re-enable, HALF_A still up
    check(irqLine, "re-enable with HALF_A pending fires the dummy IRQ");

    // ── SRC pacing (asc.cpp:1478-1492): step $8000 = one pop every 2 ────
    asc.reset();
    asc.write(0x801, 1);
    asc.write(0xF08, 0x80);                       // R_FIFOA_CTRL: SRC on
    asc.write(0xF04, 0x7F);                       // R_SRCA_H
    asc.write(0xF05, 0xFF);                       // R_SRCA_L → step $8000
    for (int i = 0; i < 0x400; i++) asc.write(0x000, 0x80);
    const int capBefore = asc.fifoCap(0);
    asc.tick(512);
    check(capBefore - asc.fifoCap(0) == 256,
          "SRC step $8000 pops 256 samples per 512 outputs");

    // ── CD-XA 8-bit ADPCM (asc.cpp:1589-1648): K=0, shift 0 → raw<<8 ────
    asc.reset();
    asc.write(0x801, 1);
    asc.write(0xF08, 0x01);                       // R_FIFOA_CTRL: CD-XA 2:1
    asc.write(0x000, 0x00);                       // block header: filter 0, shift 0
    for (int i = 0; i < 28; i++) asc.write(0x000, 0x40);
    asc.tick(1);                                  // header + first sample
    int16_t left = 0, right = 0;
    check(asc.popStereo(left, right), "CD-XA tick produced a host sample");
    check(left == 0x4000, "decoded sample = $40 << 8 (K=0, shift 0)");

    std::printf("%s\n", gFails ? "FAILED" : "PASSED");
    return gFails ? 1 : 0;
}
