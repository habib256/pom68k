// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "Scc8530.h"
#include <cstdio>
#include <cstdlib>

// SCCDBG=1: trace every register access (wire-level debugging aid; the
// flag is resolved once).
static bool sccDbg() {
    static const bool on = std::getenv("SCCDBG") != nullptr;
    return on;
}

void Scc8530::reset() {
    ch_[0] = Chan{};
    ch_[1] = Chan{};
    ptr_ = 0;
    peerHold_ = 0;                    // no peer seen yet (line state, not
                                     // machine config — abortIdle_ persists)
    lineDriven_ = false;              // virgin line: never driven, reads clean
}

void Scc8530::setClocks(int64_t cpuHz, int64_t pclkHz) {
    cpuHz_ = cpuHz;
    pclkHz_ = pclkHz;
    updateSerial(ch_[0]);
    updateSerial(ch_[1]);
}

// Derive the channel's CPU-cycles-per-byte pace from the guest's serial
// programming — the SCC async-baud LLE (MAME z80scc.cpp as oracle):
//   clock mode  WR4 bits 7-6 → ×1/16/32/64 (get_clock_mode :1157)
//   BRG rate    src/(2+(WR13<<8|WR12))/(2·mode), src = WR14 bit 1 ? PCLK
//               : RTxC (get_brg_rate :2476)
//   routing     WR11 bits 4-3 = Tx clock source (update_serial :2565)
// SDLC (WR4 bits 5-4 = 10): on a Mac the LocalTalk wire is FM0 clocked off
// the DPLL at RTxC/16 = 230 400 bit/s — deriving exactly the historical
// 272/544/868 constants at 7.8336/15.6672/25 MHz, so LLAP gates see no
// timing shift. WR4 stop bits 00 with a non-SDLC sync mode (monosync/
// bisync) is not modelled → pace 0 → the legacy fallback.
void Scc8530::updateSerial(Chan& c) {
    c.pace = 0;
    if (cpuHz_ <= 0) return;
    if (sdlcMode(c)) {
        c.pace = int(cpuHz_ * 8 / (kRtxcHz / 16));
        return;
    }
    if ((c.wr[4] & 0x0C) == 0) return;           // sync modes: not derivable
    int clockMode = 1;
    switch (c.wr[4] & 0xC0) {
        case 0x40: clockMode = 16; break;
        case 0x80: clockMode = 32; break;
        case 0xC0: clockMode = 64; break;
    }
    int64_t bitRate = 0;
    switch (c.wr[11] & 0x18) {                    // Tx clock source
        case 0x00:                                // RTxC pin
            bitRate = kRtxcHz / clockMode;
            break;
        case 0x10:                                // baud-rate generator
            if (c.wr[14] & 0x01) {                // WR14 bit 0: BRG enable
                const int64_t src = (c.wr[14] & 0x02) ? pclkHz_ : kRtxcHz;
                const int64_t brgConst = 2 + ((int64_t(c.wr[13]) << 8) | c.wr[12]);
                if (src > 0 && brgConst > 0)
                    bitRate = src / brgConst / (2 * clockMode);
            }
            break;
        default:                                  // TRxC pin / DPLL: no
            break;                                // async clock modelled
    }
    if (bitRate <= 0) return;
    // Bits per character ×2 (half-stop-bit fixed point): 1 start + data
    // (WR5 bits 6-5) + parity (WR4 bit 0) + stop (WR4 bits 3-2).
    static const int dataBits[4] = { 5, 7, 6, 8 };
    int bits2 = 2 * (1 + dataBits[(c.wr[5] >> 5) & 3] + (c.wr[4] & 1 ? 1 : 0));
    switch (c.wr[4] & 0x0C) {
        case 0x04: bits2 += 2; break;             // 1 stop bit
        case 0x08: bits2 += 3; break;             // 1.5 stop bits
        case 0x0C: bits2 += 4; break;             // 2 stop bits
    }
    c.pace = int(cpuHz_ * bits2 / (2 * bitRate));
}

// SDLC FCS = CRC-16/X25 (poly $1021 reflected, init/xorout $FFFF). The
// drivers never read its value (the chip checks it), but appending the real
// FCS keeps the wire honest for future interop captures.
static uint16_t crc16x25(const uint8_t* d, size_t n) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        crc ^= d[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 1) ? uint16_t((crc >> 1) ^ 0x8408) : uint16_t(crc >> 1);
    }
    return uint16_t(~crc);
}

// RR0 external status: bit 0 = Rx char available, bit 3 = DCD level,
// bit 5 = CTS, bit 2 = TxD empty, bit 7 = Break/Abort.
//
// The standing Break/Abort of an open LocalTalk line exists only while
// the channel hunts in SDLC mode (WR4 bits 5-4 = 10): continuous marks
// are an SDLC ABORT. In async modes the same marks are normal idle —
// presenting bit 7 there fed an endless break "interrupt storm" to the
// OS 8.1 serial driver on channel B (async WR4 $4C) and stalled boot.
bool Scc8530::sdlcMode(const Chan& c) const {
    return (c.wr[4] & 0x30) == 0x20;
}

// RR1 bit 0 All Sent: the buffer AND the shifter are empty. Always set in
// the synchronous modes, where the transmitter idles on flags rather than
// on marks and there is no "still shifting a character" to report.
bool Scc8530::allSent(const Chan& c) const {
    return (c.wr[4] & 0x0C) == 0 || (!c.txBufFull && c.txShiftIn <= 0);
}

// The /RTS and /DTR output pins (MAME z80scc.cpp `update_rts:1184-1206`).
// Setting WR5 bit 1 asserts /RTS at once; CLEARING it releases the pin
// immediately only without Auto Enables — with WR3 bit 5 set the chip holds
// the line until the transmitter has completely emptied, so this is also
// called from tick() as the shifter drains. /DTR follows WR5 bit 7 unless
// WR14 bit 2 has repurposed the pin as the DMA request output.
void Scc8530::updateRts(Chan& c) {
    if (c.wr[5] & 0x02) c.rtsPin = false;                 // asserted (low)
    else if (!(c.wr[3] & 0x20) || allSent(c)) c.rtsPin = true;
    if (!(c.wr[14] & 0x04)) c.dtrPin = !(c.wr[5] & 0x80);
}

uint8_t Scc8530::rr0(const Chan& c) const {
    // bit 0 Rx Character Available: real (FIFO) or the legacy standing flag.
    // Break/Abort (bit 7): the LINE state — masked only while a frame is
    // actually on the wire (rxCur). Unread bytes sitting in the FIFO do NOT
    // hold the carrier: the 7.6 LAP open polls RR0 for the idle abort after
    // abandoning a frame mid-read, and masking on FIFO contents wedged it
    // ("Impossible d'ouvrir AppleTalk").
    const bool rxBusy = !c.rxCur.empty();
    // bit 2 Tx Buffer Empty is LIVE: it drops while a byte waits behind a
    // busy shifter — the pacing the LAP transmit loop polls on.
    return uint8_t(((rxStanding_ || !c.fifo.empty()) ? 0x01 : 0x00)
                   | (c.dcd ? 0x08 : 0x00)
                   | (c.txBufFull ? 0x00 : 0x04) | (ctsHigh_ ? 0x20 : 0x00)
                   | ((c.hunt && sdlcMode(c)) ? 0x10 : 0x00)
                   | (c.txUnderrun ? 0x40 : 0x00)
                   | ((openLine() && sdlcMode(c) && !rxBusy) ? 0x80 : 0x00));
}

// WR1 bits 4-3: 00 = Rx int off, 01 = first char + special, 10 = all chars
// + special, 11 = special only.
void Scc8530::raiseRxInt(Chan& c, bool special) {
    const int mode = (c.wr[1] >> 3) & 3;
    if (!mode) return;
    if (special) { c.specialIp = true; return; }
    if (mode == 2) c.rxIp = true;
    else if (mode == 1 && !c.firstCharSeen) { c.rxIp = true; c.firstCharSeen = true; }
}

void Scc8530::rxStartFrame(Chan& c, int chIdx) {
    // Drop the PREVIOUS frame's unread FCS residue before this frame opens.
    // The LLAP driver reads a frame by its DDP length and skips the trailing
    // FCS (it trusts the hardware CRC, RR1 bit 6). That frame's crc_hi (its
    // EOF byte) is usually paced into the FIFO only AFTER the driver has
    // already re-armed hunt — so it cannot be caught at the Enter-Hunt write
    // (rxCur still held it there) and lingers as a phantom EOF-flagged byte
    // at the HEAD of THIS frame. The 44-byte NBP LkUpReply then desynced on
    // that phantom EOF and the AFPServer entity never populated the guest's
    // Chooser even though the reply reached the node intact (empty server
    // list, 2026-07-22 GISTPERSO live capture — the running fix at the
    // Enter-Hunt write alone did not clear it). A frame only starts after the
    // inter-dialog gap, by which time the driver has drained everything it
    // wanted; an EOF-tagged FIFO tail here is therefore always stale residue.
    if (!c.fifo.empty() && (c.fifo.back().rr1 & 0x80)) {
        c.fifo.clear();
        c.rxIp = false;                          // Rx int is FIFO-level driven
    }
    c.rxCur = std::move(c.rxQueue.front().bytes);
    c.rxPace = c.rxQueue.front().pace;
    c.rxQueue.pop_front();
    c.rxPos = 0;
    c.rxTimer = c.rxPace;
    // Opening flag: Sync/Hunt clears — the LLAP carrier sense. A 1→0
    // transition is an ext/status source when WR15 bit 4 arms it.
    if (c.hunt) {
        c.hunt = false;
        if ((c.wr[15] & 0x10) && (c.wr[1] & 0x01)) {
            if (!c.latched) { c.rr0Latch = rr0(c); c.latched = true; }
            c.extPending = true;
        }
    }
    if (sccDbg())
        fprintf(stderr, "[scc] %c rx frame start (%zu bytes)\n",
                chIdx ? 'A' : 'B', c.rxCur.size());
}

// Pace one byte of the current frame onto the line; it reaches the 3-deep
// Rx FIFO only while the receiver is enabled (else it is lost on the wire —
// the carrier is still seen via RR0 bit 7).
void Scc8530::rxPushByte(Chan& c) {
    const bool last = c.rxPos + 1 == c.rxCur.size();
    if (!rxEnabled(c)) {
        c.rxPos++;
        if (last) {
            c.rxCur.clear();
            c.rxPos = 0;
            if (!c.hunt) c.hunt = true;          // line idles again
        }
        return;
    }
    if (c.fifo.size() >= 3) {
        if (!c.fifo.empty()) c.fifo.back().rr1 |= 0x20;
        raiseRxInt(c, true);
        // Not advancing rxPos re-presents the byte next byte-time: that is the
        // virtual wire's back-pressure, and the LLAP gate depends on it. But
        // UNBOUNDED it is a wedge — a guest that stops servicing (interrupts
        // masked, LAP torn down) held the frame forever, so rxCur never
        // emptied, rxIdle never rose, the IDG test could never pass and every
        // later frame stayed queued for good with carrier sense stuck on.
        // Real hardware simply loses the byte; hold briefly, then do the same.
        if (++c.rxOverrunHold <= kMaxOverrunHold) return;
        c.rxOverrunHold = 0;
        c.rxPos++;                               // byte lost, like the real chip
        if (last) {
            c.rxCur.clear();
            c.rxPos = 0;
            if (!c.hunt) c.hunt = true;          // line idles again
        }
        return;
    }
    c.rxOverrunHold = 0;
    // End of Frame (RR1 bit 7) rides the LAST byte (2nd FCS byte), and so
    // does the CRC verdict: the receiver re-computes the FCS over the
    // frame body and compares it to the received tail — RR1 bit 6 set =
    // CRC error (a corrupted/truncated wire frame), clear = good frame.
    uint8_t st = uint8_t((last ? 0x81 : 0x01) | kResidueAligned);
    if (last && c.rxCur.size() >= 2) {
        const size_t n = c.rxCur.size();
        const uint16_t want = crc16x25(c.rxCur.data(), n - 2);
        const uint16_t got = uint16_t(c.rxCur[n - 2] | (c.rxCur[n - 1] << 8));
        if (want != got) st |= 0x40;
    }
    Chan::RxByte b{c.rxCur[c.rxPos], st};
    c.fifo.push_back(b);
    c.rxPos++;
    raiseRxInt(c, false);
    if (last) {
        raiseRxInt(c, true);                     // special: EOF condition
        c.rxCur.clear();
        c.rxPos = 0;
        // Closing flag then idle: Hunt sets again (0→1 ext/status source).
        if (!c.hunt) {
            c.hunt = true;
            if ((c.wr[15] & 0x10) && (c.wr[1] & 0x01)) {
                if (!c.latched) { c.rr0Latch = rr0(c); c.latched = true; }
                c.extPending = true;
            }
        }
    }
}

// Async Rx entry (serial-port transports): one received character with
// optional wire-error flags — parity (RR1 bit 4, only meaningful when WR4
// bit 0 enables parity) and framing (RR1 bit 6 in async). The status rides
// the byte through the FIFO; the special condition raises at READ time
// (readData), parity only under WR1 bit 2. The transport owns the pacing.
void Scc8530::injectRxByte(int ch, uint8_t d, bool parityError, bool framingError) {
    Chan& c = ch_[ch & 1];
    if (sdlcMode(c) || !rxEnabled(c)) return;
    if (c.fifo.size() >= 3) {                    // overrun: drop, flag RR1.5
        if (!c.fifo.empty()) c.fifo.back().rr1 |= 0x20;
        raiseRxInt(c, true);
        return;
    }
    // The residue field is only ever LOADED by the receiver in SDLC mode;
    // in async it keeps its previous value, which for a channel that never
    // ran SDLC is the reset code — the same one the aligned frames carry.
    uint8_t st = uint8_t(0x01 | kResidueAligned);
    if (parityError && (c.wr[4] & 0x01)) st |= 0x10;
    if (framingError) st |= 0x40;
    c.fifo.push_back({d, st});
    raiseRxInt(c, false);
}

void Scc8530::injectRxFrame(int ch, const uint8_t* d, size_t n, bool express,
                            bool badFcs) {
    Chan& c = ch_[ch & 1];
    // Receiver off = no ear... except for express (cable-synthesized) frames:
    // LLAP is half-duplex — the driver disables Rx while transmitting the RTS
    // and only re-arms it on the EOM interrupt, which is the very tick that
    // synthesizes the CTS. A real peer's CTS starts an inter-frame gap later
    // (LLAP: within 200 µs), by which time the sender has re-armed Rx —
    // express frames therefore queue through the Rx-off window and carry
    // that gap as a start delay. Delivering them instantly at 8× wire speed
    // (the previous model) played the whole CTS while the driver was still
    // dropping RTS/TxEnable and re-arming Rx: every byte was discarded on
    // the wire and the Chooser lookup retried its RTS forever (2026-07-22).
    // A non-express frame is a REAL peer transmitting on the transport
    // (an LToUDP multicast frame, not the cable's own synthesized CTS):
    // it makes the line a live, terminated network, so the open-line
    // standing abort drops for a hold window (LLE_VS_HLE §1.8 / step 8).
    // Either way the line has now carried a frame — a previously-virgin
    // line is no longer clean once the transport goes quiet (§1.10).
    lineDriven_ = true;
    if (!express) peerHold_ = kPeerHold;
    if (!n || !sdlcMode(c)) return;
    // Real hardware: a non-express frame that arrives while the receiver is
    // OFF is lost (half-duplex — no ear). The lossless virtual wire instead
    // QUEUES it and holds playback until the guest re-arms Rx, so a server
    // reply generated during the guest's own transmit (its Rx is down until
    // its EOM ISR re-arms it) is delivered, not dropped. This is what the
    // AppleShare copy "saccade" was: replies flushed into a deaf receiver →
    // 1-2 s ATP retransmit each. Express (synthesized CTS) always queues
    // through the Rx-off window regardless (its whole purpose).
    if (!express && !rxEnabled(c) && !losslessRx_) return;
    // SDLC Address Search Mode (WR3 bit 2): the chip only opens the FIFO
    // when the first byte matches WR6 or the $FF broadcast.
    if ((c.wr[3] & 0x04) && d[0] != c.wr[6] && d[0] != 0xFF) return;
    std::vector<uint8_t> f(d, d + n);
    // badFcs models wire damage: the appended tail no longer matches the
    // body, so the receiver's FCS check (rxPushByte) flags RR1 bit 6.
    const uint16_t fcs = uint16_t(crc16x25(d, n) ^ (badFcs ? 0x5A5A : 0));
    f.push_back(uint8_t(fcs & 0xFF));            // FCS little-end first (X25)
    f.push_back(uint8_t(fcs >> 8));
    // A non-express (real peer) frame defers until the line has been idle
    // for LLAP's minimum 400 µs INTER-DIALOG gap. That idle is evaluated at
    // DEQUEUE from rxIdle (tick()), NOT baked in here: when two frames are
    // injected in one poll — the router's LkUp broadcast then afpd's
    // LkUpReply — the second's gap must be measured from the FIRST frame's
    // END, not from this injection instant (when the line still reads idle).
    // Baking `IDG - rxIdle` here made the reply start the instant the
    // broadcast finished, its first bytes landing in the still-closing FIFO
    // and the rest playing into hunt — the Chooser re-sent the AFPServer
    // lookup forever and never listed the server (2026-07-22 GISTPERSO live
    // capture). Express CTS frames keep their short fixed gap: an intra-
    // dialog CTS must land inside the sender's 200 µs INTER-FRAME window.
    // Playback runs at the effective (possibly virtual-wire-boosted) pace;
    // the express-CTS gap stays at the REAL pace — it models the SENDER's
    // post-EOM Rx re-arm window, which is guest code running in real time,
    // not a property of the wire.
    const int pace = paceCycles(ch);
    const int delay = express ? kCtsGapBytes * realPaceOf(c) : 0;
    // Safety valve: the lossless queue is UNBOUNDED by construction — it
    // holds a frame until the guest re-arms Rx, and nothing guarantees the
    // guest ever does. A guest that stops servicing its LAP driver (a modal
    // tracking loop, a wedged stack) therefore turns backpressure into
    // unbounded growth: the sender keeps producing, the queue keeps
    // swallowing, latency and memory climb without limit and every frame in
    // it is already too old to be useful. A real wire cannot do this — it
    // has NO buffer, so congestion self-limits by dropping. Past the cap we
    // do the same (tail drop, counted): the peer falls back to its normal
    // retransmit, which is a stall, not a runaway. Express frames are never
    // dropped — the CTS is the handshake itself.
    // NOT gated on losslessRx_: that flag is only set for the in-process hub
    // WITHOUT a cable, so with POM68K_LTOUDP=1 — the one path fed by an
    // unfiltered multicast socket — the cap was dead code and the queue could
    // grow without limit until OOM. The reasoning above applies to every mode.
    if (!express && c.rxQueue.size() >= kLosslessQueueMax) {
        c.rxDropped++;
        return;
    }
    c.rxQueue.push_back({std::move(f), pace, delay, express, c.wireClk});
    if (c.rxQueue.size() > c.rxQueueMax) c.rxQueueMax = c.rxQueue.size();
}

uint8_t Scc8530::readCtl(int channel) {
    Chan& c = ch_[channel & 1];
    int reg = ptr_;
    ptr_ = 0;                                   // pointer auto-resets
    uint8_t rv = readCtl_(channel, c, reg);
    if (sccDbg()) fprintf(stderr, "[scc] %c rr%d -> %02X\n", channel ? 'A' : 'B', reg, rv);
    return rv;
}

uint8_t Scc8530::readCtl_(int channel, Chan& c, int reg) {
    // NMOS 8530 read-register aliases (MAME z80scc.cpp:1461-1467). Without the
    // remap, RR11 — the image of RR15 — returned 0, i.e. "every ext/status
    // source disabled": exactly the failure already fixed for RR15 itself.
    if (reg > 3 && reg < 8) reg &= 3;
    else if (reg == 9)  reg = 13;
    else if (reg == 11) reg = 15;
    switch (reg) {
        case 0: {
            rr0Reads++;
            // Only D7-D3 (Break, Underrun, CTS, Sync/Hunt, DCD) freeze at an
            // ext/status latch; D2-D0 (TxE, zero count, Rx available) always
            // read LIVE (Zilog SCC UM §3.2) — freezing bit 0 hid every Rx
            // byte from a driver with an unserviced ext/status pending.
            uint8_t live = rr0(c);
            return c.latched ? uint8_t((c.rr0Latch & 0xF8) | (live & 0x07))
                             : live;
        }
        case 1: {                               // status of the FIFO-top byte
            // Bit 0 All Sent is LIVE: set when both the buffer and the
            // shifter are empty (Zilog UM — and always set in sync modes,
            // where the transmitter idles on flags, not marks).
            const uint8_t base = c.fifo.empty() ? c.rr1Rd : c.fifo.front().rr1;
            return uint8_t((base & ~0x01) | (allSent(c) ? 0x01 : 0x00));
        }
        case 2: {
            rr2Reads++;
            // RR2 on channel B returns the vector MODIFIED by the highest
            // pending source (status-low, V3..V1): A ext = 101, B ext = 001,
            // none = 011. The Mac Plus level-2 handler dispatches on this.
            uint8_t vec = ch_[1].wr[2];
            if (channel == 0) {
                // Status-low V3..V1 code by highest-priority source. The
                // Z8530 ranks, highest first: ChA Rx, ChA Tx, ChA Ext,
                // ChB Rx, ChB Tx, ChB Ext; special Rx outranks Rx. Codes:
                // A Special=111, A Rx=110, A Tx=100, A Ext=101,
                // B Special=011, B Rx=010, B Tx=000, B Ext=001, none=011.
                int code = ch_[1].specialIp  ? 0b111
                         : ch_[1].rxIp       ? 0b110
                         : ch_[1].txIp       ? 0b100
                         : ch_[1].extPending ? 0b101
                         : ch_[0].specialIp  ? 0b011
                         : ch_[0].rxIp       ? 0b010
                         : ch_[0].txIp       ? 0b000
                         : ch_[0].extPending ? 0b001 : 0b011;
                vec = uint8_t((vec & ~0x0E) | (code << 1));
            }
            return vec;
        }
        case 3:                                 // RR3 (channel A only): IP bits
            rr3Reads++;                         // D5=A Rx, D4=A Tx, D3=A Ext,
            if (channel == 1)                   // D2=B Rx, D1=B Tx, D0=B Ext
                return uint8_t(((ch_[1].rxIp || ch_[1].specialIp) ? 0x20 : 0) |
                               (ch_[1].txIp ? 0x10 : 0) |
                               (ch_[1].extPending ? 0x08 : 0) |
                               ((ch_[0].rxIp || ch_[0].specialIp) ? 0x04 : 0) |
                               (ch_[0].txIp ? 0x02 : 0) |
                               (ch_[0].extPending ? 0x01 : 0));
            return 0;
        case 15:                                // RR15 = WR15 (ext IE mask);
            return uint8_t(c.wr[15] & 0xFA);    // bits 0/2 always read 0.
                                                // The LAP's level-4 ISR reads
                                                // this to route the ext source
                                                // — returning 0 made every
                                                // ext/status look disabled and
                                                // the LAP state machine never
                                                // advanced past carrier sense.
        case 8:  return readData(channel);       // RR8 = the Rx data port
        case 12: return c.wr[12];                // baud generator, low
        case 13: return c.wr[13];                // baud generator, high
        default: return 0;
    }
}

// Buffer → shifter, gated on WR5 bit 3 Tx Enable (z80scc tra_callback
// :1037 sends marks while disabled; tra_complete :1075 reloads only under
// TX_ENABLE). The buffer-empty TRANSITION is the TxIP source — a byte the
// shifter picks up at once (idle transmitter) interrupts immediately, a
// byte parked behind a busy shifter interrupts one character time later.
// rem carries the drain overshoot into the next character slot so
// back-to-back bytes keep the exact wire cadence.
bool Scc8530::txLoad(Chan& c, int rem) {
    if (!c.txBufFull || !(c.wr[5] & 0x08)) return false;
    // SDLC frame capture at the wire: bytes accumulate as the shifter
    // takes them, until the underrun (frame complete, onTxFrame) or a
    // Send Abort (discard). The chip appends the FCS itself, so txBuf is
    // the raw LLAP frame.
    if (sdlcMode(c)) c.txBuf.push_back(c.txBufData);
    c.txShiftData = c.txBufData;
    c.txBufFull = false;
    c.txGracing = false;                        // a late byte ends the grace
    c.txShiftIn = paceOf(c) + rem;
    if (c.txShiftIn <= 0) c.txShiftIn = 1;
    c.txEmptyEvent = true;                      // buffer BECAME empty
    if (c.wr[1] & 0x02) c.txIp = true;          // WR1 bit 1 = Tx Int Enable
    return true;
}

// Data port: one-slot Tx buffer feeding the paced shifter (txLoad). A
// driver that sleeps on the completion interrupt (AppleTalk LAP, O6.10)
// still progresses — TxIP latches the moment the buffer hands its byte
// to the shifter — but the wire now carries one character per character
// time instead of accepting bytes instantly (SCC Tx-engine LLE).
void Scc8530::writeData(int channel, uint8_t d) {
    if (sccDbg()) fprintf(stderr, "[scc] %c data <- %02X\n", channel ? 'A' : 'B', d);
    Chan& c = ch_[channel & 1];
    // Overwriting a full buffer loses the previous byte, as on the real
    // chip — drivers pace on TBE (RR0 bit 2) or TxIP.
    c.txBufData = d;
    c.txBufFull = true;
    // Data resuming while the SDLC tail drains cancels the underrun: the
    // frame continues (the old model's "each byte pushes the underrun out").
    if (c.txFlushing) {
        c.txFlushing = false;
        c.txShiftIn = 0;
    }
    if (c.txShiftIn <= 0) txLoad(c, 0);
}

uint8_t Scc8530::readData(int channel) {
    Chan& c = ch_[channel & 1];
    if (c.fifo.empty()) return 0;               // dead line / drained FIFO
    const Chan::RxByte b = c.fifo.front();
    c.fifo.pop_front();
    c.rr1Rd = b.rr1;
    if (c.fifo.empty()) c.rxIp = false;         // level: FIFO drained
    // Special Receive Condition interrupts for ERROR bytes raise when the
    // byte is READ from the FIFO, not when queued (z80scc data_read :2130;
    // Zilog UM — one status read after the fact instead of one per byte).
    // CRC/framing (bit 6) is always special; parity (bit 4) only when WR1
    // bit 2 makes it one. Error Reset clears (specialIp + rr1Rd bits).
    if (b.rr1 & uint8_t(0x40 | ((c.wr[1] & 0x04) ? 0x10 : 0)))
        raiseRxInt(c, true);
    if (sccDbg()) fprintf(stderr, "[scc] %c data -> %02X rr1=%02X\n",
                          channel ? 'A' : 'B', b.d, b.rr1);
    return b.d;
}

void Scc8530::writeCtl(int channel, uint8_t v) {
    ctlWrites++;
    Chan& c = ch_[channel & 1];
    if (sccDbg()) fprintf(stderr, "[scc] %c ctl%s wr%d <- %02X\n", channel ? 'A' : 'B', ptr_ ? "" : "0", ptr_, v);
    if (ptr_ == 0) {
        ptr_ = v & 0x07;
        if ((v & 0x38) == 0x08) ptr_ |= 8;      // point-high command
        // WR0 bits 7-6 (CRC/EOM commands) are independent of bits 5-3.
        if ((v & 0xC0) == 0xC0) {
            // Reset Tx Underrun/EOM latch — SDLC frame start. When the
            // drained shifter underruns, the chip sends CRC + closing
            // flag (kTailBytes character times at the programmed pace)
            // and the latch SETS again; that 0→1 edge is the frame-
            // complete ext/status interrupt the LAP sleeps on (Zilog
            // SCC UM §4.4.1). An idle transmitter underruns at once —
            // the tail starts draining now; a busy one arms it when the
            // shifter empties with nothing buffered (tick).
            c.txUnderrun = false;
            if (c.txShiftIn <= 0 && !c.txBufFull) {
                c.txFlushing = true;
                c.txShiftIn = kTailBytes * paceOf(c);
            }
        }
        switch (v & 0x38) {
            case 0x18:                          // Send Abort (SDLC)
                // Aborting ends the frame at once: latch sets, ext/status
                // interrupt if armed (WR15 bit 6 + WR1 bit 0). The aborted
                // frame never reaches the wire — but the abort sequence
                // itself does: the line has been driven (openLine).
                if (sdlcMode(c)) lineDriven_ = true;
                c.txBuf.clear();
                c.txBufFull = false;
                c.txShiftIn = 0;
                c.txFlushing = false;
                c.txGracing = false;
                if (!c.txUnderrun) {
                    c.txUnderrun = true;
                    if ((c.wr[15] & 0x40) && (c.wr[1] & 0x01)) {
                        if (!c.latched) { c.rr0Latch = rr0(c); c.latched = true; }
                        c.extPending = true;
                    }
                }
                break;
            case 0x20:                          // Enable Int on Next Rx Char
                c.firstCharSeen = false;
                break;
            case 0x30:                          // Error Reset
                c.specialIp = false;            // clears the special Rx
                c.firstCharSeen = false;        // condition; re-arms 1st-char
                c.rr1Rd &= ~0x70;               // resets the error bits in
                                                // RR1 (parity/overrun/CRC —
                                                // z80scc WR0_ERROR_RESET)
                break;
            case 0x10:                          // Reset External/Status ints
                c.extPending = false;
                c.latched = false;
                // Servicing re-arms the standing-abort presentation: on
                // an open line the SDLC receiver detects the next abort
                // ~130 µs later (tick delivers it). Event-driven, so a
                // channel that never services gets exactly ONE latch.
                if (openLine() && sdlcMode(c) && (c.wr[15] & 0x80) && (c.wr[1] & 0x01))
                    c.relatch = kAbortRelatch;
                break;
            case 0x28:                          // Reset Tx Int Pending
                c.txIp = false;
                c.txEmptyEvent = false;         // event consumed
                break;
            case 0x38:                          // Reset Highest IUS
                c.extPending = false;
                c.latched = false;
                c.txIp = false;
                c.txEmptyEvent = false;
                c.specialIp = false;            // (rxIp is FIFO-level driven)
                if (openLine() && sdlcMode(c) && (c.wr[15] & 0x80) && (c.wr[1] & 0x01))
                    c.relatch = kAbortRelatch;
                break;
            default: break;
        }
        return;
    }
    // WR2 (interrupt vector) and WR9 (master interrupt control) are
    // chip-global on the 8530 — mirror them so a write through either
    // channel's control port is seen by both (the LC II System programs
    // them through channel B)
    if (ptr_ == 2 || ptr_ == 9) ch_[0].wr[ptr_] = ch_[1].wr[ptr_] = v;
    else c.wr[ptr_] = v;

    // Serial-timing registers → re-derive the channel byte pace (MAME
    // z80scc.cpp update_serial is called from the same register writes).
    if (ptr_ == 4 || ptr_ == 5 || ptr_ == 11 || ptr_ == 12 || ptr_ == 13 ||
        ptr_ == 14)
        updateSerial(c);

    // WR5 carries RTS/DTR, WR3 the Auto Enables bit that defers the RTS
    // release, WR14 the DTR/REQ repurposing — all three move the pins.
    if (ptr_ == 3 || ptr_ == 5 || ptr_ == 14) updateRts(c);

    // WR5 bit 3 Tx Enable coming up flushes a byte parked in the buffer
    // while the transmitter was disabled (txLoad no-ops when still off).
    if (ptr_ == 5 && c.txShiftIn <= 0 && !c.txFlushing) txLoad(c, 0);

    // WR9 D7-D6: 01 = Channel Reset B, 10 = Channel Reset A, 11 = hardware
    // reset. Purge the channel's Rx/Tx machinery — the 7.6 LAP open resets
    // channel B and re-inits from scratch; keeping the stale Rx FIFO made
    // RR0 show RxCA forever on a line the driver believed re-idled.
    if (ptr_ == 9 && (v & 0xC0)) {
        auto resetChan = [](Chan& c2) {
            c2.fifo.clear();
            c2.rxQueue.clear();
            c2.rxCur.clear();
            c2.rxPos = 0;
            c2.rxTimer = 0;
            c2.rxIp = c2.specialIp = false;
            c2.firstCharSeen = false;
            c2.txBuf.clear();
            c2.txIp = false;
            c2.txEmptyEvent = false;
            c2.txBufFull = false;
            c2.txShiftIn = 0;
            c2.txFlushing = false;
            c2.txGracing = false;
            c2.extPending = false;
            c2.latched = false;
            c2.hunt = false;
            c2.txUnderrun = true;
            c2.relatch = 0;
            // The write registers reset too (Zilog / MAME z80scc.cpp:984-1013).
            // Leaving WR1/WR5/WR15 armed meant the relatch path could raise an
            // ext/status interrupt on a channel whose driver state was torn
            // down, and a data-port write before Tx was re-enabled went onto
            // the wire instead of being held.
            c2.wr[1]  &= 0x24;
            c2.wr[3]  &= 0x01;
            c2.wr[4]   = 0x04;
            c2.wr[5]   = 0x00;
            c2.wr[14]  = uint8_t((c2.wr[14] & 0xC3) | 0x20);
            c2.wr[15]  = 0xF8;
            // WR5 is now clear, so both pins release (Zilog: a channel
            // reset drives /RTS and /DTR high).
            c2.rtsPin = c2.dtrPin = true;
        };
        if ((v & 0xC0) == 0x40 || (v & 0xC0) == 0xC0) resetChan(ch_[0]);
        if ((v & 0xC0) == 0x80 || (v & 0xC0) == 0xC0) resetChan(ch_[1]);
    }
    if (ptr_ == 9) { updateSerial(ch_[0]); updateSerial(ch_[1]); }

    // WR3 bit 4 = Enter Hunt Mode. On an idle line the hunt persists — RR0
    // bit 4 stays set, which is what the LLAP sender's carrier sense wants
    // ("no carrier, clear to transmit"); an incoming frame clears it
    // (rxStartFrame). A Sync/Hunt transition is an ext/status source when
    // WR15 bit 4 arms it.
    //
    // Enter Hunt must NOT abort a frame already being clocked in. A synced
    // receiver is past the flag-hunt phase, and the LLAP driver re-arms
    // with Enter Hunt the instant it finishes the PREVIOUS frame's EOF —
    // which on our byte-paced wire is exactly when the NEXT frame has
    // already started. Clearing rxCur there truncated every long directed
    // frame to its first 2-3 bytes: the 44-byte NBP LkUpReply lost its DDP
    // payload, so the AppleShare server never populated the Chooser even
    // though the reply reached the node on the wire (2026-07-22, live
    // GISTPERSO capture). Only honour Enter Hunt when no frame is in
    // flight; an in-flight frame finishes and re-enters hunt at its EOF
    // (rxPushByte). Queued-but-not-started frames (rxQueue) are untouched
    // either way, as before.
    if (ptr_ == 3 && (v & 0x10) && c.rxCur.empty()) {
        // Frame-boundary re-arm: flush the previous frame's UNREAD FCS
        // residue. The LLAP driver reads a received frame by its DDP length
        // and re-enters hunt for the next opening flag WITHOUT reading the
        // trailing FCS — it trusts the hardware CRC result (RR1 bit 6). Left
        // in the 3-deep FIFO, that frame's crc_hi (its End-Of-Frame byte)
        // surfaced at the HEAD of the NEXT frame as a phantom EOF-flagged
        // byte (its value tracks the previous frame's CRC, not this one's
        // data). The 44-byte NBP LkUpReply then desynced on that phantom
        // EOF — the driver saw a 1-byte "frame", error-reset and re-hunted
        // through the real DDP/NBP header — so the AFPServer entity never
        // populated the guest's Chooser even though the reply reached the
        // node intact on the wire (empty server list, 2026-07-22 GISTPERSO
        // capture). rxCur.empty() means no NEXT frame is being clocked yet;
        // an EOF-flagged FIFO tail means the bytes are a COMPLETED frame's
        // residue — safe to drop. (A real Enter Hunt resets the receive
        // path; it does not carry a finished frame's FCS into the next one.)
        if (!c.fifo.empty() && (c.fifo.back().rr1 & 0x80)) {
            c.fifo.clear();
            c.rxIp = false;                  // Rx int is FIFO-level driven
        }
        c.rxPos = 0;
        if (!c.hunt) {
            c.hunt = true;
            if ((c.wr[15] & 0x10) && (c.wr[1] & 0x01)) {
                if (!c.latched) { c.rr0Latch = rr0(c); c.latched = true; }
                c.extPending = true;
            }
        }
    }
    // Rx disable (WR3 bit 0 cleared) flushes the CHIP's receive path —
    // FIFO and pending Rx interrupts. The wire (rxQueue/rxCur) is not the
    // chip's to empty: frames in flight keep playing with their bytes lost
    // (rxPushByte), and queued express frames survive the half-duplex
    // Rx-off window the LLAP sender opens around its RTS (2026-07-22 —
    // flushing the queue here killed the cable's delayed CTS).
    if (ptr_ == 3 && !(v & 0x01)) {
        c.fifo.clear();
        c.rxIp = false;
        c.specialIp = false;
        c.firstCharSeen = false;
    }

    // Arming Break/Abort IE (WR15 bit 7) on an open line latches the
    // external/status interrupt at once: the abort condition is already
    // standing (SDLC hunt, continuous marks). This is the carrier-sense
    // interrupt AppleTalk's LAP manager waits on (O6.10). Gated on WR1
    // bit 0 (per-channel Ext Int Enable) like the DCD path — a real
    // 8530 requires it (review 2026-07-16).
    if (ptr_ == 15 && (v & 0x80) && openLine() && sdlcMode(c) && (c.wr[1] & 0x01)) {
        if (!c.latched) { c.rr0Latch = rr0(c); c.latched = true; }
        c.extPending = true;
    }
    // Symmetric case: WR15 bit 7 armed FIRST, then WR1 bit 0 (Ext Int Enable)
    // written — the ext/status request must latch when the last required
    // enable bit is set, order-independent (Zilog SCC UM). Without this the
    // first-latch timing differed by write order (the tick() re-present only
    // papered over it ~130 µs later).
    if (ptr_ == 1 && (v & 0x01) && openLine() && sdlcMode(c) && (c.wr[15] & 0x80)) {
        if (!c.latched) { c.rr0Latch = rr0(c); c.latched = true; }
        c.extPending = true;
    }
    // Enabling Tx interrupts (WR1 bit 1) presents a PENDING became-empty
    // event (a byte was written since the last Reset Tx Int Pending) —
    // but a never-filled buffer does NOT interrupt on enable: the
    // 8530's TxIP is set when the buffer BECOMES empty, not because it
    // is empty (Zilog SCC UM; review 2026-07-16 — the old
    // latch-on-enable fired a spurious level-2 on the Plus for any app
    // arming WR1 bit 1 during driver open).
    if (ptr_ == 1 && (v & 0x02) && c.txEmptyEvent) c.txIp = true;
    ptr_ = 0;
}

// Mouse quadrature: any DCD transition latches an external/status interrupt
// when WR15 DCD IE (bit 3) and WR1 ext-int-enable (bit 0) allow it. The
// driver reads RR0 (latched at the transition), then resets ext status.
void Scc8530::setDcd(int channel, bool level) {
    Chan& c = ch_[channel & 1];
    if (c.dcd == level) return;
    dcdEdges++;
    c.dcd = level;
    bool dcdIe = (c.wr[15] & 0x08) != 0;
    bool extIe = (c.wr[1] & 0x01) != 0;
    if (dcdIe && extIe) {
        if (!c.latched) { c.rr0Latch = rr0(c); c.latched = true; }
        c.extPending = true;
    }
}

bool Scc8530::irqAsserted() const {
    bool mie = (ch_[1].wr[9] & 0x08) != 0;      // WR9 master interrupt enable
    return mie && (ch_[0].extPending || ch_[1].extPending
                || ch_[0].txIp || ch_[1].txIp
                || ch_[0].rxIp || ch_[1].rxIp
                || ch_[0].specialIp || ch_[1].specialIp);
}

// Re-present the standing Break/Abort on an open LocalTalk line —
// EVENT-driven (review 2026-07-16): each Reset Ext/Status the driver
// issues re-arms a ~130 µs countdown (writeCtl case $10/$38); when it
// expires the next abort is presented. ~2000 cycles between aborts is
// well under the LAP's per-retry budget, so its 32-deep retry counter
// runs down in a few ms and it gives up. A channel that arms WR15 bit 7
// but never services latches exactly once (a real 8530 latches on
// transitions, not levels) — no storm for non-LAP ext/status users.
// Guarded by abortIdle_ (LC II only), so the Plus mouse path is untouched.
bool Scc8530::tick(int cycles) {
    bool changed = false;
    // The "peer present" window runs down chip-wide (the LocalTalk line is
    // channel B, but the counter is a single line state). When it expires
    // the peer is treated as gone and the open-line abort returns.
    if (peerHold_ > 0) { peerHold_ -= cycles; if (peerHold_ < 0) peerHold_ = 0; }
    for (int i = 0; i < 2; i++) {
        Chan& c = ch_[i];
        // ── Tx engine: the shifter drains one character per character
        // time (paceOf); the buffered byte reloads it (TxIP per reload,
        // txLoad). When it empties with nothing buffered and an SDLC
        // frame is open, the chip appends CRC + closing flag (kTailBytes
        // character times — txFlushing), then the Tx Underrun/EOM latch
        // sets: frame complete, the raw bytes reach the wire. Runs on
        // every machine (the engine is architectural, not LC II-specific).
        if (c.txShiftIn > 0) {
            c.txShiftIn -= cycles;
            while (c.txShiftIn <= 0) {
                const int rem = c.txShiftIn;
                if (c.txFlushing) {
                    // CRC + closing flag drained: underrun/EOM. The frame
                    // just DROVE the line: the LLAP trailer ends in a real
                    // abort sequence and the driver then releases the line
                    // mid-mark — from here on the no-peer idle genuinely
                    // reads as a standing abort (openLine). Latch it
                    // BEFORE the ext/status capture so the RR0 latch
                    // carries bit 7 along with the EOM bit.
                    if (sdlcMode(c)) lineDriven_ = true;
                    c.txFlushing = false;
                    c.txShiftIn = 0;
                    c.txUnderrun = true;
                    if (!c.txBuf.empty()) {
                        if (onTxFrame) onTxFrame(i, c.txBuf.data(), c.txBuf.size());
                        c.txBuf.clear();
                    }
                    // EOM interrupt (WR15 bit 6) — and, now that the line
                    // has been driven, the trailer's own abort presents too
                    // when only Break/Abort IE (WR15 bit 7) is armed: the
                    // first ENQ probe is what starts the LAP's abort stream
                    // on a previously-virgin line.
                    const bool eomInt = (c.wr[15] & 0x40) != 0;
                    const bool abortInt =
                        openLine() && sdlcMode(c) && (c.wr[15] & 0x80) != 0;
                    if ((eomInt || abortInt) && (c.wr[1] & 0x01)) {
                        if (!c.latched) { c.rr0Latch = rr0(c); c.latched = true; }
                        c.extPending = true;
                        changed = true;
                    }
                    if (!txLoad(c, rem)) break;   // a late byte may reload
                    if (c.txIp) changed = true;
                } else {
                    // WR14 bit 4 Local Loopback (Zilog UM §5.4): the
                    // character that just finished shifting re-enters the
                    // SAME channel's receiver. The LC ROM's SCC POST sends
                    // through loopback and spins on RR0 bit 0 for its own
                    // bytes (async only — LLAP never uses loopback).
                    if ((c.wr[14] & 0x10) && !sdlcMode(c)) {
                        injectRxByte(i, c.txShiftData, false, false);
                        changed = true;
                    }
                    if (txLoad(c, rem)) {
                        if (c.txIp) changed = true;   // next byte on the wire
                    } else {
                        c.txShiftIn = 0;
                        if (sdlcMode(c) && !c.txUnderrun) {
                            // Virtual-wire grace: at a boosted pace the
                            // guest's Tx feed loop (tuned for the real
                            // 230.4 kbit/s cadence) may not have the next
                            // byte in yet — that is NOT an intentional
                            // end-of-frame. Hold the frame open one REAL
                            // byte-time; a loop that kept up on hardware
                            // always refills within it. An intentional end
                            // just reaches the tail one real byte-time
                            // later (EOM slightly deferred, harmless).
                            if (wirePace_ > 0 && !c.txGracing) {
                                c.txGracing = true;
                                c.txShiftIn = realPaceOf(c) + rem;
                                if (c.txShiftIn <= 0) c.txShiftIn = 1;
                                continue;
                            }
                            c.txGracing = false;
                            c.txFlushing = true;  // open frame underruns:
                            c.txShiftIn = kTailBytes * paceOf(c) + rem;
                            // ≤ 0: the tail fits in this tick too — the
                            // while loop re-enters and completes the EOM.
                        } else {
                            break;                // transmitter idles
                        }
                    }
                }
            }
        }
        // Auto Enables holds /RTS asserted past the WR5 write that cleared
        // it, until the last character has left the shifter — so the
        // release lands HERE, as the transmitter drains, not at the write
        // (MAME z80scc.cpp `tra_complete:1090`). Idempotent, and only ever
        // moves the pin on that one transition.
        if (!c.rtsPin && !(c.wr[5] & 0x02)) updateRts(c);
        // ── Rx pacing: one byte per LocalTalk byte-time into the FIFO ──
        // The LINE runs regardless of the receiver: LLAP senders wait for
        // the CTS with Rx disabled, watching RR0 bit 7 for the carrier
        // (the 7.5 LAP polls $F4→ waiting for the abort to drop). A frame
        // therefore always plays out on the wire — bytes only reach the
        // FIFO while the receiver is enabled (rxPushByte).
        if (sdlcMode(c)) {
            // Line-idle clock for the IDG deferral (injectRxFrame): busy
            // while a frame is playing, accumulating (capped) otherwise.
            c.wireClk += cycles;
            if (!c.rxCur.empty()) c.rxIdle = 0;
            else if (c.rxIdle < (1 << 24)) c.rxIdle += cycles;
            if (c.rxCur.empty() && !c.rxQueue.empty()) {
                // Start the queued frame once the inter-frame gap has
                // elapsed. Express (synthesized CTS): a short fixed
                // countdown so it lands inside the sender's IFG window.
                // Non-express (real peer): require a full LLAP IDG of
                // ACTUAL line-idle since the previous frame ended (rxIdle)
                // — this is what serializes back-to-back injected frames
                // (router LkUp broadcast + afpd LkUpReply) so the second
                // no longer plays into the FIFO still closing on the first
                // (the empty-Chooser bug, 2026-07-22).
                Chan::RxFrame& f = c.rxQueue.front();
                bool ready;
                if (f.express) {
                    if (f.delay > 0) f.delay -= cycles;
                    ready = f.delay <= 0;
                } else {
                    // The IDG is DRIVER turnaround time (protocol handler +
                    // re-arm between dialogs) — real pace even when the
                    // virtual wire boosts the byte pace.
                    ready = c.rxIdle >= kIdgBytes * realPaceOf(c);
                }
                // Lossless virtual wire: hold the frame until (a) the guest
                // is actually LISTENING — a reply queued during the guest's
                // half-duplex transmit opens only once its EOM ISR re-arms
                // Rx, never into a deaf receiver — and (b) the FIFO has
                // drained, so the stale-residue clear in rxStartFrame can't
                // eat undelivered bytes and no frame ever overruns. Express
                // (CTS) keeps threading the Rx-off window as before.
                if (losslessRx_ && !f.express &&
                    (!rxEnabled(c) || !c.fifo.empty()))
                    ready = false;
                if (ready) {
                    const int64_t held = c.wireClk - f.queuedAt;
                    if (held > c.rxHoldMax) c.rxHoldMax = held;
                    bool wasIrq = c.rxIp || c.specialIp || c.extPending;
                    rxStartFrame(c, i);
                    changed = changed || (!wasIrq && (c.extPending));
                }
            }
            if (!c.rxCur.empty()) {
                c.rxTimer -= cycles;
                while (c.rxTimer <= 0 && !c.rxCur.empty()) {
                    // Lossless virtual wire: HOLD the byte on the wire —
                    // rescheduling one pace ahead — rather than let
                    // rxPushByte drop it, whenever the guest can't take it:
                    // its Rx is off (mid-frame turnaround) OR its 3-byte
                    // FIFO is full. Throughput self-limits to the guest's
                    // ISR drain rate and no byte is ever lost, so no ATP
                    // retransmit fires (the copy "saccade").
                    if (losslessRx_ && (!rxEnabled(c) || c.fifo.size() >= 3)) {
                        c.rxTimer = c.rxPace;
                        break;
                    }
                    c.rxTimer += c.rxPace;
                    bool was = c.rxIp || c.specialIp;
                    rxPushByte(c);
                    if (!was && (c.rxIp || c.specialIp)) changed = true;
                }
                if (c.rxCur.empty()) c.rxTimer = 0;
            }
        }
        if (!openLine()) continue;
        if (c.relatch <= 0) continue;
        c.relatch -= cycles;
        if (c.relatch > 0) continue;
        c.relatch = 0;
        if (sdlcMode(c) && (c.wr[15] & 0x80) && (c.wr[1] & 0x01) && !c.extPending) {
            if (!c.latched) { c.rr0Latch = rr0(c); c.latched = true; }
            c.extPending = true;
            changed = true;
        }
    }
    return changed;
}

int Scc8530::cyclesToNextEvent() const {
    int best = peerHold_ > 0 ? peerHold_ : 0x7fffffff;
    auto take = [&](int n) { if (n > 0 && n < best) best = n; };
    for (const Chan& c : ch_) {
        take(c.txShiftIn);
        take(c.relatch);
        if (!c.rxCur.empty()) take(c.rxTimer > 0 ? c.rxTimer : 1);
        if (c.rxCur.empty() && !c.rxQueue.empty()) {
            const Chan::RxFrame& f = c.rxQueue.front();
            if (f.express) {
                take(f.delay > 0 ? f.delay : 1);
            } else if (!losslessRx_ || (rxEnabled(c) && c.fifo.empty())) {
                const int left = kIdgBytes * realPaceOf(c) - c.rxIdle;
                take(left > 0 ? left : 1);
            }
        }
    }
    return best;
}
