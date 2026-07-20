// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "AdbVia.h"

void AdbVia::reset() {
    state_ = IDLE;
    lastState_ = IDLE;
    cmd_.clear();
    resp_.clear();
    respPos_ = 0;
    irqPending_ = false;
    expectingListen_ = false;
    timer_ = 0;
}

void AdbVia::attach(Via6522& via, AdbBus& adb) {
    via_ = &via;
    adb_ = &adb;
    reset();
}

void AdbVia::applyIrqToVia() {
    if (!via_) return;
    // MAME via_in_b: PB3 high when !adb_irq_pending
    uint8_t in = via_->portB();
    if (irqPending_) in &= uint8_t(~0x08);
    else in |= 0x08;
    // Preserve RTC data bit (PB0) — refreshVia1PortB will OR it back;
    // here we only own PB3.
    via_->setInB(uint8_t((via_->portB() & ~0x08) | (in & 0x08)));
}

void AdbVia::sync() {
    if (!via_ || !adb_) return;
    State st = State((via_->portB() >> 4) & 3);
    if (st != state_) {
        lastState_ = state_;
        state_ = st;
        onState(st);
    }
    applyIrqToVia();
}

void AdbVia::onState(State st) {
    timer_ = 0;
    switch (st) {
    case NEW:
        // Host is shifting a command (or throwaway) byte out of the SR.
        cmd_.clear();
        resp_.clear();
        respPos_ = 0;
        expectingListen_ = false;
        irqPending_ = false;
        timer_ = kByteDelay;
        break;
    case EVEN:
    case ODD:
        // EVEN/ODD: either accept a Listen data byte from the SR, or
        // present the next Talk response byte into the SR.
        if (expectingListen_) {
            timer_ = kByteDelay;
        } else if (respPos_ < resp_.size()) {
            timer_ = kByteDelay;
        } else if (!cmd_.empty() && resp_.empty()) {
            // Command already taken on NEW; Talk with empty payload —
            // still pulse SHIFT so the ROM does not spin forever.
            timer_ = kByteDelay;
        }
        break;
    case IDLE:
        irqPending_ = false;
        expectingListen_ = false;
        break;
    }
}

void AdbVia::takeHostByte() {
    if (!via_ || !adb_) return;
    uint8_t b = via_->srValue();
    via_->raiseShift();

    if (state_ == NEW) {
        cmd_.clear();
        cmd_.push_back(b);
        const uint8_t op = (b >> 2) & 3;
        if (op == 2) {                     // Listen — data bytes follow
            expectingListen_ = true;
            resp_.clear();
            respPos_ = 0;
        } else {
            expectingListen_ = false;
            resp_ = adb_->command(b, {});
            respPos_ = 0;
        }
        irqPending_ = true;
        return;
    }

    if ((state_ == EVEN || state_ == ODD) && expectingListen_) {
        cmd_.push_back(b);
        // Listen payloads are typically 2 bytes after the command.
        if (cmd_.size() >= 3) {
            std::vector<uint8_t> data(cmd_.begin() + 1, cmd_.end());
            resp_ = adb_->command(cmd_[0], data);
            respPos_ = 0;
            expectingListen_ = false;
        }
        irqPending_ = true;
    }
}

void AdbVia::pushDeviceByte() {
    if (!via_) return;
    if (respPos_ < resp_.size()) {
        via_->loadSR(resp_[respPos_++]);
    } else {
        // Empty Talk / status byte — present 0xFF so the ROM sees a
        // completed shift (Apple keyboard idle = $FF $FF).
        via_->loadSR(0xFF);
    }
    irqPending_ = true;
}

void AdbVia::tick(int cpuCycles) {
    if (!via_ || !adb_) return;
    // Mid-transaction with a dead timer: re-arm so SHIFT is re-presented.
    // Slot Manager and ADB share VIA1 SR; a lost SHIFT edge leaves ST=EVEN
    // forever (Mac II Sys7: AppleTalk alert, no keyboard/mouse).
    if ((state_ == EVEN || state_ == ODD) && timer_ <= 0)
        timer_ = kByteDelay;
    if (timer_ > 0) {
        timer_ -= cpuCycles;
        if (timer_ <= 0) {
            timer_ = 0;
            const uint8_t acrShift = (via_->acr() >> 2) & 7;
            // ACR 1xx = shift out (host→PIC), 0x1/0x2/0x3 = shift in (PIC→host)
            const bool hostToPic = (acrShift >= 4);

            if (state_ == NEW && hostToPic) {
                // Wait until the host has actually loaded the SR (ROM often
                // sets ST=NEW before writing the command byte).
                if (!via_->srHostWritten()) { timer_ = kByteDelay; return; }
                takeHostByte();
            } else if ((state_ == EVEN || state_ == ODD) && expectingListen_ && hostToPic) {
                if (!via_->srHostWritten()) { timer_ = kByteDelay; return; }
                takeHostByte();
            } else if ((state_ == EVEN || state_ == ODD) && !expectingListen_)
                pushDeviceByte();
            else if (state_ == NEW && !hostToPic) {
                via_->raiseShift();
                irqPending_ = true;
            }
        }
    }
    // Device SRQ while the modem is idle: pull PB3 so the ADB Mgr polls.
    if (state_ == IDLE && adb_->srqPending())
        irqPending_ = true;
    applyIrqToVia();
}
