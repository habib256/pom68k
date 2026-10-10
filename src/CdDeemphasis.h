// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// CD-DA de-emphasis. A track whose Q control carries PRE (FLAGS PRE in a
// cue sheet) was mastered through the 50/15 µs pre-emphasis curve of IEC
// 60908 § 15; a drive's audio output undoes it. The analog prototype
// H(s) = (1 + s·15 µs) / (1 + s·50 µs) is mapped by the bilinear transform
// at 44 100 Hz: unity at DC, 15/50 (−10.46 dB) at Nyquist, one pole and one
// zero per channel. Gate: cd_image_test.

#pragma once

#include <cstdint>

struct CdDeemphasis {
    double x1[2] = {0, 0}, y1[2] = {0, 0};

    void reset() { *this = CdDeemphasis{}; }

    // One raw 2352-byte sector, 16-bit little-endian stereo, in place.
    void apply(std::uint8_t* raw) {
        constexpr double k = 2.0 * 44100.0, t1 = 50e-6, t2 = 15e-6;
        constexpr double b0 = (1 + k * t2) / (1 + k * t1);
        constexpr double b1 = (1 - k * t2) / (1 + k * t1);
        constexpr double a1 = (1 - k * t1) / (1 + k * t1);
        for (int i = 0; i < 2352; i += 2) {
            const int c = (i / 2) & 1;
            const double x = double(std::int16_t(std::uint16_t(raw[i] | raw[i + 1] << 8)));
            const double y = b0 * x + b1 * x1[c] - a1 * y1[c];
            x1[c] = x;
            y1[c] = y;
            const double r = y < -32768.0 ? -32768.0 : y > 32767.0 ? 32767.0 : y;
            const auto s = std::uint16_t(std::int16_t(r < 0 ? r - 0.5 : r + 0.5));
            raw[i] = std::uint8_t(s);
            raw[i + 1] = std::uint8_t(s >> 8);
        }
    }
};
