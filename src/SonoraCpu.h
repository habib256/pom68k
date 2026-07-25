// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── 68030 CPU (Moira wrapper, Mac LC III) ──
// The Cpu030 (LC II) pattern on the Sonora bus at 25 MHz: functional
// accuracy, PMMU for the 24-bit mode, external /BERR through
// Moira::extBusError (the SCSI pseudo-DMA timeout). Keeps the Cpu030
// i-cache throughput overlay (cacheBoost ceiling + per-miss charge,
// rationale in Cpu030.h) — the real LC III leans on its 030 caches just
// as hard. The 68882 socket is empty on a stock LC III (maclc3.cpp:338
// set_fpu_enable(false)); pass withFpu to populate it, as the target
// System 7.5 images expect (the LC II precedent).
// Gate: tests/lc3_boot_etalon.cpp.

#pragma once
#include "Moira.h"
#include <cstdint>

class SonoraMemory;

class SonoraCpu : public moira::Moira {
public:
    explicit SonoraCpu(SonoraMemory& mem, bool withFpu = false);

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

    SonoraMemory& mem_;
    moira::i64 lastPeriphClock_ = 0;

    // Cpu030's i-cache throughput model (knobs + rationale there).
    int cacheBoost_ = 4;
    int icacheMiss_ = 4;
    moira::i64 periphAccum_ = 0;
    static constexpr moira::i64 kPeriphBatch = 128;
};
