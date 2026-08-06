// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// MAME-parity gate for Via6522 — docs/MAME_PARITY_AUDIT.md #21-#23:
//   #21 T2 pulse-counting mode (ACR bit 5) counts PB6 falling edges
//       (MAME 6522via.cpp:129-150 counter2_decrement, :1146-1165 write_pb).
//   #22 External shift-out recirculates the MSB into bit 0
//       (MAME 6522via.cpp:443 shift_out).
//   #23 T1-driven PB7 square wave under ACR bit 7
//       (MAME 6522via.cpp:538-553 t1_tick, :607-620 read_pb).
// Plus regression guards that the used paths (T2 timer mode, plain port-B
// reads, Mac II PIC shift-out bit stream) are byte-identical to before.
// Exit 0 = pass, 1 = fail.

#include "Via6522.h"

#include <cstdio>

namespace {
int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}
} // namespace

// #21 — T2 counts PB6 pulses when ACR bit 5 is set; φ2 leaves it alone.
void t2PulseCount() {
    std::printf("via6522_parity — #21 T2 PB6 pulse counting (ACR5)\n");

    Via6522 via;
    via.reset();
    via.write(Via6522::ACR, 0x20);               // T2 pulse-count mode
    via.write(Via6522::T2CL, 0x03);
    via.write(Via6522::T2CH, 0x00);              // T2 = 3, armed

    via.setInB(0xFF);                            // PB6 high (idle)
    via.tick(1000);
    check(via.read(Via6522::T2CL) == 0x03 && via.read(Via6522::T2CH) == 0x00,
          "#21: phi2 does not decrement T2 in pulse mode");

    // One decrement per PB6 FALLING edge only (MAME write_pb :1160-1165)
    via.setInB(0xBF);                            // 1 -> 0: count
    via.setInB(0xBF);                            // steady low: no count
    via.setInB(0xFF);                            // 0 -> 1: no count
    check(via.read(Via6522::T2CL) == 0x02,
          "#21: one count per falling edge, none on rise/steady");

    via.setInB(0xBF); via.setInB(0xFF);          // 2 -> 1
    via.setInB(0xBF); via.setInB(0xFF);          // 1 -> 0
    check(via.read(Via6522::T2CL) == 0x00 && !(via.ifrRaw() & Via6522::TIMER2),
          "#21: reaching zero does not flag yet (int on underflow)");

    via.setInB(0xBF); via.setInB(0xFF);          // 0 -> 0xFFFF, underflow
    check((via.ifrRaw() & Via6522::TIMER2) != 0,
          "#21: the 0000->FFFF underflow sets IFR.T2");
    check(via.read(Via6522::T2CL) == 0xFF && via.read(Via6522::T2CH) == 0xFF,
          "#21: counter wrapped to FFFF (borrow through T2CH)");

    // Only one interrupt between T2CH writes (MAME :142-148 m_t2_active)
    for (int i = 0; i < 0x10005; i++) { via.setInB(0xBF); via.setInB(0xFF); }
    check(!(via.ifrRaw() & Via6522::TIMER2),
          "#21: only one T2 flag between T2CH writes");

    via.write(Via6522::T2CH, 0x00);              // re-arm (latch low = 3)
    via.setInB(0xBF); via.setInB(0xFF);
    via.setInB(0xBF); via.setInB(0xFF);
    via.setInB(0xBF); via.setInB(0xFF);
    via.setInB(0xBF); via.setInB(0xFF);
    check((via.ifrRaw() & Via6522::TIMER2) != 0,
          "#21: T2CH write re-arms the pulse underflow flag");

    // Regression: with ACR5 clear, PB6 edges never touch T2 (timer mode)
    Via6522 t;
    t.reset();
    t.write(Via6522::T2CL, 0x10);
    t.write(Via6522::T2CH, 0x00);
    t.setInB(0xBF); t.setInB(0xFF); t.setInB(0xBF);
    check(t.read(Via6522::T2CL) == 0x10,
          "#21: timer mode ignores PB6 edges (used path preserved)");
    t.tick(0x11);
    check((t.ifrRaw() & Via6522::TIMER2) != 0,
          "#21: timer mode still counts phi2 (used path preserved)");
}

// #22 — external shift-out (mode 111) recirculates bit 7 into bit 0.
void srRecirculate() {
    std::printf("via6522_parity — #22 SR recirculation on external shift-out\n");

    Via6522 via;
    via.reset();
    via.write(Via6522::ACR, 0x1C);               // SR mode 111: out on CB1
    via.write(Via6522::SR, 0xB1);                // 1011 0001

    // Drive 8 CB1 cells like the PIC does (rise = PIC reads CB2, then fall).
    // The bit stream on CB2 must be the byte MSB-first — unchanged by the
    // recirculation fix (a recirculated bit needs 8 shifts to reach bit 7).
    uint8_t seen = 0;
    for (int i = 0; i < 8; i++) {
        via.extShiftCB1(false, false);           // fall (advances SR, cells 2+)
        via.extShiftCB1(true, false);            // rise (bit consumed)
        seen = uint8_t((seen << 1) | (via.extShiftCB2Out() ? 1 : 0));
        // NB: extShiftCB2Out() is sampled after the rise, matching the PIC's
        // read-after-rise; the first sampled bit is the original bit 7.
    }
    // The model presents bit7 across the first rise, then shifts on falls;
    // sample order above reads bit7 late by design — re-derive per model:
    // rise k samples bit (7-(k-1)) after k-1 falls. seen == original byte.
    check(seen == 0xB1, "#22: CB2 bit stream is the byte MSB-first (unchanged)");
    check((via.ifrRaw() & Via6522::SHIFT) != 0, "#22: SHIFT flags after 8 bits");

    // MAME 6522via.cpp:443: m_sr = (m_sr << 1) | out. Seven shifts happen
    // per byte in this model (falls of cells 2..8), so the residue is
    // rotr(byte,1) — bit 0 recirculated into bit 7's old place chain.
    check(via.srValue() == uint8_t((0xB1 >> 1) | (0xB1 << 7)),
          "#22: SR residue is the rotated byte, not byte<<7");
}

// #23 — T1 square wave on PB7 under ACR bit 7.
void t1Pb7Wave() {
    std::printf("via6522_parity — #23 T1-driven PB7 square wave (ACR7)\n");

    Via6522 via;
    via.reset();
    via.setInB(0xFF);
    via.write(Via6522::DDRB, 0x80);              // PB7 an output
    via.write(Via6522::ORB, 0x80);               // ORB bit 7 = 1

    // Regression first: ACR7 clear -> PB7 is plain ORB/DDRB (vSndEnb path)
    via.write(Via6522::T1CL, 0x04);
    via.write(Via6522::T1CH, 0x00);              // one-shot; drops t1Pb7_
    check((via.portB() & 0x80) != 0,
          "#23: ACR7 clear: PB7 = ORB output (used path preserved)");

    via.write(Via6522::ACR, 0xC0);               // T1 free-run + PB7 enable
    via.write(Via6522::T1CL, 0x04);
    via.write(Via6522::T1LH, 0x00);
    via.write(Via6522::T1CH, 0x00);              // MAME :934: PB7 goes low
    check((via.portB() & 0x80) == 0,
          "#23: T1CH write drives PB7 low");
    check((via.read(Via6522::ORB) & 0x80) == 0,
          "#23: ORB read shows the T1 level, not the ORB latch");

    via.tick(6);                                 // one period (latch+2)
    check((via.portB() & 0x80) != 0, "#23: first underflow toggles PB7 high");
    via.tick(6);
    check((via.portB() & 0x80) == 0, "#23: next underflow toggles it back");
    via.tick(12);                                // two periods in one slice
    check((via.portB() & 0x80) == 0, "#23: even underflows in a slice = no net flip");
    via.tick(6);
    check((via.portB() & 0x80) != 0, "#23: odd underflow count flips again");

    // One-shot mode leaves PB7 high after the single underflow (MAME :546)
    Via6522 os;
    os.reset();
    os.write(Via6522::DDRB, 0x80);
    os.write(Via6522::ORB, 0x00);
    os.write(Via6522::ACR, 0x80);                // PB7 enable, T1 one-shot
    os.write(Via6522::T1CL, 0x04);
    os.write(Via6522::T1CH, 0x00);
    check((os.portB() & 0x80) == 0, "#23: one-shot: low while counting");
    os.tick(6);
    check((os.portB() & 0x80) != 0, "#23: one-shot: high after underflow");

    // Input pins never bleed into PB7 while ACR7 is set
    os.setInB(0x00);
    check((os.portB() & 0x80) != 0, "#23: ACR7 overrides the PB7 input pin");
}

// docs/MAME_PARITY_AUDIT.md § 2.1 "cosmétique" — the two divergences that
// are DELIBERATE. Pinned here so a later parity pass cannot quietly adopt
// MAME's shape: both would be regressions, and the reasoning lives in
// src/Via6522.cpp next to the code.
void deliberateDivergences() {
    std::printf("via6522_parity — § 2.1 deliberate divergences\n");

    // (a) free-run T1 period = N+2 φ2 cycles (R6522 datasheet § 5.1), NOT
    // MAME's N+IFR_DELAY = N+3 (6522via.cpp:102, :542 — an interrupt-latency
    // fudge that :117-119 subtracts back out of the counter read).
    Via6522 via;
    via.reset();
    via.write(Via6522::ACR, 0x40);               // T1 free-run
    via.write(Via6522::T1CL, 0x08);
    via.write(Via6522::T1CH, 0x00);              // N = 8
    int guard = 0;
    while (!via.tick(1) && ++guard < 100) {}     // first underflow
    via.write(Via6522::IFR, Via6522::TIMER1);    // ack
    int period = 0;
    do { ++period; } while (!via.tick(1) && period < 100);
    check(period == 10, "T1 free-run period is N+2 (datasheet), not MAME's N+3");

    // (b) reset() zeroes the T1/T2 latches, the counters and the SR, which
    // a real RES leaves alone (datasheet § 2.1) and MAME preserves
    // (device_reset :329-346 only disarms the timers). Determinism of the
    // warm-reset state across 36 machines is worth more than the parity.
    via.write(Via6522::SR, 0x5A);
    via.write(Via6522::T1LL, 0x33);
    via.write(Via6522::T1LH, 0x44);
    via.write(Via6522::T2CL, 0x55);
    via.write(Via6522::T2CH, 0x66);
    via.reset();
    check(via.read(Via6522::SR) == 0x00, "reset clears the SR (MAME preserves it)");
    check(via.read(Via6522::T1LL) == 0x00 && via.read(Via6522::T1LH) == 0x00,
          "reset clears the T1 latch (MAME preserves it)");
    check(via.read(Via6522::T2CL) == 0x00 && via.read(Via6522::T2CH) == 0x00,
          "reset clears the T2 counter (MAME preserves it)");
}

int main() {
    t2PulseCount();
    srRecirculate();
    t1Pb7Wave();
    deliberateDivergences();
    if (gFails) { std::printf("via6522_parity_test: %d FAIL\n", gFails); return 1; }
    std::printf("via6522_parity_test: all green\n");
    return 0;
}
