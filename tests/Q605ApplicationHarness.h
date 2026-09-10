// POM68K — shared Quadra 605 real-application gate driver.
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The 68040 counterpart of `LciiApplicationHarness.h`: the rig the Mac OS
// 8.1 application etalons share (boot to the Finder, keyboard and mouse
// gestures, the guest's own observables). Everything here reads the guest's
// word rather than a pixel calibrated on one volume: `CurApName` for the
// front application, `KeyMap` for whether a keystroke was accepted, the
// low-memory `Mouse` global for closed-loop pointer steering, and the
// catalog bytes of the host-owned image for a persistent artefact.
//
// Slow Keys. The 8.1 reference image ships with Easy Access Slow Keys
// enabled (q605_cudalle_key_etalon, CHANGELOG 2026-07-31): a key-down held
// shorter than the acceptance delay is rejected by the guest. A Finder
// type-select needs several characters inside one second, which no hold
// can satisfy, so `ensureFastKeys()` PROBES the state — a short tap whose
// bit never reaches KeyMap — and only then performs the user's own gesture,
// the eight-second Return hold that toggles Slow Keys. The probe is what
// keeps the toggle from turning the feature back ON on an image where it
// is already off (the trap that header warns about).

#pragma once

#include "AssetFingerprint.h"
#include "BenchHarness.h"
#include "Cpu040.h"
#include "FinderSignature.h"
#include "JitTestConfig.h"
#include "Q605Memory.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace q605app {

inline Q605Memory* gMem = nullptr;
inline Cpu040* gCpu = nullptr;
constexpr int kFrameCycles = 416667;          // 25 MHz / ~60 Hz

inline std::string find(const char* rel) { return testasset::find(rel); }

// Optional per-frame hook. A gate that owns a device clock outside the
// machine — the AppleTalk hub, whose NAT and timers advance in machine
// cycles — installs it here, so every gesture in this header (clicks,
// typing, settle loops) keeps that clock moving with guest time instead
// of freezing it for the length of a dialog.
inline std::function<void()> gAfterFrame;

inline void runFrames(long n) {
    for (long f = 0; f < n && !gCpu->isHalted(); f++) {
        gCpu->runCycles(kFrameCycles);
        if (gAfterFrame) gAfterFrame();
    }
}

inline uint32_t peek32(uint32_t addr) {
    return uint32_t(gMem->peek8(addr)) << 24 |
           uint32_t(gMem->peek8(addr + 1)) << 16 |
           uint32_t(gMem->peek8(addr + 2)) << 8 | gMem->peek8(addr + 3);
}

// ── screen ───────────────────────────────────────────────────────────────
struct Screen {
    int width = 0, height = 0, depth = 0;
    uint32_t stride = 0, offset = 0;
    std::vector<uint32_t> pixels;
};

// The main GDevice's PixMap through the DAFB CLUT — the guest's own idea of
// the screen (q605_boot_etalon's decode).
inline Screen decodeScreen() {
    const Q605Memory& mem = *gMem;
    Screen s;
    uint32_t scrnBase = peek32(0x0824);
    uint32_t mainDevH = peek32(0x08A4);
    uint32_t mainDev = mainDevH ? peek32(mainDevH) : 0;
    uint32_t pmapH = mainDev ? peek32(mainDev + 0x16) : 0;
    uint32_t pmap = pmapH ? peek32(pmapH) : 0;
    if (!pmap) return s;
    uint32_t pmBase = peek32(pmap);
    uint32_t boundsA = peek32(pmap + 0x06), boundsB = peek32(pmap + 0x0A);
    int top = int(boundsA >> 16), left = int(boundsA & 0xFFFF);
    int bottom = int(boundsB >> 16), right = int(boundsB & 0xFFFF);
    s.width = right - left;
    s.height = bottom - top;
    s.depth = mem.dafbDepth();
    s.stride = mem.dafbStride();
    s.offset = (pmBase ? pmBase : scrnBase) & (Q605Memory::kVramSize - 1);
    if (s.width <= 0 || s.width > 1600 || s.height <= 0 || s.height > 1200 ||
        (s.depth != 1 && s.depth != 2 && s.depth != 4 && s.depth != 8) ||
        s.stride < uint32_t((s.width * s.depth + 7) / 8) ||
        uint64_t(s.offset) + uint64_t(s.height) * s.stride > Q605Memory::kVramSize)
        return Screen{};
    const uint8_t* vram = mem.vram();
    const uint8_t (*clut)[3] = mem.clut();
    s.pixels.resize(size_t(s.width) * s.height);
    for (int y = 0; y < s.height; y++) {
        uint32_t row = s.offset + uint32_t(y) * s.stride;
        for (int x = 0; x < s.width; x++) {
            uint8_t packed = vram[row + uint32_t(x * s.depth / 8)], pen;
            if (s.depth == 1) pen = (packed >> (7 - (x & 7))) & 1;
            else if (s.depth == 2) pen = (packed >> (6 - 2 * (x & 3))) & 3;
            else if (s.depth == 4) pen = (x & 1) ? packed & 0x0F : packed >> 4;
            else pen = packed;
            const uint8_t* c = clut[pen];
            s.pixels[size_t(y) * s.width + x] =
                uint32_t(c[0]) << 16 | uint32_t(c[1]) << 8 | c[2];
        }
    }
    return s;
}

inline double luminance(uint32_t p) {
    return ((p >> 16) * 54 + ((p >> 8) & 0xFF) * 183 + (p & 0xFF) * 19) / 256.0;
}

struct Stats { double mean = 0, deviation = 0; };
inline Stats luminanceStats(const Screen& s, int x0, int x1, int y0, int y1) {
    if (x1 > s.width) x1 = s.width;
    if (y1 > s.height) y1 = s.height;
    if (x0 >= x1 || y0 >= y1) return {};
    double sum = 0, sum2 = 0;
    long count = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            const double lum = luminance(s.pixels[size_t(y) * s.width + x]);
            sum += lum; sum2 += lum * lum; count++;
        }
    Stats r;
    if (count) {
        r.mean = sum / count;
        r.deviation = std::sqrt(sum2 / count - r.mean * r.mean);
    }
    return r;
}

// q605_boot_etalon's full idle-Finder signature (menu bar AND desktop).
inline bool finderUp(const Screen& s) {
    if (s.width != 640 || s.height != 480 || s.depth != 8) return false;
    Stats m = luminanceStats(s, 0, s.width, 2, 16);
    Stats d = luminanceStats(s, 520, 630, 40, 430);
    return m.mean > 170 && m.mean < 235 && m.deviation > 40 && m.deviation < 100 &&
           d.mean > 100 && d.mean < 190 && d.deviation > 30 && d.deviation < 90 &&
           m.mean - d.mean > 35;
}

// The menu bar alone: liveness once windows cover the desktop sample.
inline bool menuBarUp(const Screen& s) {
    if (s.width != 640 || s.height != 480) return false;
    Stats m = luminanceStats(s, 0, s.width, 2, 16);
    return m.mean > 170 && m.mean < 235 && m.deviation > 40;
}

inline void dump(const char* name) {
    if (!std::getenv("POM68K_DUMP")) return;
    Screen s = decodeScreen();
    if (s.pixels.empty()) return;
    FILE* fp = std::fopen(name, "wb");
    if (!fp) return;
    std::fprintf(fp, "P6\n%d %d\n255\n", s.width, s.height);
    for (uint32_t p : s.pixels) {
        uint8_t rgb[3] = { uint8_t(p >> 16), uint8_t(p >> 8), uint8_t(p) };
        std::fwrite(rgb, 1, 3, fp);
    }
    std::fclose(fp);
}

inline double changed(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) {
    long n = 0;
    for (size_t i = 0; i < a.size() && i < b.size(); i++)
        if (a[i] != b[i]) n++;
    return a.empty() ? 0.0 : double(n) / double(a.size());
}

inline uint64_t screenFingerprint() {
    Screen s = decodeScreen();
    uint64_t fp = 1469598103934665603ull;
    for (uint32_t pixel : s.pixels)
        for (int byte = 0; byte < 4; byte++) {
            fp ^= (pixel >> (byte * 8)) & 0xFF;
            fp *= 1099511628211ull;
        }
    return fp;
}

// FNV-1a over the dark/light mask of one rectangle: "did THIS region
// change", palette- and clock-independent.
inline uint64_t regionMaskFingerprint(int x0, int x1, int y0, int y1) {
    Screen s = decodeScreen();
    uint64_t fp = 1469598103934665603ull;
    for (int y = y0; y < y1 && y < s.height; y++)
        for (int x = x0; x < x1 && x < s.width; x++) {
            fp ^= luminance(s.pixels[size_t(y) * s.width + x]) < 128.0;
            fp *= 1099511628211ull;
        }
    return fp;
}

// ── the guest's own words ────────────────────────────────────────────────
// Low memory is physical on the Quadra 605 (32-bit mode, RAM at 0), so
// CurApName, KeyMap and Mouse read straight through peek8.
// CurApName names the process RUNNING when it is sampled, and an 8.1 volume
// with background-only applications (MyEyes on this image) hands them most
// idle time slices: one sample can name a faceless process while the Finder
// owns the menu bar, and a majority vote elects the background process. The
// guest's own word that a gate can rely on is therefore "does this process
// RUN at all": sampled every frame across a second.
struct ProcessSample { std::string name; int frames = 0; };

inline std::vector<ProcessSample> runningProcesses(int frames = 60) {
    std::vector<ProcessSample> seen;
    for (int f = 0; f < frames; f++) {
        runFrames(1);
        const std::string name = findersig::curApName(*gMem);
        bool hit = false;
        for (auto& p : seen)
            if (p.name == name) { p.frames++; hit = true; break; }
        if (!hit) seen.push_back({name, 1});
    }
    return seen;
}

inline bool processRuns(const std::vector<ProcessSample>& seen, const char* name) {
    for (const auto& p : seen)
        if (p.name == name) return true;
    return false;
}

inline std::string describe(const std::vector<ProcessSample>& seen) {
    std::string s;
    for (const auto& p : seen) {
        if (!s.empty()) s += ", ";
        s += "'" + p.name + "'x" + std::to_string(p.frames);
    }
    return s;
}

inline bool keyMapBit(uint8_t code) {
    return (gMem->peek8(0x174 + (code >> 3)) >> (code & 7)) & 1;
}

inline void pointer(int& x, int& y) {
    x = int16_t(uint16_t(gMem->peek8(0x832)) << 8 | gMem->peek8(0x833));
    y = int16_t(uint16_t(gMem->peek8(0x830)) << 8 | gMem->peek8(0x831));
}

// ── input ────────────────────────────────────────────────────────────────
// ASCII -> ADB on a US layout. ADB codes are PHYSICAL: a character must be
// sent on the key that produces it under the GUEST's layout.
inline uint8_t adbForUs(char c) {
    switch (c) {
        case 'a': return 0x00; case 's': return 0x01; case 'd': return 0x02;
        case 'f': return 0x03; case 'h': return 0x04; case 'g': return 0x05;
        case 'z': return 0x06; case 'x': return 0x07; case 'c': return 0x08;
        case 'v': return 0x09; case 'b': return 0x0B; case 'q': return 0x0C;
        case 'w': return 0x0D; case 'e': return 0x0E; case 'r': return 0x0F;
        case 'y': return 0x10; case 't': return 0x11; case '1': return 0x12;
        case '2': return 0x13; case '3': return 0x14; case '4': return 0x15;
        case '6': return 0x16; case '5': return 0x17; case '9': return 0x19;
        case '7': return 0x1A; case '-': return 0x1B; case '8': return 0x1C;
        case '0': return 0x1D; case 'o': return 0x1F; case 'u': return 0x20;
        case 'i': return 0x22; case 'p': return 0x23; case 'l': return 0x25;
        case 'j': return 0x26; case 'k': return 0x28; case ';': return 0x29;
        case ',': return 0x2B; case 'n': return 0x2D; case 'm': return 0x2E;
        case '.': return 0x2F; case ' ': return 0x31;
        default: return 0xFF;
    }
}

// The 8.1 reference image is a US System with the FRENCH keyboard layout
// selected (the tricolour flag in its menu bar): AZERTY. The first run of
// the SimpleText gate typed "mac-8" on US codes, the Finder received
// ",qc)!" and opened Mac OS Info Center — which launched Netscape
// (2026-09-08). Letters are remapped here; digits and most punctuation are
// shifted on AZERTY and deliberately unsupported (0xFF), so a gate types
// letters and spaces only. The guest's KCHR could not be located from low
// memory ($1B40 points into System code on 8.1), so this stays a table.
inline bool gAzertyGuest = true;

inline uint8_t adbFor(char c) {
    if (gAzertyGuest) {
        switch (c) {
            case 'a': return 0x0C;              // US Q key
            case 'q': return 0x00;              // US A key
            case 'z': return 0x0D;              // US W key
            case 'w': return 0x06;              // US Z key
            case 'm': return 0x29;              // US ; key
            case ',': return 0x2E;              // US M key
            default: break;
        }
        if ((c >= '0' && c <= '9') || c == '.' || c == ';' || c == '-') return 0xFF;
    }
    return adbForUs(c);
}

inline std::string describeTyping(const char* value) {
    std::string s;
    char buf[8];
    for (const char* p = value; *p; p++) {
        std::snprintf(buf, sizeof buf, "%02X ", adbFor(*p));
        s += buf;
    }
    return s;
}

inline void keyHold(uint8_t code, long frames) {
    gMem->keyEvent(code, true);
    runFrames(frames);
    gMem->keyEvent(code, false);
    runFrames(6);
}

// Ordinary typing: 3 frames down, 3 up — inside Finder's type-select
// window. Requires Slow Keys OFF (see ensureFastKeys).
inline void typeText(const char* value) {
    for (const char* p = value; *p; p++) {
        const uint8_t code = adbFor(*p);
        if (code != 0xFF) keyHold(code, 3);
    }
}

// Cmd + shortcut (a PHYSICAL code — pass adbFor(letter) so the chord follows
// the guest's layout: Cmd-$0C is Cmd-Q on a US table and Cmd-A on AZERTY,
// which is how the first SimpleText run selected all instead of quitting).
// The 6-frame pause establishes Command before the letter so the ADB poll
// cannot return both in one packet.
inline void command(uint8_t shortcut, long settle, long hold = 30) {
    gMem->keyEvent(0x37, true);
    runFrames(6);
    keyHold(shortcut, hold);
    gMem->keyEvent(0x37, false);
    runFrames(settle);
}

// Frames between a key-down and its KeyMap bit (-1 when it never lands
// within maxFrames). The guest's acceptance delay, measured rather than
// assumed: a plain keyboard answers within the ADB poll (2-3 frames
// measured); Slow Keys answers only after its acceptance delay (32-34
// frames measured at the image's setting, 2026-09-08). Escape is the probe
// key: no character, no Finder gesture of its own. Modifiers are not usable
// here — the ADB keyboard reports them through register 2 and the driver
// never sets their KeyMap bits from a plain poll (Control measured absent).
inline int keyLatency(uint8_t code = 0x35, int maxFrames = 240) {
    gMem->keyEvent(code, true);
    int seen = -1;
    for (int f = 0; f < maxFrames; f++) {
        runFrames(1);
        if (keyMapBit(code)) { seen = f + 1; break; }
        if (std::getenv("POM68K_KEYS_DEBUG") && (f % 30) == 29) {
            std::fprintf(stderr, "[keys] f=%d KeyMap", f + 1);
            for (int i = 0; i < 8; i++)
                std::fprintf(stderr, " %02X", gMem->peek8(0x174 + uint32_t(i)));
            std::fprintf(stderr, "\n");
        }
    }
    gMem->keyEvent(code, false);
    runFrames(12);
    return seen;
}

// Ordinary typing (3-frame taps) needs the acceptance well under a tap.
inline bool slowKeysActive() {
    const int latency = keyLatency();
    std::fprintf(stderr, "[keys] key-down reaches KeyMap after %d frames\n", latency);
    return latency < 0 || latency > 3;
}

// Make ordinary typing land: Easy Access toggles Slow Keys when Return is
// held ~8 s. Applied only when the probe says the feature is ON, and
// re-probed afterwards; returns whether short taps are accepted now.
inline bool ensureFastKeys() {
    if (!slowKeysActive()) return true;
    std::fprintf(stderr, "[keys] Slow Keys active — toggling with the "
                         "eight-second Return hold\n");
    keyHold(0x24, 560);
    runFrames(120);
    const bool fast = !slowKeysActive();
    std::fprintf(stderr, "[keys] after the toggle: short taps %s\n",
                 fast ? "accepted" : "STILL rejected");
    return fast;
}

// Closed-loop pointer steering against the Mouse global; raw motion is
// accelerated by the guest and cannot target a coordinate open-loop.
inline void steer(int tx, int ty) {
    int px = 0, py = 0;
    for (int it = 0; it < 800; it++) {
        pointer(px, py);
        const int dx = tx - px, dy = ty - py;
        if (!dx && !dy) break;
        auto step = [](int d) {
            int s = d / 2;
            if (!s) s = d > 0 ? 1 : (d < 0 ? -1 : 0);
            return std::max(-8, std::min(8, s));
        };
        gMem->mouseMove(step(dx), step(dy));
        runFrames(1);
    }
    pointer(px, py);
    if (std::abs(px - tx) > 2 || std::abs(py - ty) > 2)
        std::fprintf(stderr, "steer: wanted (%d,%d), reached (%d,%d)\n",
                     tx, ty, px, py);
}

inline void click(int tx, int ty, long settle = 30) {
    steer(tx, ty);
    gMem->mouseButton(true);
    runFrames(6);
    gMem->mouseButton(false);
    runFrames(settle);
}

// ── Finder navigation ────────────────────────────────────────────────────
// Cmd-Option-W closes every Finder window: the one known scope reset.
inline void closeAllFinderWindows() {
    gMem->keyEvent(0x37, true);                 // Cmd
    runFrames(12);
    gMem->keyEvent(0x3A, true);                 // Option
    runFrames(12);
    keyHold(adbFor('w'), 30);                   // W on the guest's layout
    gMem->keyEvent(0x3A, false);
    gMem->keyEvent(0x37, false);
    runFrames(300);
}

// Open a desktop icon by click + Cmd-O. Desktop icon positions live in the
// volume's own Desktop database, so on a host-owned image that is never
// written back they are as fixed as its name — the volume's own is the one
// item type-select cannot reach here (hyphen and digits, see adbFor).
inline void openByClick(int x, int y, long settle) {
    click(x, y, 30);
    command(0x1F, settle);                      // 'o' — Open
}

// Type-select by prefix, then Cmd-O. The prefix must be unambiguous in the
// frontmost window: a prefix that matches nothing selects the alphabetical
// neighbour, silently.
inline void openBySelect(const char* prefix, long settle) {
    typeText(prefix);
    runFrames(30);
    command(0x1F, settle);                      // 'o' — Open
}

// ── catalog ──────────────────────────────────────────────────────────────
// Occurrences of a length-prefixed name in the host-owned image: an HFS
// catalog stores a file's name in its file record and its thread record,
// so a saved document moves its count by 2. Direction only is asserted.
inline long catalogCount(const std::vector<uint8_t>& img, const char* name) {
    const size_t n = std::strlen(name);
    if (!n || n > 31) return 0;
    std::vector<uint8_t> key(n + 1);
    key[0] = uint8_t(n);
    std::memcpy(key.data() + 1, name, n);
    long count = 0;
    const uint8_t* p = img.data();
    const uint8_t* end = img.data() + img.size();
    while ((p = static_cast<const uint8_t*>(std::memchr(p, key[0], size_t(end - p)))) != nullptr) {
        if (size_t(end - p) < key.size()) break;
        if (!std::memcmp(p, key.data(), key.size())) count++;
        p++;
    }
    return count;
}

// ── boot ─────────────────────────────────────────────────────────────────
// q605_beyond_etalon's loop: sparse sampling after the SCSI traffic says
// the System is up, stopping on the full Finder signature.
inline bool bootToFinder(int maxFrames) {
    while (gMem->cpuHeld()) gMem->tick(1000);
    const long scsi0 = gMem->scsi().commands;
    for (int frame = 0; frame < maxFrames && !gCpu->isHalted(); frame++) {
        gCpu->runCycles(kFrameCycles);
        if (frame >= 3600 && !(frame % 60) && gMem->scsi().commands - scsi0 > 4000) {
            if (finderUp(decodeScreen())) {
                runFrames(300);
                return !gCpu->isHalted();
            }
        }
    }
    Screen s = decodeScreen();
    if (s.pixels.empty()) {
        std::fprintf(stderr, "[boot] no decodable screen after %d frames "
                     "(halted=%d)\n", maxFrames, gCpu->isHalted());
    } else {
        Stats m = luminanceStats(s, 0, s.width, 2, 16);
        Stats d = luminanceStats(s, 520, 630, 40, 430);
        std::fprintf(stderr, "[boot] no Finder after %d frames: %dx%d@%d "
                     "menu %.1f/%.1f desktop %.1f/%.1f (halted=%d)\n",
                     maxFrames, s.width, s.height, s.depth, m.mean, m.deviation,
                     d.mean, d.deviation, gCpu->isHalted());
    }
    return false;
}

}  // namespace q605app
