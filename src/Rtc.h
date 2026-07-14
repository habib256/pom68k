// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── RTC 343-0042 ──
// The clock/PRAM chip, bit-banged over VIA port B: PB2 = /enable, PB1 =
// clock, PB0 = data (bidirectional). Command byte then data byte, MSB
// first; command bit 7 = read. Registers: 0-3 seconds (aliases 4-7 write),
// 8-11 = PRAM 16-19, 12 test / 13 write-protect, 16-31 = PRAM 0-15.
// Model: Mini vMac RTC.c; MAME rtc3430042.cpp.
// Gate: rom_boot_etalon (the ROM reads/writes PRAM + clock during boot).

#pragma once
#include <cstdint>

class Rtc {
public:
    void reset();
    // Called whenever VIA port B outputs change.
    void setLines(bool enable, bool clock, bool dataOut);
    uint8_t dataBit() const { return out_; }   // CPU reads this on PB0
    void tickSecond() { seconds_++; }
    void setSeconds(uint32_t s) { seconds_ = s; }

private:
    uint8_t readReg(uint8_t cmd) const;
    void writeReg(uint8_t cmd, uint8_t v);

    uint32_t seconds_ = 0;                     // since 1904-01-01
    uint8_t pram_[20] = {};
    bool writeProtect_ = false;

    enum Phase { CMD, WRITE_DATA, READ_DATA, DONE };
    int phase_ = CMD;
    int bitCnt_ = 0;
    uint8_t shift_ = 0, cmd_ = 0, outData_ = 0, out_ = 1;
    bool enabled_ = false, lastClk_ = false;
};
