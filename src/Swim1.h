// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Apple SWIM1 (LC II / early SuperDrive Macs) ──
// Two personalities behind one address window (MAME swim1.cpp):
// - IWM-compatible mode at reset — delegated to the proven `Iwm`
//   (state lines, GCR nibble stream, the M5.1 write engine).
// - ISM mode, entered by four IWM mode-register writes whose bit 6
//   follows the 1-0-1-1 magic pattern (swim1.cpp:555-579); left by an
//   ISM mode-clear dropping bit 6. The ISM half is the SWIM2-lineage
//   register file (data/mark/error/param/phases/setup/mode0/mode1,
//   2-deep FIFO, TSS write serializer, serial CRC-CCITT) with SWIM1's
//   16-entry parameter RAM and param-driven write cell timing
//   (P_TIME0/P_TIME1, swim1.cpp:904-916).
// The ISM read side is MAME's REAL engine since § 1.3 flux plan step 4b
// (2026-08-14): the LS-pair cell state machine over inter-transition
// times in half-cycles, the Correction State Machine (64 minimum cells
// calibrate a per-pair-side u8 correction factor scaling every
// threshold; the `|256` fold makes the u8 a window on 192..447), and the
// Trans-Space Machine assembling FIFO bytes (MFM nb/bb tables with the
// missing-clock mark detection; GCR gap→bits shift) — swim1.cpp:885-1233
// verbatim, edges from SonyDrive's flux view. GCR ACTION starts directly
// in CSM_SYNCHRONIZED (swim1.cpp:394), so only MFM calibrates. The
// thresholds live entirely in the 16-byte parameter RAM: with the CSM
// the params are load-bearing, exactly as on silicon. Read error bits
// $20 (cell too long), $40 (consecutive marginal pairs), $08
// (calibration out of range) come with it.
// Gate: tests/swim1_test.cpp (incl. jitter, GCR-ISM and the
// correction-factor P_MULT bite block).

#pragma once
#include "SaveState.h"
#include "Iwm.h"
#include <cstdint>
#include <functional>
#include <vector>

class SonyDrive;

class Swim1 {
public:
    // Every SWIM1 host ticks the chip in the C15M domain; the nested IWM
    // personality doubles its bit windows there (MAME iwm_half_window_size).
    Swim1() { iwm_.setClockHz(15667200); }

    void reset();
    void attachDrive(SonyDrive* internal, SonyDrive* external);

    // A SWIM1 is pin-compatible with the IWM it replaced.  The Plus, SE and
    // original Mac II therefore share this wrapper with their FDHD siblings,
    // but must remain plain-IWM machines: the 1-0-1-1 personality switch is
    // absent and their MFD-51W mechanism is not HD-capable.  This is board
    // configuration, not live state, so the owning memory map reapplies it
    // from its model and it is deliberately not serialized.
    void configureSuperDrive(bool enabled);
    bool superDriveConfigured() const { return superDriveConfigured_; }

    // Bus access: reg = addr bits A9-A12 (IWM state lines; ISM regs & 7)
    uint8_t read(int reg);
    void write(int reg, uint8_t v);
    void tick(int cycles);
    int cyclesToNextEvent() const;

    // The drive's side-select line, as the BOARD drives it — MAME models
    // it on the drive (`floppy_image_device::ss_w`, floppy.h:334) and every
    // Mac host writes it from somewhere different: VIA1 PA5 through the
    // glue on the V8/VASP/RBV/Spike boards (`maclc.cpp:309-318` hdsel_w,
    // `maciivx.cpp:283-292`, `macii.cpp:441`, `macquadra700.cpp:614-625`),
    // and the SWIM's own HDSEL output pin on the IIfx and the Eclipse
    // towers (`maciifx.cpp:430`, `macquadra700.cpp:881`, which is `onHdsel`
    // below). It reaches BOTH personalities: the ISM sense address takes it
    // as bit 3 (`floppy.cpp:3328` `(phases & 7) | (m_actual_ss ? 8 : 0)`)
    // exactly as the IWM's does, so it lives in one place — the IWM's SEL
    // latch — and `hdsel()` reads it back for the ISM half.
    void setSel(bool sel) { iwm_.setSel(sel); }

    // SWIM HDSEL output pin: ISM mode register bit 5 (swim1.cpp:345-346,
    // and :108 at reset). Only the boards that WIRE that pin to the drive
    // subscribe; on the others the pin is a no-connect and the board's own
    // PA5 path owns `setSel` above.
    std::function<void(bool)> onHdsel;

    // DAT1BYTE (swim1.cpp:1226-1238) — the controller's "the ISM FIFO can
    // take/give a byte NOW" line: in write mode it asserts while the 2-deep
    // FIFO has room, in read mode while it is not empty. On the LC II
    // nothing is wired to it (the guest polls the FIFO through the register
    // file), but the Quadra 900/950 route it to BOTH DMA request channels
    // of the SWIM IOP (`macquadra700.cpp:879-880`), which is how the IOP
    // firmware can move a sector without the 65C02 polling for each byte.
    // Level, not edge: re-evaluated on every FIFO movement and mode write.
    std::function<void(bool)> onDat1Byte;

    bool ism() const { return ismMode_; }
    uint8_t ismModeReg() const { return mode_; }
    uint8_t ismSetupReg() const { return setup_; }
    int fifoCount() const { return fifoPos_; }
    // Diagnostic counters for the ISM read engine (2026-09-07, the LC II
    // 1.44 MB mount hunt): what the driver popped, what it saw as errors,
    // and what the engine produced. Not hardware state; never serialized.
    struct IsmStats {
        long dataPops = 0;       // register 0/1 reads
        long emptyPops = 0;      // …of an empty FIFO (underrun as seen by CPU)
        long errorReads = 0;     // register 2 reads returning non-zero
        long overruns = 0;       // engine pushed into a full FIFO
        long marks = 0;          // MFM/GCR mark bytes produced
        long syncs = 0;          // CSM reached SYNCHRONIZED
        long bytes = 0;          // bytes the engine produced
    };
    const IsmStats& ismStats() const { return ismStats_; }
    Iwm& iwm() { return iwm_; }

    // ── Save states (SaveState.h) ───────────────────────────────────────
    // The IWM personality nests in, then the ISM half: mode/setup/phases,
    // the 16-byte parameter RAM, the 2-entry FIFO and the whole bit engine
    // (CRC, shift register, TSS, half-cell position). The in-flight write
    // transition list travels too — a snapshot taken mid-sector must resume
    // writing the same cells or the medium ends up with a torn field.
    template <class Ar> void visit(Ar& ar) {
        ar(iwm_);
        ar(ismMode_, iwmToIsm_, driveSel_, lstrb_,
           mode_, setup_, phases_, params_, paramIdx_,
           fifo_, fifoPos_, error_);
        // The ISM read engine is live machine state (edge clock,
        // calibration counters, correction factors, pair phase, TSM
        // assembly) — none of it re-derivable. Snapshot format v7.
        ar(ismClock_, lastSync_, latestEdge_, prevLs_, csmState_,
           csmErr_, csmPairSide_, csmMinCount_, correction_,
           tsmOut_, tsmBits_, tsmMark_);
        ar(crc_, sr_, tssSr_, tssOutput_, currentBit_,
           halfWait_, writeHalfPos_, writeStartTick_, writeActive_,
           writeTransitions_);
    }

private:
    IsmStats ismStats_;
    // FIFO entry tags — MAME swim1.h M_MARK/M_CRC/M_CRC0
    enum : uint16_t { MARK = 0x100, CRC = 0x200, CRC0 = 0x400 };
    // 16-entry parameter RAM indices (swim1.h:67-70)
    enum { P_MINCT, P_MULT, P_SSL, P_SSS, P_SLL, P_SLS, P_RPT, P_CSLS,
           P_LSL, P_LSS, P_LLL, P_LLS, P_LATE, P_TIME0, P_EARLY, P_TIME1 };
    // Correction State Machine states (swim1.h:73-79)
    enum { CsmInit, CsmCountMin, CsmWaitNonMin, CsmCheckMark,
           CsmSynchronized };

    void updateDat1Byte();
    bool fifoPush(uint16_t value);
    uint16_t fifoPop();
    void fifoClear();
    void crcClear() { crc_ = 0xCDB4; }
    void crcUpdate(int bit) {
        if ((crc_ ^ (bit ? 0x8000 : 0x0000)) & 0x8000)
            crc_ = uint16_t((crc_ << 1) ^ 0x1021);
        else
            crc_ = uint16_t(crc_ << 1);
    }

    uint8_t ismRead(int reg);
    void ismWrite(int reg, uint8_t v);
    // 1-0-1-1 bit-6 pattern on offset 0xf; runs on EVERY IWM access —
    // anything that is not the expected next step resets the counter.
    void iwmModeWatch(int reg, uint8_t v);
    void enterIsm();
    void leaveIsm();

    int cellCycles() const;
    void tickRead(int cycles);
    void tickWrite(int cycles);
    void startWrite();
    void finishWrite();

    int senseAddr() const;
    void applyPhases(uint8_t value);
    void updateDevsel();
    SonyDrive* selectedDrive() const;
    // The head the ISM half reads/writes and the top bit of its sense
    // address: the drive's side-select line, not the mode register.
    bool hdsel() const { return iwm_.sel(); }

    Iwm iwm_;                                    // IWM personality
    SonyDrive* drive_[2] = { nullptr, nullptr };
    bool superDriveConfigured_ = true;            // board wiring, not state
    bool ismMode_ = false;
    int iwmToIsm_ = 0;                           // magic-pattern counter

    int driveSel_ = 0;
    bool lstrb_ = false;
    uint8_t mode_ = 0x40, setup_ = 0, phases_ = 0;
    uint8_t params_[16] = {};
    uint8_t paramIdx_ = 0;
    uint16_t fifo_[2] = {};
    uint8_t fifoPos_ = 0;
    uint8_t error_ = 0;

    // ── ISM read engine — MAME swim1.cpp:885-1233, in HALF-CYCLES of the
    // controller clock (time_to_cycles = 2×clock, swim1.cpp:624-632).
    // Edges come from SonyDrive's flux view (FluxPll ticks → halves).
    // The IWM personality keeps its own byte-granular path (§ 1.3).
    int64_t ismClock_ = 0;               // halves granted so far (next_sync)
    int64_t lastSync_ = 0;               // m_last_sync
    int64_t latestEdge_ = 0;             // m_ism_latest_edge
    uint8_t prevLs_ = 0x5;               // m_ism_prev_ls, (1<<2)|1
    uint8_t csmState_ = CsmInit;
    uint32_t csmErr_[2] = {};            // m_ism_csm_error_counter
    uint8_t csmPairSide_ = 0;
    uint8_t csmMinCount_ = 0;
    uint8_t correction_[2] = {};         // u8 on purpose — the |256 window
    uint8_t tsmOut_ = 0;                 // m_ism_tsm_out
    uint8_t tsmBits_ = 0;
    bool tsmMark_ = false;

    // Bit-engine state shared with the TSS write serializer
    uint16_t crc_ = 0xCDB4;
    uint16_t sr_ = 0;
    uint8_t tssSr_ = 0;
    uint8_t tssOutput_ = 0;
    int currentBit_ = -1;
    uint32_t halfWait_ = 0;

    uint64_t writeHalfPos_ = 0;
    int64_t writeStartTick_ = 0;
    bool writeActive_ = false;
    std::vector<uint64_t> writeTransitions_;
};
