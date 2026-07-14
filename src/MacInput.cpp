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
