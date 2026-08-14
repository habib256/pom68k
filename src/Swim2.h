// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Apple SWIM2 register/FIFO + SuperDrive media engine (PrimeTime/IOSB).
// Register model AND bit engine: MAME swim2.cpp — the MFM sync-hunting
// shifter (swim2.cpp:498-546), serial CRC-CCITT seeded $CDB4 with the
// M_CRC0 tag (swim2.cpp:343-355, 535-543), and the TSS write serializer
// in half-cycles (swim2.cpp:402-481). Since the § 1.3 flux-plan step 3
// (2026-08-14) the read side runs a real data separator: a FluxPll
// (MAME fdc_pll_t) windowing SonyDrive's flux view, nominal period from
// setup[3:2] (cycles_per_cell {16,31,31,63}, swim2.cpp:329-331) — not one
// pre-aligned cell per fixed window. Drive CA protocol: applefdintf
// phases_w → mac_floppy seek_phase_w; mode/devsel/hdsel from
// swim2.cpp:128-137, 300-303.
// Gate: tests/swim2_test.cpp, tests/swim2_media_test.cpp (incl. the
// 12 %-jitter and off-rate separator blocks).
//
// KNOWN MAME DIVERGENCE, deliberately kept (parity audit § 2.4, cosmetic):
// **no DAT1BYTE line.** MAME's swim2 has one (update_dat1byte, swim2.cpp:
// 553-565, driven from :283/:289/:370/:379/:390 — assert while the 2-deep
// FIFO has room in write mode, or holds a byte in read mode), and so does
// ours on Swim1 (`onDat1Byte`, wired for the IIfx/Eclipse IOPs). It stays
// unmodelled here because **no consumer exists on either side**: the three
// MAME devices that instantiate a swim2 — v8.cpp, sonora.cpp, iosb.cpp —
// never call dat1byte_cb(), and the only two wirings in the whole tree are
// swim1 consumers (maciifx.cpp:486, macquadra700.cpp:879-880). Same split
// here: the two POM68K platforms that own an Apple PIC IOP — IIfxMemory and
// Q700Memory (Quadra 700/900/950) — both instantiate a `Swim1`, and so do
// RbvMemory and VaspMemory; the `Swim2` boards (Sonora, MEMCjr/Q605, djMEMC/
// Centris, F108/Q630, the V8 side that uses one) have no DMA engine to feed.
// Adding it would mean new serialized state driving a callback nobody sets.
// Reopen the moment a swim2 machine grows an IOP or a DMA client: the
// implementation is a one-liner copy of Swim1::updateDat1Byte, called from
// fifoPush/fifoPop/fifoClear and the mode-register writes.

#pragma once
#include "SaveState.h"
#include "FluxPll.h"
#include <cstdint>
#include <vector>

class SonyDrive;

class Swim2 {
public:
    void reset();
    void attachDrive(SonyDrive* internal, SonyDrive* external);

    uint8_t read(int reg);
    void write(int reg, uint8_t value);
    void tick(int controllerCycles);
    int cyclesToNextEvent() const;

    uint8_t mode() const { return mode_; }
    uint8_t setup() const { return setup_; }
    uint8_t phases() const { return phases_; }
    int fifoCount() const { return fifoPos_; }

    SonyDrive* selectedDrive() const;
    bool isWriteProtected() const;

    // ── Save states (SaveState.h) ───────────────────────────────────────
    // Same shape as Swim1's ISM half (registers, 4-byte parameter RAM,
    // 2-entry FIFO, serial CRC/TSS bit engine, in-flight write transitions);
    // `drive_[2]` are machine-owned pointers, re-attached on restore.
    template <class Ar> void visit(Ar& ar) {
        ar(driveSel_, lstrb_, mode_, setup_, phases_, params_, paramIdx_,
           fifo_, fifoPos_, error_);
        // The data separator is live machine state, not a derivable cache:
        // a snapshot taken mid-sector must resume with the same window
        // phase and the same pulled period (snapshot format v6).
        ar(pll_, fluxClock_);
        ar(crc_, sr_, tssSr_, tssOutput_, mfmSyncCounter_, currentBit_,
           halfWait_, writeHalfPos_, writeStartCell_, writeActive_,
           writeTransitions_);
    }

private:
    // FIFO entry tags — MAME swim2.h:41-45 (M_MARK/M_CRC/M_CRC0)
    enum : uint16_t { MARK = 0x100, CRC = 0x200, CRC0 = 0x400 };
    bool fifoPush(uint16_t value);
    uint16_t fifoPop();
    void fifoClear();

    // Serial CRC-CCITT engine (swim2.cpp:343-355). $CDB4 is the CCITT
    // state after the three A1 sync marks, re-seeded on every mark byte.
    void crcClear() { crc_ = 0xCDB4; }
    void crcUpdate(int bit) {
        if ((crc_ ^ (bit ? 0x8000 : 0x0000)) & 0x8000)
            crc_ = uint16_t((crc_ << 1) ^ 0x1021);
        else
            crc_ = uint16_t(crc_ << 1);
    }

    int cellCycles() const;                  // setup[3:2] → clocks per cell
    void armReadPll();                       // read-ACTION entry: land + lock
    void tickRead(int cycles);
    void tickWrite(int cycles);
    void startWrite();
    void finishWrite();

    int senseAddr() const;
    void applyPhases(uint8_t value);
    void updateDevsel();
    bool side1() const { return (mode_ & 0x20) != 0; }   // HDSEL
    bool gcrRead() const { return (setup_ & 0x04) != 0; }

    SonyDrive* drive_[2] = { nullptr, nullptr };
    int driveSel_ = 0;                   // 0 = A (internal), 1 = B
    bool lstrb_ = false;

    uint8_t mode_ = 0x40, setup_ = 0, phases_ = 0;
    uint8_t params_[4] = {};
    uint8_t paramIdx_ = 0;
    uint16_t fifo_[2] = {};
    uint8_t fifoPos_ = 0;
    uint8_t error_ = 0;

    // Read pacing: the FluxPll data separator over SonyDrive's flux view.
    // `fluxClock_` is the absolute flux time granted to the reader so far
    // (1 controller cycle = FluxPll::kSubCell ticks) — the PLL closes
    // windows up to it and stalls with -1 beyond, exactly MAME's `limit`.
    FluxPll pll_;
    int64_t fluxClock_ = 0;

    // Bit-engine state (MAME m_sr/m_crc/m_tss_*/m_mfm_sync_counter)
    uint16_t crc_ = 0xCDB4;
    uint16_t sr_ = 0;
    uint8_t tssSr_ = 0;
    uint8_t tssOutput_ = 0;
    int mfmSyncCounter_ = 0;
    int currentBit_ = -1;                // -1 = engine idle (MAME 0xff)
    uint32_t halfWait_ = 0;              // m_half_cycles_before_change

    // Write capture: absolute half-cycles since write start, one entry
    // per flux transition (MAME m_flux_write → floppy write_flux).
    uint64_t writeHalfPos_ = 0;
    int64_t writeStartCell_ = 0;
    bool writeActive_ = false;
    std::vector<uint64_t> writeTransitions_;
};
