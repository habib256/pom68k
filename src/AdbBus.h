// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── ADB bus + HLE keyboard/mouse ──
// Command-level ADB (no bit serialization — Egret is already HLE):
// command byte = addr<<4 | op (0=SendReset, 1=Flush, %10rr=Listen r,
// %11rr=Talk r). Standard devices: keyboard at $2 (handler 2), mouse at
// $3 (handler 1). Talk reg 0 returns queued events; Talk on an address
// with nothing pending returns empty (ADB timeout). srqPending() lets
// Egret raise a service request during autopoll.
// Keyboard reg 0: two key bytes, bit 7 = release, $FF = none. Mouse
// reg 0: [b7 = button up, y6..0][b7 = 1, x6..0], 7-bit two's-complement
// deltas. Source: MAME macadb.cpp behaviour; Inside Macintosh: Devices.
// Events ride the existing MacInput conventions (Mac Plus phase).
// Gate: tests/egret_test.cpp (talk reg 0 + SRQ under autopoll).

#pragma once
#include <cstdint>
#include <deque>
#include <vector>

class AdbBus {
public:
    void reset();

    // Egret-level command: returns the response payload (empty = timeout)
    std::vector<uint8_t> command(uint8_t cmd, const std::vector<uint8_t>& data);

    bool srqPending() const { return !keyQueue_.empty() || mousePending(); }
    uint8_t keyboardAddr() const { return kbdAddr_; }
    uint8_t mouseAddr() const { return mouseAddr_; }

    // Host-side input events (UI thread → machine, MacInput conventions)
    void keyEvent(uint8_t adbCode, bool down);
    void mouseMove(int dx, int dy);
    void mouseButton(bool down);

private:
    bool mousePending() const { return mdx_ || mdy_ || mbtn_ != mbtnSent_; }

    std::deque<uint8_t> keyQueue_;       // raw ADB key transitions
    int mdx_ = 0, mdy_ = 0;
    bool mbtn_ = false, mbtnSent_ = false;
    uint8_t kbdAddr_ = 2, mouseAddr_ = 3;
};
