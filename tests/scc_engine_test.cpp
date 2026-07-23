// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// SCC Tx/Rx-engine LLE gate (backlog "Medium — Tx/Rx engine fidelity"),
// pinned against MAME z80scc.cpp:
//   tra_callback  (:1037)  WR5 bit 3 Tx Enable gates the transmitter
//   tra_complete  (:1075)  shifter reload + TxIP on the buffer-empty edge
//   data_read     (:2130)  special condition raised when the ERRORED byte
//                          is read; parity special only under WR1 bit 2
//   receive_data  (:2282)  per-byte error status rides the Rx FIFO
// plus the SDLC tail: the underrun/EOM latch sets after the chip drains
// CRC + closing flag — 24 bit times at the PROGRAMMED pace, not a flat
// constant — and the receiver verifies the FCS (RR1 bit 6 on the EOF byte).

#include "Scc8530.h"
#include <cstdio>
#include <vector>

namespace {
int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-62s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}
void wrReg(Scc8530& s, int ch, int reg, uint8_t v) {
    s.writeCtl(ch, uint8_t(reg));
    s.writeCtl(ch, v);
}
uint8_t rr(Scc8530& s, int ch, int reg) {
    s.writeCtl(ch, uint8_t(reg));
    return s.readCtl(ch);
}
constexpr int kB = 0;
constexpr int kPace = 544;             // legacy fallback = LocalTalk @ LC II
} // namespace

int main() {
    std::printf("scc_engine_test — Tx buffer/shifter, WR5 gating, FCS, errors\n");

    {   // ── Tx pacing: one character per character time, TxIP per reload ──
        Scc8530 s; s.reset();
        wrReg(s, kB, 9, 0x08);          // MIE
        wrReg(s, kB, 4, 0x44);          // async x16 (All Sent is live here;
                                        // sync modes read it always-1)
        wrReg(s, kB, 5, 0x68);          // Tx 8-bit + Tx Enable
        wrReg(s, kB, 1, 0x02);          // Tx Int Enable
        check((rr(s, kB, 0) & 0x04) != 0, "idle transmitter: TBE set");
        check((rr(s, kB, 1) & 0x01) != 0, "idle transmitter: All Sent set");
        s.writeData(kB, 0x11);          // buffer -> idle shifter at once
        check(s.irqAsserted(), "first byte: TxIP on the buffer-empty edge");
        check((rr(s, kB, 0) & 0x04) != 0, "TBE up again (byte is in the shifter)");
        check(!(rr(s, kB, 1) & 0x01), "All Sent low while the shifter drains");
        s.writeCtl(kB, 0x28);           // Reset Tx Int Pending
        s.writeData(kB, 0x22);          // parks behind the busy shifter
        check(!(rr(s, kB, 0) & 0x04), "TBE low while a byte waits");
        check(!s.irqAsserted(), "no TxIP while the byte waits");
        s.tick(kPace / 2);
        check(!s.irqAsserted(), "half a character time: still waiting");
        s.tick(kPace);                  // first byte done -> reload
        check(s.irqAsserted(), "reload after one character time: TxIP");
        check((rr(s, kB, 0) & 0x04) != 0, "TBE up after the reload");
        s.writeCtl(kB, 0x28);
        s.tick(2 * kPace);              // drain the second byte too
        check((rr(s, kB, 1) & 0x01) != 0, "All Sent once buffer+shifter empty");
    }

    {   // ── WR5 bit 3 gates the transmitter (SDLC frame capture too) ──
        Scc8530 s; s.reset();
        long frames = 0;
        s.onTxFrame = [&](int, const uint8_t*, size_t) { frames++; };
        wrReg(s, kB, 9, 0x08);
        wrReg(s, kB, 4, 0x20);          // SDLC
        wrReg(s, kB, 1, 0x01);          // ext IE
        wrReg(s, kB, 15, 0x40);         // Tx Underrun/EOM IE
        s.writeCtl(kB, 0xC0);           // frame start, Tx still DISABLED
        s.writeData(kB, 0x42);          // parks: WR5 bit 3 is off
        s.tick(20 * kPace);
        check(frames == 0, "Tx disabled: nothing reaches the wire");
        check(!(rr(s, kB, 0) & 0x04), "Tx disabled: the byte still waits (TBE low)");
        wrReg(s, kB, 5, 0x68);          // Tx Enable: the parked byte flows
        s.tick(20 * kPace);
        check(frames == 1, "enabling Tx releases the frame to the wire");
    }

    {   // ── SDLC underrun = CRC+flag at the programmed pace, not 1200 ──
        Scc8530 s; s.reset();
        s.setClocks(15667200, 7833600); // derive SDLC pace = 544 exactly
        long frames = 0;
        s.onTxFrame = [&](int, const uint8_t*, size_t) { frames++; };
        wrReg(s, kB, 9, 0x08);
        wrReg(s, kB, 4, 0x20);          // SDLC (pace derives now)
        wrReg(s, kB, 5, 0x68);
        s.writeCtl(kB, 0xC0);
        s.writeData(kB, 0x42);          // one-byte frame
        // Timeline: byte drains at 544, then CRC+flag = 3 x 544 = 1632.
        s.tick(544 + 1500);
        check(frames == 0 && !(rr(s, kB, 0) & 0x40),
              "EOM not before the CRC+flag tail has drained");
        s.tick(300);                    // past 544 + 1632
        check(frames == 1, "frame completes after byte + 24 bit times");
        check((rr(s, kB, 0) & 0x40) != 0, "Tx Underrun/EOM latch set at completion");
    }

    {   // ── Rx FCS verification: RR1 bit 6 rides the EOF byte ──
        Scc8530 good, bad;
        for (Scc8530* s : {&good, &bad}) {
            s->reset();
            wrReg(*s, kB, 9, 0x08);
            wrReg(*s, kB, 4, 0x20);     // SDLC
            wrReg(*s, kB, 3, 0xD1);     // Rx 8-bit + hunt + RxEN (no search)
            wrReg(*s, kB, 1, 0x10);     // Rx int on all chars
        }
        const uint8_t f[3] = {1, 2, 0x81};
        good.injectRxFrame(kB, f, 3);
        bad.injectRxFrame(kB, f, 3, false, true);   // corrupted FCS
        auto drain = [](Scc8530& s) {
            uint8_t last = 0;
            for (int t = 0; t < 32; t++) {
                s.tick(kPace);
                while (rr(s, kB, 0) & 0x01) {
                    last = rr(s, kB, 1);
                    (void)s.readData(kB);
                }
            }
            return last;
        };
        const uint8_t rGood = drain(good), rBad = drain(bad);
        check((rGood & 0x80) && !(rGood & 0x40), "intact frame: EOF, CRC good");
        check((rBad & 0x80) && (rBad & 0x40), "corrupted frame: EOF + CRC error");
        // The CRC-errored read raises the special condition (read-time,
        // z80scc data_read); Error Reset clears it and the RR1 bits.
        check(bad.irqAsserted(), "CRC error raises the special Rx condition");
        bad.writeCtl(kB, 0x30);         // Error Reset
        check(!bad.irqAsserted(), "Error Reset clears the special condition");
        check(!(rr(bad, kB, 1) & 0x40), "Error Reset clears the RR1 error bits");
    }

    {   // ── Async Rx errors: parity under WR4 bit 0, special under WR1 bit 2 ──
        Scc8530 s; s.reset();
        wrReg(s, kB, 9, 0x08);          // MIE
        wrReg(s, kB, 4, 0x45);          // async x16, 1 stop, parity ENABLED
        wrReg(s, kB, 3, 0xC1);          // Rx 8-bit + RxEN
        wrReg(s, kB, 1, 0x10);          // Rx int on all chars (no WR1 bit 2)
        s.injectRxByte(kB, 0x41, true, false);      // parity-errored byte
        check((rr(s, kB, 1) & 0x10) != 0, "parity error rides the byte in RR1");
        (void)s.readData(kB);
        check(!s.irqAsserted(), "parity NOT special without WR1 bit 2");
        wrReg(s, kB, 1, 0x14);          // + parity-is-special-condition
        s.injectRxByte(kB, 0x42, true, false);
        (void)s.readData(kB);
        check(s.irqAsserted(), "WR1 bit 2 makes the parity error special");
        s.writeCtl(kB, 0x30);           // Error Reset
        check(!s.irqAsserted(), "Error Reset clears it");
        // Framing error is always a special condition; parity bit only
        // exists when WR4 bit 0 enables parity.
        s.injectRxByte(kB, 0x43, false, true);
        (void)s.readData(kB);
        check(s.irqAsserted(), "framing error is special regardless of WR1 bit 2");
        s.writeCtl(kB, 0x30);
        wrReg(s, kB, 4, 0x44);          // parity off
        s.injectRxByte(kB, 0x44, true, false);
        check(!(rr(s, kB, 1) & 0x10), "no parity bit when WR4 parity is off");
    }

    std::printf("%s\n", gFails ? "FAILED" : "PASSED");
    return gFails ? 1 : 0;
}
