// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── The M0110A $79 keypad prefix, byte for byte ──
//
// The compact keyboard answers Inquiry/Instant with ONE byte per
// transaction, so a keypad or arrow key is a two- or three-transaction
// sequence. What the keyboard owes, per MAME
// src/devices/bus/mackbd/pluskbd.cpp ("Keypad keys and arrow keys produce
// scan codes with the 0x79 prefix. The keyboard simulates holding shift
// when pressing the = / * + keys on the keypad.") and the raw-code chart
// in tmk_keyboard tmk_core/protocol/m0110.h (page 7 of Apple's *Technical
// Information for the Macintosh Plus*):
//
//   main block     one byte, (vk << 1) | 1, bit 7 = release
//   keypad, arrows $79 then that byte
//   keypad = / * + $71/$F1 (a synthetic Shift) then $79 then that byte
//
// This gate asserts the exact drained sequence for one keypad digit, one
// calc key, one arrow, and — the negative case that keeps the fix narrow —
// a plain letter, which must stay a single byte. No ROM, no media.

#include "MacInput.h"

#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

namespace {

int failures = 0;

// Drain the keyboard the way MacMemory does: one Instant ($14) per VIA
// shift-register transaction, until it answers Null ($7B).
std::vector<uint8_t> drain(MacKeyboard& kbd) {
    std::vector<uint8_t> out;
    while (kbd.pending() && out.size() < 8) out.push_back(kbd.respond(0x14));
    return out;
}

std::string hex(const std::vector<uint8_t>& bytes) {
    std::string s;
    char buf[8];
    for (uint8_t b : bytes) {
        std::snprintf(buf, sizeof buf, "%s%02X", s.empty() ? "" : " ", b);
        s += buf;
    }
    return s.empty() ? "(nothing)" : s;
}

void expect(const char* what, MacKeyboard& kbd, uint8_t vk, bool down,
            std::initializer_list<uint8_t> want) {
    kbd.keyEvent(vk, down);
    const std::vector<uint8_t> got = drain(kbd);
    const std::vector<uint8_t> wanted(want);
    if (got != wanted) {
        std::fprintf(stderr, "FAIL: %s vk=$%02X %s -> %s, want %s\n", what, vk,
                     down ? "down" : "up", hex(got).c_str(),
                     hex(wanted).c_str());
        failures++;
        return;
    }
    std::printf("  %-16s vk=$%02X %-4s  %s\n", what, vk, down ? "down" : "up",
                hex(got).c_str());
}

}  // namespace

int main() {
    MacKeyboard kbd;

    std::printf("main block (unaffected — one byte, no prefix)\n");
    expect("A", kbd, 0x00, true, {0x01});
    expect("A", kbd, 0x00, false, {0x81});
    expect("Return", kbd, 0x24, true, {0x49});
    expect("Return", kbd, 0x24, false, {0xC9});
    expect("Shift", kbd, 0x38, true, {0x71});
    expect("Shift", kbd, 0x38, false, {0xF1});

    std::printf("keypad digits and friends ($79 prefix)\n");
    expect("keypad 0", kbd, 0x52, true, {0x79, 0x25});
    expect("keypad 0", kbd, 0x52, false, {0x79, 0xA5});
    expect("keypad 9", kbd, 0x5C, true, {0x79, 0x39});
    expect("keypad 9", kbd, 0x5C, false, {0x79, 0xB9});
    expect("keypad .", kbd, 0x41, true, {0x79, 0x03});
    expect("keypad Enter", kbd, 0x4C, true, {0x79, 0x19});
    expect("Clear", kbd, 0x47, true, {0x79, 0x0F});
    expect("keypad -", kbd, 0x4E, true, {0x79, 0x1D});

    std::printf("arrows ($79 prefix, no Shift)\n");
    expect("Left", kbd, 0x3B, true, {0x79, 0x0D});
    expect("Left", kbd, 0x3B, false, {0x79, 0x8D});
    expect("Right", kbd, 0x3C, true, {0x79, 0x05});
    expect("Down", kbd, 0x3D, true, {0x79, 0x11});
    expect("Up", kbd, 0x3E, true, {0x79, 0x1B});
    expect("Up", kbd, 0x3E, false, {0x79, 0x9B});

    std::printf("calc keys (synthetic Shift, then $79)\n");
    expect("keypad +", kbd, 0x45, true, {0x71, 0x79, 0x0D});
    expect("keypad +", kbd, 0x45, false, {0xF1, 0x79, 0x8D});
    expect("keypad *", kbd, 0x43, true, {0x71, 0x79, 0x05});
    expect("keypad /", kbd, 0x4B, true, {0x71, 0x79, 0x1B});
    expect("keypad =", kbd, 0x51, true, {0x71, 0x79, 0x11});
    expect("keypad =", kbd, 0x51, false, {0xF1, 0x79, 0x91});

    // A code the M0110A has no key for must produce NOTHING. The old
    // (vk << 1) | 1 encoding turned every code >= $40 into a byte with bit
    // 7 set, i.e. a forged release of an unrelated key.
    std::printf("keys this keyboard does not have\n");
    expect("F1 (ADB $7A)", kbd, 0x7A, true, {});
    expect("right Shift", kbd, 0x7B, true, {});

    // The queue is a byte stream: a prefix must never be split from its
    // code by an intervening event, and a reset must drop the whole thing.
    kbd.keyEvent(0x52, true);
    kbd.keyEvent(0x00, true);
    const std::vector<uint8_t> mixed = drain(kbd);
    if (mixed != std::vector<uint8_t>{0x79, 0x25, 0x01}) {
        std::fprintf(stderr, "FAIL: sequence interleaved -> %s\n",
                     hex(mixed).c_str());
        failures++;
    }
    kbd.keyEvent(0x45, true);
    kbd.reset();
    if (kbd.pending() || kbd.respond(0x14) != 0x7B) {
        std::fprintf(stderr, "FAIL: reset left a half-sent sequence queued\n");
        failures++;
    }

    if (failures) {
        std::fprintf(stderr, "m0110_keypad_test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("m0110_keypad_test: $79 framing exact, main block untouched\n");
    return 0;
}
