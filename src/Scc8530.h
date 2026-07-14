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
// Source of truth: MAME z80scc.cpp + mac128.cpp; DEV.md § SCC (pinned).
// Gate: tests/input_etalon.cpp.

#pragma once
#include <cstdint>

class Scc8530 {
public:
    void reset();

    // Bus access: channel 0 = B, 1 = A; ctl/data per address decode.
    uint8_t readCtl(int channel);
    void writeCtl(int channel, uint8_t v);
    uint8_t readData(int channel) { return 0; }
    void writeData(int channel, uint8_t) { }

    // Mouse quadrature inputs (X1 → channel A DCD, Y1 → channel B DCD)
    void setDcd(int channel, bool level);

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
    };
    uint8_t rr0(const Chan& c) const;
    Chan ch_[2];                     // [0] = B, [1] = A
    int ptr_ = 0;                    // register pointer (WR0 low bits)
};
