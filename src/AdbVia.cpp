// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "AdbVia.h"
#include "FirmwareChoice.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

void AdbVia::reset() {
    state_ = IDLE;
    lastState_ = IDLE;
    cmd_.clear();
    resp_.clear();
    respPos_ = 0;
    irqPending_ = false;
    expectingListen_ = false;
    timer_ = 0;
    // The LLE half must reset too — attach() already does exactly this, so
    // the two entry points otherwise left the object in different states and
    // a hard reset mid-transaction kept the PIC's PC/registers and the ADB
    // line's frame state from the previous boot. (CudaLle::reset does both.)
    // lastPicClock_ is deliberately untouched: the Moira clock is monotonic
    // across hardReset(), so zeroing it would stall syncTo().
    if (lle_) { pic_.reset(); line_.reset(); picAcc_ = 0; }
}

void AdbVia::attach(Via6522& via, AdbBus& adb, int64_t cpuHz) {
    via_ = &via;
    adb_ = &adb;
    cpuHz_ = cpuHz > 0 ? cpuHz : 15667200;
    reset();

    // LLE by DEFAULT when the firmware dump is present (2026-07-22 — the
    // cycle-exact co-stepping + VIA ext-shift fixes made it the reference;
    // the mouse only moves on this path). POM68K_ADB_LLE=0 forces the old
    // HLE byte-model. The fallback stays (dumps are user-provided, not
    // distributable) but never silently: the byte-model is a documented
    // NON-CONFORMANT substitute (LLE_VS_HLE §2) and §1.9's ORB→SHIFT
    // re-arm lives only on this path.
    pom68k::fw::Request req{pom68k::lle::HleAdbModem,
                            pom68k::FirmwareTarget::Adb};
    req.name = "Transcepteur ADB PIC1654S (342S0440-B)";
    req.enableKnob = "POM68K_ADB_LLE";
    req.pathKnob = "POM68K_ADB_FW";
    req.logTag = "AdbVia";
    req.enabled = firmwareEnabled_;
    req.forcedPath = firmwarePath_;
    req.candidates = { "roms/adbmodem/342s0440-b.bin",
                       "../roms/adbmodem/342s0440-b.bin" };
    lle_ = pom68k::fw::select(req, [this](const std::vector<uint8_t>& rom) {
        return pic_.loadRom(rom.data(), rom.size());
    });
    if (lle_) { line_.reset(); picAcc_ = 0; setupPicPorts(); }
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
        if (trace_) {
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
        // ── MAME's CB2 anti-race guard is DELIBERATELY not ported ──
        // MAME-parity audit §2.9 (cosmetic, DOCUMENT-SKIP 2026-08-06,
        // claim re-verified). `adbmodem_device::set_via_data`
        // (adbmodem.cpp:195-217) accepts a new CB2 level only while the
        // last CB1 clock it wrote was 0, because MAME's wiring is a PUSH
        // model: the VIA calls set_via_data whenever CB2 moves, the value
        // sits in m_via_data, and the PIC reads that LATCH whenever it next
        // runs. On a fast guest (MAME's own example is the IIci) the 68k
        // writes the ACR — driving CB2 high — before the PIC has sampled
        // the last bit, and the stale latch is overwritten; MAME's symptom
        // was Talk R0 decoded as R1 and the mouse button going berserk.
        // POM68K has no latch to clobber. This is a PULL model: the line
        // below reads sr_ bit 7 live (`Via6522::extShiftCB2Out`, a pure
        // accessor) at the instant the PIC executes its port-B read, and
        // AdbVia::syncTo runs the PIC forward to the exact machine cycle
        // before ANY guest VIA1 access is decoded (MacIIMemory.cpp:274,
        // and the equivalent hook on every other AdbVia platform). The
        // 68k therefore cannot write the ACR "before the PIC sampled" —
        // the PIC has already been stepped to that cycle. Porting the
        // guard would ADD a failure mode: it would reject legitimate CB2
        // levels during the shift-out phase, where the PIC reads CB2
        // AFTER raising CB1 (see Via6522::extShiftCB1's mode-7 comment).
        // Reopening condition: only if the co-stepping is ever relaxed to
        // batch the PIC against a peripheral deadline instead of the CPU
        // clock, i.e. if syncTo stops being called per VIA1 access.
        return uint8_t(0xF7 | (via_->extShiftCB2Out() ? 0x08 : 0));   // RB3 = CB2 in
    };
    pic_.writeB = [this](uint8_t v) {
        if (trace_) {
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
    // Rational accumulator against the machine's OWN clock: picAcc_ counts
    // machine-cycles x kPicHz, so one PIC cycle costs cpuHz_ of them. This is
    // exact at every clock from the 7.8336 MHz compacts to the 33.33 MHz
    // Quadras, where the old fixed /34 divisor was only right on the Mac II.
    const int64_t cyPerPic = cpuHz_;              // in accumulator units
    picAcc_ += int64_t(cpuCycles) * kPicHz;
    while (picAcc_ >= cyPerPic) {
        // Cycle-exact co-stepping: charge the *real* instruction cost.
        // run(1) executes one instruction and returns its cost in PIC cycles
        // (1; branches/skips 2; computed goto 3). The old code charged every
        // instruction 1 cycle, so branch-heavy firmware (DECFSZ+GOTO delay
        // loops = 3 cycles/iter) ran up to 2-3× too fast vs the 68k — its
        // inter-state timeouts expired early and the ROM's ADB self-test ST
        // ramp was misrouted as a command (TODO ★, DEV.md "PIC1654S LLE").
        // With true cost the measured bit cell lands on the ADB spec 100 µs.
        int cost = pic_.run(1);                    // drives the ports
        // AdbLine's thresholds are calibrated in PIC-cycle units scaled by
        // kPicTick (544/1020/1564 = 16/30/46 PIC cycles), so its time base
        // stays tied to the PIC's own fixed rate — NOT to the host machine
        // clock, which is what the accumulator above now tracks separately.
        line_.tick(cost * kPicTick);
        picAcc_ -= int64_t(cost) * cyPerPic;       // may borrow; self-corrects
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

int AdbVia::cyclesToNextEvent() const {
    if (!via_ || !adb_) return 0x7fffffff;
    if (lle_) {
        const int64_t need = cpuHz_ - picAcc_;
        if (need <= 0) return 1;
        return int(std::max<int64_t>(1, (need + kPicHz - 1) / kPicHz));
    }
    if (timer_ > 0) return timer_;
    // The HLE polls SRQ and may re-arm a transaction on its next tick.
    return 1;
}
