// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── IWM (Integrated Woz Machine) ──
// Floppy controller as wired in the Mac Plus: 8 internal state lines
// (CA0-CA2/LSTRB = ph0-ph3, ENABLE, SELECT, Q6, Q7) toggled by address
// bits A9-A12 ($C00000-$DFFFFF odd bytes; reg = line*2 + set/clear).
// (Q7,Q6) select DATA / STATUS / HANDSHAKE / MODE registers. The ph lines
// double as the Sony drive's register address; SEL comes from VIA PA5.
// Source of truth: MAME iwm.cpp; DEV.md § IWM (research-pinned).
// Gate: tests/gcr_test.cpp, tests/disk_boot_etalon.cpp.

#pragma once
#include <cstdint>

class SonyDrive;

class Iwm {
public:
    void reset();
    void attachDrive(SonyDrive* internal, SonyDrive* external) {
        drive_[0] = internal; drive_[1] = external;
    }

    // Bus access: reg = addr bits A9-A12.
    uint8_t read(int reg);
    void write(int reg, uint8_t v);

    // VIA PA5 — SEL bit of the drive sense/command address + head select.
    void setSel(bool sel) { sel_ = sel; }

    // Advance internal time (CPU cycles) — paces the nibble stream.
    void tick(int cpuCycles);

    long readCount[16] = {};              // per-reg access stats (debug)
    long dataReads = 0, dataHits = 0;     // data-reg polls vs MSB-set reads
    long senseCount[16] = {};             // status reads per sense address
    uint8_t consumed[512] = {};           // ring of nibbles the CPU consumed
    int consumedPos = 0;
    long overwritten = 0;                 // nibbles replaced before being read

private:
    uint8_t access(int reg);
    uint8_t readRegister();
    SonyDrive* selectedDrive() const { return drive_[driveSel_ ? 1 : 0]; }
    int senseAddr() const;

    SonyDrive* drive_[2] = { nullptr, nullptr };
    bool ph_[4] = { false, false, false, false };
    bool enable_ = false, driveSel_ = false, q6_ = false, q7_ = false;
    bool sel_ = false;
    uint8_t mode_ = 0, dataReg_ = 0;
    int cellPhase_ = 0;
    int clearCountdown_ = 0;              // delayed clear after a data read
};
