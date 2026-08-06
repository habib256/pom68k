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

    if ((cmd & 0x0F) == 0) {             // ADB reset
        kbdAddr_ = 2;
        mouseAddr_ = 3;
        return {};
    }

    if (op == 3 && addr == kbdAddr_) {   // keyboard talk
        if (reg == 0) {
            // MAME-parity audit §2.9 (cosmetic, DOCUMENT-SKIP 2026-08-06):
            // this HLE path puts the OLDEST queued event in byte 0. The LLE
            // path does the opposite — AdbLine::adbTalk (AdbLine.cpp:358-375)
            // reproduces MAME's buffer[1]-first quirk verbatim
            // (macadb.cpp:660-672: m_buffer[1] = keybuf[start], then
            // m_buffer[0] = the next one, and buffer[0] goes out first). The
            // two paths are therefore inconsistent with each other and the
            // HLE one is the non-MAME order.
            // NOT aligned, deliberately. The order is only distinguishable
            // when two events are queued in the same poll, both are accepted
            // by the ADB Manager, and — decisively — NO CTest gate reaches
            // this code: roms/adbmodem/342s0440-b.bin is present, so AdbVia
            // runs LLE by default (AdbVia::attach) and Egret/Cuda run LLE by
            // default too, which leaves AdbBus's keyboard branch on the
            // stderr-announced NON-CONFORMANT fallback only
            // (docs/LLE_VS_HLE.md §2). Swapping it would be an unvalidatable
            // behaviour change on a live input path for zero measurable
            // parity gain.
            // Reopening condition: if this HLE model ever gets its own gate,
            // or a dump-less configuration becomes supported, swap to
            // { k1, k0 } and re-run family_input_etalon + input_etalon.
            if (keyQueue_.empty()) return { 0xFF, 0xFF };
            uint8_t k0 = keyQueue_.front(); keyQueue_.pop_front();
            uint8_t k1 = 0xFF;
            if (!keyQueue_.empty()) { k1 = keyQueue_.front(); keyQueue_.pop_front(); }
            return { k0, k1 };
        }
        if (reg == 2) return { 0xFF, 0xFF };
        if (reg == 3) return { uint8_t(0x60 | kbdAddr_), 0x05 };  // handler ID 5
        return {};
    }

    if (op == 3 && addr == mouseAddr_) { // mouse talk
        if (reg == 0) {
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
        // The ACTIVATOR (data[1]) decides, exactly as in macadb.cpp:735-777
        // and our own AdbLine: $00 = set handler AND address, $FE =
        // unconditional address change. Any other value (a plain handler
        // write, say) must leave the address alone. Moving the device on
        // every Listen R3 makes ADBReInit's relocation dance never
        // converge — the Duo's third ADBReInit hangs on exactly that.
        if (reg == 3 && data.size() >= 2 &&
            (data[1] == 0x00 || data[1] == 0xFE)) {
            const uint8_t newAddr = data[0] & 0x0F;
            if (addr == kbdAddr_) kbdAddr_ = newAddr;
            else if (addr == mouseAddr_) mouseAddr_ = newAddr;
        }
        return {};
    }

    return {};                           // reset/flush/absent device
}
