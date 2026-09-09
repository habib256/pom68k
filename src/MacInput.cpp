// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "MacInput.h"

// Keyboard commands (M0110 protocol, DEV.md § Input — TMK/MAME-pinned)
namespace {
constexpr uint8_t kCmdInquiry = 0x10;
constexpr uint8_t kCmdInstant = 0x14;
constexpr uint8_t kCmdModel   = 0x16;
constexpr uint8_t kCmdTest    = 0x36;
constexpr uint8_t kNull       = 0x7B;
constexpr uint8_t kModelM0110 = 0x0B;   // M0110A (Mini vMac value)
constexpr uint8_t kTestAck    = 0x7D;

// ── The $79 keypad prefix ───────────────────────────────────────────────
// The M0110A "emulates an M0120 keypad with an M0110 keyboard plugged in
// to it. Keypad keys and arrow keys produce scan codes with the 0x79
// prefix. The keyboard simulates holding shift when pressing the = / * +
// keys on the keypad." — MAME src/devices/bus/mackbd/pluskbd.cpp, header
// comment. The raw codes and the exact byte sequences are the chart in
// tmk_keyboard tmk_core/protocol/m0110.h/.c ("ARROW KEYS" / "RAW CODE"),
// itself page 7 of Apple's *Technical Information for the Macintosh Plus*:
//
//     Left:         $79 $0D        $79 $8D
//     Right:        $79 $05        $79 $85
//     Up:           $79 $1B        $79 $9B
//     Down:         $79 $11        $79 $91
//     Pad+:   $71 $79 $0D    $F1 $79 $8D
//     Pad*:   $71 $79 $05    $F1 $79 $85
//     Pad/:   $71 $79 $1B    $F1 $79 $9B
//     Pad=:   $71 $79 $11    $F1 $79 $91
//
// The four "calc" keys share their raw code with an arrow key; only the
// synthetic Shift transition ($71 press / $F1 release) tells them apart,
// which is why the keyboard sends one. Without the prefix these keys are
// not merely mislabelled: keypad virtual codes are >= $40, so the plain
// (vk<<1)|1 encoding overflows into bit 7 and the ROM reads a RELEASE of
// some other key, and Up/Down would emit $7C/$7A next to $7B = Null.
constexpr uint8_t kKeypadPrefix = 0x79;
constexpr uint8_t kShiftDown    = 0x71;   // raw Shift transition
constexpr uint8_t kShiftUp      = 0xF1;

struct PadKey {
    uint8_t vk;     // Macintosh virtual key code
    uint8_t raw;    // M0110A raw transition code, behind the $79 prefix
    bool    calc;   // keyboard fakes Shift around it (= / * +)
};
constexpr PadKey kPadKeys[] = {
    { 0x41, 0x03, false },   // keypad .
    { 0x43, 0x05, true  },   // keypad *
    { 0x45, 0x0D, true  },   // keypad +
    { 0x47, 0x0F, false },   // Clear
    { 0x4B, 0x1B, true  },   // keypad /
    { 0x4C, 0x19, false },   // keypad Enter
    { 0x4E, 0x1D, false },   // keypad -
    { 0x51, 0x11, true  },   // keypad =
    { 0x52, 0x25, false },   // keypad 0
    { 0x53, 0x27, false },   // keypad 1
    { 0x54, 0x29, false },   // keypad 2
    { 0x55, 0x2B, false },   // keypad 3
    { 0x56, 0x2D, false },   // keypad 4
    { 0x57, 0x2F, false },   // keypad 5
    { 0x58, 0x31, false },   // keypad 6
    { 0x59, 0x33, false },   // keypad 7
    { 0x5B, 0x37, false },   // keypad 8
    { 0x5C, 0x39, false },   // keypad 9
    { 0x3B, 0x0D, false },   // Left
    { 0x3C, 0x05, false },   // Right
    { 0x3D, 0x11, false },   // Down
    { 0x3E, 0x1B, false },   // Up
};
}

void MacKeyboard::keyEvent(uint8_t virtualKey, bool down) {
    for (const PadKey& k : kPadKeys) {
        if (k.vk != virtualKey) continue;
        if (k.calc) queue_.push_back(down ? kShiftDown : kShiftUp);
        queue_.push_back(kKeypadPrefix);
        queue_.push_back(uint8_t(k.raw | (down ? 0x00 : 0x80)));
        return;
    }
    // Main block: the raw code IS the virtual code doubled, bit 0 always 1,
    // bit 7 = release (tmk m0110.h, "KEY EVENT"). Codes past $3F belong to
    // no key this keyboard has — encoding them anyway would set bit 7 and
    // forge a release, so drop them instead.
    if (virtualKey > 0x3F) return;
    queue_.push_back(uint8_t((virtualKey << 1) | 1 | (down ? 0x00 : 0x80)));
}

uint8_t MacKeyboard::respond(uint8_t cmd) {
    switch (cmd) {
        case kCmdInquiry:
        case kCmdInstant:
            if (!queue_.empty()) { uint8_t r = queue_.front(); queue_.pop_front(); return r; }
            return kNull;
        case kCmdModel: return kModelM0110;
        case kCmdTest:  return kTestAck;
        default:        return kNull;
    }
}

// One quadrature step: toggle the interrupt line (X1/Y1) and place the
// phase line (X2/Y2) so the driver decodes the wanted direction. Polarity
// per DEV.md § Input (tuned against the System 6 driver).
int MacMouse::tick(int cpuCycles) {
    phase_ += cpuCycles;
    if (phase_ < kStepCycles) return 0;
    phase_ = 0;
    int stepped = 0;
    if (dx_ != 0) {
        bool dir = dx_ > 0;
        x1 = !x1;
        x2 = dir ? !x1 : x1;    // right = X2 opposite to X1 (System 6 driver)
        dx_ += dir ? -1 : 1;
        stepped |= 1;
    }
    if (dy_ != 0) {
        bool dir = dy_ > 0;             // positive = toward screen bottom
        y1 = !y1;
        y2 = dir ? y1 : !y1;
        dy_ += dir ? -1 : 1;
        stepped |= 2;
    }
    return stepped;
}
