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

class SonyDrive;

class Iwm {
public:
    void reset();
    void attachDrive(SonyDrive* internal, SonyDrive* external) {
        drive_[0] = internal; drive_[1] = external;
    }

    // Bus access: reg = addr bits A9-A12.
    uint8_t read(int reg);
    void write(int reg, uint8_t v);

    // VIA PA5 — SEL bit of the drive sense/command address + head select.
    void setSel(bool sel) { sel_ = sel; }

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
    // Register/phase state plus the byte-granular write engine. `drive_[2]`
    // are machine-owned pointers, re-attached on restore (see Ncr5380's
    // note on why pointers never travel).
    // The read engine's window state is live machine state since the cell
    // engine landed (§ 1.3 flux plan step 6): a snapshot taken mid-nibble
    // must resume with the same window phase and the same partial shifter,
    // or the next byte off the disk differs from the un-snapshotted run.
    template <class Ar> void visit(Ar& ar) {
        ar(ph_, enable_, driveSel_, q6_, q7_, sel_, mode_, dataReg_,
           clearCountdown_, selDelay_,
           writing_, wrPending_, wrUnderrun_, wrData_, wrPhase_);
        ar(fluxClock_, nextStateChange_, nextFluxChange_, syncUpdate_,
           rwState_, rsh_, readArmed_);
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
    void latchData(uint8_t v);
    bool isSync() const { return !(mode_ & 0x02); }
    int64_t clockTick() const;                   // one clock of THIS chip
    int64_t halfWindowTicks() const;
    int64_t windowTicks() const;
    int64_t updateDelayTicks() const;
    SonyDrive* selectedDrive() const { return drive_[driveSel_ ? 1 : 0]; }
    // MAME iwm.cpp:243-247 devsel: sense/commands reach a drive only while
    // one is selected — ENABLE set, or the ~1 s motor-off delay window when
    // mode bit 2 is clear (MODE_DELAY, iwm.cpp:236-239; the Mac's mode $1F
    // sets bit 2, making deselect immediate).
    bool driveSelected() const { return enable_ || selDelay_ > 0; }
    int senseAddr() const;

    SonyDrive* drive_[2] = { nullptr, nullptr };
    bool ph_[4] = { false, false, false, false };
    bool enable_ = false, driveSel_ = false, q6_ = false, q7_ = false;
    bool sel_ = false;
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
    int64_t selDelay_ = 0;                // devsel hold after ENABLE drops
                                          // (MODE_DELAY, mode bit 2 clear)

    // Write engine (MAME iwm.cpp MODE_WRITE, byte-granular): q7 while
    // enabled = write mode; the data register holds one pending byte the
    // shifter consumes every 8 bit windows (128 cycles at mode $1F).
    bool writing_ = false;
    bool wrPending_ = false;              // handshake bit 7 low = byte pending
    bool wrUnderrun_ = false;             // handshake bit 6 low once starved
    uint8_t wrData_ = 0;
    int wrPhase_ = 0;                     // cycles until the next shifter load
};
