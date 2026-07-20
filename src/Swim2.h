// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Apple SWIM2 register/FIFO + SuperDrive media engine (PrimeTime/IOSB).
// Register model: MAME swim2.cpp. Drive CA protocol: applefdintf phases_w
// → mac_floppy seek_phase_w; mode/devsel/hdsel from swim2.cpp:128-137,
// 300-303. Gate: tests/swim2_test.cpp, tests/swim2_media_test.cpp.

#pragma once
#include <cstdint>

class SonyDrive;

class Swim2 {
public:
    void reset();
    void attachDrive(SonyDrive* internal, SonyDrive* external);

    uint8_t read(int reg);
    void write(int reg, uint8_t value);
    void tick(int controllerCycles);

    uint8_t mode() const { return mode_; }
    uint8_t setup() const { return setup_; }
    uint8_t phases() const { return phases_; }
    int fifoCount() const { return fifoPos_; }

    SonyDrive* selectedDrive() const;
    bool isWriteProtected() const;

private:
    enum : uint16_t { MARK = 0x100, CRC = 0x200 };
    bool fifoPush(uint16_t value);
    uint16_t fifoPop();
    void fifoClear();

    int senseAddr() const;
    void applyPhases(uint8_t value);
    void updateDevsel();
    bool side1() const { return (mode_ & 0x20) != 0; }   // HDSEL
    bool gcrRead() const { return (setup_ & 0x04) != 0; }

    SonyDrive* drive_[2] = { nullptr, nullptr };
    int driveSel_ = 0;                   // 0 = A (internal), 1 = B
    bool lstrb_ = false;

    uint8_t mode_ = 0x40, setup_ = 0, phases_ = 0;
    uint8_t params_[4] = {};
    uint8_t paramIdx_ = 0;
    uint16_t fifo_[2] = {};
    uint8_t fifoPos_ = 0;
    uint8_t error_ = 0;
    int cellPhase_ = 0;
};
