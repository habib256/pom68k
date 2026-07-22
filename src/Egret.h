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
//
// Packet framing (LLE_VS_HLE §1.6b redo, 2026-07-22) follows the real
// Cuda wire per DingusPPC viacuda.cpp (design oracle) + MAME cuda.cpp
// (real 6805 firmware, timing oracle):
//   reply  = [type, flags, cmdEcho, data…]   (response_header, :498)
//   error  = [$02, errCode, pktType, cmd]    (error_response, :509)
// with the ATTENTION byte a separate wire event (a dummy SHIFT with a
// stale SR), NOT part of the packet buffer — that is what lets the ROM
// device-manager ISR count a 4-byte header (attention + 3) while the
// System-side reader counts 3, both correct. Cuda-flavor per-byte
// pacing = DingusPPC's real schedule: session close-ack +61 µs, host
// command byte ack +71 µs, response byte +88 µs, TREQ re-assert +13 µs,
// initiated-packet attention +30 µs. The Egret flavor keeps its pinned
// LC II wire (byte 0 of the buffer doubles as the attention byte,
// kByteDelay pacing) with the real-framed header behind it.
// Commands $02/$08 are READ/WRITE_MCU_MEM with a 16-bit MCU address:
// PRAM lives at $0100-$01FF, $0000-$00FF is MCU scratch RAM (the
// System's parameter block at $B3 round-trips through it). PRAM/MCU
// reads are genuinely open-ended streams — no length on the wire, the
// host terminates the session after its count. One-second packets obey
// pseudo command $1B (mode 0 = off, 1 = full, 2 = header, 3 = single
// tick byte; first packet after a mode change is always full — ERS).
//
// Sources: MAME maclc.cpp:418-433 (wiring), egret.cpp (reset hold),
// DingusPPC viacuda.cpp + zdocs/developers/viacuda.md (framing/pacing),
// Linux via-cuda.c:57-95 (handshake shape); pinned in
// docs/LCII_HARDWARE.md § Egret. The LC II ROM's own handshake code
// (traced at $A14CE0-$A14E9C) is the acceptance oracle for the Egret
// flavor; the Quadra ROM readers ($408A9BBE ISR, $408B3Bxx pollers) for
// the Cuda flavor. Gate: tests/egret_test.cpp.

#pragma once
#include "Via6522.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class AdbBus;

class Egret {
public:
    // cudaPolarity: the Cuda's TIP (PB5) and BYTEACK (PB4) are ACTIVE
    // LOW (Linux via-cuda.c) where the Egret's SYS_SESSION/VIA_FULL are
    // active high — the wire protocol is otherwise identical, so the
    // Cuda HLE is this Egret with the PB4/PB5 sense inverted (Q5).
    // clockHz: machine cycles per second as seen by tick() — 15.6672 MHz
    // on the LC II, 25 MHz on the Quadra 605 (µs pacing + RTC seconds).
    explicit Egret(Via6522& via, bool cudaPolarity = false,
                   int clockHz = 15667200);

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
    // XPRAM/PRAM write hook (offset, value) — Q6.4 restart-loop trace
    std::function<void(int, uint8_t)> onXPramWrite;

private:
    enum StreamSrc { NO_STREAM, STREAM_PRAM, STREAM_MCU, STREAM_ZERO };

    void hostByte(uint8_t b);            // latched on VIA_FULL rise
    void endCommand();                   // SYS_SESSION fall → process
    void process(const std::vector<uint8_t>& cmd);
    // Real reply framing [type, flags, cmdEcho]; the Egret flavor
    // prepends its on-buffer attention byte ($01).
    std::vector<uint8_t> replyHeader(uint8_t type, uint8_t flags,
                                     uint8_t echo) const;
    void queueResponse(std::vector<uint8_t> resp,
                       StreamSrc stream = NO_STREAM, uint16_t addr = 0);
    void queueError(uint8_t err, uint8_t pktType, uint8_t cmd);
    void loadNextByte();
    void releaseWire();                  // response done → idle bus
    int usToCycles(int us) const {
        return int(int64_t(us) * clockHz_ / 1000000);
    }

    Via6522& via_;
    AdbBus* adb_ = nullptr;
    bool cudaPolarity_ = false;
    int clockHz_;

    // RESP_DELAY = egret "thinking" pause before asserting XCVR (Cuda:
    // the +13 µs TREQ delay); RESP_WAIT = Cuda TREQ asserted, waiting
    // for the host to open the read session (TIP fall).
    enum Phase { IDLE, HOST_CMD, RESP_DELAY, RESP_WAIT, RESP_SEND };
    Phase phase_ = IDLE;
    bool held_ = true;
    bool xcvr_ = false;                  // true = asserted (PB3 reads 0)
    uint8_t lastPb_ = 0;
    int delay_ = 0;                      // cycles until next byte action
    size_t respIdx_ = 0;
    std::vector<uint8_t> cmd_, resp_;
    std::vector<std::vector<uint8_t>> pending_;  // Egret-initiated packets
    StreamSrc streamSrc_ = NO_STREAM;    // open-ended reply continuation
    uint16_t streamAddr_ = 0;

    uint32_t seconds_ = 0;               // Mac epoch (1904)
    int64_t secAcc_ = 0;
    int pollAcc_ = 0;                    // ADB autopoll cadence
    uint8_t pram_[256] = {};
    uint8_t mcuRam_[256] = {};           // 68HC05 scratch RAM ($0000-$00FF)
    bool autopoll_ = false;
    uint8_t pollRate_ = 11;              // ms, pseudo cmd $14/$16
    uint8_t deviceMap_[2] = { 0, 0 };    // pseudo cmd $19/$1A
    // One-second packets, pseudo cmd $1B: 0 = off, 1 = full, 2 = header
    // only, 3 = single tick byte. Power-on default = 1 (boot heartbeat;
    // the LC II ROM turns it off with $1B 00 in its first commands).
    uint8_t oneSecMode_ = 1;
    bool oneSecFirst_ = true;            // next packet forced full (ERS)

    // Egret-flavor wire pacing (pinned against the LC II ROM driver).
    static constexpr int kByteDelay = 200;     // ≈13 µs at 15.67 MHz
    static constexpr int kResetHold = 100000;  // ≈6 ms power-on hold
    static constexpr int kQuietDelay = 100000; // gap between own packets
    // Cuda-flavor real per-byte schedule (µs, DingusPPC viacuda.cpp)
    static constexpr int kCloseAckUs = 61;     // dummy SHIFT after TIP rise
    static constexpr int kCmdAckUs = 71;       // host command byte ack
    static constexpr int kRespByteUs = 88;     // response byte
    static constexpr int kTreqUs = 13;         // TREQ assert after close
    static constexpr int kAttnUs = 30;         // initiated-packet attention
    int holdTimer_ = kResetHold;
    int quiet_ = 0;
    int syncDelay_ = 0;                  // Q5 startup sync: delayed 2nd byte
    int ackDelay_ = 0;                   // pending stale-SR SHIFT (close ack
                                         // / attention / cmd byte ack)
    int treqDelay_ = 0;                  // pending TREQ assert (Cuda)
};
