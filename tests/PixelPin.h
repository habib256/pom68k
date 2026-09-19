// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── A screen pinned by its PIXELS, not by a statistic ──
// A boot etalon asks "is this a Finder?" with luminance ratios: a menu bar
// mostly white, a desktop in a dithered band. That answer survives a wrong
// font, a shifted icon, a lost colour and a scrambled CLUT, because none of
// those move the average. This pins the exact pixels instead, as a 64-bit
// hash, and it is the jalon 4 criterion "N profils sous etalon
// pixel-accurate" (TODO § Fidélité).
//
// Two properties make the pin hold:
//
//   SETTLED — a boot ends when the screen stops changing, not at a frame
//   count. Two captures a settle apart must be identical before the hash is
//   taken, so the value does not depend on which frame an engine happened
//   to land on. A pin that held only for one engine would pin the engine,
//   not the machine.
//
//   MASKED — Mac OS draws a CLOCK in the menu bar, and the guest's clock
//   advances with host wall time on a GUI session. The top `menuRows` rows
//   are excluded, which also drops the menu titles: stable, but not worth
//   the risk of a one-pixel menu-bar layout difference failing a gate about
//   the desktop.
//
// The expected values live in `tools/pixel_pins.tsv`, one row per gate, so
// re-pinning after a deliberate change is one edit and the diff names the
// profile that moved. A gate with NO row prints its measured value and
// passes — that is how a new profile is pinned: copy the printed line in.
// A row whose value is `-` is a profile deliberately left unpinned.

#pragma once

#include "AssetFingerprint.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace pixelpin {

// FNV-1a over the pixels, 24 bits each: the alpha byte is the decoders'
// own business (some force $FF, some leave 0) and never reaches a screen.
inline std::uint64_t hashRegion(const std::vector<std::uint32_t>& fb,
                                int width, int height, int menuRows) {
    std::uint64_t h = 1469598103934665603ull;
    if (width <= 0 || height <= 0) return 0;
    if (fb.size() < std::size_t(width) * std::size_t(height)) return 0;
    for (int y = menuRows; y < height; y++)
        for (int x = 0; x < width; x++) {
            const std::uint32_t p = fb[std::size_t(y) * width + x] & 0xFFFFFFu;
            for (int shift = 0; shift < 24; shift += 8) {
                h ^= std::uint8_t(p >> shift);
                h *= 1099511628211ull;
            }
        }
    return h;
}

struct Result {
    bool settled = false;
    std::uint64_t hash = 0;
    int captures = 0;                    // how many settles it took
};

// `capture(fb)` fills a frame; `run(frames)` advances the machine. The
// screen is captured, advanced, captured again, until two agree.
template <class Capture, class Run>
Result settleAndHash(Capture capture, Run run, int width, int height,
                     int menuRows, int tries = 8, long framesBetween = 120) {
    Result r;
    std::vector<std::uint32_t> a, b;
    capture(a);
    for (int i = 0; i < tries; i++) {
        run(framesBetween);
        capture(b);
        r.captures = i + 1;
        if (!a.empty() && a == b) {
            r.settled = true;
            r.hash = hashRegion(b, width, height, menuRows);
            return r;
        }
        a.swap(b);
    }
    if (!a.empty()) r.hash = hashRegion(a, width, height, menuRows);
    return r;
}

// The pinned value for a gate, or 0 when the table has no row for it.
// `known` separates "no row" from "a row pinning zero".
inline std::uint64_t pinned(const std::string& gate, bool& known) {
    known = false;
    const std::string path = testasset::find("tools/pixel_pins.tsv");
    if (path.empty()) return 0;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream row(line);
        std::string name, value;
        if (!(row >> name >> value) || name != gate) continue;
        if (value == "-") return 0;      // deliberately unpinned
        known = true;
        return std::strtoull(value.c_str(), nullptr, 16);
    }
    return 0;
}

// Prints the verdict and returns whether the gate may pass. An unpinned
// gate always passes, loudly: its line is the one to paste into the table.
inline bool check(const std::string& gate, const Result& r) {
    if (!r.settled) {
        std::printf("pixel pin: %s NOT SETTLED after %d captures — the screen "
                    "was still changing, pin not taken\n", gate.c_str(),
                    r.captures);
        return false;
    }
    bool known = false;
    const std::uint64_t want = pinned(gate, known);
    if (!known) {
        std::printf("pixel pin: %s %016llx (settled in %d) — no row in "
                    "tools/pixel_pins.tsv; paste:\n%s\t%016llx\n", gate.c_str(),
                    (unsigned long long)r.hash, r.captures, gate.c_str(),
                    (unsigned long long)r.hash);
        return true;
    }
    if (r.hash != want) {
        std::printf("pixel pin: %s %016llx, pinned %016llx — the screen "
                    "MOVED. If that is the intended change, edit "
                    "tools/pixel_pins.tsv\n", gate.c_str(),
                    (unsigned long long)r.hash, (unsigned long long)want);
        return false;
    }
    std::printf("pixel pin: %s %016llx (settled in %d)\n", gate.c_str(),
                (unsigned long long)r.hash, r.captures);
    return true;
}

} // namespace pixelpin
