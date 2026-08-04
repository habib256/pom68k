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
//   • LLE (default when roms/adbmodem/342s0440-b.bin is present): the real
//     PIC1654S firmware (Pic1654s) bit-bangs the VIA shifter and the
//     bit-serial ADB bus (AdbLine). Exact by construction — fixes the
//     dropped-command ADBReInit corruption; the mouse moves only on this
//     path. Source: MAME macadb + adbmodem; lampmerchant/macseadb88.
//   • HLE (fallback without the dump, or POM68K_ADB_LLE=0): byte-level state
//     machine + VIA SR + SHIFT IFR, command payload to AdbBus. Approximate —
//     drops fast ADBReInit traffic (mouse at phantom address, frozen).

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
    // cpuHz = the owning machine's clock; the PIC's own rate is fixed.
    void attach(Via6522& via, AdbBus& adb, int64_t cpuHz = 15667200);

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
    void mouseButton(bool down, int button = 0) {
        if (lle_) line_.mouseButton(down, button);
        else if (adb_ && button == 0) adb_->mouseButton(down);
    }

    // ── Save states (SaveState.h contract) ──────────────────────────────
    // Both paths travel: the HLE byte machine (cmd_/resp_/timer_) and the
    // LLE transceiver (the PIC core mid-instruction + the bit-serial ADB
    // line + the clock accumulators that phase it against the CPU). The
    // via_/adb_ pointers and lle_/cpuHz_ are attach()-time wiring.
    template <class Ar> void visit(Ar& ar) {
        ar(state_, lastState_, cmd_, resp_);
        std::uint64_t pos = respPos_;
        ar(pos);
        if constexpr (Ar::loading) respPos_ = static_cast<size_t>(pos);
        ar(irqPending_, expectingListen_, timer_);
        ar(pic_, line_, picAcc_, lastPicClock_);
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
    int64_t  picAcc_ = 0;                      // machine-cycle → PIC-cycle accumulator
    int64_t  lastPicClock_ = -1;               // absolute CPU clock the PIC has run to
    // The PIC1654S runs at a FIXED 460.8 kHz (3.6864 MHz / 8) whatever the
    // host machine's clock is, so the divisor must be derived per machine —
    // hardcoding 34 (correct only at the Mac II's 15.6672 MHz) overclocked the
    // transceiver 2.13x on a Quadra 650 and halved it on a Mac SE, stretching
    // or shrinking every ADB bit cell away from the 100 us spec.
    static constexpr int64_t kPicHz = 460800;
    // AdbLine's pulse thresholds are expressed in this unit (1 PIC cycle =
    // kPicTick); it is a property of the PIC domain, not of the host clock.
    static constexpr int kPicTick = 34;
    int64_t  cpuHz_ = 15667200;                 // set by attach()
};
