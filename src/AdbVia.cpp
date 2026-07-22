// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "AdbVia.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>

// Diagnostic tracer (POM68K_ADB_PIC_TRACE=1): logs every ST value the PIC
// samples (with its PC) and every PIC port-B write that changes CB1/CB2/IRQ.
static bool picTrace() {
    static const bool t = std::getenv("POM68K_ADB_PIC_TRACE") != nullptr;
    return t;
}

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

    // LLE by DEFAULT when the firmware dump is present (2026-07-22 — the
    // cycle-exact co-stepping + VIA ext-shift fixes made it the reference;
    // the mouse only moves on this path). POM68K_ADB_LLE=0 forces the old
    // HLE byte-model; missing dump falls back to HLE silently.
    const char* env = std::getenv("POM68K_ADB_LLE");
    if (!env || env[0] != '0') {
        for (const char* p : { "roms/adbmodem/342s0440-b.bin",
                               "../roms/adbmodem/342s0440-b.bin" }) {
            std::ifstream f(p, std::ios::binary);
            if (!f) continue;
            std::vector<uint8_t> rom((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
            if (pic_.loadRom(rom.data(), rom.size())) { lle_ = true; break; }
        }
        if (lle_) { line_.reset(); picAcc_ = 0; setupPicPorts(); }
    }
}

void AdbVia::setupPicPorts() {
    // Wiring per MAME adbmodem.cpp:
    //   RA0/RA1 (in)  = VIA PB4/PB5 (ST0/ST1)     RA2 (out, inverted) = ADB line
    //   RA3 (in)      = ADB line                   RB2 (out) = VIA CB1 shift clock
    //   RB3 (in/out)  = VIA CB2 shift data         RB4 (out) = VIA PB3 IRQ (active low)
    pic_.readA = [this]() -> uint8_t {
        // ST0/ST1 on PB4/PB5. When the 68k leaves those pins as inputs (not
        // driving a state) they pull high → ST=IDLE(3), not NEW(0); otherwise
        // the PIC would treat every idle sample as a fresh command. So force
        // input ST bits to 1.
        uint8_t pb = uint8_t(via_->portB() | ~via_->ddrb());
        uint8_t st = uint8_t((pb >> 4) & 3);
        if (picTrace()) {
            static uint8_t lastSt = 0xFF;
            if (st != lastSt) {
                lastSt = st;
                std::fprintf(stderr, "pic: sampled ST=%d @pc=%03X clk=%lld\n",
                             st, pic_.pc(), (long long)lastPicClock_);
            }
        }
        return uint8_t(st | (line_.line() ? 0x08 : 0));
    };
    pic_.writeA = [this](uint8_t v) {
        line_.setHostDrive(!(v & 0x04));           // RA2=1 pulls the line low
    };
    pic_.readB = [this]() -> uint8_t {
        return uint8_t(0xF7 | (via_->extShiftCB2Out() ? 0x08 : 0));   // RB3 = CB2 in
    };
    pic_.writeB = [this](uint8_t v) {
        if (picTrace()) {
            static uint8_t lastB = 0xFF;
            if ((v ^ lastB) & 0x1C) {
                std::fprintf(stderr, "pic: portB=%02X (CB1=%d CB2=%d IRQ=%d) @pc=%03X clk=%lld\n",
                             v, !!(v & 4), !!(v & 8), !(v & 0x10), pic_.pc(),
                             (long long)lastPicClock_);
                lastB = v;
            }
        }
        via_->extShiftCB1((v & 0x04) != 0, (v & 0x08) != 0);   // RB2 clock, RB3 data
        irqPending_ = !(v & 0x10);                             // RB4 → PB3 IRQ
    };
}

void AdbVia::tickLle(int cpuCycles) {
    picAcc_ += cpuCycles;
    while (picAcc_ >= kCyclesPerPicInsn) {
        // Cycle-exact co-stepping: charge the *real* instruction cost.
        // run(1) executes one instruction and returns its cost in PIC cycles
        // (1; branches/skips 2; computed goto 3). The old code charged every
        // instruction 1 cycle, so branch-heavy firmware (DECFSZ+GOTO delay
        // loops = 3 cycles/iter) ran up to 2-3× too fast vs the 68k — its
        // inter-state timeouts expired early and the ROM's ADB self-test ST
        // ramp was misrouted as a command (TODO ★, DEV.md "PIC1654S LLE").
        // With true cost the measured bit cell lands on the ADB spec 100 µs.
        int cost = pic_.run(1);                    // drives the ports
        line_.tick(cost * kCyclesPerPicInsn);      // ADB device send timers
        picAcc_ -= cost * kCyclesPerPicInsn;       // may borrow ≤2 cycles; self-corrects
        pic_.setRtcc(line_.line());                // RTCC pin tracks the ADB line
    }
    applyIrqToVia();
}

void AdbVia::syncTo(int64_t cpuClock) {
    if (!lle_) return;
    if (lastPicClock_ < 0) { lastPicClock_ = cpuClock; return; }
    int64_t delta = cpuClock - lastPicClock_;
    if (delta <= 0) return;
    lastPicClock_ = cpuClock;
    tickLle(int(delta));
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
    if (lle_) { applyIrqToVia(); return; }   // the PIC samples ST via its ports
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
    if (lle_) return;   // LLE PIC is driven by syncTo(absolute CPU clock)
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
