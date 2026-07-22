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
// Two implementations coexist:
//   • HLE (default): byte-level state machine + VIA SR + SHIFT IFR, command
//     payload to AdbBus. Approximate — drops fast ADBReInit traffic.
//   • LLE (opt-in, POM68K_ADB_LLE=1 + roms/adbmodem/342s0440-b.bin present):
//     the real PIC1654S firmware (Pic1654s) bit-bangs the VIA shifter and the
//     bit-serial ADB bus (AdbLine). Exact by construction — this is the fix
//     for the dropped-command ADBReInit corruption. Source: MAME macadb +
//     adbmodem; lampmerchant/macseadb88.

#pragma once
#include "Via6522.h"
#include "AdbBus.h"
#include "Pic1654s.h"
#include "AdbLine.h"
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
    // LLE: run the PIC forward to an absolute CPU-clock stamp. Called both by
    // the periodic peripheral batch and — crucially — at every VIA1 SR/ACR/ORB
    // access, so the PIC interleaves with the ROM's byte handshake at bit
    // granularity instead of a whole sequence per 64-cycle batch.
    void syncTo(int64_t cpuClock);
    bool irqPending() const { return irqPending_; }
    bool lle() const { return lle_; }

    void keyEvent(uint8_t adbCode, bool down) {
        if (lle_) line_.keyEvent(adbCode, down); else if (adb_) adb_->keyEvent(adbCode, down);
    }
    void mouseMove(int dx, int dy) {
        if (lle_) line_.mouseMove(dx, dy); else if (adb_) adb_->mouseMove(dx, dy);
    }
    void mouseButton(bool down) {
        if (lle_) line_.mouseButton(down); else if (adb_) adb_->mouseButton(down);
    }

private:
    void setupPicPorts();
    void tickLle(int cpuCycles);
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

    // ── LLE path ──
    bool lle_ = false;
    Pic1654s pic_;
    AdbLine  line_;
    int      picAcc_ = 0;                       // CPU-cycle → PIC-instruction accumulator
    int64_t  lastPicClock_ = -1;               // absolute CPU clock the PIC has run to
    static constexpr int kCyclesPerPicInsn = 34;   // 15.6672 MHz / (3.6864 MHz / 8)
};
