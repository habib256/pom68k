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

    // Diagnostic write tap (null = off, zero cost). offset is masked to the
    // $F14000 window (0..0xFFF): < 0x400 = FIFO A byte, 0x800+ = registers.
    // Used to check whether an app actually feeds the ASC (SC2K silence,
    // TODO § App-compat). Not part of the hardware model.
    std::function<void(uint32_t, uint8_t)> onWrite;
    std::function<void(uint32_t, uint8_t)> onRead;   // diagnostic read tap

    // Audio host pull (miniaudio callback side)
    int available() const { return int((outWr_ - outRd_) & (kOutSize - 1)); }
    int16_t pop() {
        if (outRd_ == outWr_) return 0;
        return out_[outRd_++ & (kOutSize - 1)];
    }
    int fifoCap() const { return cap_; }

private:
    uint8_t readReg(uint32_t offset);        // read logic (onRead tap wraps it)
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

// Audio cell copied into IOSB/PrimeTime (LC 475 / Quadra 605). Despite
// MAME's historical ASC_EASC wiring in iosb.cpp, ASCTester identifies this
// as the distinct $BB IOSB variant: two 1 KB FIFOs drained as stereo at
// 22.257 kHz, with writable FIFO interrupt enables.
class AscIosb {
public:
    static constexpr int kSampleRate = 22257;
    static constexpr int64_t kCpuHz = 15667200;

    void reset();
    uint8_t read(uint32_t offset);
    void write(uint32_t offset, uint8_t v);
    void tick(int cpuCycles);

    bool irqAsserted() const { return irq_; }
    std::function<void(bool)> onIrq;
    std::function<void(uint32_t, uint8_t)> onWrite;
    std::function<void(uint32_t, uint8_t)> onRead;

    int available() const { return int((outWr_ - outRd_) & (kOutSize - 1)); }
    bool popStereo(int16_t& left, int16_t& right) {
        if (outRd_ == outWr_) { left = right = 0; return false; }
        uint32_t i = outRd_++ & (kOutSize - 1);
        left = outL_[i]; right = outR_[i];
        return true;
    }
    int fifoCap(int channel) const { return cap_[channel & 1]; }

private:
    enum : uint8_t {
        STAT_HALF_A = 0x01, STAT_EMPTY_OR_FULL_A = 0x02,
        STAT_HALF_B = 0x04, STAT_EMPTY_OR_FULL_B = 0x08
    };
    void setIrq(bool s) {
        if (s != irq_) { irq_ = s; if (onIrq) onIrq(s); }
    }
    void clearFifos();
    void push(int channel, uint8_t v);

    uint8_t fifo_[2][0x400] = {};
    uint16_t rd_[2] = {}, wr_[2] = {};
    int cap_[2] = {};
    uint8_t mode_ = 1, fifoStat_ = 0x0E, playRec_ = 0;
    uint8_t fifoIrqEn_[2] = { 1, 1 };         // bit 0: 0 = enabled
    bool irq_ = false;
    int8_t lastL_ = 0, lastR_ = 0;
    int64_t drainAcc_ = 0;

    static constexpr int kOutSize = 8192;
    int16_t outL_[kOutSize] = {}, outR_[kOutSize] = {};
    uint32_t outRd_ = 0, outWr_ = 0;
};
