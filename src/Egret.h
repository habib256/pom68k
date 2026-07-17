// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Egret MCU (68HC05EG, firmware 341S0850) — high-level emulation ──
// The LC II's ADB/RTC/PRAM/power controller, HLE'd instead of running
// the MCU firmware (CLAUDE.md: one concern per file, functional machine).
// Transport = VIA1 shift register in external-clock mode: Egret clocks
// CB1 and drives/reads CB2; handshake lines PB3 (XCVR_SESSION, Egret →
// host, ACTIVE LOW... asserted = 0), PB4 (VIA_FULL, host byte ack), PB5
// (SYS_SESSION, host command session). In HLE the CB1/CB2 bit clocking
// collapses into Via6522::loadSR/raiseShift per byte.
// Packets are Cuda-compatible: [type, cmd, data…], type 0 = ADB,
// 1 = pseudo (RTC/PRAM/power), 2 = error, 3 = timer. Pseudo commands
// per Linux cuda.h: AUTOPOLL=1, GET_TIME=3, GET_PRAM=7, SET_TIME=9,
// POWERDOWN=$A, SET_PRAM=$C, SEND_DFAC=$E, RESET_SYSTEM=$11; plus the
// Egret XPRAM block ops READ_XPRAM=2 / WRITE_XPRAM=8. XPRAM reads
// ($02 and $07) are host-terminated byte STREAMS — no length on the
// wire; the host drops SYS_SESSION after its count (O6.11, DEV.md
// § Egret XPRAM wire protocol).
// Sources: MAME maclc.cpp:418-433 (wiring), egret.cpp (reset hold,
// timings), Linux via-cuda.c:57-95 (handshake shape), MAME macadb.cpp
// (ADB HLE behaviour); pinned in docs/LCII_HARDWARE.md § Egret.
// The LC II ROM's own handshake code (traced at $A14CE0-$A14E9C) is the
// acceptance oracle. Gate: tests/egret_test.cpp.

#pragma once
#include "Via6522.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class AdbBus;

class Egret {
public:
    explicit Egret(Via6522& via);

    void reset();                        // power-on: holds the 68030
    bool cpuHeld() const { return held_; }

    // Host-side VIA1 port B outputs (V8Memory calls on ORB/DDRB writes)
    void portBChanged(uint8_t pb);
    // → VIA1 PB3 input: 1 = idle, 0 = Egret has data / is transmitting
    uint8_t xcvrSession() const { return xcvr_ ? 0 : 1; }

    void tick(int cpuCycles);            // delays + RTC seconds

    void setAdbBus(AdbBus* bus) { adb_ = bus; }
    void setSeconds(uint32_t s) { seconds_ = s; }
    uint32_t seconds() const { return seconds_; }
    uint8_t pram(int i) const { return pram_[i & 0xFF]; }
    void setPram(int i, uint8_t v) { pram_[i & 0xFF] = v; }

    // Battery-backed store: 256-byte PRAM + 4-byte clock. A cold (all
    // zero) PRAM makes the ROM run its LONG full-RAM burn-in on every
    // boot — persistence is what skips it, like a real battery.
    bool loadPram(const std::string& path);
    void savePram(const std::string& path) const;
    // No-op when the PRAM already carries the 'NuMc' signature (see .cpp)
    void factoryDefaults();

    // Debug hooks for lcii_trace: direction (true = host→Egret), byte;
    // onEdge fires on every host port-B change (pb, phase, xcvr)
    std::function<void(bool, uint8_t)> onByte;
    std::function<void(uint8_t, int, bool)> onEdge;
    // XPRAM/PRAM read hook (offset, count) — O6.11 LocalTalk-config trace
    std::function<void(int, int)> onXPramRead;

private:
    void hostByte(uint8_t b);            // latched on VIA_FULL rise
    void endCommand();                   // SYS_SESSION fall → process
    void process(const std::vector<uint8_t>& cmd);
    void queueResponse(std::vector<uint8_t> resp);
    void loadNextByte();

    Via6522& via_;
    AdbBus* adb_ = nullptr;

    enum Phase { IDLE, HOST_CMD, RESP_DELAY, RESP_SEND };
    Phase phase_ = IDLE;
    bool held_ = true;
    bool xcvr_ = false;                  // true = asserted (PB3 reads 0)
    uint8_t lastPb_ = 0;
    int delay_ = 0;                      // cycles until the next action
    size_t respIdx_ = 0;
    std::vector<uint8_t> cmd_, resp_;
    std::vector<std::vector<uint8_t>> pending_;  // Egret-initiated packets

    uint32_t seconds_ = 0;               // Mac epoch (1904)
    int64_t secAcc_ = 0;
    bool firstTick_ = true;              // boot packet is longer (see .cpp)
    int pollAcc_ = 0;                    // ADB autopoll cadence (~11 ms)
    uint8_t pram_[256] = {};
    bool autopoll_ = false;

    static constexpr int kByteDelay = 200;     // ≈13 µs at 15.67 MHz
    static constexpr int kResetHold = 100000;  // ≈6 ms power-on hold
    static constexpr int kQuietDelay = 100000; // gap between own packets
    int holdTimer_ = kResetHold;
    int quiet_ = 0;                            // XCVR must be seen deasserted
    bool initiated_ = false;                   // current resp is Egret-initiated
};
