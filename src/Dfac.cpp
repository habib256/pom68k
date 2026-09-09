// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "Dfac.h"

#include <cmath>

void Dfac::reset() {
    data_ = clock_ = latch_ = false;
    shift_ = settings_ = 0;
}

void Dfac::clockWrite(bool level) {
    if (level && !clock_) {
        // The Egret sends bit 0 first. Each rising edge shifts the previous
        // bits down and places the current data level at bit 7.
        shift_ = uint8_t(shift_ >> 1);
        if (data_) shift_ |= 0x80;
    }
    clock_ = level;
}

void Dfac::latchWrite(bool level) {
    if (level && !latch_) settings_ = shift_;
    latch_ = level;
}

float Dfac::gain() const {
    // MAME apple/dfac.cpp: volume code 0 is mute; 1..7 are
    // -18/-15/-12/-9/-6/-3/0 dB. Bits 7..5 carry the code.
    static constexpr float kGain[8] = {
        0.0f, 0.125892541179417f, 0.177827941003892f,
        0.251188643150958f, 0.354813389233575f,
        0.501187233627272f, 0.707945784384138f, 1.0f
    };
    return inputEnabled() ? kGain[(settings_ >> 5) & 7] : 0.0f;
}

int16_t Dfac::process(int16_t sample) const {
    return int16_t(std::lround(float(sample) * gain()));
}
