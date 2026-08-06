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
//
// ONE SUPERSET FOR BOTH PARTS (audit § 2.2, cosmetic — not aligned). MAME
// splits the family in two devices: rtc3430042 (256-byte XPRAM) and
// rtc3430040 (the compacts' part — 20 bytes of NVRAM, macrtc.cpp:415-424,
// and a write-protect register that answers ONLY to the exact command $35,
// anything else being logged as illegal, :306-317). POM68K runs the -0042
// superset on the compacts too: full XPRAM array, WP accepted from any
// reg-13 command form. Unobservable — the compact ROMs never issue an
// extended command and write WP as $35 — and splitting it would buy a
// model flag, a second code path and a save-state field for nothing.
// Reopen if a compact-era title is ever seen probing XPRAM past $13 or
// poking reg 13 with a non-$35 command.
// Model: MAME rtc3430042 (macrtc.cpp); Mini vMac RTC.c.
// Gates: rom_boot_etalon (Plus classic), macii_boot_etalon (extended —
// the ROM boots on an unpatched image only if XPRAM answers).

#pragma once
#include <cstdint>
#include <string>

class Rtc {
public:
    void reset();
    // Called whenever VIA port B outputs change.
    void setLines(bool enable, bool clock, bool dataOut);
    uint8_t dataBit() const { return out_; }   // CPU reads this on PB0
    void tickSecond() { seconds_++; }
    void setSeconds(uint32_t s) { seconds_ = s; }
    uint32_t seconds() const { return seconds_; }   // as Egret::seconds()

    // Cold-start XPRAM: Basilisk II XPRAMInit defaults ('NuMc' validity,
    // DynWait, SPConfig $22, OSDefault/timeout $76-$77, no default
    // startup device at $78-$7B → the ROM scans SCSI 6→0 itself).
    void factoryDefaults();

    uint8_t xpram(uint8_t addr) const { return pram_[addr]; }
    void setXpram(uint8_t addr, uint8_t v) { pram_[addr] = v; }

    // ── Battery file ────────────────────────────────────────────────────
    // The 256-byte XPRAM, flat, no header — the format `CentrisMemory`
    // has written since the djMEMC bring-up, so existing `.pram` files
    // keep loading. The chip lives on a battery on real hardware; here
    // the machine calls these around the run loop (`<image>.<profile>.pram`,
    // main.cpp), which is what makes a guest's Control Panel settings
    // survive a session.
    //
    // Seconds are deliberately NOT in the file. Every machine seeds the
    // clock from host wall time at construction, and a restored stale
    // count would be strictly worse than the true current time — the
    // opposite trade-off from `Egret`/`CudaLle`, whose MCU owns the clock
    // across the transport and whose file therefore carries a 4-byte
    // seconds tail (Egret.cpp:27-45).
    bool loadPram(const std::string& path);
    void savePram(const std::string& path) const;

    // ── Save states (SaveState.h contract) ──────────────────────────────
    // The whole chip travels: clock, unified XPRAM, and the bit-serial
    // command engine mid-transaction — a snapshot taken between two PB1
    // clock edges must resume the byte where it stood.
    template <class Ar> void visit(Ar& ar) {
        ar(seconds_, pram_, writeProtect_, phase_, bitCnt_,
           shift_, cmd_, outData_, out_, xpAddr_, enabled_, lastClk_);
    }

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
