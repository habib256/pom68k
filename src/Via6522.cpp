// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "Via6522.h"

void Via6522::reset() {
    ora_ = orb_ = ddra_ = ddrb_ = 0;
    inA_ = inB_ = 0xFF;
    acr_ = pcr_ = sr_ = ifr_ = ier_ = 0;
    t1_ = t2_ = 0; t1latch_ = 0; t2ll_ = 0;
    t1armed_ = t2armed_ = false;
    srHostWritten_ = false;
    cb1_ = cb2_ = true;
}

// T1: sets IFR6 on underflow; free-run mode (ACR6) reloads from the latch
// and re-arms, one-shot keeps counting through $FFFF without re-flagging.
// T2 (timer mode): one-shot only. Reload granularity is one VIA cycle —
// fine-grained ±1-cycle 6522 reload latency is not modeled (DEV.md § VIA).
bool Via6522::tick(int n) {
    bool hit = false;
    t1_ -= n;
    if (t1_ < 0) {
        if (t1armed_) { setIfr(TIMER1); hit = true; }
        if (acr_ & 0x40) {                       // free-run: reload, stay armed
            int period = int(t1latch_) + 2;
            t1_ = period - 1 + ((t1_ + 1) % period);
        } else {
            t1armed_ = false;
            t1_ &= 0xFFFF;                       // wraps and keeps counting
        }
    }
    if (!(acr_ & 0x20)) {                        // T2 timer mode (not PB6 pulses)
        t2_ -= n;
        if (t2_ < 0) {
            if (t2armed_) { setIfr(TIMER2); hit = true; t2armed_ = false; }
            t2_ &= 0xFFFF;
        }
    }
    return hit;
}

void Via6522::setCb1(bool level) {
    if (level == cb1_) return;
    const bool fell = cb1_ && !level;
    cb1_ = level;
    if (fell) setIfr(CB1);
}

void Via6522::setCb2(bool level) {
    if (level == cb2_) return;
    const bool fell = cb2_ && !level;
    cb2_ = level;
    if (fell) setIfr(CB2);
}

uint8_t Via6522::read(int reg) {
    switch (reg) {
        case ORB:    clearCbFlags(); return portB();
        case ORA:    clearCaFlags(); return portA();
        case ORA_NH: return portA();
        case DDRB:   return ddrb_;
        case DDRA:   return ddra_;
        case T1CL:   ifr_ &= uint8_t(~TIMER1); return uint8_t(t1_ & 0xFF);
        case T1CH:   return uint8_t((t1_ >> 8) & 0xFF);
        case T1LL:   return uint8_t(t1latch_);
        case T1LH:   return uint8_t(t1latch_ >> 8);
        case T2CL:   ifr_ &= uint8_t(~TIMER2); return uint8_t(t2_ & 0xFF);
        case T2CH:   return uint8_t((t2_ >> 8) & 0xFF);
        case SR:     ifr_ &= uint8_t(~SHIFT); return sr_;
        case ACR:    return acr_;
        case PCR:    return pcr_;
        case IFR:    return uint8_t((ifr_ & 0x7F) | (irqAsserted() ? ANY : 0));
        case IER:    return uint8_t(ier_ | 0x80);
    }
    return 0xFF;
}

void Via6522::write(int reg, uint8_t v) {
    switch (reg) {
        case ORB:    orb_ = v; clearCbFlags(); break;
        case ORA:    ora_ = v; clearCaFlags(); break;
        case ORA_NH: ora_ = v; break;
        case DDRB:   ddrb_ = v; break;
        case DDRA:   ddra_ = v; break;
        case T1CL: case T1LL: t1latch_ = uint16_t((t1latch_ & 0xFF00) | v); break;
        case T1LH:   t1latch_ = uint16_t((t1latch_ & 0x00FF) | (v << 8)); break;
        case T1CH:   t1latch_ = uint16_t((t1latch_ & 0x00FF) | (v << 8));
                     t1_ = t1latch_; t1armed_ = true;
                     ifr_ &= uint8_t(~TIMER1); break;
        case T2CL:   t2ll_ = v; break;          // stage the low latch (R6522 §5.6)
        case T2CH:   t2_ = int32_t((uint32_t(v) << 8) | t2ll_);   // latch → counter
                     t2armed_ = true;
                     ifr_ &= uint8_t(~TIMER2); break;
        case SR:     sr_ = v; ifr_ &= uint8_t(~SHIFT); srHostWritten_ = true; break;
        case ACR:    acr_ = v; break;
        case PCR:    pcr_ = v; break;
        case IFR:    if (v & CA1) ++ca1Cleared;
                     ifr_ &= uint8_t(~(v & 0x7F)); break;   // write-1-to-clear
        case IER:    if (v & 0x80) ier_ |= (v & 0x7F); else ier_ &= uint8_t(~(v & 0x7F)); break;
    }
}
