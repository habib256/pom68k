// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── RTC 343-0042 ──
// The clock/PRAM chip, bit-banged over VIA port B: PB2 = /enable, PB1 =
// clock, PB0 = data (bidirectional). Command byte then data byte, MSB
// first; command bit 7 = read. Classic registers: 0-3 seconds (aliases
// 4-7 write), 8-11 = XPRAM $08-$0B, 12 test / 13 write-protect,
// 16-31 = XPRAM $10-$1F (MAME macrtc.cpp: classic addresses index the
// unified 256-byte array directly — SysParam low-mem $1F8-$207 is XPRAM
// $10-$1F, $208-$20B is $08-$0B, so SPConfig $1FB = XPRAM $13).
// Extended XPRAM commands (Mac II and later): first byte %z0111aaa
// ((cmd & $78) == $38, bit 7 = read, bits 2-0 = address bits 7-5),
// second byte %aaaaa_xx (bits 6-2 = address bits 4-0), then the data
// byte (read back or written). The Plus 343-0040 never issues them.
// Model: MAME rtc3430042 (macrtc.cpp); Mini vMac RTC.c.
// Gates: rom_boot_etalon (Plus classic), macii_boot_etalon (extended —
// the ROM boots on an unpatched image only if XPRAM answers).

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

    // Cold-start XPRAM: Basilisk II XPRAMInit defaults ('NuMc' validity,
    // DynWait, SPConfig $22, OSDefault/timeout $76-$77, no default
    // startup device at $78-$7B → the ROM scans SCSI 6→0 itself).
    void factoryDefaults();

    uint8_t xpram(uint8_t addr) const { return pram_[addr]; }
    void setXpram(uint8_t addr, uint8_t v) { pram_[addr] = v; }

private:
    uint8_t readReg(uint8_t cmd) const;
    void writeReg(uint8_t cmd, uint8_t v);

    uint32_t seconds_ = 0;                     // since 1904-01-01
    uint8_t pram_[256] = {};                   // unified XPRAM (classic aliases inside)
    bool writeProtect_ = false;

    enum Phase { CMD, WRITE_DATA, READ_DATA, XP_ADDR, XP_WRITE, DONE };
    int phase_ = CMD;
    int bitCnt_ = 0;
    uint8_t shift_ = 0, cmd_ = 0, outData_ = 0, out_ = 1;
    uint8_t xpAddr_ = 0;
    bool enabled_ = false, lastClk_ = false;
};
