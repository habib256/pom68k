// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Bit-serial ADB devices (keyboard + mouse). Ported from MAME macadb.cpp
// (R. Belmont, BSD-3-Clause). Timing rebased to Mac II CPU cycles: one MAME
// 2 MHz ADB tick = 15667200/2000000 ≈ 7.8336 cycles.

#include "AdbLine.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

// Bit-cell / handshake durations, in CPU cycles, calibrated to the PIC1654S
// firmware's actual ADB line timing under cycle-exact co-stepping (1 PIC
// cycle = 34 CPU cyc, real 1/2/3-cycle instruction cost). Measured: bit "0"
// low 1020 + high 544 = bit cell 1564 cyc ≈ 99.8 µs — the ADB spec 100 µs —
// attention low 12410 ≈ 792 µs (spec ~800 µs). The PIC IS the LLE timing
// reference; thresholds sit at the mid-points of its short/long pulses.
static constexpr int64_t kShort  = 544;    // PIC short pulse (bit "1" low, 35 %)
static constexpr int64_t kLong   = 1020;   // PIC long pulse  (bit "0" low, 65 %)
static constexpr int64_t kSrq    = 2250;
static constexpr int64_t kT1t    = 1020;
static constexpr int64_t kStop   = 1125;
static constexpr int64_t T_BIT   = 782;    // "1" bit threshold (544↔1020 midpoint)
static constexpr int64_t T_SYNC  = 450;    // Tsync
static constexpr int64_t T_ATTEN = 6000;   // attention (between bit-cell 1564 and 12410)
static constexpr int64_t T_RESET = 30000;  // reset (between 12410 and ~62000)
static constexpr int64_t T_T1T   = 1800;

void AdbLine::reset() {
    hostDrive_ = deviceDrive_ = true;
    linestate_ = LST_IDLE;
    now_ = lastEdge_ = 0;
    sendTimer_ = -1;
    command_ = 0; waitingCmd_ = false; direction_ = 0;
    datasize_ = 0; streamPtr_ = 0; srqFlag_ = srqSwitch_ = false;
    kbdAddr_ = 2; kbdHandler_ = 0x22;
    mouseAddr_ = 3; mouseHandler_ = 0x23;
    keyBuf_.clear();
    mdx_ = mdy_ = 0; mbtn_ = mbtnSent_ = false;
    lastMouse_[0] = lastMouse_[1] = 0x80;   // idle mouse report (no motion, button up)
    lastKbd_[0] = lastKbd_[1] = 0xFF;       // idle keyboard report (no key)
}

void AdbLine::keyEvent(uint8_t adbCode, bool down) {
    keyBuf_.push_back(uint8_t((down ? 0x00 : 0x80) | (adbCode & 0x7F)));
}
void AdbLine::mouseMove(int dx, int dy) {
    mdx_ = std::clamp(mdx_ + dx, -256, 256);
    mdy_ = std::clamp(mdy_ + dy, -256, 256);
}
void AdbLine::mouseButton(bool down) { mbtn_ = down; }

bool AdbLine::mousePending() const { return mdx_ || mdy_ || mbtn_ != mbtnSent_; }

void AdbLine::writeData(bool level) {
    if (deviceDrive_ == level) return;
    deviceDrive_ = level;
    // Device-driven edge: advance the edge clock (host isn't decoding now).
    lastEdge_ = now_;
}

void AdbLine::setHostDrive(bool high) {
    bool old = line();
    hostDrive_ = high;
    bool nw = line();
    if (nw == old) return;
    int64_t dtime = now_ - lastEdge_;
    lastEdge_ = now_;
    // Diagnostic tracer: POM68K_ADB_LLE_TRACE=1 dumps every host edge
    // (new level + previous-level duration) — used to calibrate the constants.
    static const bool trace = std::getenv("POM68K_ADB_LLE_TRACE") != nullptr;
    if (trace)
        std::fprintf(stderr, "adbline: %s after %lld (state %d)\n",
                     nw ? "rise" : "fall", (long long)dtime, linestate_);
    receiveEdge(nw, dtime);
}

void AdbLine::tick(int cyc) {
    now_ += cyc;
    if (sendTimer_ < 0) return;
    sendTimer_ -= cyc;
    while (sendTimer_ <= 0 && sendTimer_ >= -(1 << 30)) {
        int64_t carry = sendTimer_;
        sendTimer_ = -1;
        timerTick();
        if (sendTimer_ < 0) break;      // machine disabled itself
        sendTimer_ += carry;            // fold overshoot into the next interval
    }
}

// ---- receive: host drives the line, we decode command / listen bytes ----
void AdbLine::receiveEdge(bool level, int64_t dtime) {
    // Mid-listen: an even byte can continue if another bit arrives quickly.
    if (direction_ && linestate_ == LST_TSTOP) {
        if (streamPtr_ & 1) linestate_ = LST_BIT0;
        else if (dtime < T_SYNC) linestate_ = LST_BIT0;
    }

    switch (linestate_) {
    case LST_IDLE:
        if (level && dtime >= T_RESET) {          // reset pulse (host held low long)
            kbdAddr_ = 2; mouseAddr_ = 3;
        } else if (level && dtime >= T_ATTEN) {   // attention
            waitingCmd_ = true; direction_ = 0;
            linestate_ = LST_ATTENTION;
        }
        break;

    case LST_ATTENTION:
        if (!level && dtime >= T_SYNC) { command_ = 0; linestate_ = LST_BIT0; }
        break;

    case LST_BIT0: case LST_BIT1: case LST_BIT2: case LST_BIT3:
    case LST_BIT4: case LST_BIT5: case LST_BIT6: case LST_BIT7:
        if (!level) {
            if (dtime >= T_BIT) command_ |= 1;
            if (linestate_ != LST_BIT7) command_ <<= 1;
            else if (direction_) { buffer_[streamPtr_++] = command_; command_ = 0; }
            linestate_++;
        }
        break;

    case LST_TSTOP:
        if (level) {                              // stop bit — process the command
            if (direction_) command_ = buffer_[0];
            srqFlag_ = false;
            adbTalk();
            if (srqFlag_) {
                writeData(false);                 // hold line low for SRQ
                linestate_ = LST_SRQNODATA;
                armTimer(kSrq);
            } else {
                writeData(true);
                if (datasize_ > 0) {
                    linestate_ = LST_TSTOPSTART;  // T1t before responding
                    armTimer(kT1t);
                    streamPtr_ = 0;
                } else if (direction_) {
                    linestate_ = LST_WAITT1T;     // valid Listen, no reply
                } else {
                    linestate_ = LST_IDLE;        // no device / timeout
                }
            }
        }
        break;

    case LST_WAITT1T:
        if (!level && dtime >= T_T1T) linestate_ = LST_RCVSTARTBIT;
        break;

    case LST_RCVSTARTBIT:
        if (!level && dtime >= T_SYNC) { linestate_ = LST_BIT0; command_ = 0; }
        break;
    }
}

// ---- send: the device drives the line to transmit its data bits ----
void AdbLine::timerTick() {
    switch (linestate_) {
    case LST_SRQNODATA:
        writeData(true); linestate_ = LST_IDLE; break;

    case LST_TSTOPSTART:
        writeData(true);  armTimer(kShort); linestate_++; break;
    case LST_TSTOPSTARTa:
        writeData(false); armTimer(kShort); linestate_++; break;
    case LST_STARTBIT:
        writeData(true);  armTimer(kLong);  linestate_++; break;

    case LST_SENDBIT0: case LST_SENDBIT1: case LST_SENDBIT2: case LST_SENDBIT3:
    case LST_SENDBIT4: case LST_SENDBIT5: case LST_SENDBIT6: case LST_SENDBIT7:
        writeData(false);
        armTimer((buffer_[streamPtr_] & 0x80) ? kShort : kLong);
        linestate_++;
        break;

    case LST_SENDBIT0a: case LST_SENDBIT1a: case LST_SENDBIT2a: case LST_SENDBIT3a:
    case LST_SENDBIT4a: case LST_SENDBIT5a: case LST_SENDBIT6a:
        writeData(true);
        armTimer((buffer_[streamPtr_] & 0x80) ? kLong : kShort);
        buffer_[streamPtr_] <<= 1;
        linestate_++;
        break;

    case LST_SENDBIT7a:
        writeData(true);
        armTimer((buffer_[streamPtr_] & 0x80) ? kLong : kShort);
        streamPtr_++;
        linestate_ = (streamPtr_ == datasize_) ? LST_SENDSTOP : LST_SENDBIT0;
        break;

    case LST_SENDSTOP:
        writeData(false); armTimer(kStop); linestate_++; break;
    case LST_SENDSTOPa:
        writeData(true); sendTimer_ = -1; linestate_ = LST_IDLE; break;
    }
}

// ---- command decode (MAME macadb adb_talk) ----
void AdbLine::adbTalk() {
    const uint8_t addr = command_ >> 4;
    const uint8_t op = (command_ >> 2) & 3;   // 0/1 reset-flush, 2 listen, 3 talk
    const uint8_t reg = command_ & 3;
    static const bool trace = std::getenv("POM68K_ADB_LLE_TRACE") != nullptr;
    if (trace)
        std::fprintf(stderr,
                     "adbtalk: %s cmd=%02X (addr=%d op=%d reg=%d) kbd@%d mouse@%d "
                     "buf=%02X %02X now=%lld\n",
                     waitingCmd_ ? "cmd" : "listen-data", command_, addr, op, reg,
                     kbdAddr_, mouseAddr_, buffer_[0], buffer_[1], (long long)now_);

    if (waitingCmd_) {
        datasize_ = 0;
        switch (op) {
        case 0:
        case 1:                               // reset / flush
            direction_ = 0;
            if (command_ == 0) { kbdAddr_ = 2; mouseAddr_ = 3; }   // SendReset
            break;

        case 2:                               // listen — data bytes follow
            if (addr == kbdAddr_ || addr == mouseAddr_) {
                direction_ = 1; command_ = 0;
                listenReg_ = reg; listenAddr_ = addr;
                streamPtr_ = 0;
                for (auto& b : buffer_) b = 0;
            } else {
                direction_ = 0;               // unknown device → timeout
            }
            break;

        case 3:                               // talk
            direction_ = 0;
            if (addr == mouseAddr_) {
                if (reg == 0) {
                    uint8_t dy = 0, dx = 0;
                    if (srqSwitch_) { srqSwitch_ = false; }
                    else {
                        auto clamp7 = [](int& v) {
                            int d = std::clamp(v, -64, 63); v -= d; return uint8_t(d & 0x7F);
                        };
                        dy = clamp7(mdy_); dx = clamp7(mdx_);
                    }
                    buffer_[0] = uint8_t((mbtn_ ? 0x00 : 0x80) | dy);
                    buffer_[1] = uint8_t(0x80 | dx);
                    mbtnSent_ = mbtn_;
                    if (buffer_[0] != lastMouse_[0] || buffer_[1] != lastMouse_[1]) {
                        datasize_ = 2; lastMouse_[0] = buffer_[0]; lastMouse_[1] = buffer_[1];
                    } else if (keyPending() && (kbdHandler_ & 0x20)) srqFlag_ = true;
                } else if (reg == 3) {
                    buffer_[0] = mouseHandler_; buffer_[1] = 0x01; datasize_ = 2;
                }
            } else if (addr == kbdAddr_) {
                if (reg == 0) {
                    if (keyBuf_.empty()) { buffer_[0] = buffer_[1] = 0xFF; }
                    else {
                        buffer_[1] = keyBuf_.front(); keyBuf_.pop_front();
                        if (!keyBuf_.empty()) { buffer_[0] = keyBuf_.front(); keyBuf_.pop_front(); }
                        else buffer_[0] = 0xFF;
                    }
                    if (buffer_[0] != lastKbd_[0] || buffer_[1] != lastKbd_[1]) {
                        datasize_ = 2; lastKbd_[0] = buffer_[0]; lastKbd_[1] = buffer_[1];
                    } else if (mousePending() && (mouseHandler_ & 0x20)) srqFlag_ = true;
                } else if (reg == 2) {
                    buffer_[0] = 0xFF; buffer_[1] = 0xFF; datasize_ = 2;   // modifiers (none held)
                } else if (reg == 3) {
                    buffer_[0] = uint8_t(0x60 | kbdAddr_); buffer_[1] = kbdHandler_; datasize_ = 2;
                }
            }
            break;
        }
        waitingCmd_ = false;
        return;
    }

    // Listen data phase: command_ = buffer_[0] (first data byte), buffer_[1]
    // = second (the activator). Register 3 relocates the device.
    direction_ = 0;
    if (listenReg_ == 3) {
        if (listenAddr_ == mouseAddr_) {
            if (buffer_[1] == 0x00) { mouseHandler_ = uint8_t(command_ & 0x7F); mouseAddr_ = command_ & 0x0F; }
            else if (buffer_[1] == 0xFE) { mouseAddr_ = command_ & 0x0F;
                                           mouseHandler_ = uint8_t((mouseHandler_ & 0xF0) | mouseAddr_); }
        } else if (listenAddr_ == kbdAddr_) {
            if (buffer_[1] == 0x00) { kbdHandler_ = uint8_t(command_ & 0x7F); kbdAddr_ = command_ & 0x0F; }
            else if (buffer_[1] == 0xFE) { kbdAddr_ = command_ & 0x0F;
                                           kbdHandler_ = uint8_t((kbdHandler_ & 0xF0) | kbdAddr_); }
        }
    }
}
