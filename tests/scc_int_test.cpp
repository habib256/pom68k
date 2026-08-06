// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// SCC interrupt-trio gate — the three acknowledge paths the shipped Mac OS
// ISR pattern ("serve one source, end with Reset Highest IUS") exercises,
// fixed together 2026-08-05 against MAME z80scc.cpp:
//   1. Reset Highest IUS ($38) resets an In-Service bit only — the IP
//      latches SURVIVE, so a second pending source keeps requesting
//      (z80scc.cpp:1782-1802). On a Mac no INTACK ever sets an IUS bit,
//      so $38 must not touch the IPs at all; the old model cleared every
//      channel IP and threw the second source away.
//   2. Writing the data register IS the Tx acknowledgment: the single-slot
//      buffer going full clears a pending TxIP (z80scc.cpp:2477-2500).
//   3. Reset Ext/Status ($10) honours the chip's queueing logic (Zilog SCC
//      UM WR0; z80scc update_extint:793-818): a status change that arrived
//      during the latched window and still PERSISTS re-latches — from the
//      LIVE state, so the mouse driver reads the new DCD level — and
//      interrupts again; a change that did not persist (two transitions)
//      does not.
//
// Checks: multi-source survival across $38; TxIP clear on data write and
// re-raise at the next buffer-empty edge; DCD re-presentation with the new
// level; the two-transition non-representation. Exit 0 = pass, 1 = fail.

#include "Scc8530.h"

#include <cstdio>

namespace {
int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-60s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}
// Point WR0 at register r on channel ch, then write v to it.
void wrReg(Scc8530& s, int ch, int r, uint8_t v) {
    s.writeCtl(ch, uint8_t(r & 7) | ((r & 8) ? 0x08 : 0));
    s.writeCtl(ch, v);
}
uint8_t rr(Scc8530& s, int ch, int reg) {
    s.writeCtl(ch, uint8_t(reg & 7) | ((reg & 8) ? 0x08 : 0));
    return s.readCtl(ch);
}
constexpr int B = 0;                   // channel B (index 0)
constexpr int A = 1;                   // channel A — RR3 lives here
constexpr int kPace = 544;             // legacy fallback pace (LC II)
} // namespace

int main() {
    std::printf("scc_int_test — IP survival across $38, Tx ack on write, "
                "ext/status queueing\n");

    {   // ── 1. two pending sources: $38 must not eat the second one ──
        Scc8530 s; s.reset();
        wrReg(s, B, 9, 0x08);           // WR9 MIE
        wrReg(s, B, 4, 0x44);           // async x16, 1 stop
        wrReg(s, B, 5, 0x68);           // Tx 8-bit + Tx Enable
        wrReg(s, B, 1, 0x03);           // ext IE + Tx IE
        wrReg(s, B, 15, 0x08);          // WR15 DCD IE
        s.writeData(B, 0x11);           // idle shifter takes it: TxIP edge
        check(s.irqAsserted(), "TxIP pending after the first data write");
        s.setDcd(B, true);              // mouse quadrature edge: ext latches
        check((rr(s, A, 3) & 0x03) == 0x03,
              "RR3: B Tx and B Ext both pending");
        // The shipped ISR: dispatch ONE source (ext), ack it, end with $38.
        (void)rr(s, B, 0);              // read the frozen RR0
        s.writeCtl(B, 0x10);            // Reset Ext/Status — the ext ack
        check((rr(s, A, 3) & 0x03) == 0x02,
              "$10 clears B Ext; B Tx still pending");
        s.writeCtl(B, 0x38);            // Reset Highest IUS ends the ISR
        check(s.irqAsserted(), "$38 leaves the second source requesting");
        check((rr(s, A, 3) & 0x03) == 0x02, "RR3 after $38: B Tx survives");
        // The survivor re-enters the ISR and is served normally.
        s.writeCtl(B, 0x28);            // Reset Tx Int Pending (end of data)
        check(!s.irqAsserted(), "$28 retires the surviving Tx source");
    }

    {   // ── 2. writing the data register IS the Tx acknowledgment ──
        Scc8530 s; s.reset();
        wrReg(s, B, 9, 0x08);
        wrReg(s, B, 4, 0x44);
        wrReg(s, B, 5, 0x68);
        wrReg(s, B, 1, 0x02);           // Tx IE
        s.writeData(B, 0x11);           // buffer → idle shifter: TxIP
        check(s.irqAsserted(), "TxIP on the buffer-empty edge");
        s.writeData(B, 0x22);           // next char parks behind the shifter:
        check(!s.irqAsserted(),         // the write acks the pending TxIP
              "data write clears the pending TxIP (no $28, no $38)");
        s.tick(kPace + kPace / 4);      // first char drains → reload edge
        check(s.irqAsserted(), "TxIP re-raises at the next reload");
        s.writeCtl(B, 0x38);            // $38 alone must NOT ack it
        check(s.irqAsserted(), "$38 does not stand in for the Tx ack");
        s.writeData(B, 0x33);           // ...the next data write does
        check(!s.irqAsserted(), "the next data write is the ack again");
        s.tick(kPace);                  // 2nd char drains → 3rd reloads
        check(s.irqAsserted(), "TxIP again at the following reload");
        s.writeCtl(B, 0x28);            // end of message: nothing more to
        s.tick(4 * kPace);              // send, the driver disarms with $28
        check(!s.irqAsserted(), "drained after $28: line stays down");
    }

    {   // ── 3. ext/status queueing: a persisting change re-presents ──
        Scc8530 s; s.reset();
        wrReg(s, B, 9, 0x08);
        wrReg(s, B, 1, 0x01);           // ext IE
        wrReg(s, B, 15, 0x08);          // DCD IE
        s.setDcd(B, true);              // first edge: latch freezes DCD=1
        check(s.irqAsserted(), "first DCD edge latches ext/status");
        check((rr(s, B, 0) & 0x08) != 0, "frozen RR0 carries DCD=1");
        s.setDcd(B, false);             // second edge DURING the window,
                                        // and the low level PERSISTS
        check((rr(s, B, 0) & 0x08) != 0,
              "RR0 stays frozen at the old level while latched");
        s.writeCtl(B, 0x10);            // Reset Ext/Status
        check(s.irqAsserted(),
              "persisting in-window DCD change re-presents at $10");
        check(!(rr(s, B, 0) & 0x08),
              "the re-latch froze the NEW level (DCD=0)");
        s.writeCtl(B, 0x10);            // second service: nothing queued
        check(!s.irqAsserted(), "second $10 retires it (no change left)");
        check(!(rr(s, B, 0) & 0x08), "RR0 reads live and low afterwards");
    }

    {   // ── 4. two transitions (change did NOT persist): no re-present ──
        Scc8530 s; s.reset();
        wrReg(s, B, 9, 0x08);
        wrReg(s, B, 1, 0x01);
        wrReg(s, B, 15, 0x08);
        s.setDcd(B, true);              // latch freezes DCD=1
        check(s.irqAsserted(), "edge latches");
        s.setDcd(B, false);             // pulse inside the window...
        s.setDcd(B, true);              // ...back to the latched level
        s.writeCtl(B, 0x10);
        check(!s.irqAsserted(),
              "non-persisting pulse does not re-present (UM queueing rule)");
    }

    {   // ── 5. #32: only WR15-armed bits freeze at the latch ──
        Scc8530 s; s.reset();
        wrReg(s, B, 9, 0x08);
        wrReg(s, B, 1, 0x01);
        wrReg(s, B, 15, 0x08);          // only DCD armed
        s.setDcd(B, true);              // latch: rr0 frozen with D6=1 (reset
                                        // state of the underrun/EOM latch)
        check(s.irqAsserted(), "DCD edge latches");
        s.writeCtl(B, 0xC0);            // Reset Tx Underrun/EOM: live D6 -> 0
        check((rr(s, B, 0) & 0x08) != 0, "armed DCD bit reads frozen");
        // unarmed Tx-underrun/EOM (bit 6) reads LIVE (z80scc.cpp:1438-1443)
        check((rr(s, B, 0) & 0x40) == 0, "unarmed D6 reads live, not frozen");
        s.writeCtl(B, 0x10);
    }

    {   // ── 6. #33: WR8 via the control pointer transmits ──
        Scc8530 s; s.reset();
        wrReg(s, B, 4, 0x44);
        wrReg(s, B, 5, 0x68);           // Tx Enable
        wrReg(s, B, 8, 0x5A);           // control-pointer WR8 = data write
        // the byte must be IN the engine: shifter busy → All Sent low
        // (the old code stored it dead in wr[8] and All Sent stayed 1)
        check((rr(s, B, 1) & 0x01) == 0, "All Sent low: WR8 byte is shifting");
    }

    {   // ── 7. #34: Sync/Hunt visible in async; set on Rx disable ──
        Scc8530 s; s.reset();
        wrReg(s, B, 4, 0x44);           // async
        wrReg(s, B, 3, 0x01);           // Rx enable
        wrReg(s, B, 3, 0x00);           // Rx DISABLE → Sync/Hunt sets
        check((rr(s, B, 0) & 0x10) != 0,
              "Rx disable sets RR0 Sync/Hunt in async (z80scc:1937-1940)");
        check(!s.irqAsserted(), "…silently: no ext/status latch on this path");
    }

    {   // ── 8. #35: an edge while Tx-int is DISARMED is lost ──
        Scc8530 s; s.reset();
        wrReg(s, B, 9, 0x08);
        wrReg(s, B, 4, 0x44);
        wrReg(s, B, 5, 0x68);           // Tx Enable
        wrReg(s, B, 1, 0x01);           // ext IE only — Tx IE off
        s.writeData(B, 0x22);           // became-empty edge, disarmed
        wrReg(s, B, 1, 0x03);           // now arm Tx IE
        check(!s.irqAsserted(),
              "arming Tx IE does not replay the lost edge (z80scc:1866-1903)");
        s.writeData(B, 0x33);           // parks behind the busy shifter;
                                        // the edge fires at the reload
        for (int i = 0; i < 4 * kPace; i++) s.tick(1);
        check(s.irqAsserted(), "the next armed edge interrupts normally");
    }

    {   // ── 9. #36: WR9 Status High moves the code to V6-V4, reversed ──
        Scc8530 s; s.reset();
        wrReg(s, B, 2, 0x20);           // vector $20
        wrReg(s, B, 9, 0x18);           // MIE + Status High
        wrReg(s, B, 1, 0x01);
        wrReg(s, B, 15, 0x08);
        s.setDcd(B, true);              // B ext pending: code 001
        // status low gives $22; high writes the code reversed (100) to V6-V4
        check(rr(s, B, 2) == 0x40,
              "Status High: B-ext modifies V6-V4 reversed ($20 -> $40)");
        wrReg(s, B, 9, 0x08);           // back to status low
        check(rr(s, B, 2) == 0x22, "Status Low: same source reads $22");
        s.writeCtl(B, 0x10);
    }

    {   // ── 10. § 2.5 cosmetic: a chip reset clears the residual RR1 ──
        // z80scc.cpp:1157-1159 resets RR1 to $07 with the rest of the
        // channel; POM68K answers RR1 from rr1Rd once the FIFO is drained,
        // so a stale framing/parity/overrun bit used to outlive the reset.
        Scc8530 s; s.reset();
        wrReg(s, B, 4, 0x44);           // async x16, 1 stop
        wrReg(s, B, 3, 0xC1);           // Rx 8-bit + Rx Enable
        s.injectRxByte(B, 0x5A, false, true);       // framing error rides it
        check(s.readData(B) == 0x5A, "the framing-error byte reads back");
        check(rr(s, B, 1) == 0x47,
              "RR1 keeps the error status of the last byte read");
        wrReg(s, B, 9, 0xC0);           // hardware reset (both channels)
        check(rr(s, B, 1) == 0x07,
              "WR9 $C0 resets RR1 to $07 (z80scc.cpp:1157-1159)");
        check(rr(s, B, 15) == 0xF8,
              "…and applies the channel-reset WR15 = $F8");
    }

    std::printf("scc_int_test: %s\n", gFails ? "FAIL" : "PASS");
    return gFails ? 1 : 0;
}
