// POM68K — shared LC II real-application measurement driver.
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#pragma once

#include "AssetFingerprint.h"
#include "BenchHarness.h"
#include "Cpu030.h"
#include "FinderSignature.h"
#include "JitTestConfig.h"
#include "V8Memory.h"
#include "V8Video.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace lciiapp {

inline V8Memory* gMem = nullptr;
inline Cpu030* gCpu = nullptr;

inline std::string find(const char* rel) { return testasset::find(rel); }

inline void runFrames(long n) {
    for (long f = 0; f < n && !gCpu->isHalted(); f++)
        gCpu->runCycles(V8Memory::kCpuHz / 60);
}

inline void screen(std::vector<uint32_t>& out) {
    V8Video video(*gMem);
    video.decode(out);
}

inline double blackRatio(const std::vector<uint32_t>& fb, int x0, int x1,
                         int y0, int y1) {
    long black = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            const size_t i = size_t(y) * 512 + x;
            if (i < fb.size() && (fb[i] & 0xFF) < 0x80) black++;
        }
    return double(black) / double(x1 - x0) / double(y1 - y0);
}

inline bool finderUp() {
    std::vector<uint32_t> fb;
    screen(fb);
    // A light menu bar over a desktop that has SOMETHING on it. The usual
    // upper desktop bound belongs to another volume: GISTPERSO's pattern
    // measures 0.75, so using that foreign calibration rejects a live Finder.
    return blackRatio(fb, 0, 512, 2, 16) < 0.30 &&
           blackRatio(fb, 400, 512, 40, 340) > 0.20;
}

inline void dump(const char* name) {
    if (!std::getenv("POM68K_DUMP")) return;
    std::vector<uint32_t> fb;
    screen(fb);
    if (fb.empty()) return;
    std::FILE* f = std::fopen(name, "wb");
    if (!f) return;
    std::fprintf(f, "P6\n512 384\n255\n");
    for (uint32_t p : fb) {
        const unsigned char rgb[3] = {
            static_cast<unsigned char>(p >> 16),
            static_cast<unsigned char>(p >> 8),
            static_cast<unsigned char>(p)};
        std::fwrite(rgb, 1, 3, f);
    }
    std::fclose(f);
}

// The driver-descriptor patch every LC II harness applies: without a type
// $6A entry the ROM refuses this volume and issues no SCSI command.
inline void ensureBootDriverType(std::vector<uint8_t>& img) {
    if (img.size() < 512 || img[0] != 'E' || img[1] != 'R') return;
    const int count = (img[0x10] << 8) | img[0x11];
    for (int i = 0; i < count && 0x12 + i * 8 + 8 <= 512; i++) {
        const int e = 0x12 + i * 8;
        if (((img[e + 6] << 8) | img[e + 7]) == 0x6A) return;
    }
    if (count >= 1 && 0x12 + count * 8 + 8 <= 512) {
        const int src = 0x12, dst = 0x12 + count * 8;
        for (int k = 0; k < 8; k++) img[dst + k] = img[src + k];
        img[dst + 6] = 0x00;
        img[dst + 7] = 0x6A;
        img[0x10] = uint8_t((count + 1) >> 8);
        img[0x11] = uint8_t(count + 1);
    }
}

// ASCII -> ADB for Finder type-select and numeric benchmark fields. Typing
// must stay inside Finder's roughly one-second type-select window.
inline uint8_t adbFor(char c) {
    switch (c) {
        case 'a': return 0x00; case 's': return 0x01; case 'd': return 0x02;
        case 'f': return 0x03; case 'h': return 0x04; case 'g': return 0x05;
        case 'z': return 0x06; case 'x': return 0x07; case 'c': return 0x08;
        case 'v': return 0x09; case 'b': return 0x0B; case 'q': return 0x0C;
        case 'w': return 0x0D; case 'e': return 0x0E; case 'r': return 0x0F;
        case 'y': return 0x10; case 't': return 0x11; case '1': return 0x12;
        case '2': return 0x13; case '3': return 0x14; case '4': return 0x15;
        case '6': return 0x16; case '5': return 0x17; case '9': return 0x19;
        case '7': return 0x1A; case '8': return 0x1C; case '0': return 0x1D;
        case 'o': return 0x1F; case 'u': return 0x20; case 'i': return 0x22;
        case 'p': return 0x23; case 'l': return 0x25; case 'j': return 0x26;
        case 'k': return 0x28; case 'n': return 0x2D; case 'm': return 0x2E;
        case ' ': return 0x31;
        default: return 0xFF;
    }
}

// GISTPERSO runs a French System with an AZERTY KCHR: the physical key that
// is Q on a US keyboard types 'a', W types 'z', and the ';' key types 'm'.
// ADB codes are physical, so a character must be sent on the key that
// PRODUCES it under the guest's layout, or "black forest m" arrives as
// "blqck forest ," and the Finder's type-select lands on the alphabetical
// neighbour of THAT — DESAS CITY, on 2026-09-07. Every earlier hop on this
// volume only worked because the mistyped characters happened to sort
// before the intended item. Digits are shifted on AZERTY and deliberately
// unsupported here.
inline bool gAzertyGuest = true;

inline uint8_t adbForGuest(char c) {
    if (gAzertyGuest) {
        switch (c) {
            case 'a': return 0x0C;             // US Q key
            case 'q': return 0x00;             // US A key
            case 'z': return 0x0D;             // US W key
            case 'w': return 0x06;             // US Z key
            case 'm': return 0x29;             // US ; key
            default: break;
        }
        if (c >= '0' && c <= '9') return 0xFF;
    }
    return adbFor(c);
}

inline void typeText(const char* value) {
    for (const char* p = value; *p; p++) {
        const uint8_t code = adbForGuest(*p);
        if (code == 0xFF) continue;
        gMem->keyEvent(code, true);
        runFrames(3);
        gMem->keyEvent(code, false);
        runFrames(3);
    }
}

inline void keyHold(uint8_t code, long frames) {
    gMem->keyEvent(code, true);
    runFrames(frames);
    gMem->keyEvent(code, false);
    runFrames(6);
}

inline double changed(const std::vector<uint32_t>& a,
                      const std::vector<uint32_t>& b) {
    long n = 0;
    for (size_t i = 0; i < a.size() && i < b.size(); i++)
        if (a[i] != b[i]) n++;
    return a.empty() ? 0.0 : double(n) / double(a.size());
}

inline uint64_t screenFingerprint() {
    std::vector<uint32_t> fb;
    screen(fb);
    uint64_t fp = 1469598103934665603ull;
    for (uint32_t pixel : fb) {
        for (int byte = 0; byte < 4; byte++) {
            fp ^= (pixel >> (byte * 8)) & 0xFF;
            fp *= 1099511628211ull;
        }
    }
    return fp;
}

// FNV-1a over the monochrome mask of one screen rectangle: the way a gate
// asks "did THIS region change" without caring about palette or about the
// rest of the screen (a menu-bar clock, a blinking cursor elsewhere).
inline uint64_t regionMaskFingerprint(int x0, int x1, int y0, int y1) {
    std::vector<uint32_t> fb;
    screen(fb);
    uint64_t fp = 1469598103934665603ull;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            const size_t i = size_t(y) * 512 + x;
            fp ^= i < fb.size() && (fb[i] & 0xFF) < 0x80;
            fp *= 1099511628211ull;
        }
    return fp;
}

// Low memory is physical on the LC II (the V8 maps RAM at 0 with the PMMU
// translating identity there), so the Mouse and CurApName globals read
// straight through peek8 — the same read the Speedometer steering uses.
// Non-ASCII bytes (MacRoman accents, the trademark sign) are shown as '?'
// rather than voiding the name: an application called "SimCity 2000\xAA"
// must still be recognisable as SimCity.
inline std::string frontApplication() {
    const int n = gMem->peek8(0x910);
    const int len = n > 31 ? 31 : n;
    std::string s;
    for (int i = 0; i < len; i++) {
        const int c = gMem->peek8(0x910 + 1 + uint32_t(i));
        if (c < 0x20) return {};                // not a live Str31
        s += c >= 0x7F ? '?' : char(c);
    }
    return s;
}

// ── Deterministic Finder navigation ─────────────────────────────────────
// This volume auto-opens several overlapping Finder windows at boot, and a
// type-select lands in whichever one is frontmost. On 2026-09-06 that put the
// Speedometer census in Prince of Persia's Read Me, and the SimCity census
// had been opening TED CITY — the alphabetical neighbour of a prefix with
// no match in the wrong window — instead of the city it named. Reset the
// scope rather than infer the window stack: Cmd-Option-W closes every
// Finder window, then the desktop volume icon is the one known root.
// ADB codes are physical and this volume uses a French layout, where W is
// code $06 rather than QWERTY's $0D; send both while the chord is held. The
// non-W key is Z on either layout, and Cmd-Option-Z is harmless in the
// Finder.
inline void closeAllFinderWindows() {
    for (uint8_t w : {uint8_t(0x06), uint8_t(0x0D)}) {
        gMem->keyEvent(0x37, true);            // Cmd
        runFrames(12);
        gMem->keyEvent(0x3A, true);            // Option
        runFrames(12);
        keyHold(w, 75);                        // W (AZERTY, then QWERTY)
        gMem->keyEvent(0x3A, false);
        gMem->keyEvent(0x37, false);
        runFrames(300);
    }
}

// Finder type-select by prefix, then Cmd-O opens the selection. The prefix
// must stay inside Finder's roughly one-second type-select window, and it
// must be unambiguous within the frontmost window — a prefix that matches
// nothing selects the alphabetical neighbour, silently.
inline void openBySelect(const char* prefix, long settle) {
    typeText(prefix);
    runFrames(30);
    gMem->keyEvent(0x37, true);                // Cmd
    runFrames(6);
    keyHold(0x1F, 60);                         // 'o' — Open
    gMem->keyEvent(0x37, false);
    runFrames(settle);
}

}  // namespace lciiapp
