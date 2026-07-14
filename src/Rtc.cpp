// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "Rtc.h"

void Rtc::reset() {
    phase_ = CMD; bitCnt_ = 0; shift_ = 0; cmd_ = 0; out_ = 1;
    enabled_ = false; lastClk_ = false;
    // seconds_ / pram_ survive reset (battery-backed)
}

uint8_t Rtc::readReg(uint8_t cmd) const {
    uint8_t addr = (cmd >> 2) & 0x1F;
    if (addr < 4)   return uint8_t(seconds_ >> (8 * addr));
    if (addr < 8)   return uint8_t(seconds_ >> (8 * (addr - 4)));
    if (addr < 12)  return pram_[16 + (addr - 8)];
    if (addr < 16)  return 0xFF;               // test / write-protect: write-only
    return pram_[addr - 16];
}

void Rtc::writeReg(uint8_t cmd, uint8_t v) {
    uint8_t addr = (cmd >> 2) & 0x1F;
    if (addr == 13) { writeProtect_ = (v & 0x80) != 0; return; }
    if (addr == 12) return;                    // test register: ignored
    if (writeProtect_) return;
    if (addr < 4)       seconds_ = (seconds_ & ~(0xFFu << (8 * addr)))       | (uint32_t(v) << (8 * addr));
    else if (addr < 8)  seconds_ = (seconds_ & ~(0xFFu << (8 * (addr - 4)))) | (uint32_t(v) << (8 * (addr - 4)));
    else if (addr < 12) pram_[16 + (addr - 8)] = v;
    else                pram_[addr - 16] = v;
}

void Rtc::setLines(bool enable, bool clock, bool dataOut) {
    if (!enable) {                             // /enable high = idle: reset shifter
        enabled_ = false; phase_ = CMD; bitCnt_ = 0; shift_ = 0; out_ = 1;
        lastClk_ = clock;
        return;
    }
    if (!enabled_) { enabled_ = true; phase_ = CMD; bitCnt_ = 0; shift_ = 0; }

    bool rising = clock && !lastClk_;
    lastClk_ = clock;
    if (!rising) return;                       // bits move on the clock's rising edge

    switch (phase_) {
        case CMD:
        case WRITE_DATA:
            shift_ = uint8_t((shift_ << 1) | (dataOut ? 1 : 0));
            if (++bitCnt_ < 8) break;
            bitCnt_ = 0;
            if (phase_ == CMD) {
                cmd_ = shift_; shift_ = 0;
                if (cmd_ & 0x80) { phase_ = READ_DATA; outData_ = readReg(cmd_); out_ = (outData_ >> 7) & 1; }
                else phase_ = WRITE_DATA;
            } else {
                writeReg(cmd_, shift_); shift_ = 0; phase_ = DONE;
            }
            break;
        case READ_DATA:                        // present next bit after each clock
            if (++bitCnt_ >= 8) { phase_ = DONE; out_ = 1; break; }
            out_ = (outData_ >> (7 - bitCnt_)) & 1;
            break;
        case DONE:
            break;
    }
}
