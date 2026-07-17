// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "Scc8530.h"

void Scc8530::reset() {
    ch_[0] = Chan{};
    ch_[1] = Chan{};
    ptr_ = 0;
}

// RR0 external status: bit 3 = DCD level, bit 5 = CTS, bit 2 = TxD empty,
// bit 7 = Break/Abort (standing on an open LocalTalk line, O6.10).
uint8_t Scc8530::rr0(const Chan& c) const {
    return uint8_t((c.dcd ? 0x08 : 0x00) | 0x04 | 0x20
                   | (abortIdle_ ? 0x80 : 0x00));
}

uint8_t Scc8530::readCtl(int channel) {
    Chan& c = ch_[channel & 1];
    int reg = ptr_;
    ptr_ = 0;                                   // pointer auto-resets
    switch (reg) {
        case 0:  rr0Reads++; return c.latched ? c.rr0Latch : rr0(c);
        case 1:  return 0x07;                   // all-sent, no errors
        case 2: {
            rr2Reads++;
            // RR2 on channel B returns the vector MODIFIED by the highest
            // pending source (status-low, V3..V1): A ext = 101, B ext = 001,
            // none = 011. The Mac Plus level-2 handler dispatches on this.
            uint8_t vec = ch_[1].wr[2];
            if (channel == 0) {
                // Status-low V3..V1 code by highest-priority source. The
                // Z8530 ranks, highest first: ChA Rx, ChA Tx, ChA Ext,
                // ChB Rx, ChB Tx, ChB Ext — so within a channel Tx
                // outranks Ext. Codes: A Tx=100, A Ext=101, B Tx=000,
                // B Ext=001, none=011.
                int code = ch_[1].txIp       ? 0b100
                         : ch_[1].extPending ? 0b101
                         : ch_[0].txIp       ? 0b000
                         : ch_[0].extPending ? 0b001 : 0b011;
                vec = uint8_t((vec & ~0x0E) | (code << 1));
            }
            return vec;
        }
        case 3:                                 // RR3 (channel A only): IP bits
            rr3Reads++;                         // D4=A Tx, D3=A Ext, D1=B Tx,
            if (channel == 1)                   // D0=B Ext (D5/D2 Rx unmodelled)
                return uint8_t((ch_[1].txIp ? 0x10 : 0) |
                               (ch_[1].extPending ? 0x08 : 0) |
                               (ch_[0].txIp ? 0x02 : 0) |
                               (ch_[0].extPending ? 0x01 : 0));
            return 0;
        default: return 0;
    }
}

// Data port: the transmit buffer accepts the byte instantly (no shift
// register modelled), so if Tx interrupts are enabled the buffer-empty
// interrupt latches at once. This is what lets a driver that arms a
// serial transaction and sleeps for its completion interrupt (AppleTalk
// LAP on the LC II, O6.10) make progress instead of spinning forever.
void Scc8530::writeData(int channel, uint8_t) {
    Chan& c = ch_[channel & 1];
    c.txEmptyEvent = true;                      // filled → instantly empty
    if (c.wr[1] & 0x02) c.txIp = true;          // WR1 bit 1 = Tx Int Enable
}

uint8_t Scc8530::readData(int channel) {
    (void)channel;
    return 0;                                   // no RX (dead line)
}

void Scc8530::writeCtl(int channel, uint8_t v) {
    ctlWrites++;
    Chan& c = ch_[channel & 1];
    if (ptr_ == 0) {
        ptr_ = v & 0x07;
        if ((v & 0x38) == 0x08) ptr_ |= 8;      // point-high command
        switch (v & 0x38) {
            case 0x10:                          // Reset External/Status ints
                c.extPending = false;
                c.latched = false;
                // Servicing re-arms the standing-abort presentation: on
                // an open line the SDLC receiver detects the next abort
                // ~130 µs later (tick delivers it). Event-driven, so a
                // channel that never services gets exactly ONE latch.
                if (abortIdle_ && (c.wr[15] & 0x80) && (c.wr[1] & 0x01))
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
                if (abortIdle_ && (c.wr[15] & 0x80) && (c.wr[1] & 0x01))
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

    // Arming Break/Abort IE (WR15 bit 7) on an open line latches the
    // external/status interrupt at once: the abort condition is already
    // standing (SDLC hunt, continuous marks). This is the carrier-sense
    // interrupt AppleTalk's LAP manager waits on (O6.10). Gated on WR1
    // bit 0 (per-channel Ext Int Enable) like the DCD path — a real
    // 8530 requires it (review 2026-07-16).
    if (ptr_ == 15 && (v & 0x80) && abortIdle_ && (c.wr[1] & 0x01)) {
        if (!c.latched) { c.rr0Latch = rr0(c); c.latched = true; }
        c.extPending = true;
    }
    // Symmetric case: WR15 bit 7 armed FIRST, then WR1 bit 0 (Ext Int Enable)
    // written — the ext/status request must latch when the last required
    // enable bit is set, order-independent (Zilog SCC UM). Without this the
    // first-latch timing differed by write order (the tick() re-present only
    // papered over it ~130 µs later).
    if (ptr_ == 1 && (v & 0x01) && abortIdle_ && (c.wr[15] & 0x80)) {
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
                || ch_[0].txIp || ch_[1].txIp);
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
    if (!abortIdle_) return false;
    bool changed = false;
    for (Chan& c : ch_) {
        if (c.relatch <= 0) continue;
        c.relatch -= cycles;
        if (c.relatch > 0) continue;
        c.relatch = 0;
        if ((c.wr[15] & 0x80) && (c.wr[1] & 0x01) && !c.extPending) {
            if (!c.latched) { c.rr0Latch = rr0(c); c.latched = true; }
            c.extPending = true;
            changed = true;
        }
    }
    return changed;
}
