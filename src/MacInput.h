// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Input: M0110 keyboard + quadrature mouse ──
// Keyboard: the M0110 talks over the VIA shift register (CB1 clock from
// the keyboard, CB2 data). The Mac writes a command byte (Inquiry $10,
// Instant $14, Model $16, Test $36) into the SR; the keyboard answers with
// a transition code (bit 7 = key-up), $7B = null. We model the transaction
// with a response delay so the SR interrupt (IFR bit 2) paces correctly.
// Mouse: quadrature X1/Y1 → SCC DCD A/B, X2/Y2 → VIA PB4/PB5, button →
// VIA PB3 (0 = down). Steps are emitted at a bounded rate from the host
// deltas; direction polarity pinned by DEV.md § Input.
// Gate: tests/input_etalon.cpp.

#pragma once
#include <cstdint>
#include <deque>

class MacKeyboard {
public:
    void reset() { queue_.clear(); }
    // Host key event → M0110 transition code (already encoded by caller)
    void enqueue(uint8_t transitionCode) { queue_.push_back(transitionCode); }

    // Execute a command byte, return the response (transaction pacing —
    // the two SR interrupts, 3 ms apart — is MacMemory's job).
    uint8_t respond(uint8_t cmd);

private:
    std::deque<uint8_t> queue_;
};

class MacMouse {
public:
    void move(int dx, int dy) { dx_ += dx; dy_ += dy; }
    void setButton(bool down) { button_ = down; }
    bool button() const { return button_; }

    // Emit at most one quadrature step per axis every kStepCycles.
    // Returns bitmask: 1 = X stepped, 2 = Y stepped.
    int tick(int cpuCycles);

    bool x1 = false, y1 = false;     // → SCC DCD A / B
    bool x2 = false, y2 = false;     // → VIA PB4 / PB5

private:
    static constexpr int kStepCycles = 4000;   // ~2 kHz max step rate
    int dx_ = 0, dy_ = 0;
    int phase_ = 0;
    bool button_ = false;
};
