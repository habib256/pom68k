// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── IWM (Integrated Woz Machine) ──
// Floppy controller as wired in the Mac Plus: 8 internal state lines
// (CA0-CA2/LSTRB = ph0-ph3, ENABLE, SELECT, Q6, Q7) toggled by address
// bits A9-A12 ($C00000-$DFFFFF odd bytes; reg = line*2 + set/clear).
// (Q7,Q6) select DATA / STATUS / HANDSHAKE / MODE registers. The ph lines
// double as the Sony drive's register address; SEL comes from VIA PA5.
// Source of truth: MAME iwm.cpp; DEV.md § IWM (research-pinned).
// Gate: tests/gcr_test.cpp, tests/disk_boot_etalon.cpp.

#pragma once
#include "FluxPll.h"
#include "SaveState.h"
#include <cstdint>
#include <algorithm>
#include <vector>

class SonyDrive;

class Iwm {
public:
    void reset();
    void attachDrive(SonyDrive* internal, SonyDrive* external) {
        drive_[0] = internal; drive_[1] = external;
    }
    // ── The PA4 internal-connector line (SE, SE FDHD, Classic) ─────────
    // These boards put two internal connectors behind ENABLE1 and let
    // VIA1 PA4 choose between them; ENABLE2 still reaches the external
    // port. The SE, SE FDHD and Classic ROMs' DiskSelect (B2E362A8
    // $35316, B306E171 $35562/$35CBE, A49F9914 $3F806) set PA4 for
    // physical drive slot 1 and clear it for slot 2 before asserting
    // ENABLE1 or the ISM drive-1 enable; slot 3 is ENABLE2. MAME
    // mac128.cpp:879 names PA4 0 = upper, 1 = lower and leaves PA4 high
    // unconnected; Snow's swim/mod.rs selects drive index 2 for PA4 high
    // and fits that mechanism on the SE and SE FDHD only.
    // `drive_[0]` answers PA4 low and `second` PA4 high — null is an empty
    // connector, which selects no mechanism at all. Boards without the
    // line keep ENABLE1 on `drive_[0]` whatever PA4 does.
    // Wiring, re-attached at reset like drive_; the line level is state.
    void wireInternalSelect(bool wired, SonyDrive* second) {
        intSelWired_ = wired; drive_[2] = wired ? second : nullptr;
    }
    void setInternalSelect(bool high);
    bool internalSelect() const { return intSel_; }
    // The mechanism ENABLE1 (or the ISM drive-1 enable) reaches now.
    SonyDrive* enable1Drive() const {
        return (intSelWired_ && intSel_) ? drive_[2] : drive_[0];
    }

    // Bus access: reg = addr bits A9-A12.
    uint8_t read(int reg);
    void write(int reg, uint8_t v);

    // VIA PA5 — SEL bit of the drive sense/command address + head select.
    void setSel(bool sel);
    bool sel() const { return sel_; }

    // ── TWO clocks, and on the Mac SE they are not the same one ─────────
    // `setTickHz` is the unit of tick()'s argument: the cycle the platform
    // counts in (CPU C7M on the compacts, machine C15M on the Mac II
    // family and the SWIM1 personality).
    // `setChipHz` is the clock the BOARD wires to the chip's CLK pin,
    // which is the unit the mode register's window tables are counted in
    // (MAME `time_to_cycles`, i.e. `clock()` — see Iwm.cpp).
    // They coincide on every board but the compacts with ADB: MAME's
    // `macse` re-declares `IWM(config.replace(), m_iwm, C7M*2)`
    // (mac128.cpp:1317, inherited by macsefd and macclasc) on a machine
    // whose CPU stays at C7M. Assuming one clock for both is what made the
    // SE, SE FDHD and Classic eject a perfectly good 800K disk: their ROM
    // writes mode **$17**, the C15M pair (measured; the Plus writes $1F),
    // and a 36-clock window counted in C7M is 2.3x the cell — the guest
    // reads `F7 BD EF F7 BD EF` forever and `.Sony` gives up.
    // Wiring, not state — re-set at construction, never serialized (like
    // drive_).
    void setClockHz(int64_t hz) { setTickHz(hz); setChipHz(hz); }
    void setTickHz(int64_t hz) { clockScale_ = hz >= 15667200 ? 2 : 1; }
    void setChipHz(int64_t hz) { chipScale_ = hz >= 15667200 ? 2 : 1; }

    // Advance internal time (CPU cycles) — paces the nibble stream.
    void tick(int cpuCycles);

    // NOT given a `cyclesToNextEvent()`, and that was TRIED and dropped on
    // 2026-08-15. The theory was good — the cell engine keeps ONE framed
    // byte, so a scheduler batch as long as a GCR byte would hide bytes
    // from the guest, and a bare-Iwm probe shows exactly that cliff (one
    // revolution: 12000 bytes / 16 prologues at half-byte batches, 554 / 0
    // at one byte, 2 / 0 at two). It is not what the machines do: every
    // register access on these boards calls `flushTicks()` first, so the
    // chip is already caught up to the poll that is about to read it, and
    // wiring a one-window deadline into V8/RBV/VASP changed the LC II
    // floppy gate's counters by ZERO — same nibbles, same polls, same
    // hits, still red. The real defect was `windowTicks()`; see Iwm.cpp.

    long readCount[16] = {};              // per-reg access stats (debug)
    long dataReads = 0, dataHits = 0;     // data-reg polls vs MSB-set reads
    long senseCount[16] = {};             // status reads per sense address
    uint8_t consumed[512] = {};           // ring of nibbles the CPU consumed
    int consumedPos = 0;
    long overwritten = 0;                 // nibbles replaced before being read
    long reReads = 0;                     // MSB-set reads of an already-latched byte
    long written = 0;                     // bytes shipped to the drive

    // ── Save states (SaveState.h) ───────────────────────────────────────
    // Register/phase state plus the bit-level write engine. `drive_[3]`
    // are machine-owned pointers, re-attached on restore (see Ncr5380's
    // note on why pointers never travel).
    // The read engine's window state is live machine state since the cell
    // engine landed (§ 1.3 flux plan step 6): a snapshot taken mid-nibble
    // must resume with the same window phase and the same partial shifter,
    // or the next byte off the disk differs from the un-snapshotted run.
    template <class Ar> void visit(Ar& ar) {
        ar(ph_, enable_, driveSel_, q6_, q7_, sel_, mode_, dataReg_,
           clearCountdown_, selDelay_,
           writing_, wrPending_, wrUnderrun_, wrData_, wrPhase_, wrShift_, wrBits_, wrState_,
           wrElapsed_, wrStart_, wrEdges_);
        if constexpr (Ar::loading) {
            if (wrPhase_ < 0 || wrPhase_ > 32 * kIwmTick || wrBits_ < 0 || wrBits_ > 8 ||
                wrState_ < 0 || wrState_ > 2 || wrElapsed_ < 0 ||
                !std::is_sorted(wrEdges_.begin(), wrEdges_.end()) ||
                (!wrEdges_.empty() && (wrEdges_.front() < 0 || wrEdges_.back() > wrElapsed_))) ar.fail();
        }
        ar(fluxClock_, nextStateChange_, nextFluxChange_, syncUpdate_,
           rwState_, rsh_, readArmed_, intSel_, armedFluxRev_, armedSpinRev_);
        ar(readCount, dataReads, dataHits, senseCount,
           consumed, consumedPos, overwritten, written, reReads);
    }

private:
    // MAME iwm.cpp m_rw_state (:413-437).
    enum : int { kIdle = 0, kEdge0 = 1, kEdge1 = 2 };
    // One IWM clock (C7M) is two C15M clocks, and the drive counts flux in
    // FluxPll::kSubCell subdivisions of a C15M clock — so the whole read
    // engine can run in the drive's own unit with no conversion per edge.
    static constexpr int64_t kIwmTick = 2 * FluxPll::kSubCell;

    uint8_t access(int reg);
    uint8_t readRegister();
    void updateRw();
    void tickRead(int64_t elapsedTicks);
    void tickWrite(int64_t elapsedTicks);
    void flushWriteFlux();
    void beginWriteFlux();
    void latchData(uint8_t v);
    bool isSync() const { return !(mode_ & 0x02); }
    int64_t clockTick() const;                   // one clock of THIS chip
    int64_t halfWindowTicks() const;
    int64_t windowTicks() const;
    int64_t updateDelayTicks() const;
    SonyDrive* selectedDrive() const { return driveSel_ ? drive_[1] : enable1Drive(); }
    // MAME iwm.cpp:243-247 devsel: sense/commands reach a drive only while
    // one is selected — ENABLE set, or the ~1 s motor-off delay window when
    // mode bit 2 is clear (MODE_DELAY, iwm.cpp:236-239; the Mac's mode $1F
    // sets bit 2, making deselect immediate).
    bool driveSelected() const { return enable_ || selDelay_ > 0; }
    int senseAddr() const;

    SonyDrive* drive_[3] = { nullptr, nullptr, nullptr };
    bool ph_[4] = { false, false, false, false };
    bool enable_ = false, driveSel_ = false, q6_ = false, q7_ = false;
    bool sel_ = false;
    bool intSel_ = false;                 // VIA1 PA4 on the SE board
    bool intSelWired_ = false;            // board wiring, not serialized
    int clockScale_ = 1;                  // tick() cycles per C7M clock
    int chipScale_ = 1;                   // CHIP clocks per C7M clock
    uint8_t mode_ = 0, dataReg_ = 0;
    int clearCountdown_ = 0;              // delayed clear after a data read

    // ── Read engine (MAME iwm.cpp sync(), MODE_READ :398-455) ──────────
    // A window state machine, not a PLL: every flux transition re-centres
    // the current window (the EDGE_0 branch), which is how the chip tracks
    // a data rate its nominal 2 us window does not exactly share — the Sony
    // GCR cell is 31 C15M clocks, the IWM window 32. GCR 6&2 never runs more
    // than two cells without a transition, so it re-centres often enough.
    int64_t fluxClock_ = 0;               // absolute, in step with the spindle
    int64_t nextStateChange_ = 0;         // start of the current window
    int64_t nextFluxChange_ = 0;          // cached next transition
    int64_t syncUpdate_ = 0;              // latch-mode deferred data update
    int rwState_ = kIdle;
    uint8_t rsh_ = 0;                     // MAME m_rsh, the read shifter
    bool readArmed_ = false;              // parked at the drive's angle
    int64_t armedFluxRev_ = 0;            // revolution lengths when parked
    int64_t armedSpinRev_ = 0;
    int64_t selDelay_ = 0;                // devsel hold after ENABLE drops
                                          // (MODE_DELAY, mode bit 2 clear)

    // MAME 0.285: LOAD (+7 chip clocks), MIDDLE (flux), END (shift).
    bool writing_ = false, wrPending_ = false, wrUnderrun_ = false;
    uint8_t wrData_ = 0, wrShift_ = 0;
    int64_t wrPhase_ = 0;                 // flux ticks until next event
    int wrBits_ = 0, wrState_ = 0;        // 0=load, 1=middle, 2=end
    int64_t wrElapsed_ = 0, wrStart_ = 0;
    std::vector<int64_t> wrEdges_;        // pending physical write arc
};
