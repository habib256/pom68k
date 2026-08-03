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
// Cells come from SonyDrive's raw cell track like Swim2; MAME's
// LS-pair cell state machine + correction factors (swim1.cpp:965-1140)
// discriminate real-world flux jitter, which our ideal discrete cells
// do not have — the read engine therefore reduces to the SWIM2 shifter
// (accepted simplification, LLE_VS_HLE §3). Gate: tests/swim1_test.cpp,
// tests/swim1_media_test.cpp.

#pragma once
#include "SaveState.h"
#include "Iwm.h"
#include <cstdint>
#include <functional>
#include <vector>

class SonyDrive;

class Swim1 {
public:
    void reset();
    void attachDrive(SonyDrive* internal, SonyDrive* external);

    // Bus access: reg = addr bits A9-A12 (IWM state lines; ISM regs & 7)
    uint8_t read(int reg);
    void write(int reg, uint8_t v);
    void tick(int cycles);

    // VIA PA5 → IWM-personality SEL (ISM ignores it: HDSEL = mode bit 5)
    void setSel(bool sel) { iwm_.setSel(sel); }

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
    int fifoCount() const { return fifoPos_; }
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
           fifo_, fifoPos_, error_, cellPhase_);
        ar(crc_, sr_, tssSr_, tssOutput_, mfmSyncCounter_, currentBit_,
           halfWait_, writeHalfPos_, writeStartCell_, writeActive_,
           writeTransitions_);
    }

private:
    // FIFO entry tags — MAME swim1.h M_MARK/M_CRC/M_CRC0
    enum : uint16_t { MARK = 0x100, CRC = 0x200, CRC0 = 0x400 };
    // 16-entry parameter RAM indices (swim1.h:67-70)
    enum { P_MINCT, P_MULT, P_SSL, P_SSS, P_SLL, P_SLS, P_RPT, P_CSLS,
           P_LSL, P_LSS, P_LLL, P_LLS, P_LATE, P_TIME0, P_EARLY, P_TIME1 };

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
    void iwmModeWatch(uint8_t v);                // 1-0-1-1 bit-6 pattern
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
    bool side1() const { return (mode_ & 0x20) != 0; }   // ISM HDSEL

    Iwm iwm_;                                    // IWM personality
    SonyDrive* drive_[2] = { nullptr, nullptr };
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
    int cellPhase_ = 0;

    // Bit-engine state (same shape as Swim2 — swim1.cpp ism_sync)
    uint16_t crc_ = 0xCDB4;
    uint16_t sr_ = 0;
    uint8_t tssSr_ = 0;
    uint8_t tssOutput_ = 0;
    int mfmSyncCounter_ = 0;
    int currentBit_ = -1;
    uint32_t halfWait_ = 0;

    uint64_t writeHalfPos_ = 0;
    int64_t writeStartCell_ = 0;
    bool writeActive_ = false;
    std::vector<uint64_t> writeTransitions_;
};
