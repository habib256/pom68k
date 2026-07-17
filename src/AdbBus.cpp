// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "AdbBus.h"
#include <algorithm>

void AdbBus::reset() {
    keyQueue_.clear();
    mdx_ = mdy_ = 0;
    mbtn_ = mbtnSent_ = false;
    kbdAddr_ = 2;
    mouseAddr_ = 3;
}

void AdbBus::keyEvent(uint8_t adbCode, bool down) {
    keyQueue_.push_back(uint8_t((down ? 0x00 : 0x80) | (adbCode & 0x7F)));
}

void AdbBus::mouseMove(int dx, int dy) {
    mdx_ = std::clamp(mdx_ + dx, -256, 256);
    mdy_ = std::clamp(mdy_ + dy, -256, 256);
}

void AdbBus::mouseButton(bool down) { mbtn_ = down; }

std::vector<uint8_t> AdbBus::command(uint8_t cmd, const std::vector<uint8_t>& data) {
    const uint8_t addr = cmd >> 4;
    const uint8_t op = (cmd >> 2) & 3;   // 0 = reset/flush, 2 = listen, 3 = talk
    const uint8_t reg = cmd & 3;

    if (op == 3 && addr == kbdAddr_) {   // keyboard talk
        if (reg == 0) {
            if (keyQueue_.empty()) return {};
            uint8_t k0 = keyQueue_.front(); keyQueue_.pop_front();
            uint8_t k1 = 0xFF;
            if (!keyQueue_.empty()) { k1 = keyQueue_.front(); keyQueue_.pop_front(); }
            return { k0, k1 };
        }
        if (reg == 3) return { uint8_t(0x60 | kbdAddr_), 0x02 };  // handler ID 2
        return {};
    }

    if (op == 3 && addr == mouseAddr_) { // mouse talk
        if (reg == 0) {
            if (!mousePending()) return {};
            auto clamp7 = [](int& v) {
                int d = std::clamp(v, -64, 63);
                v -= d;
                return uint8_t(d & 0x7F);
            };
            uint8_t dy = clamp7(mdy_), dx = clamp7(mdx_);
            mbtnSent_ = mbtn_;
            return { uint8_t((mbtn_ ? 0x00 : 0x80) | dy), uint8_t(0x80 | dx) };
        }
        if (reg == 3) return { uint8_t(0x60 | mouseAddr_), 0x01 };  // handler ID 1
        return {};
    }

    if (op == 2) {                       // listen: address moves (reg 3)
        if (reg == 3 && data.size() >= 2) {
            const uint8_t newAddr = data[0] & 0x0F;
            if (addr == kbdAddr_) kbdAddr_ = newAddr;
            else if (addr == mouseAddr_) mouseAddr_ = newAddr;
        }
        return {};
    }

    return {};                           // reset/flush/absent device
}
