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
    // Mac II: ASC IRQ → VIA2 CB1 (active low), SCSI IRQ → VIA2 CB2.
    // Edge into IFR on the active (falling) transition — MAME write_cb1/2.
    void setCb1(bool level);
    void setCb2(bool level);
    bool cb1() const { return cb1_; }
    bool cb2() const { return cb2_; }
    // Keyboard transaction hooks (M0110 over the shift register)
    void loadSR(uint8_t v) { sr_ = v; setIfr(SHIFT); srHostWritten_ = false; }
    void raiseShift() { setIfr(SHIFT); }    // command byte finished shifting out
    uint8_t acr() const { return acr_; }
    uint8_t srValue() const { return sr_; }
    // Q6: true when the HOST (guest CPU) has written the SR since the last
    // device loadSR — i.e. the SR holds a fresh host-supplied byte, not a
    // stale device value. The Cuda HLE uses this to reject a ghost command
    // session where the ROM toggles the handshake without loading a byte.
    bool srHostWritten() const { return srHostWritten_; }
    // SR byte loaded but not yet read (Egret paces its clocking on this)
    bool shiftPending() const { return (ifr_ & SHIFT) != 0; }
    bool irqAsserted() const { return (ifr_ & ier_ & 0x7F) != 0; }
    // Side-effect-free peeks (read(IFR/IER) ORs ANY / forces IER bit7).
    uint8_t ifrRaw() const { return ifr_; }
    uint8_t ierRaw() const { return ier_; }

private:
    void setIfr(uint8_t bits) { ifr_ |= bits; }
    // An ORA/ORB access clears CA1/CB1 always, but CA2/CB2 only when NOT in
    // the "independent interrupt" PCR mode (001/011) — R6522 §3.2.3. The Mac
    // ROM runs CA2 in independent mode (PCR=$22) so the RTC 1-second flag
    // must survive a port access that races it.
    void clearCaFlags() {
        if (ifr_ & CA1) ++ca1Cleared;
        ifr_ &= uint8_t(~CA1);
        if ((pcr_ & 0x0A) != 0x02) ifr_ &= uint8_t(~CA2);
    }
    void clearCbFlags() { ifr_ &= uint8_t(~CB1); if ((pcr_ & 0xA0) != 0x20) ifr_ &= uint8_t(~CB2); }
    uint8_t ora_ = 0, orb_ = 0, ddra_ = 0, ddrb_ = 0;
    uint8_t inA_ = 0xFF, inB_ = 0xFF;
    uint8_t acr_ = 0, pcr_ = 0, sr_ = 0, ifr_ = 0, ier_ = 0;
    bool srHostWritten_ = false;                // Q6: host wrote SR (see .h)
    bool cb1_ = true, cb2_ = true;              // input pin levels (idle high)
    int32_t t1_ = 0, t2_ = 0;
    uint16_t t1latch_ = 0;
    uint8_t t2ll_ = 0;                          // T2 low-latch (staged by T2CL)
    bool t1armed_ = false, t2armed_ = false;   // one-shot IFR arming
public:
    long ca1Cleared = 0;                        // diagnostic
};
