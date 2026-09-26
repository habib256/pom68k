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
// A row is keyed `<gate>@<volume>`: the gate's name (plus a model when one
// binary serves several) and the base name of the first disk, floppy or CD
// the gate reported (testasset::reportedMedia), spaces as underscores. A
// host that boots a different reference image finds no row and prints its
// value, instead of failing a pin that was never about its volume.
//
// The expected values live in `tools/pixel_pins.tsv`, one row per key, so
// re-pinning after a deliberate change is one edit and the diff names the
// profile that moved. A gate with NO row prints its measured value and
// passes — that is how a new profile is pinned: copy the printed line in.
// A row whose value is `-` is a profile deliberately left unpinned.
//
// NOT UNDER THE AGENT — a boot etalon's `_agent_boot_etalon` variant has
// the Finder launch « POM68K Disques » (tests/AgentBootProbe.h), which may
// then be the front application: that screen is not the pinned desktop,
// and what it shows depends on when the launch lands. The variant's proof
// is the agent's; the pixels are pinned by the base gate. The pin there
// is reported as not applicable and advances no frame, so the variant
// runs exactly as it did before pins existed.

#pragma once

#include "AssetFingerprint.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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

// POM68K_TEST_AGENT=1, read as agentboot::enabled() reads it — that header
// drags the HFS/SCSI stack in, and `pixel_pin_test` links nothing.
inline bool agentInBoot() {
    const char* v = std::getenv("POM68K_TEST_AGENT");
    return v && *v == '1';
}

struct Result {
    bool notApplicable = false;          // the agent variant: see above
    bool settled = false;
    std::uint64_t hash = 0;
    int captures = 0;                    // how many settles it took
    // When the screen never settled: what moved between the last two
    // captures, so the refusal names a region instead of just a verdict.
    long changed = 0;                    // pixels that differed
    int x0 = 0, y0 = 0, x1 = -1, y1 = -1; // their bounding box (inclusive)
    long activity = 0;                   // disk operations in the last span
};

// Fills the "what moved" fields of `r` from two same-size captures.
inline void describeMotion(Result& r, const std::vector<std::uint32_t>& a,
                           const std::vector<std::uint32_t>& b, int width) {
    r.changed = 0;
    r.x0 = r.y0 = 1 << 30; r.x1 = r.y1 = -1;
    if (width <= 0 || a.size() != b.size()) return;
    for (std::size_t i = 0; i < a.size(); i++) {
        if (((a[i] ^ b[i]) & 0xFFFFFFu) == 0) continue;
        const int x = int(i % std::size_t(width)), y = int(i / std::size_t(width));
        r.changed++;
        if (x < r.x0) r.x0 = x;
        if (x > r.x1) r.x1 = x;
        if (y < r.y0) r.y0 = y;
        if (y > r.y1) r.y1 = y;
    }
}

// Whether two captures agree where the hash looks: the masked menu rows are
// excluded from the settle exactly as from the value, or a clock that the
// hash ignores would keep a still desktop from ever settling.
inline bool sameBelow(const std::vector<std::uint32_t>& a,
                      const std::vector<std::uint32_t>& b,
                      int width, int menuRows) {
    if (a.empty() || a.size() != b.size() || width <= 0) return false;
    const std::size_t from =
        std::min(a.size(), std::size_t(width) * std::size_t(menuRows));
    for (std::size_t i = from; i < a.size(); i++)
        if ((a[i] ^ b[i]) & 0xFFFFFFu) return false;
    return true;
}

// POM68K_PIN_PPM=<path> writes the frame that was hashed (menu rows
// included) as a binary PPM: when a pin moves, or two profiles that should
// agree do not, look at the pixels before reasoning about them.
inline void dumpIfAsked(const std::vector<std::uint32_t>& fb, int width,
                        int height) {
    const char* path = std::getenv("POM68K_PIN_PPM");
    if (!path || !*path || width <= 0 || height <= 0 ||
        fb.size() < std::size_t(width) * std::size_t(height))
        return;
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << width << " " << height << "\n255\n";
    for (std::size_t i = 0; i < std::size_t(width) * std::size_t(height); i++) {
        const char rgb[3] = { char(fb[i] >> 16), char(fb[i] >> 8), char(fb[i]) };
        out.write(rgb, 3);
    }
}

// `capture(fb)` fills a frame; `run(frames)` advances the machine;
// `activity()` returns a counter of the machine's disk operations (SCSI
// commands, floppy nibbles). The screen is captured, advanced, captured
// again, until two captures agree below the menu rows AND the disk did
// nothing in between. A still screen alone is not a finished boot: the
// LC 575's Finder held its bare desktop for 170 frames while it issued
// 123 SCSI commands, and a 120-frame screen-only settle pinned that
// half-drawn desktop instead of the one the LC 475 draws.
template <class Capture, class Run, class Activity>
Result settleAndHash(Capture capture, Run run, Activity activity, int width,
                     int height, int menuRows, int tries = 8,
                     long framesBetween = 120) {
    Result r;
    if (agentInBoot()) { r.notApplicable = true; return r; }
    std::vector<std::uint32_t> a, b;
    capture(a);
    for (int i = 0; i < tries; i++) {
        const long before = long(activity());
        run(framesBetween);
        capture(b);
        r.captures = i + 1;
        r.activity = long(activity()) - before;
        if (r.activity == 0 && sameBelow(a, b, width, menuRows)) {
            r.settled = true;
            r.hash = hashRegion(b, width, height, menuRows);
            dumpIfAsked(b, width, height);
            return r;
        }
        describeMotion(r, a, b, width);
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

// `gate@volume`, the volume being the first medium the gate reported.
inline std::string keyFor(const std::string& gate) {
    const std::vector<std::string>& media = testasset::reportedMedia();
    if (media.empty()) return gate;
    std::string key = gate + "@" + media.front();
    for (char& c : key) if (c == ' ') c = '_';
    return key;
}

// Prints the verdict and returns whether the gate may pass. An unpinned
// gate always passes, loudly: its line is the one to paste into the table.
inline bool check(const std::string& gateName, const Result& r) {
    const std::string gate = keyFor(gateName);
    if (r.notApplicable) {
        std::printf("pixel pin: %s not applicable — the agent is in this "
                    "boot; the base gate pins the desktop\n", gate.c_str());
        return true;
    }
    if (!r.settled) {
        std::printf("pixel pin: %s NOT SETTLED after %d captures — the screen "
                    "was still changing, pin not taken\n", gate.c_str(),
                    r.captures);
        if (r.changed > 0)
            std::printf("pixel pin: last two captures differ in %ld pixels "
                        "within x %d-%d, y %d-%d\n", r.changed, r.x0, r.x1,
                        r.y0, r.y1);
        if (r.activity != 0)
            std::printf("pixel pin: the disk was still busy (%ld operations "
                        "in the last span)\n", r.activity);
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
