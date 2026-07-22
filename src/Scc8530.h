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
#include <deque>
#include <functional>
#include <vector>

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

    // ── LLAP wire (LLE LocalTalk milestone) ──
    // Tx: SDLC frame bytes accumulate across writeData; when the drained
    // shifter underruns (frame complete, CRC+flag on the real wire) the raw
    // frame — WITHOUT the FCS the chip appends — is handed to onTxFrame.
    // Send Abort discards the pending bytes. Channel 0 = B (LocalTalk port).
    std::function<void(int ch, const uint8_t* d, size_t n)> onTxFrame;
    // Rx: inject one LLAP frame (dest, src, type, payload — no FCS; the
    // "chip" computes and appends it so the driver sees the SDLC tail).
    // Delivered at wire pace (setByteCycles) through the 3-deep Rx FIFO with
    // Hunt exit, per-byte Rx interrupts (WR1 modes), address search (WR3
    // bit 2 vs WR6 / $FF broadcast) and End-of-Frame status in RR1.
    // express=true marks a frame synthesized BY the cable itself (LToUDP
    // local CTS): it queues even while the receiver is off (the LLAP
    // sender is half-duplex around its RTS) and starts only after an
    // inter-frame gap, like a real peer's CTS — early delivery played the
    // frame while the driver was still re-arming Rx and every byte was
    // lost on the wire (Chooser RTS retry storm, 2026-07-22).
    void injectRxFrame(int ch, const uint8_t* d, size_t n, bool express = false);
    // CPU cycles per LocalTalk byte (230.4 kbit/s): 544 @ 15.6672 MHz
    // (LC II / Mac II), 272 @ 7.8336 (Plus), 868 @ 25 MHz (Q605).
    void setByteCycles(int c) { byteCycles_ = c > 0 ? c : 544; }

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
                                     // bit 4 (Enter Hunt); clears when a
                                     // frame arrives (opening flag), sets
                                     // again after it. LLAP carrier sense
                                     // reads it as "line idle/busy".
        // ── LLAP Rx/Tx wire state ──
        std::vector<uint8_t> txBuf;  // SDLC frame being written (no FCS)
        struct RxFrame { std::vector<uint8_t> bytes; int pace; int delay;
                         bool express; };
        std::deque<RxFrame> rxQueue; // injected frames (FCS added) + pace.
                                     // express: countdown `delay` = CTS
                                     // inter-frame gap. non-express: gated
                                     // on rxIdle ≥ IDG at dequeue (the gap
                                     // is measured from the PREVIOUS frame's
                                     // end, not baked in at injection)
        std::vector<uint8_t> rxCur;  // frame being paced onto the FIFO
        int rxPace = 0;              // cycles/byte for the current frame
        size_t rxPos = 0;
        int rxTimer = 0;             // cycles to the next FIFO byte
        int rxIdle = 1 << 24;        // cycles since the wire last carried a
                                     // byte (starts "long idle"; capped) —
                                     // injectRxFrame fills the LLAP IDG
                                     // remainder from it
        struct RxByte { uint8_t d; uint8_t rr1; };
        std::deque<RxByte> fifo;     // 3-deep Rx FIFO, per-byte RR1 status
        bool rxIp = false;           // Rx-char-available interrupt pending
        bool specialIp = false;      // special receive condition (EOF/ovr)
        bool firstCharSeen = false;  // WR1 mode 01: int on FIRST char only
        uint8_t rr1Rd = 0x07;        // RR1 of the last byte read (all-sent)
    };
    uint8_t rr0(const Chan& c) const;
    bool sdlcMode(const Chan& c) const;  // WR4 bits 5-4 = 10
    bool rxEnabled(const Chan& c) const { return (c.wr[3] & 0x01) != 0; }
    uint8_t readCtl_(int channel, Chan& c, int reg);
    void rxPushByte(Chan& c);        // pace one frame byte into the FIFO
    void rxStartFrame(Chan& c, int chIdx);
    void raiseRxInt(Chan& c, bool special);
    // The open-line standing Break/Abort is a LINE state, not a machine
    // constant: setAbortIdle(true) marks a connector with no hardwired
    // peer, but the moment a REAL peer transmits on the transport (a
    // non-express injectRxFrame — an LToUDP multicast frame, not the
    // cable's own synthesized CTS) the line becomes a live, terminated
    // network whose idle is clean flags, not aborts. peerHold_ counts
    // down the "peer present" window from the last real peer frame; while
    // it is positive the standing abort is suppressed (openLine() false).
    // A solo boot (no cable, no peer traffic) never refreshes it, so the
    // no-peer LAP timeout is unchanged — LLE_VS_HLE §1.8 / step 8.
    bool openLine() const { return abortIdle_ && peerHold_ <= 0; }
    Chan ch_[2];                     // [0] = B, [1] = A
    int ptr_ = 0;                    // register pointer (WR0 low bits)
    bool abortIdle_ = false;         // no hardwired LocalTalk peer (LC II/Q605)
    int peerHold_ = 0;               // cycles a real peer stays "present"
    bool ctsHigh_ = true;
    bool rxStanding_ = false;        // Mac II POST: standing Rx available
    int byteCycles_ = 544;           // CPU cycles per LocalTalk byte
    static constexpr int kAbortRelatch = 2000;   // ≈130 µs @ 15.67 MHz
    static constexpr int kPeerHold = 30000000;   // ≈2 s: a peer that has
                                                 // transmitted holds the line
                                                 // "live" until it goes quiet
    static constexpr int kUnderrunDelay = 1200;  // ≈2 byte times at LocalTalk
                                                 // 230.4 kbps (CRC + flag)
    static constexpr int kCtsGapBytes = 4;       // synthesized-CTS inter-frame
                                                 // gap in byte times (~139 µs:
                                                 // after the sender's post-EOM
                                                 // Rx re-arm, inside its wait)
    static constexpr int kIdgBytes = 12;         // every other frame: LLAP
                                                 // minimum 400 µs inter-dialog
                                                 // gap (~417 µs) so the driver
                                                 // re-arms on an idle line
};
