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
#include "SaveState.h"
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

    // Modem-control OUTPUT pins, as levels a transport can read. /RTS and
    // /DTR are active low on the package; these say ASSERTED, so the caller
    // never has to remember which way the pin runs.
    //
    // /RTS is not simply WR5 bit 1: with Auto Enables (WR3 bit 5) the chip
    // holds it asserted after the bit is cleared until the transmitter is
    // completely empty, so the last character is not truncated by the line
    // driver going away (Zilog SCC UM §4.2; MAME z80scc.cpp:1184-1199 and
    // the deferred release in `tra_complete:1090`). /DTR follows WR5 bit 7
    // unless WR14 bit 2 hands the pin to the DMA request function.
    bool rtsAsserted(int channel) const { return !ch_[channel & 1].rtsPin; }
    bool dtrAsserted(int channel) const { return !ch_[channel & 1].dtrPin; }

    // Machine marker: no hardwired LocalTalk peer. The SDLC hunt sees a
    // standing abort only under a genuine abort condition — once the line
    // has actually carried a frame and no live peer holds it (see
    // openLine() below; a virgin line reads clean, which is what OT's
    // .MPP bind waits for).
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
    // Earliest countdown-driven state transition. Purely accumulated clocks
    // (wireClk/rxIdle) are omitted unless a queued frame is waiting on them;
    // register reads flush elapsed time explicitly.
    int cyclesToNextEvent() const;

    bool irqAsserted() const;

    // ── LLAP wire (LLE LocalTalk milestone) ──
    // Tx: SDLC frame bytes accumulate across writeData; when the drained
    // shifter underruns (frame complete, CRC+flag on the real wire) the raw
    // frame — WITHOUT the FCS the chip appends — is handed to onTxFrame.
    // Send Abort discards the pending bytes. Channel 0 = B (LocalTalk port).
    std::function<void(int ch, const uint8_t* d, size_t n)> onTxFrame;
    // Rx: inject one LLAP frame (dest, src, type, payload — no FCS; the
    // "chip" computes and appends it so the driver sees the SDLC tail).
    // The receiver VERIFIES that FCS as the frame closes: RR1 bit 6 (CRC
    // error) rides the EOF byte — badFcs corrupts the appended FCS (a
    // wire-damage test hook; a truncated transport datagram would look
    // the same).
    // Delivered at wire pace (setByteCycles) through the 3-deep Rx FIFO with
    // Hunt exit, per-byte Rx interrupts (WR1 modes), address search (WR3
    // bit 2 vs WR6 / $FF broadcast) and End-of-Frame status in RR1.
    // express=true marks a frame synthesized BY the cable itself (LToUDP
    // local CTS): it queues even while the receiver is off (the LLAP
    // sender is half-duplex around its RTS) and starts only after an
    // inter-frame gap, like a real peer's CTS — early delivery played the
    // frame while the driver was still re-arming Rx and every byte was
    // lost on the wire (Chooser RTS retry storm, 2026-07-22).
    void injectRxFrame(int ch, const uint8_t* d, size_t n, bool express = false,
                       bool badFcs = false);
    // Async Rx entry (serial-port transports): one received character with
    // optional wire-error flags. Parity error (RR1 bit 4) exists only when
    // WR4 bit 0 enables parity; framing error is RR1 bit 6 in async. The
    // error status rides the byte through the FIFO; the special-condition
    // interrupt raises when the errored byte is READ (z80scc data_read
    // :2130), parity only when WR1 bit 2 makes it special. The transport
    // owns the pacing (no line model on the async side yet).
    void injectRxByte(int ch, uint8_t d, bool parityError = false,
                      bool framingError = false);
    // CPU cycles per LocalTalk byte (230.4 kbit/s): 544 @ 15.6672 MHz
    // (LC II / Mac II), 272 @ 7.8336 (Plus), 868 @ 25 MHz (Q605).
    // Legacy fixed pace — the fallback when the machine has not provided
    // clocks or the guest has not programmed a mode we can derive.
    void setByteCycles(int c) { byteCycles_ = c > 0 ? c : 544; }

    // ── Virtual-wire accelerator (in-process AppleTalk hub only) ──
    // A real LocalTalk wire is 230.4 kbit/s and lossy; the in-process
    // hub's wire is neither law. setWirePace(N) overrides the SDLC
    // per-byte pace in BOTH directions (N < the derived real pace = a
    // faster wire; 0 = off). It deliberately wins over the guest-derived
    // 230.4 kbit/s SDLC pace — that fidelity is exactly what a
    // several-minute Finder copy is made of. Guest-facing TIMING WINDOWS
    // stay at the real pace regardless: the express-CTS gap and the
    // LLAP inter-dialog gap are about the DRIVER's turnaround time, not
    // the wire's (realPaceOf), and an open frame that underruns gets one
    // real byte-time of grace for a late Tx byte before the tail flushes
    // (a fast virtual wire must not truncate a frame the guest could
    // feed fine at hardware speed).
    // setLosslessRx(true) adds flow control: a full Rx FIFO PAUSES the
    // frame on the wire instead of dropping the byte (RR1 overrun), and
    // a queued frame waits for the FIFO to drain before starting — a
    // virtual cable can exert backpressure a real one cannot, and every
    // avoided drop is an avoided 1-2 s ATP retransmit stall. Both are
    // off by default (LToUDP interop and all timing gates keep real
    // hardware semantics).
    void setWirePace(int cyclesPerByte) { wirePace_ = cyclesPerByte > 0 ? cyclesPerByte : 0; }
    void setLosslessRx(bool on) { losslessRx_ = on; }
    // Backpressure meters (the lossless wire trades drops for delay — these
    // say how much delay). Backlog = frames injected but not yet played;
    // hold = the longest injection→wire wait any frame suffered.
    size_t rxBacklog(int ch) const { return ch_[ch & 1].rxQueue.size(); }
    size_t rxBacklogMax(int ch) const { return ch_[ch & 1].rxQueueMax; }
    int64_t rxHoldMaxCycles(int ch) const { return ch_[ch & 1].rxHoldMax; }
    long rxOverflowDrops(int ch) const { return ch_[ch & 1].rxDropped; }

    // ── Guest-derived pacing (SCC async-baud LLE, MAME z80scc oracle) ──
    // With the machine's CPU clock and the SCC's PCLK provided, the per-
    // channel byte pace is DERIVED from the guest's WR4 (clock mode ×1/16/
    // 32/64, stop bits, parity), WR11 (Tx clock source), WR12/13 (BRG time
    // constant) and WR14 (BRG enable/source) — z80scc.cpp get_clock_mode
    // (:1157), get_brg_rate (:2476, rate = src/(2+(WR13<<8|WR12))/(2·mode)),
    // update_serial (:2565). RTxC is the 3.6864 MHz serial crystal on every
    // Mac (MAME configure_channels 3'686'400; LCII_HARDWARE.md). In SDLC
    // mode the Mac clocks LocalTalk's FM0 wire off the DPLL at RTxC/16 =
    // 230 400 bit/s, which derives EXACTLY the legacy constants above.
    void setClocks(int64_t cpuHz, int64_t pclkHz);
    // Effective pace for a channel: derived if available, else the legacy
    // fixed byteCycles_. Public as the scc_baud_test hook.
    int paceCycles(int ch) const { return paceOf(ch_[ch & 1]); }

    uint8_t wr(int ch, int r) const { return ch_[ch & 1].wr[r & 15]; }
    long dcdEdges = 0, ctlWrites = 0;   // debug counters
    long rr0Reads = 0, rr3Reads = 0, rr2Reads = 0;

    // ── Save states (SaveState.h) ───────────────────────────────────────
    // Both channels plus the shared register pointer and line state. The
    // wire CONFIGURATION (`abortIdle_`, `byteCycles_`, `wirePace_`,
    // `losslessRx_`, `cpuHz_`, `pclkHz_`) is installed by the machine and
    // the LToUDP/AppleTalk wiring, so it stays out — but `lineDriven_` and
    // `peerHold_` are observed history, not configuration, and they decide
    // whether the guest sees a standing abort. Restoring them wrong would
    // change whether Open Transport's LLAP driver ever binds .MPP.
    template <class Ar> void visit(Ar& ar) {
        ar(ch_, ptr_, lineDriven_, peerHold_, ctsHigh_, rxStanding_);
    }

private:
    struct Chan {
        uint8_t wr[16] = {};
        bool dcd = false;            // current line level
        bool extPending = false;     // latched external/status interrupt
        uint8_t rr0Latch = 0;        // RR0 frozen at interrupt time
        bool latched = false;
        bool txIp = false;           // Tx Buffer Empty interrupt pending
                                     // (edge-latched at became-empty when
                                     // WR1 bit 1 is armed; an edge while
                                     // disarmed is lost — z80scc.cpp:1866)
        // ── Tx engine (SCC Tx/Rx-fidelity LLE, MAME z80scc oracle) ──
        // One-slot Tx buffer (NMOS 8530) feeding a modelled shift register
        // that drains at the programmed character pace. The byte moves
        // buffer → shifter only under WR5 bit 3 Tx Enable (tra_complete
        // :1075 reloads only under TX_ENABLE); that buffer-empty
        // TRANSITION is the TxIP source and sets RR0 bit 2 again.
        bool txBufFull = false;      // a byte waits in the Tx buffer
        bool txGracing = false;      // virtual-wire underrun grace window:
                                     // the shifter drained with nothing
                                     // buffered, but the frame stays open
                                     // one REAL byte-time for a late byte
        uint8_t txBufData = 0;
        uint8_t txShiftData = 0;     // the character IN the shifter — the
                                     // WR14 local-loopback tap reads it as
                                     // it completes (LC ROM POST relies on
                                     // hearing its own bytes back)
        int txShiftIn = 0;           // cycles left in the shifter (0 = idle)
        bool txFlushing = false;     // shifter is draining the SDLC tail
                                     // (CRC + closing flag, 24 bit times);
                                     // completion = Tx underrun/EOM
        int relatch = 0;             // countdown to the next standing-
                                     // abort presentation (0 = disarmed)
        bool txUnderrun = true;      // RR0 bit 6 Tx Underrun/EOM latch —
                                     // SET while the transmitter idles;
                                     // WR0 $C0 clears it at frame start
        // Modem-control output pins, stored at PIN level (true = high =
        // deasserted, both being active low). rtsPin is a latch rather than
        // a view of WR5 because Auto Enables defers its release to the end
        // of the last character — see rtsAsserted() above.
        bool rtsPin = true;
        bool dtrPin = true;
        bool hunt = false;           // RR0 bit 4 Sync/Hunt — set by WR3
                                     // bit 4 (Enter Hunt); clears when a
                                     // frame arrives (opening flag), sets
                                     // again after it. LLAP carrier sense
                                     // reads it as "line idle/busy".
        // ── LLAP Rx/Tx wire state ──
        std::vector<uint8_t> txBuf;  // SDLC frame being written (no FCS)
        struct RxFrame { std::vector<uint8_t> bytes; int pace; int delay;
                         bool express; int64_t queuedAt = 0;
            template <class Ar> void visit(Ar& ar) {
                ar(bytes, pace, delay, express, queuedAt);
            } };
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
        // Lossless-wire observability: the backpressure that replaces a
        // drop is a DELAY, and a long enough delay is an ATP retransmit
        // all the same. wireClk stamps each queued frame so the hold time
        // (injection → first byte on the wire) can be measured.
        int64_t wireClk = 0;         // monotonic SDLC line clock (cycles)
        int64_t rxHoldMax = 0;       // longest hold a queued frame suffered
        size_t rxQueueMax = 0;       // deepest the injection backlog got
        long rxDropped = 0;          // frames refused at the queue cap
        int  rxOverrunHold = 0;      // byte-times a full FIFO has held the wire
        int rxIdle = 1 << 24;        // cycles since the wire last carried a
                                     // byte (starts "long idle"; capped) —
                                     // injectRxFrame fills the LLAP IDG
                                     // remainder from it
        struct RxByte { uint8_t d; uint8_t rr1;
            template <class Ar> void visit(Ar& ar) { ar(d, rr1); } };
        std::deque<RxByte> fifo;     // 3-deep Rx FIFO, per-byte RR1 status
        bool rxIp = false;           // Rx-char-available interrupt pending
        bool specialIp = false;      // special receive condition (EOF/ovr)
        bool firstCharSeen = false;  // WR1 mode 01: int on FIRST char only
        uint8_t rr1Rd = 0x07;        // RR1 of the last byte read (all-sent)
        int pace = 0;                // guest-derived CPU cycles per byte
                                     // (0 = not derivable → byteCycles_)

        // ── Save states (SaveState.h) ───────────────────────────────────
        // The full channel: WR file, the interrupt latches (RR0 freeze, the
        // edge-triggered TxIP), the modelled Tx shifter
        // mid-character, and the whole LLAP Rx side — injected frame queue,
        // the frame being paced onto the FIFO, and the 3-deep Rx FIFO with
        // its per-byte RR1. Interrupt latches are the load-bearing part: an
        // 8530 driver acknowledges edges, so a restore that cleared a
        // pending TxIP loses an interrupt the guest is blocked on.
        template <class Ar> void visit(Ar& ar) {
            ar(wr, dcd, extPending, rr0Latch, latched, txIp);
            ar(txBufFull, txGracing, txBufData, txShiftData, txShiftIn,
               txFlushing, relatch, txUnderrun, hunt, rtsPin, dtrPin);
            ar(txBuf, rxQueue, rxCur, rxPace, rxPos, rxTimer);
            ar(wireClk, rxHoldMax, rxQueueMax, rxDropped, rxOverrunHold,
               rxIdle);
            ar(fifo, rxIp, specialIp, firstCharSeen, rr1Rd, pace);
        }
    };
    uint8_t rr0(const Chan& c) const;
    bool sdlcMode(const Chan& c) const;  // WR4 bits 5-4 = 10
    bool allSent(const Chan& c) const;   // RR1 bit 0: buffer AND shifter empty
    void updateRts(Chan& c);             // WR3/WR5/WR14 → the /RTS, /DTR pins
    void updateSerial(Chan& c);          // derive Chan::pace from WR4/11/12-14
    // The guest-visible pace: derived (or legacy) — the REAL wire speed.
    int realPaceOf(const Chan& c) const { return c.pace > 0 ? c.pace : byteCycles_; }
    // The effective per-byte pace: the virtual-wire override wins on the
    // SDLC (LocalTalk) side; async channels always keep the real baud.
    int paceOf(const Chan& c) const {
        return (wirePace_ > 0 && sdlcMode(c)) ? wirePace_ : realPaceOf(c);
    }
    bool txLoad(Chan& c, int rem);       // buffer → shifter (WR5 gated)
    bool rxEnabled(const Chan& c) const { return (c.wr[3] & 0x01) != 0; }
    uint8_t readCtl_(int channel, Chan& c, int reg);
    void rxPushByte(Chan& c);        // pace one frame byte into the FIFO
    void rxStartFrame(Chan& c, int chIdx);
    void raiseRxInt(Chan& c, bool special);
    // The open-line standing Break/Abort is a LINE state, not a machine
    // constant: setAbortIdle(true) marks a connector with no hardwired
    // peer, but the abort exists only under a genuine abort condition —
    // LLE_VS_HLE §1.8 + §1.10 / steps 8 and 7:
    //  • A VIRGIN line (never driven since reset) reads CLEAN. LocalTalk
    //    is FM0: the SCC recovers its receive clock from the line's own
    //    transitions, and a line that has never carried a frame has never
    //    given the DPLL an edge — no recovered clock, no sampled 1s, no
    //    abort. This is what Open Transport's LLAP driver waits on before
    //    binding .MPP (it spins until RR0 bit 7 clears; System 7's LAP
    //    never did). lineDriven_ latches at the first frame the line
    //    carries: a local SDLC frame completion (the LLAP trailer ends in
    //    a real abort sequence, then the driver releases the line
    //    mid-mark — the receiver's last recovered state IS the abort),
    //    a Send Abort, or any transport frame (injectRxFrame).
    //  • A live peer suppresses it: the moment a REAL peer transmits
    //    (a non-express injectRxFrame — an LToUDP multicast frame, not
    //    the cable's own synthesized CTS) the line is a live, terminated
    //    network. peerHold_ counts down the "peer present" window from
    //    the last real peer frame; while positive the standing abort is
    //    suppressed. A solo boot (no cable, no peer traffic) never
    //    refreshes it, so the no-peer LAP timeout is unchanged.
    bool openLine() const { return abortIdle_ && lineDriven_ && peerHold_ <= 0; }
    Chan ch_[2];                     // [0] = B, [1] = A
    int ptr_ = 0;                    // register pointer (WR0 low bits)
    bool abortIdle_ = false;         // no hardwired LocalTalk peer (LC II/Q605)
    bool lineDriven_ = false;        // the line has carried a frame since reset
    int peerHold_ = 0;               // cycles a real peer stays "present"
    bool ctsHigh_ = true;
    bool rxStanding_ = false;        // Mac II POST: standing Rx available
    int byteCycles_ = 544;           // CPU cycles per LocalTalk byte
    int wirePace_ = 0;               // virtual-wire SDLC pace override (0=off)
    bool losslessRx_ = false;        // virtual-wire Rx flow control
    int64_t cpuHz_ = 0;              // machine CPU clock (0 = legacy pacing)
    int64_t pclkHz_ = 0;             // SCC PCLK (BRG source, WR14 bit 1)
    static constexpr int64_t kRtxcHz = 3'686'400;   // Mac serial crystal
    // RR1 bits 3-1 are the SDLC RESIDUE CODE: how much of the I-field's
    // last character is valid when a frame does not end on a character
    // boundary. With 8 bits/character the byte-aligned case — every frame
    // this model can carry, since the wire is byte-granular (LLE_VS_HLE
    // §1.4) — is code 011, which is also the chip's own reset value and
    // why `rr1Rd` starts at $07. Frame bytes used to report 000, a code
    // that means a partial character on real silicon.
    static constexpr uint8_t kResidueAligned = 0x06;
    static constexpr int kAbortRelatch = 2000;   // ≈130 µs @ 15.67 MHz
    static constexpr int kPeerHold = 30000000;   // ≈2 s: a peer that has
                                                 // transmitted holds the line
                                                 // "live" until it goes quiet
    static constexpr int kTailBytes = 3;         // SDLC frame tail the chip
                                                 // appends after the underrun:
                                                 // CRC (2 bytes) + closing
                                                 // flag = 24 bit times at the
                                                 // programmed pace
    static constexpr int kCtsGapBytes = 4;       // synthesized-CTS inter-frame
                                                 // gap in byte times (~139 µs:
                                                 // after the sender's post-EOM
                                                 // Rx re-arm, inside its wait)
    static constexpr int kIdgBytes = 12;         // every other frame: LLAP
                                                 // minimum 400 µs inter-dialog
                                                 // gap (~417 µs) so the driver
                                                 // re-arms on an idle line
    static constexpr size_t kLosslessQueueMax = 64;  // lossless-wire backlog
    // How long a full Rx FIFO may stall the wire before the byte is lost the
    // way real hardware loses it. Long enough for any servicing guest, short
    // enough that an unresponsive one cannot wedge the line permanently.
    static constexpr int kMaxOverrunHold = 256;
                                                 // ceiling (~38 KB of frames,
                                                 // several seconds of guest
                                                 // drain): past it the wire
                                                 // drops like a real one
                                                 // rather than growing without
                                                 // bound behind a guest that
                                                 // stopped listening
};
