// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "Scc8530.h"

void Scc8530::reset() {
    ch_[0] = Chan{};
    ch_[1] = Chan{};
    ptr_ = 0;
}

// RR0 external status: bit 3 = DCD level, bit 5 = CTS, bit 2 = TxD empty.
uint8_t Scc8530::rr0(const Chan& c) const {
    return uint8_t((c.dcd ? 0x08 : 0x00) | 0x04 | 0x20);
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
                int code = ch_[1].extPending ? 0b101
                         : ch_[0].extPending ? 0b001 : 0b011;
                vec = uint8_t((vec & ~0x0E) | (code << 1));
            }
            return vec;
        }
        case 3:                                 // RR3 (channel A only): IP bits
            rr3Reads++;
            if (channel == 1)
                return uint8_t((ch_[1].extPending ? 0x08 : 0) |
                               (ch_[0].extPending ? 0x01 : 0));
            return 0;
        default: return 0;
    }
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
                break;
            case 0x38:                          // Reset Highest IUS
                c.extPending = false;
                c.latched = false;
                break;
            default: break;
        }
        return;
    }
    c.wr[ptr_] = v;
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
    return mie && (ch_[0].extPending || ch_[1].extPending);
}
