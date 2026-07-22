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

uint8_t Scc8530::rr0(const Chan& c) const {
    // bit 0 Rx Character Available: real (FIFO) or the legacy standing flag.
    // Break/Abort (bit 7): the LINE state — masked only while a frame is
    // actually on the wire (rxCur). Unread bytes sitting in the FIFO do NOT
    // hold the carrier: the 7.6 LAP open polls RR0 for the idle abort after
    // abandoning a frame mid-read, and masking on FIFO contents wedged it
    // ("Impossible d'ouvrir AppleTalk").
    const bool rxBusy = !c.rxCur.empty();
    return uint8_t(((rxStanding_ || !c.fifo.empty()) ? 0x01 : 0x00)
                   | (c.dcd ? 0x08 : 0x00) | 0x04 | (ctsHigh_ ? 0x20 : 0x00)
                   | ((c.hunt && sdlcMode(c)) ? 0x10 : 0x00)
                   | (c.txUnderrun ? 0x40 : 0x00)
                   | ((abortIdle_ && sdlcMode(c) && !rxBusy) ? 0x80 : 0x00));
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
    if (c.fifo.size() >= 3) {                    // overrun: drop, flag RR1.5
        if (!c.fifo.empty()) c.fifo.back().rr1 |= 0x20;
        raiseRxInt(c, true);
        return;
    }
    // End of Frame (RR1 bit 7) rides the LAST byte (2nd FCS byte); CRC
    // error (bit 6) stays 0 = good frame. Bit 0 = all-sent.
    Chan::RxByte b{c.rxCur[c.rxPos], uint8_t(last ? 0x81 : 0x01)};
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

void Scc8530::injectRxFrame(int ch, const uint8_t* d, size_t n, bool express) {
    Chan& c = ch_[ch & 1];
    // Receiver off = no ear... except for express (cable-synthesized) frames:
    // LLAP is half-duplex — the driver disables Rx while transmitting the RTS
    // and only re-arms it on the EOM interrupt, which is the very tick that
    // synthesizes the CTS. A real peer's CTS arrives ~100 µs later with the
    // receiver back on; express frames therefore WAIT in the queue (delivery
    // in tick() is already gated on rxEnabled) instead of being dropped.
    if (!n || !sdlcMode(c) || (!express && !rxEnabled(c))) return;
    // SDLC Address Search Mode (WR3 bit 2): the chip only opens the FIFO
    // when the first byte matches WR6 or the $FF broadcast.
    if ((c.wr[3] & 0x04) && d[0] != c.wr[6] && d[0] != 0xFF) return;
    std::vector<uint8_t> f(d, d + n);
    const uint16_t fcs = crc16x25(d, n);
    f.push_back(uint8_t(fcs & 0xFF));            // FCS little-end first (X25)
    f.push_back(uint8_t(fcs >> 8));
    const int pace = express ? (byteCycles_ + 7) / 8 : byteCycles_;
    c.rxQueue.push_back({std::move(f), pace});
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
        case 1:                                 // status of the FIFO-top byte
            return c.fifo.empty() ? c.rr1Rd : c.fifo.front().rr1;
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
        default: return 0;
    }
}

// Data port: the transmit buffer accepts the byte instantly (no shift
// register modelled), so if Tx interrupts are enabled the buffer-empty
// interrupt latches at once. This is what lets a driver that arms a
// serial transaction and sleeps for its completion interrupt (AppleTalk
// LAP on the LC II, O6.10) make progress instead of spinning forever.
void Scc8530::writeData(int channel, uint8_t d) {
    if (sccDbg()) fprintf(stderr, "[scc] %c data <- %02X\n", channel ? 'A' : 'B', d);
    Chan& c = ch_[channel & 1];
    c.txEmptyEvent = true;                      // filled → instantly empty
    if (c.wr[1] & 0x02) c.txIp = true;          // WR1 bit 1 = Tx Int Enable
    // SDLC frame capture: bytes accumulate until the underrun (frame
    // complete, onTxFrame) or a Send Abort (discard). The chip appends the
    // FCS itself, so txBuf is the raw LLAP frame.
    if (sdlcMode(c)) c.txBuf.push_back(d);
    // While an SDLC frame is open (underrun latch reset), each data byte
    // pushes the underrun point out — the frame ends kUnderrunDelay after
    // the LAST byte drains.
    if (!c.txUnderrun) c.underrunIn = kUnderrunDelay;
}

uint8_t Scc8530::readData(int channel) {
    Chan& c = ch_[channel & 1];
    if (c.fifo.empty()) return 0;               // dead line / drained FIFO
    const Chan::RxByte b = c.fifo.front();
    c.fifo.pop_front();
    c.rr1Rd = b.rr1;
    if (c.fifo.empty()) c.rxIp = false;         // level: FIFO drained
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
            // (instantly drained) transmitter underruns, the chip sends
            // CRC + closing flag and the latch SETS again; that 0→1 edge
            // is the frame-complete ext/status interrupt the LAP sleeps
            // on (Zilog SCC UM §4.4.1; kUnderrunDelay ≈ CRC+flag time).
            c.txUnderrun = false;
            c.underrunIn = kUnderrunDelay;
        }
        switch (v & 0x38) {
            case 0x18:                          // Send Abort (SDLC)
                // Aborting ends the frame at once: latch sets, ext/status
                // interrupt if armed (WR15 bit 6 + WR1 bit 0). The aborted
                // frame never reaches the wire.
                c.txBuf.clear();
                if (!c.txUnderrun) {
                    c.txUnderrun = true;
                    c.underrunIn = 0;
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
                break;
            case 0x10:                          // Reset External/Status ints
                c.extPending = false;
                c.latched = false;
                // Servicing re-arms the standing-abort presentation: on
                // an open line the SDLC receiver detects the next abort
                // ~130 µs later (tick delivers it). Event-driven, so a
                // channel that never services gets exactly ONE latch.
                if (abortIdle_ && sdlcMode(c) && (c.wr[15] & 0x80) && (c.wr[1] & 0x01))
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
                if (abortIdle_ && sdlcMode(c) && (c.wr[15] & 0x80) && (c.wr[1] & 0x01))
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
            c2.extPending = false;
            c2.latched = false;
            c2.hunt = false;
            c2.txUnderrun = true;
            c2.underrunIn = 0;
            c2.relatch = 0;
        };
        if ((v & 0xC0) == 0x40 || (v & 0xC0) == 0xC0) resetChan(ch_[0]);
        if ((v & 0xC0) == 0x80 || (v & 0xC0) == 0xC0) resetChan(ch_[1]);
    }

    // WR3 bit 4 = Enter Hunt Mode. On an idle line the hunt persists — RR0
    // bit 4 stays set, which is what the LLAP sender's carrier sense wants
    // ("no carrier, clear to transmit"); an incoming frame clears it
    // (rxStartFrame). A Sync/Hunt transition is an ext/status source when
    // WR15 bit 4 arms it. Entering hunt abandons a frame mid-delivery.
    if (ptr_ == 3 && (v & 0x10)) {
        c.rxCur.clear();
        c.rxPos = 0;
        if (!c.hunt) {
            c.hunt = true;
            if ((c.wr[15] & 0x10) && (c.wr[1] & 0x01)) {
                if (!c.latched) { c.rr0Latch = rr0(c); c.latched = true; }
                c.extPending = true;
            }
        }
    }
    // Rx disable (WR3 bit 0 cleared) flushes the receive path.
    if (ptr_ == 3 && !(v & 0x01)) {
        c.rxQueue.clear();
        c.rxCur.clear();
        c.rxPos = 0;
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
    if (ptr_ == 15 && (v & 0x80) && abortIdle_ && sdlcMode(c) && (c.wr[1] & 0x01)) {
        if (!c.latched) { c.rr0Latch = rr0(c); c.latched = true; }
        c.extPending = true;
    }
    // Symmetric case: WR15 bit 7 armed FIRST, then WR1 bit 0 (Ext Int Enable)
    // written — the ext/status request must latch when the last required
    // enable bit is set, order-independent (Zilog SCC UM). Without this the
    // first-latch timing differed by write order (the tick() re-present only
    // papered over it ~130 µs later).
    if (ptr_ == 1 && (v & 0x01) && abortIdle_ && sdlcMode(c) && (c.wr[15] & 0x80)) {
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
    for (int i = 0; i < 2; i++) {
        Chan& c = ch_[i];
        // SDLC Tx underrun: the drained shifter sends CRC + closing flag,
        // then the Tx Underrun/EOM latch sets — frame complete. Runs on
        // every machine (the latch is architectural, not LC II-specific).
        if (!c.txUnderrun && c.underrunIn > 0) {
            c.underrunIn -= cycles;
            if (c.underrunIn <= 0) {
                c.underrunIn = 0;
                c.txUnderrun = true;
                // Frame complete: hand the raw bytes to the wire.
                if (!c.txBuf.empty()) {
                    if (onTxFrame) onTxFrame(i, c.txBuf.data(), c.txBuf.size());
                    c.txBuf.clear();
                }
                if ((c.wr[15] & 0x40) && (c.wr[1] & 0x01)) {
                    if (!c.latched) { c.rr0Latch = rr0(c); c.latched = true; }
                    c.extPending = true;
                    changed = true;
                }
            }
        }
        // ── Rx pacing: one byte per LocalTalk byte-time into the FIFO ──
        // The LINE runs regardless of the receiver: LLAP senders wait for
        // the CTS with Rx disabled, watching RR0 bit 7 for the carrier
        // (the 7.5 LAP polls $F4→ waiting for the abort to drop). A frame
        // therefore always plays out on the wire — bytes only reach the
        // FIFO while the receiver is enabled (rxPushByte).
        if (sdlcMode(c)) {
            if (c.rxCur.empty() && !c.rxQueue.empty()) {
                bool wasIrq = c.rxIp || c.specialIp || c.extPending;
                rxStartFrame(c, i);
                changed = changed || (!wasIrq && (c.extPending));
            }
            if (!c.rxCur.empty()) {
                c.rxTimer -= cycles;
                while (c.rxTimer <= 0 && !c.rxCur.empty()) {
                    c.rxTimer += c.rxPace;
                    bool was = c.rxIp || c.specialIp;
                    rxPushByte(c);
                    if (!was && (c.rxIp || c.specialIp)) changed = true;
                }
                if (c.rxCur.empty()) c.rxTimer = 0;
            }
        }
        if (!abortIdle_) continue;
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
