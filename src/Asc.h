// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── ASC, V8 variant (LC II sound) ──
// The Apple Sound Chip block inside the V8 at $F14000: version $E8,
// mode forced to 1 (FIFO), MONO — FIFO A only (1 KB), writes to FIFO B
// ignored, wavetable/clock/control registers read as constants. Sample
// rate fixed 22 257 Hz. FIFO status ($804): bit 0 = A half-empty
// (< $200 bytes — asserts the IRQ, LEVEL-triggered on the pseudo-VIA),
// bit 1 = A empty/full. Reading $804 clears the IRQ only when not still
// half-empty. The DFAC output stage is a unit-gain pass-through.
// Source of truth: MAME asc.cpp asc_v8_device (master 2026-07-15,
// :755-903 + base :369-431), hardware-tested (ASCTester dumps in-file);
// pinned in docs/LCII_HARDWARE.md § Sound.
// Boot dependency: the ROM's beep code fills FIFO A until status bit 1
// (full) sets — traced at $A45F26-$A45F34.
// Gate: tests/asc_test.cpp.

#pragma once
#include <cstdint>
#include <functional>

class AscV8 {
public:
    static constexpr int kSampleRate = 22257;
    static constexpr int64_t kCpuHz = 15667200;

    void reset();

    // Bus access, offset = byte offset inside the $F14000-$F15FFF window
    uint8_t read(uint32_t offset);
    void write(uint32_t offset, uint8_t v);

    // Drain FIFO A at the fixed sample rate; produced samples land in a
    // small ring for the audio host (dropped when nobody consumes).
    void tick(int cpuCycles);

    bool irqAsserted() const { return irq_; }
    std::function<void(bool)> onIrq;         // level → pseudo-VIA IFR bit 4

    // Audio host pull (miniaudio callback side)
    int available() const { return int((outWr_ - outRd_) & (kOutSize - 1)); }
    int16_t pop() {
        if (outRd_ == outWr_) return 0;
        return out_[outRd_++ & (kOutSize - 1)];
    }
    int fifoCap() const { return cap_; }

private:
    enum { STAT_HALF_A = 0x01, STAT_EMPTY_OR_FULL_A = 0x02 };
    void setIrq(bool s) {
        if (s != irq_) { irq_ = s; if (onIrq) onIrq(s); }
    }

    uint8_t fifo_[0x400] = {};
    uint16_t rd_ = 0, wr_ = 0;
    int cap_ = 0;
    uint8_t regs_[0x20] = {};                // sparse classic regs ($800+)
    uint8_t fifoStat_ = STAT_EMPTY_OR_FULL_A;
    bool irq_ = false;
    int64_t drainAcc_ = 0;

    static constexpr int kOutSize = 8192;    // power of two
    int16_t out_[kOutSize] = {};
    uint32_t outRd_ = 0, outWr_ = 0;
};
