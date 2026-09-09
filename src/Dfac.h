// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Original Apple DFAC (Digitally Filtered Audio Chip): the three-wire
// Egret-controlled output stage used by the LC/LC II/Classic II.

#pragma once

#include <cstdint>

class Dfac {
public:
    void reset();

    void dataWrite(bool level) { data_ = level; }
    void clockWrite(bool level);
    void latchWrite(bool level);
    void writeSettings(uint8_t settings) { settings_ = settings; }

    uint8_t settings() const { return settings_; }
    bool inputEnabled() const { return (settings_ & 0x02) != 0; }
    float gain() const;
    int16_t process(int16_t sample) const;

    template <class Ar> void visit(Ar& ar) {
        ar(data_, clock_, latch_, shift_, settings_);
    }

private:
    bool data_ = false;
    bool clock_ = false;
    bool latch_ = false;
    uint8_t shift_ = 0;
    uint8_t settings_ = 0;
};
