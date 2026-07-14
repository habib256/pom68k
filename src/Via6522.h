// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── VIA 6522 ──
// Rockwell/Synertek 6522 as wired in the Mac Plus: port A drives overlay
// (PA4), screen buffer select (PA6), sound volume; port B drives RTC lines,
// sound enable; CA1 = vblank (60.15 Hz), CA2 = one-second RTC tick.
// M3 scope: register file + IFR/IER logic + port latches. Timers T1/T2 and
// the shift register land with the real-ROM boot milestone (M4).
// Source of truth: Guide to the Macintosh Family Hardware; MAME mac.cpp.
// Gate: tests/cpu_smoke.cpp (overlay switch via ORA write).

#pragma once
#include <cstdint>

class Via6522 {
public:
    // 6522 register indices (addr bits 12..9 on the Mac)
    enum Reg { ORB = 0, ORA = 1, DDRB = 2, DDRA = 3,
               T1CL = 4, T1CH = 5, T1LL = 6, T1LH = 7,
               T2CL = 8, T2CH = 9, SR = 10, ACR = 11, PCR = 12,
               IFR = 13, IER = 14, ORA_NH = 15 };

    // IFR/IER bits
    enum Irq { CA2 = 0x01, CA1 = 0x02, SHIFT = 0x04, CB2 = 0x08,
               CB1 = 0x10, TIMER2 = 0x20, TIMER1 = 0x40, ANY = 0x80 };

    void reset();
    uint8_t read(int reg);
    void write(int reg, uint8_t v);

    // Effective pin levels: outputs from OR latch, inputs from inA_/inB_
    // (updated by MacMemory before reads; default high — pull-ups).
    uint8_t portA() const { return uint8_t((ora_ & ddra_) | (inA_ & ~ddra_)); }
    uint8_t portB() const { return uint8_t((orb_ & ddrb_) | (inB_ & ~ddrb_)); }
    void setInA(uint8_t v) { inA_ = v; }
    void setInB(uint8_t v) { inB_ = v; }
    uint8_t ddrb() const { return ddrb_; }

    // Advance T1/T2 by n VIA cycles (φ2 = CPU clock / 10 = 783.36 kHz).
    // Returns true when a timer underflow newly set an IFR bit.
    bool tick(int n);

    void raiseCa1() { setIfr(CA1); }        // vblank, per video frame
    void raiseCa2() { setIfr(CA2); }        // RTC one-second tick
    // Keyboard transaction hooks (M0110 over the shift register)
    void loadSR(uint8_t v) { sr_ = v; setIfr(SHIFT); }
    void raiseShift() { setIfr(SHIFT); }    // command byte finished shifting out
    uint8_t acr() const { return acr_; }
    uint8_t srValue() const { return sr_; }
    bool irqAsserted() const { return (ifr_ & ier_ & 0x7F) != 0; }

private:
    void setIfr(uint8_t bits) { ifr_ |= bits; }
    uint8_t ora_ = 0, orb_ = 0, ddra_ = 0, ddrb_ = 0;
    uint8_t inA_ = 0xFF, inB_ = 0xFF;
    uint8_t acr_ = 0, pcr_ = 0, sr_ = 0, ifr_ = 0, ier_ = 0;
    int32_t t1_ = 0, t2_ = 0;
    uint16_t t1latch_ = 0;
    bool t1armed_ = false, t2armed_ = false;   // one-shot IFR arming
};
