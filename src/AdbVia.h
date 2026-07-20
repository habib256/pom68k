// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Mac II / SE ADB Modem HLE (PIC1654S 342S0440-B).
//
// Hardware (MAME adbmodem.cpp / macii.cpp):
//   PB4/PB5 = ST0/ST1 state to the PIC (NEW=0, EVEN=1, ODD=2, IDLE=3)
//   PB3     = ADB IRQ from PIC (active low)
//   CB1/CB2 = VIA shift clock/data driven by the PIC
//
// We HLE at byte level: state transitions + VIA SR + SHIFT IFR, no bit
// serialization. Command payload goes to AdbBus. Source: MAME macadb +
// lampmerchant/macseadb88 jump table.

#pragma once
#include "Via6522.h"
#include "AdbBus.h"
#include <cstdint>
#include <vector>

class AdbVia {
public:
    enum State : uint8_t { NEW = 0, EVEN = 1, ODD = 2, IDLE = 3 };

    void reset();
    void attach(Via6522& via, AdbBus& adb);

    // Call after every VIA1 ORB/DDRB/SR/ACR write and before ORB reads.
    void sync();

    void tick(int cpuCycles);
    bool irqPending() const { return irqPending_; }

    void keyEvent(uint8_t adbCode, bool down) { if (adb_) adb_->keyEvent(adbCode, down); }
    void mouseMove(int dx, int dy) { if (adb_) adb_->mouseMove(dx, dy); }
    void mouseButton(bool down) { if (adb_) adb_->mouseButton(down); }

private:
    void applyIrqToVia();
    void onState(State st);
    void takeHostByte();
    void pushDeviceByte();
    void finishCommand();

    Via6522* via_ = nullptr;
    AdbBus* adb_ = nullptr;
    State state_ = IDLE;
    State lastState_ = IDLE;

    std::vector<uint8_t> cmd_;     // host→device bytes this transaction
    std::vector<uint8_t> resp_;    // device→host bytes
    size_t respPos_ = 0;
    bool irqPending_ = false;
    bool expectingListen_ = false;
    int timer_ = 0;
    static constexpr int kByteDelay = 2000;   // ~128 µs @ 15.67 MHz
};
