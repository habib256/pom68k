// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Register model ported from MAME iwm.cpp, cross-checked against pce iwm.c
// and Snow iwm.rs — see DEV.md § IWM.

#include "Iwm.h"
#include "SonyDrive.h"

void Iwm::reset() {
    for (bool& p : ph_) p = false;
    enable_ = driveSel_ = q6_ = q7_ = sel_ = false;
    mode_ = 0;
    dataReg_ = 0;
    writing_ = wrPending_ = wrUnderrun_ = false;
    wrPhase_ = 0;
    selDelay_ = 0;
    fluxClock_ = nextStateChange_ = nextFluxChange_ = syncUpdate_ = 0;
    rwState_ = kIdle;
    rsh_ = 0;
    readArmed_ = false;
}

// ── The window tables are counted in the CHIP'S OWN clock ───────────────
// MAME iwm.cpp:335-361, and the unit is `time_to_cycles`, i.e. `clock()` —
// the clock the machine gives the device, which is C7M on the compacts
// (`mac128.cpp:1182: IWM(config, m_iwm, C7M)`) and C15M on everything
// after (`macii.cpp:938: IWM(config, m_fdc, C15M)`; every `SWIM1(config,
// …, C15M)`). swim1.cpp doubles the same numbers because ITS counter runs
// at `2*clock()` (`swim1.cpp:626`), so both land on the same absolute
// window — 36 C15M clocks for the mode the Mac actually uses.
//
// Which is the whole point, and what this code got wrong until 2026-08-15:
// the mode register's bit 3 is the chip's CLOCK SPEED and bit 4 its cell
// time, so a driver picks the pair to suit the clock it wired up. A C7M
// Mac writes `$1F` (0x18 → window 16 C7M ≈ 2.04 µs); a C15M one writes
// **`$17`** (0x10 → window 36 C15M ≈ 2.30 µs). Both are one 2 µs Sony GCR
// cell. Multiplying the C15M table by a C7M-sized unit — which is what
// "the doubled swim1.cpp values come for free" amounted to — gives 72 C15M
// clocks, 2.3× the 31-clock cell, and nothing frames at all: 50 000 bytes
// off the medium and not one `D5 AA 96` in them, `.Sony` giving up with
// -67 noAdrMkErr, and no floppy mounting on any non-compact machine
// (`CHANGELOG.md` 2026-08-15 (third)).
//
// So the unit is `clockTick()` — one clock of THIS chip — everywhere.
//
// And "this chip's clock" is NOT "the cycle the machine ticks it in": the
// Mac SE hands the IWM C7M CPU cycles while the board clocks it at C15M
// (`mac128.cpp:1317`), which is why the two scales are separate fields
// (Iwm.h `setTickHz`/`setChipHz`). Q3-clocked mode is not modelled: no Mac
// wires it.
int64_t Iwm::clockTick() const { return kIwmTick / chipScale_; }

int64_t Iwm::halfWindowTicks() const {
    const int64_t u = clockTick();
    switch (mode_ & 0x18) {
        case 0x00: return 14 * u;
        case 0x08: return  7 * u;
        case 0x10: return 16 * u;                // the Mac's C15M mode $17
        default:   return  8 * u;                // 0x18 — the Plus's $1F
    }
}

int64_t Iwm::windowTicks() const {
    const int64_t u = clockTick();
    switch (mode_ & 0x18) {
        case 0x00: return 28 * u;
        case 0x08: return 14 * u;
        case 0x10: return 36 * u;                // NOT 2x the half window
        default:   return 16 * u;
    }
}

int64_t Iwm::updateDelayTicks() const {
    return (mode_ & 0x08 ? 4 : 8) * clockTick(); // iwm.cpp:363-366
}

// Sense/command address presented to the drive: CA2 CA1 CA0 = ph2 ph1 ph0,
// SEL from VIA PA5.
int Iwm::senseAddr() const {
    return (ph_[2] ? 8 : 0) | (ph_[1] ? 4 : 0) | (ph_[0] ? 2 : 0) | (sel_ ? 1 : 0);
}

// Every access (read or write) toggles one of the 8 state lines:
// reg = line*2 + (A9: 0 = clear, 1 = set).
uint8_t Iwm::access(int reg) {
    bool set = reg & 1;
    switch (reg >> 1) {
        case 0: case 1: case 2:
            ph_[reg >> 1] = set;
            break;
        case 3: {                                 // ph3 = LSTRB
            bool rising = set && !ph_[3];
            ph_[3] = set;
            // Commands only reach a SELECTED drive (MAME iwm.cpp:243-247:
            // devsel drops to "none" when the controller is idle).
            if (rising && driveSelected() && selectedDrive())
                selectedDrive()->command(senseAddr());
            break;
        }
        case 4: {                                 // drive ENABLE
            const bool was = enable_;
            enable_ = set;
            // MAME iwm.cpp:224-239: dropping ENABLE with mode bit 2 set
            // ($1F on every Mac) deselects immediately; with the motor-off
            // timer enabled (bit 2 clear, the reset default) the selection
            // lingers MODE_DELAY = cycles_to_time(8388608) ≈ 1 s.
            if (set) selDelay_ = 0;
            else if (was && !(mode_ & 0x04)) selDelay_ = 8388608LL * clockScale_;
            break;
        }
        case 5: driveSel_ = set; break;           // 0 = internal, 1 = external
        case 6: q6_ = set; break;
        case 7: q7_ = set; break;
    }
    updateRw();
    // MAME iwm.cpp:249-250: any access that leaves the chip on the STATUS
    // register while it is actively READING clears the read shifter. Real,
    // and now observable — the byte in flight is lost, which is why a
    // driver polls sense between sectors and not between nibbles.
    if (q6_ && !q7_ && enable_ && !writing_) rsh_ = 0;
    return readRegister();
}

// Read/write mode tracking (MAME iwm.cpp control(), :198-221): q7 while
// enabled enters write mode — the access that sets q7 can carry the first
// data byte; clearing q7 (or ENABLE) leaves it and flushes the write-back.
void Iwm::updateRw() {
    const bool wantWrite = enable_ && q7_;
    if (wantWrite && !writing_) {
        writing_ = true;
        wrUnderrun_ = false;
        wrPending_ = false;
        wrPhase_ = 7;                             // first load: S_IDLE + 7
        readArmed_ = false;                       // MAME m_rw_state = S_IDLE
    } else if (!wantWrite && writing_) {
        writing_ = false;
        wrPending_ = false;
        readArmed_ = false;                       // re-park on the spindle
        if (selectedDrive()) selectedDrive()->flushWrite(sel_);
    }
    // Losing the drive loses the head position with it.
    if (!enable_) readArmed_ = false;
}

uint8_t Iwm::read(int reg) {
    readCount[reg]++;
    uint8_t v = access(reg);
    if (!q6_ && !q7_) { dataReads++; if (v & 0x80) dataHits++; }
    return v;
}

void Iwm::write(int reg, uint8_t v) {
    access(reg);
    // MAME iwm.cpp control(): mode/data writes reach the chip only through
    // the odd (line-set) addresses — an even access just toggles its line.
    if ((reg & 1) && q6_ && q7_) {
        // $1F on a C7M Mac, $17 on a C15M one (windowTicks's note); the
        // guest picks the pair that matches the clock the board wired up,
        // and the LC II ROM was measured writing $57 → $17.
        if (!enable_) mode_ = v & 0x1F;
        else if (writing_) {                      // write-data register
            wrData_ = v;
            wrPending_ = true;                    // latched: handshake b7 low
        }
    }
}

uint8_t Iwm::readRegister() {
    if (!q6_ && !q7_) {                           // DATA register
        if (!enable_) return 0xFF;
        uint8_t v = dataReg_;
        // Real IWM latched mode: the register clears ~14 IWM clocks AFTER a
        // read (MAME m_last_sync + 14). The ROM relies on it: `tst.b` polls
        // the MSB, then `move.b` re-reads the same nibble an instant later.
        if ((v & 0x80) && clearCountdown_ == 0) {
            // 14 ticks of the IWM'S OWN clock (MAME `m_last_sync + 14`,
            // iwm.cpp:285, where last_sync counts clock() ticks) — so the
            // hold is ~1.79 us of wall time whatever the host. It must be
            // scaled onto the tick unit exactly like kCyclesPerNibble is,
            // or a C15M host holds the byte for half as long as silicon.
            // KNOWN MAME DIVERGENCE, deliberately pinned (parity audit
            // #24): MAME re-arms the clear on EVERY access (iwm.cpp:284-285
            // runs in control(); expiry applied lazily at :461-465), so the
            // byte dies 14 ticks after the LAST access, not the first read.
            // Re-arming here would move the clear later and shrink the
            // anti-duplicate margin of Apple's denibble loop (tst.b poll +
            // move.b consume) by the tst->move gap — the exact badDCksum
            // failure mode fixed 2026-08-05. Do not align without first
            // measuring reReads under lcii_sony_trace at boost 1.
            clearCountdown_ = 14 * clockScale_;
            consumed[consumedPos] = v; consumedPos = (consumedPos + 1) & 511;
        } else if (v & 0x80) {
            // Re-read of a byte already latched: on silicon the clear runs
            // in continuous time, so a faster CPU simply sees the same
            // valid byte for the rest of the 1.79 us. Counted because our
            // clear is quantized to tick() — a driver that takes each
            // MSB-set read as a NEW nibble would decode duplicates, and
            // the count is how a boosted CPU could change decode outcomes.
            reReads++;
        }
        return v;
    }
    if (q6_ && !q7_) {                            // STATUS register
        const_cast<Iwm*>(this)->senseCount[senseAddr()]++;
        // Deselected drive = pull-up on the sense line (MAME iwm.cpp:129
        // reads high when m_floppy is null, i.e. devsel none).
        bool sense = (driveSelected() && selectedDrive())
                         ? selectedDrive()->sense(senseAddr()) : true;
        return uint8_t((sense ? 0x80 : 0x00) | (enable_ ? 0x20 : 0x00) | (mode_ & 0x1F));
    }
    if (!q6_ && q7_) {                            // write HANDSHAKE (m_whd)
        // b7 = register empty (ready for the next byte), b6 = write mode
        // healthy; low bits ride high like MAME's 0xBF reset value.
        return uint8_t((wrPending_ ? 0x00 : 0x80) |
                       (writing_ && !wrUnderrun_ ? 0x40 : 0x00) | 0x3F);
    }
    return 0xFF;                                  // (Q6,Q7) = (1,1)
}

// Nibble pacing: mode $1F = 2 µs bit cells → one GCR byte every 16 µs
// ≈ 128 C7M clocks — clockScale_ maps that onto the platform's tick unit.
void Iwm::tick(int cpuCycles) {
    if (clearCountdown_ > 0) {
        clearCountdown_ -= cpuCycles;
        if (clearCountdown_ <= 0) { clearCountdown_ = 0; dataReg_ = 0; }
    }
    if (selDelay_ > 0) {                          // MODE_DELAY deselect timer
        selDelay_ -= cpuCycles;
        if (selDelay_ < 0) selDelay_ = 0;
    }
    if (!enable_ || !selectedDrive() || !selectedDrive()->hasDisk()) return;
    const int kCyclesPerNibble = 128 * clockScale_;
    if (writing_) {
        // Write shifter (MAME MODE_WRITE): consume the pending byte every
        // 8 bit windows; loading with nothing pending is an underrun that
        // halts the engine (SW_UNDERRUN) and flushes what was written.
        if (wrUnderrun_) return;
        wrPhase_ -= cpuCycles;
        while (wrPhase_ <= 0) {
            if (!wrPending_) {
                wrUnderrun_ = true;
                selectedDrive()->flushWrite(sel_);
                wrPhase_ = 0;
                break;
            }
            selectedDrive()->writeNibble(wrData_);
            written++;
            wrPending_ = false;
            wrPhase_ += kCyclesPerNibble;
        }
        return;
    }
    // MAME iwm.cpp:398-405: the read shifter advances on flux transitions,
    // and floppy.cpp:1175-1178 get_next_transition() returns `never` while
    // the spindle is stopped (m_mon) — Mac drives gate the motor by command,
    // not by ENABLE (mon_w override, floppy.cpp:3417-3420). No spin, no
    // nibbles. (The write engine above stays ungated: the Sony driver never
    // writes motor-off, and iwm_write_test drives it without a motor.)
    if (!selectedDrive()->motorOn()) { readArmed_ = false; return; }
    // One tick() cycle in the drive's flux unit. Exact in integers: kIwmTick
    // is 2048 and clockScale_ is 1 or 2.
    tickRead(int64_t(cpuCycles) * (kIwmTick / clockScale_));
}

// The byte reached the data register. MAME clears its async update at the
// same moment (iwm.cpp:449); clearCountdown_ is that latch here.
void Iwm::latchData(uint8_t v) {
    if (dataReg_ & 0x80) overwritten++;
    dataReg_ = v;
    clearCountdown_ = 0;
    if (SonyDrive* d = selectedDrive()) d->nibblesRead++;
}

// MAME iwm.cpp sync(), case MODE_READ (:398-455) — ported verbatim, with
// attotime replaced by the drive's flux ticks. This is § 1.3 flux plan
// step 6: the byte stream is recovered from transitions now, where it used
// to be handed over one pre-encoded nibble per fixed 128-cycle slot.
//
// What changes for a guest: bytes no longer arrive on a metronome. A GCR
// data nibble always has bit 7 set, so it still self-frames in 8 windows,
// but a 10-cell self-sync group takes ten — which is precisely how the
// format keeps a real IWM in step, and precisely what a fixed cadence
// could not express.
void Iwm::tickRead(int64_t elapsedTicks) {
    SonyDrive* d = selectedDrive();
    if (!readArmed_) {
        // Park on the spindle: the head is wherever rotation left it, the
        // same rotational latency Swim2::armReadPll takes.
        fluxClock_ = d->fluxAngleTicks(sel_);
        nextStateChange_ = fluxClock_;
        nextFluxChange_ = 0;
        syncUpdate_ = 0;
        rwState_ = kIdle;
        readArmed_ = true;
    }
    const int64_t nextSync = fluxClock_ + elapsedTicks;
    const int64_t win = windowTicks(), half = halfWindowTicks();
    while (nextSync > fluxClock_) {
        if (nextFluxChange_ <= fluxClock_) {
            const int64_t e = d->nextFluxAfter(fluxClock_ + 1, sel_);
            nextFluxChange_ = e;
            if (e != FluxPll::kNever && nextFluxChange_ <= fluxClock_)
                nextFluxChange_ = fluxClock_ + 1;
        }
        if (nextSync < nextStateChange_) { fluxClock_ = nextSync; break; }
        if (fluxClock_ < nextStateChange_) fluxClock_ = nextStateChange_;
        if (rwState_ == kIdle) {
            rsh_ = 0;
            rwState_ = kEdge0;
            nextStateChange_ = fluxClock_ + win;
            clearCountdown_ = 0;
            syncUpdate_ = 0;
            continue;
        }
        const int64_t endw = nextStateChange_ + (rwState_ == kEdge0 ? win : half);
        // A transition inside the window re-centres it and closes the cell
        // half a window later — the chip's whole rate-tracking mechanism.
        if (rwState_ == kEdge0 && nextFluxChange_ != FluxPll::kNever &&
            endw >= nextFluxChange_ && nextSync >= nextFluxChange_) {
            fluxClock_ = nextStateChange_ = nextFluxChange_;
            rwState_ = kEdge1;
            continue;
        }
        if (nextSync < endw) { fluxClock_ = nextSync; break; }
        rsh_ = uint8_t((rsh_ << 1) | (rwState_ == kEdge1 ? 1 : 0));
        nextStateChange_ = fluxClock_ = endw;
        rwState_ = kEdge0;
        if (isSync()) {
            // Latch mode (iwm.cpp:437-446). Unused by the Mac — mode $1F
            // has bit 1 set, so every Mac reads asynchronously — but the
            // chip has it and it costs three lines.
            if (rsh_ >= 0x80) { latchData(rsh_); rsh_ = 0; }
            else if (rsh_ >= 0x04) { latchData(rsh_); syncUpdate_ = 0; }
            else if (rsh_ >= 0x02) syncUpdate_ = fluxClock_ + updateDelayTicks();
        } else if (rsh_ >= 0x80) {
            latchData(rsh_);
            rsh_ = 0;
        }
    }
    if (syncUpdate_ && syncUpdate_ <= fluxClock_) {
        if (isSync()) dataReg_ = rsh_;
        syncUpdate_ = 0;
    }
}
