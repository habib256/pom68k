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

// Power-on device state. Reached three ways: emulator reset, the ADB
// SendReset command ($00) and the line-reset pulse — all of which return a
// real device to its default address AND its default handler/protocol.
// (MAME restores only the addresses, `macadb.cpp:742`; the ADB spec is
// explicit that reset re-selects the default handler, and that matters the
// moment a handler is switchable at all.)
void AdbLine::resetDevices() {
    kbdAddr_ = 2; kbdHandler_ = 0x22; modifiers_ = 0xFF; kbdLeds_ = 0x07;
    mouseAddr_ = 3; mouseHandler_ = 0x23;
    static const uint8_t kbdId = [] {
        const char* e = std::getenv("POM68K_ADB_KBD_ID");
        const int id = e ? std::atoi(e) : 1;
        return uint8_t(id >= 1 && id <= 3 ? id : 1);
    }();
    kbdHandlerId_ = kbdId; mouseHandlerId_ = 1;
}

void AdbLine::reset() {
    hostDrive_ = deviceDrive_ = true;
    linestate_ = LST_IDLE;
    now_ = lastEdge_ = 0;
    sendTimer_ = -1;
    command_ = 0; waitingCmd_ = false; direction_ = 0;
    datasize_ = 0; streamPtr_ = 0; srqFlag_ = srqSwitch_ = false;
    resetDevices();
    keyBuf_.clear();
    mdx_ = mdy_ = 0; mbtn_ = mbtnSent_ = false; mbtn2_ = mbtn2Sent_ = false;
}

void AdbLine::keyEvent(uint8_t adbCode, bool down) {
    // Only the extended protocol (handler 3) reports the right-hand
    // modifiers under their own key codes; every other handler folds them
    // onto the left-hand ones, because the keyboard it is pretending to be
    // physically has one of each (DingusPPC `adbkeyboard.cpp:81-87`).
    uint8_t folded = adbCode & 0x7F;
    switch (folded) {
        case 0x7D: folded = 0x36; break;           // right Control → Control
        case 0x7B: folded = 0x38; break;           // right Shift   → Shift
        case 0x7C: folded = 0x3A; break;           // right Option  → Option
        default: break;
    }
    keyBuf_.push_back(uint8_t((down ? 0x00 : 0x80) |
                              (kbdHandlerId_ == 3 ? (adbCode & 0x7F) : folded)));
    // Modifiers also live in register 2 as a bitmap the guest can poll
    // independently of the key stream (MAME macadb.cpp:355-415 tracks the
    // same five). Active low: clear on press, set on release. The bitmap
    // has ONE bit per modifier whatever the handler, so it is always the
    // folded code that drives it — right Shift lights the Shift bit.
    uint8_t bit = 0;
    switch (folded) {
        case 0x39: bit = 0x20; break;      // Caps Lock
        case 0x36: bit = 0x08; break;      // Control
        case 0x38: bit = 0x04; break;      // Shift
        case 0x3A: bit = 0x02; break;      // Option
        case 0x37: bit = 0x01; break;      // Command
        default: return;
    }
    if (down) modifiers_ = uint8_t(modifiers_ & ~bit);
    else      modifiers_ = uint8_t(modifiers_ | bit);
}
void AdbLine::mouseMove(int dx, int dy) {
    mdx_ = std::clamp(mdx_ + dx, -256, 256);
    mdy_ = std::clamp(mdy_ + dy, -256, 256);
}
void AdbLine::mouseButton(bool down, int button) {
    if (button == 0) mbtn_ = down;
    else if (button == 1) mbtn2_ = down;
}

// A button-2 change is only *reportable* under the Extended Mouse Protocol,
// so it only counts as pending there; otherwise a host that clicked the
// right button on a one-button mouse would leave a change the device can
// never clear, and every autopoll would answer with an empty report.
bool AdbLine::mousePending() const {
    return mdx_ || mdy_ || mbtn_ != mbtnSent_ ||
           (mouseHandlerId_ == 4 && mbtn2_ != mbtn2Sent_);
}

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
        // dtime here is the PREVIOUS bit's LOW duration, which the PIC only
        // ever emits as kShort(544) or kLong(1020) — so a T_SYNC(450)
        // threshold was unsatisfiable and a Listen was hard-capped at two data
        // bytes, with byte 3+ re-decoded as a bogus command. MAME's identical
        // test sits at the short/long midpoint, which here is T_BIT.
        else if (dtime < T_BIT) linestate_ = LST_BIT0;
    }

    switch (linestate_) {
    case LST_IDLE:
        if (level && dtime >= T_RESET) {          // reset pulse (host held low long)
            resetDevices();
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
            else if (direction_) {          // buffer_ is 8 bytes, unbounded
                if (streamPtr_ < int(sizeof buffer_)) buffer_[streamPtr_++] = command_;
                command_ = 0;
            }
            linestate_++;
        }
        break;

    case LST_TSTOP:
        if (level) {                              // stop bit — process the command
            if (direction_) command_ = buffer_[0];
            srqFlag_ = false;
            adbTalk();
            if (srqFlag_) {
                if (std::getenv("POM68K_ADB_LLE_TRACE"))
                    std::fprintf(stderr, "adbline: SRQ presented (cmd=%02X)\n",
                                 command_);
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
            if (command_ == 0) resetDevices();                     // SendReset
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
                    // A real mouse answers Talk R0 whenever it has data
                    // PENDING (accumulated motion / button change) — even
                    // if the report bytes equal the previous one. MAME
                    // macadb's byte-compare dedup eats byte-identical
                    // consecutive reports; under the Egret firmware's
                    // fast autopoll every steady-drag report is
                    // identical, which starved the LC II mouse to ~0.5%
                    // delivery (2026-07-23). The pending test is MAME's
                    // own adb_pollmouse() "did it change" principle
                    // applied to our delta model.
                    uint8_t dy = 0, dx = 0;
                    bool pending = false;
                    if (srqSwitch_) {
                        // The SRQ-initiated poll is answered (empty
                        // report) — the requesting flow needs the reply
                        // to complete, not a timeout.
                        srqSwitch_ = false;
                        pending = true;
                    } else {
                        pending = mdx_ || mdy_ || (mbtn_ != mbtnSent_);
                        auto clamp7 = [](int& v) {
                            int d = std::clamp(v, -64, 63); v -= d; return uint8_t(d & 0x7F);
                        };
                        dy = clamp7(mdy_); dx = clamp7(mdx_);
                    }
                    // Both buttons are ACTIVE LOW. Bit 7 of the second byte
                    // is a constant 1 on a one-button mouse; under the
                    // Extended Mouse Protocol (handler 4) it carries button
                    // 2, which is the whole of two-button support on this
                    // bus (DingusPPC `adbmouse.cpp:70-118` packs exactly
                    // this for num_buttons=2, num_bits=7 — a 2-byte report).
                    const bool b2 = mouseHandlerId_ == 4 && mbtn2_;
                    buffer_[0] = uint8_t((mbtn_ ? 0x00 : 0x80) | dy);
                    buffer_[1] = uint8_t((b2 ? 0x00 : 0x80) | dx);
                    mbtnSent_ = mbtn_; mbtn2Sent_ = mbtn2_;
                    if (pending) {
                        datasize_ = 2;
                        if (trace)
                            std::fprintf(stderr, "adbline: mouse report %02X %02X\n",
                                         buffer_[0], buffer_[1]);
                    }
                    else if (keyPending() && (kbdHandler_ & 0x20)) srqFlag_ = true;
                } else if (reg == 1 && mouseHandlerId_ == 4) {
                    // Register 1 exists only under the extended protocol: the
                    // device identifier block a driver reads to size the
                    // report — 'appl', resolution in dpi, device class,
                    // button count (`adbmouse.cpp:127-140`).
                    buffer_[0] = 'a'; buffer_[1] = 'p';
                    buffer_[2] = 'p'; buffer_[3] = 'l';
                    buffer_[4] = 0x01; buffer_[5] = 0x2C;   // 300 dpi
                    buffer_[6] = 0x01;                      // class: mouse
                    buffer_[7] = 0x02;                      // buttons
                    datasize_ = 8;
                } else if (reg == 3) {
                    buffer_[0] = mouseHandler_; buffer_[1] = mouseHandlerId_;
                    datasize_ = 2;
                }
            } else if (addr == kbdAddr_) {
                if (reg == 0) {
                    // Same pending rule as the mouse: an event popped
                    // from the queue is always reported (byte-identical
                    // consecutive keystroke pairs are legitimate).
                    const bool pending = !keyBuf_.empty();
                    if (keyBuf_.empty()) { buffer_[0] = buffer_[1] = 0xFF; }
                    else {
                        buffer_[1] = keyBuf_.front(); keyBuf_.pop_front();
                        if (!keyBuf_.empty()) { buffer_[0] = keyBuf_.front(); keyBuf_.pop_front(); }
                        else buffer_[0] = 0xFF;
                    }
                    if (pending) {
                        datasize_ = 2;
                        if (trace)
                            std::fprintf(stderr,
                                         "adbline: kbd report %02X %02X (queue %zu)\n",
                                         buffer_[0], buffer_[1], keyBuf_.size());
                    }
                    else if (mousePending() && (mouseHandler_ & 0x20)) srqFlag_ = true;
                } else if (reg == 2) {
                    // Byte 0 = the modifier bitmap; byte 1's low three bits
                    // are the LED latches the guest wrote with Listen R2,
                    // the rest being released keys — so an untouched R2
                    // still reads $FF, MAME's constant (`macadb.cpp:696`).
                    buffer_[0] = modifiers_;
                    buffer_[1] = uint8_t(0xF8 | (kbdLeds_ & 0x07));
                    datasize_ = 2;
                } else if (reg == 3) {
                    // Byte 0 is the R3 flags byte, byte 1 the HANDLER ID —
                    // kbdHandler_ carries R3-byte-0 semantics everywhere else
                    // in this file, so returning it as the handler made
                    // keyboard-type detection see an undefined ID ($22), and
                    // hard-setting bits 6/5 reported SRQ enabled even after a
                    // Listen R3 cleared it. Mouse branch above has it right.
                    buffer_[0] = kbdHandler_; buffer_[1] = kbdHandlerId_;
                    datasize_ = 2;
                }
            } else {
                // Talk to an UNCONNECTED address (ADBReInit scans 1..15, and
                // every relocation leaves gaps): MAME raises the mouse SRQ
                // here, which is what gets accumulated motion delivered
                // promptly instead of waiting for a keyboard Talk.
                buffer_[0] = buffer_[1] = 0;
                datasize_ = 0;
                if (mousePending() && (mouseHandler_ & 0x20)) srqFlag_ = true;
            }
            break;
        }
        waitingCmd_ = false;
        return;
    }

    // Listen data phase: command_ = buffer_[0] (first data byte), buffer_[1]
    // = second (the activator). Register 3 relocates the device or selects
    // its protocol; register 2 drives the keyboard LEDs.
    direction_ = 0;
    if (listenReg_ == 2) {
        if (listenAddr_ == kbdAddr_) {
            // The only writable part of R2: the three LED latches, active
            // low (DingusPPC `adbkeyboard.cpp:100-110`). A real Extended
            // Keyboard II lights Num/Caps/Scroll Lock from them; POM68K has
            // no indicators to light, so the value is stored and exposed
            // through keyboardLeds() for a front end that grows some.
            kbdLeds_ = uint8_t(buffer_[1] & 0x07);
            if (std::getenv("POM68K_ADB_LLE_TRACE"))
                std::fprintf(stderr, "adbline: kbd Listen R2 LEDs %02X\n", kbdLeds_);
        }
        return;
    }
    if (listenReg_ == 3) {
        // The activator byte selects what the write means. $00 sets address
        // + flags unconditionally, $FE moves the address only if there was
        // no collision, and a small handler number selects a PROTOCOL: the
        // device accepts it when it implements it and ignores it otherwise,
        // which is precisely how a driver discovers what it is talking to
        // (Guide to the Macintosh Family Hardware ch. 8; DingusPPC
        // `adbkeyboard.cpp:118-140`, `adbmouse.cpp:143-160`).
        if (listenAddr_ == mouseAddr_) {
            if (std::getenv("POM68K_ADB_LLE_TRACE"))
                std::fprintf(stderr, "adbline: mouse Listen R3 %02X %02X "
                             "(handler %02X id %02X)\n", command_, buffer_[1],
                             mouseHandler_, mouseHandlerId_);
            if (buffer_[1] == 0x00) { mouseHandler_ = uint8_t(command_ & 0x7F); mouseAddr_ = command_ & 0x0F; }
            else if (buffer_[1] == 0xFE) { mouseAddr_ = command_ & 0x0F;
                                           mouseHandler_ = uint8_t((mouseHandler_ & 0xF0) | mouseAddr_); }
            // 1 = 100 cpi, 2 = 200 cpi, 4 = Extended Mouse Protocol. Any
            // other ID is a device we are not, so it is refused by silence:
            // the driver reads R3 back, sees the old ID and moves on.
            else if (buffer_[1] == 0x01 || buffer_[1] == 0x02 || buffer_[1] == 0x04)
                mouseHandlerId_ = buffer_[1];
        } else if (listenAddr_ == kbdAddr_) {
            if (buffer_[1] == 0x00) { kbdHandler_ = uint8_t(command_ & 0x7F); kbdAddr_ = command_ & 0x0F; }
            else if (buffer_[1] == 0xFE) { kbdAddr_ = command_ & 0x0F;
                                           kbdHandler_ = uint8_t((kbdHandler_ & 0xF0) | kbdAddr_); }
            // 1 = Apple Standard Keyboard, 2 = Apple Extended Keyboard II,
            // 3 = the extended protocol with distinct right-hand modifiers.
            // A standard keyboard (ID 1) must NOT accept 3 — that is the
            // whole test a driver uses to decide it has an extended one.
            else if (buffer_[1] == 0x01 || buffer_[1] == 0x02)
                kbdHandlerId_ = buffer_[1];
            else if (buffer_[1] == 0x03 && kbdHandlerId_ != 1)
                kbdHandlerId_ = 3;
        }
    }
}
