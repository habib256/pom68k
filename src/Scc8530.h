// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── SCC Z8530 (minimal) ──
// Just enough of the Zilog 8530 for the Mac Plus mouse: the X1/Y1
// quadrature lines drive the DCD inputs (channel A = mouse X, B = mouse Y);
// DCD transitions latch an external/status interrupt (68000 level 2,
// autovectored) which the mouse driver services by reading RR0 and issuing
// the Reset External/Status Interrupts command. Serial ports come later
// (M7, POMIIGS Scc8530 port).
// O6.10 (LC II): same chip behind V8 at $50F04000, IRQ = 68030 level 4.
// setAbortIdle(true) models an OPEN LocalTalk line. LLE step 3
// (2026-07-21) made the LLAP path real: the standing Break/Abort
// (RR0 bit 7) exists only while the channel hunts in SDLC mode (async
// marks are idle, not a break); RR0 bit 4 Sync/Hunt sets on WR3 Enter
// Hunt and never clears on the dead line — the LLAP sender reads that
// as "no carrier, clear to send" and transmits its ENQ probes; the Tx
// Underrun/EOM latch (RR0 bit 6, WR0 $C0 reset, Send Abort) raises the
// frame-complete ext/status interrupt. RR15 reads back WR15. The LAP
// then times out on its own — no guest-state watchdog needed.
// WR2 (vector) and WR9 (master int ctl) are chip-global on a real 8530
// and are mirrored to both channels here.
// Source of truth: MAME z80scc.cpp + mac128.cpp; DEV.md § SCC (pinned).
// Gates: tests/input_etalon.cpp (Plus), tests/scc_ext_test.cpp (LAP arm).

#pragma once
#include <cstdint>

class Scc8530 {
public:
    // Q5: the Quadra POST reads CTS as "serial debugger attached" — the
    // LC 475 machine pulls it low; the LC II keeps the historic high.
    void setCtsHigh(bool v) { ctsHigh_ = v; }
    // Optional standing Rx (RR0 bit 0). Mac II POST does NOT need this when
    // GLUE RAM banking is correct; kept for targeted SCC experiments.
    void setRxStanding(bool on) { rxStanding_ = on; }
    void reset();

    // Bus access: channel 0 = B, 1 = A; ctl/data per address decode.
    uint8_t readCtl(int channel);
    void writeCtl(int channel, uint8_t v);
    uint8_t readData(int channel);
    void writeData(int channel, uint8_t v);

    // Mouse quadrature inputs (X1 → channel A DCD, Y1 → channel B DCD)
    void setDcd(int channel, bool level);

    // LC II: the machine has no LocalTalk peer — SDLC hunt sees a
    // standing abort (see header comment)
    void setAbortIdle(bool on) { abortIdle_ = on; }

    // Periodic tick (CPU cycles). On an open LocalTalk line the SDLC
    // receiver keeps detecting aborts, so the ext/status interrupt must
    // RE-present after each Reset Ext/Status — the LAP retry loop waits
    // on a *stream* of aborts to run down its retry counter, then
    // reports "no node" and boot continues (O6.11). EVENT-driven since
    // the 2026-07-16 review: servicing (Reset Ext/Status) re-arms a
    // ~130 µs countdown; an armed-but-unserviced channel latches ONCE
    // (a real 8530 latches on transitions, not levels), so non-LAP
    // ext/status users don't get an interrupt storm. Returns true if
    // the IRQ line may have changed (caller recomputes IPL).
    bool tick(int cycles);

    bool irqAsserted() const;

    uint8_t wr(int ch, int r) const { return ch_[ch & 1].wr[r & 15]; }
    long dcdEdges = 0, ctlWrites = 0;   // debug counters
    long rr0Reads = 0, rr3Reads = 0, rr2Reads = 0;

private:
    struct Chan {
        uint8_t wr[16] = {};
        bool dcd = false;            // current line level
        bool extPending = false;     // latched external/status interrupt
        uint8_t rr0Latch = 0;        // RR0 frozen at interrupt time
        bool latched = false;
        bool txIp = false;           // Tx Buffer Empty interrupt pending
        bool txEmptyEvent = false;   // buffer BECAME empty since last
                                     // Reset Tx Int Pending (8530: TxIP
                                     // is edge-, not level-triggered)
        int relatch = 0;             // countdown to the next standing-
                                     // abort presentation (0 = disarmed)
        bool txUnderrun = true;      // RR0 bit 6 Tx Underrun/EOM latch —
                                     // SET while the transmitter idles;
                                     // WR0 $C0 clears it at frame start
        int underrunIn = 0;          // cycles until the drained shifter
                                     // underruns (SDLC frame end)
        bool hunt = false;           // RR0 bit 4 Sync/Hunt — set by WR3
                                     // bit 4 (Enter Hunt); never clears
                                     // on a dead line (no flags arrive).
                                     // LLAP carrier sense reads it as
                                     // "line idle, clear to send".
    };
    uint8_t rr0(const Chan& c) const;
    bool sdlcMode(const Chan& c) const;  // WR4 bits 5-4 = 10
    uint8_t readCtl_(int channel, Chan& c, int reg);
    Chan ch_[2];                     // [0] = B, [1] = A
    int ptr_ = 0;                    // register pointer (WR0 low bits)
    bool abortIdle_ = false;         // open-line Break/Abort (LC II)
    bool ctsHigh_ = true;
    bool rxStanding_ = false;        // Mac II POST: standing Rx available
    static constexpr int kAbortRelatch = 2000;   // ≈130 µs @ 15.67 MHz
    static constexpr int kUnderrunDelay = 1200;  // ≈2 byte times at LocalTalk
                                                 // 230.4 kbps (CRC + flag)
};
