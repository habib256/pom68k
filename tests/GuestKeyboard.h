// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// GuestKeyboard -- which PHYSICAL key, shifted or not, produces a character
// under the guest's layout. ADB and M0110 codes name key positions; the
// character they yield is the guest's KCHR resource's business, and the
// reference volumes run two layouts: the stock US one, and the French
// (AZERTY) one GIST PERSO and the Mac OS 8.1 reference select.
//
// Until 2026-09-16 the harnesses typed letters and spaces only: digits and
// most punctuation are shifted or moved on AZERTY, were mapped to "no key",
// and every gate that needed a dash or a digit opened things with the mouse
// instead (TODO § Preuve). The guest's KCHR could not be located from low
// memory ($1B40 points into System code on 8.1), so this is a TABLE of the
// Apple French keyboard, one entry per printable ASCII character, checked
// against the guest by q605_persist_etalon: it types a folder name with
// digits, a dash, a dot and a capital, and the host reads that exact name
// back from the HFS catalog. Dead keys (^ ¨ ` on the French layout) and
// characters outside ASCII are not offered.

#pragma once

#include <cstdint>

namespace guestkbd {

struct Keystroke {
    uint8_t code = 0xFF;   // physical key (virtual key code); 0xFF = not typeable
    bool shift = false;
};

constexpr uint8_t kShift = 0x38;
constexpr uint8_t kSpace = 0x31;

// US layout: the code of the key that carries `c` unshifted.
inline Keystroke us(char c) {
    switch (c) {
        case 'a': return {0x00}; case 's': return {0x01}; case 'd': return {0x02};
        case 'f': return {0x03}; case 'h': return {0x04}; case 'g': return {0x05};
        case 'z': return {0x06}; case 'x': return {0x07}; case 'c': return {0x08};
        case 'v': return {0x09}; case 'b': return {0x0B}; case 'q': return {0x0C};
        case 'w': return {0x0D}; case 'e': return {0x0E}; case 'r': return {0x0F};
        case 'y': return {0x10}; case 't': return {0x11}; case '1': return {0x12};
        case '2': return {0x13}; case '3': return {0x14}; case '4': return {0x15};
        case '6': return {0x16}; case '5': return {0x17}; case '=': return {0x18};
        case '9': return {0x19}; case '7': return {0x1A}; case '-': return {0x1B};
        case '8': return {0x1C}; case '0': return {0x1D}; case ']': return {0x1E};
        case 'o': return {0x1F}; case 'u': return {0x20}; case '[': return {0x21};
        case 'i': return {0x22}; case 'p': return {0x23}; case 'l': return {0x25};
        case 'j': return {0x26}; case '\'': return {0x27}; case 'k': return {0x28};
        case ';': return {0x29}; case '\\': return {0x2A}; case ',': return {0x2B};
        case '/': return {0x2C}; case 'n': return {0x2D}; case 'm': return {0x2E};
        case '.': return {0x2F}; case '`': return {0x32}; case ' ': return {kSpace};
        // shifted characters of the US layout
        case '!': return {0x12, true}; case '@': return {0x13, true};
        case '#': return {0x14, true}; case '$': return {0x15, true};
        case '%': return {0x17, true}; case '^': return {0x16, true};
        case '&': return {0x1A, true}; case '*': return {0x1C, true};
        case '(': return {0x19, true}; case ')': return {0x1D, true};
        case '_': return {0x1B, true}; case '+': return {0x18, true};
        case ':': return {0x29, true}; case '"': return {0x27, true};
        case '<': return {0x2B, true}; case '>': return {0x2F, true};
        case '?': return {0x2C, true}; case '~': return {0x32, true};
        case '{': return {0x21, true}; case '}': return {0x1E, true};
        case '|': return {0x2A, true};
        default: break;
    }
    if (c >= 'A' && c <= 'Z') { Keystroke k = us(char(c - 'A' + 'a')); k.shift = true; return k; }
    return {};
}

// Apple French (AZERTY) layout, by the US key position each character sits on.
inline Keystroke azerty(char c) {
    switch (c) {
        case 'a': return {0x0C}; case 'q': return {0x00};     // A/Q swapped
        case 'z': return {0x0D}; case 'w': return {0x06};     // Z/W swapped
        case 'm': return {0x29};                              // M on the US ; key
        case ',': return {0x2E}; case '?': return {0x2E, true};   // US M key
        case ';': return {0x2B}; case '.': return {0x2B, true};   // US , key
        case ':': return {0x2F}; case '/': return {0x2F, true};   // US . key
        case '=': return {0x2C}; case '+': return {0x2C, true};   // US / key
        case '&': return {0x12}; case '1': return {0x12, true};
        case '2': return {0x13, true};                            // é unshifted
        case '"': return {0x14}; case '3': return {0x14, true};
        case '\'': return {0x15}; case '4': return {0x15, true};
        case '(': return {0x17}; case '5': return {0x17, true};
        case '6': return {0x16, true};                            // § unshifted
        case '7': return {0x1A, true};                            // è unshifted
        case '!': return {0x1C}; case '8': return {0x1C, true};
        case '9': return {0x19, true};                            // ç unshifted
        case '0': return {0x1D, true};                            // à unshifted
        case ')': return {0x1B};                                  // ° shifted
        case '-': return {0x18}; case '_': return {0x18, true};   // US = key
        case '$': return {0x1E}; case '*': return {0x1E, true};   // US ] key
        case '%': return {0x27, true};                            // ù unshifted
        case '@': return {0x32}; case '#': return {0x32, true};   // US ` key
        case '<': return {0x0A}; case '>': return {0x0A, true};   // the ISO key left of W
        case ' ': return {kSpace};
        default: break;
    }
    if (c >= 'A' && c <= 'Z') { Keystroke k = azerty(char(c - 'A' + 'a')); k.shift = true; return k; }
    if (c >= 'a' && c <= 'z') return us(c);                      // the other letters sit where the US ones do
    return {};
}

inline Keystroke keystrokeFor(char c, bool azertyGuest) {
    return azertyGuest ? azerty(c) : us(c);
}

// Type `value` through `keyEvent(code, down)`, holding Shift around a
// shifted key and pacing with `wait(frames)`. Untypeable characters are
// skipped and counted in the return value.
template <class KeyEvent, class Wait>
inline int type(const char* value, bool azertyGuest, KeyEvent&& keyEvent, Wait&& wait,
                long hold = 3, long gap = 3) {
    int skipped = 0;
    for (const char* p = value; *p; p++) {
        const Keystroke k = keystrokeFor(*p, azertyGuest);
        if (k.code == 0xFF) { skipped++; continue; }
        if (k.shift) { keyEvent(kShift, true); wait(2); }
        keyEvent(k.code, true);
        wait(hold);
        keyEvent(k.code, false);
        if (k.shift) { wait(1); keyEvent(kShift, false); }
        wait(gap);
    }
    return skipped;
}

} // namespace guestkbd
