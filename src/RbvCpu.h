// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── 68030 CPU (Moira wrapper, Mac IIsi) ──
// The SonoraCpu pattern on the RBV bus at 20 MHz: functional accuracy,
// PMMU for the 24-bit mode, external /BERR through Moira::extBusError
// (the SCSI pseudo-DMA timeout), the Cpu030 i-cache throughput overlay.
// The IIsi socket takes an optional 68882 on the adapter card (MAME
// maciici.cpp:514 defaults to "No FPU"); pass withFpu to populate it,
// as the target System 7.5 images expect (the LC II precedent).
// Gate: tests/iisi_boot_etalon.cpp.

#pragma once
#include "Moira.h"
#include <cstdint>

class RbvMemory;

class RbvCpu : public moira::Moira {
public:
    explicit RbvCpu(RbvMemory& mem, bool withFpu = false);

    void hardReset();
    void runCycles(moira::i64 n);
    void runUntil(moira::i64 clockTarget);
    void updateIpl();
    void stall(int cycles);
    void flushTicks();

private:
    moira::u8  read8(moira::u32 addr) const override;
    moira::u16 read16(moira::u32 addr) const override;
    void write8(moira::u32 addr, moira::u8 v) const override;
    void write16(moira::u32 addr, moira::u16 v) const override;
    void sync(int cycles) override;
    void didChangeCACR(moira::u32 value) override;
    void catchUp();

    RbvMemory& mem_;
    moira::i64 lastPeriphClock_ = 0;

    // Cpu030's i-cache throughput model (knobs + rationale there).
    int cacheBoost_ = 4;
    int icacheMiss_ = 4;
    moira::i64 periphAccum_ = 0;
    static constexpr moira::i64 kPeriphBatch = 128;
};
