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
#include <algorithm>
#include <cstdint>
#include <functional>

class AscV8 {
public:
    static constexpr int kSampleRate = 22257;
    static constexpr int64_t kCpuHz = 15667200;

    // Mac II discrete ASC reports version $00; V8 (LC II) reports $E8.
    explicit AscV8(uint8_t version = 0xE8) : version_(version) {}

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

    // $807 CLOCK RATE (asc.cpp:30 — "0 = Mac 22257 Hz, 1 = undefined,
    // 2 = 22050 Hz, 3 = 44100 Hz"). MAME documents the register and does
    // NOT implement it, so the manual is the reference here, not the
    // oracle. Only the classic (Mac II discrete) ASC accepts a write to it
    // — on the V8/Sonora/IOSB integrations the register is read-only and
    // reads back 0, which is exactly why honouring it costs nothing on
    // every machine that boots today. Code 1 is undefined: keep the Mac
    // rate rather than invent one.
    //
    // Host caveat, deliberate: the output ring is consumed by a fixed-rate
    // host DAC, so a guest that actually programmed 44.1 kHz would get its
    // FIFO IRQs at the right cadence but the emulator would pace to half
    // speed. Resampling is out of scope; no known guest writes this.
    int drainHz() const {
        if (!classic()) return kSampleRate;
        switch (regs_[0x07] & 3) {
        case 2:  return 22050;
        case 3:  return 44100;
        default: return kSampleRate;
        }
    }

    int fifoCap() const { return cap_; }
    int fifoCapB() const { return capB_; }

    // ── Save states (SaveState.h) ───────────────────────────────────────
    // Both FIFOs with their pointers and fill levels, the register block and
    // the drain accumulator — the Sound Manager polls the half/empty status
    // bits, so a restore that changed the fill level would confuse it.
    //
    // NOT carried: the `out_` ring and its pointers. That is the HOST audio
    // output queue (a few hundred ms of already-produced samples), not
    // something the guest can observe, and it would add 16 KB to every
    // snapshot. A restore drops the queued audio instead — the audible cost
    // is one gap, the alternative is a bigger file and no fidelity gain.
    // `version_` is board identity, set at construction.
    template <class Ar> void visit(Ar& ar) {
        ar(fifo_, fifoB_, rd_, wr_, rdB_, wrB_, cap_, capB_,
           regs_, fifoStat_, irq_, drainAcc_, emptyCycleSamples_);
        if constexpr (Ar::loading) outRd_ = outWr_ = 0;
    }

private:
    uint8_t readReg(uint32_t offset);        // read logic (onRead tap wraps it)
    enum {
        STAT_HALF_A = 0x01, STAT_EMPTY_OR_FULL_A = 0x02,
        STAT_HALF_B = 0x04, STAT_EMPTY_OR_FULL_B = 0x08
    };
    void setIrq(bool s) {
        if (s != irq_) { irq_ = s; if (onIrq) onIrq(s); }
    }
    bool classic() const { return version_ != 0xE8; }
    void classicResetFifos();

    uint8_t fifo_[0x400] = {};
    uint8_t fifoB_[0x400] = {};              // classic ASC channel B
    uint16_t rd_ = 0, wr_ = 0;
    uint16_t rdB_ = 0, wrB_ = 0;
    int cap_ = 0;
    int capB_ = 0;
    // Audit § 2.8(b), DOCUMENT-SKIP — register-file WIDTH. MAME's
    // asc_base_device carries one flat `u8 m_regs[0x800]` (sound/asc.h:100)
    // covering the whole $800-$FFF window: every byte written there is stored
    // and reads back, and reads only fall to $FF past $1000
    // (asc.cpp:359-365). We keep sparse blocks instead — $20 here, $40 +
    // $100 ($F00-$FFF) on Sonora/EASC, none on IOSB — and unmodelled offsets
    // read 0. Not aligned, on three counts: (1) no producer/consumer pair
    // exists — a guest reading back a byte it wrote into dead register space
    // would be reading a value the chip's logic never touches, and none does;
    // (2) `regs_`/`xtraRegs_` are in visit<Ar>(), so widening them changes
    // the save-state chunk layout and invalidates every existing .pomss for
    // eleven machine families — a real cost against a zero-benefit parity
    // gain; (3) a flat backing store would have to carry explicit exceptions
    // for the two hardware-pinned read-back quirks below ($F09/$F29 = 0 on
    // V8, $F0E/$F2E = $2C on IOSB), i.e. it would put the pins at risk to buy
    // nothing. The *wavetable* register loss ($810-$82F, MAME rebuilds
    // phase/incr from live copies at asc.cpp:344-357) is a separate, larger
    // item — the wavetable-mode stub, inventoried in docs/LLE_VS_HLE.md — and
    // is not what this note excuses. Reopening condition: the wavetable
    // engine lands, or a guest is caught reading back register space.
    uint8_t regs_[0x20] = {};                // sparse classic regs ($800+)
    uint8_t fifoStat_ = STAT_EMPTY_OR_FULL_A;
    bool irq_ = false;
    int64_t drainAcc_ = 0;
    // Classic ASC: Mac OS leaves FIFO mode on after playback and expects a
    // fresh empty/half IRQ once per 1 KB drain cycle (QEMU asc.c).
    int emptyCycleSamples_ = 0;
    uint8_t version_ = 0xE8;

    static constexpr int kOutSize = 8192;    // power of two
    int16_t out_[kOutSize] = {};
    uint32_t outRd_ = 0, outWr_ = 0;
};

// Sonora/Spice EASC variant ($BC) — the ASC block shared by the Color
// Classic (Spice, MAME v8.cpp:717 ASC_SONORA replace) and the LC III
// (Sonora). Hardware-pinned by ASCTester on a real LC III (MAME
// asc.cpp:916-1035): stereo FIFO pair (A = left, B = right) drained at
// 22 257 Hz, with the COMBINED stereo status folded onto the B bits —
// bit 2 = either FIFO below half, bit 3 = either FIFO empty — while
// playback mode ($80A bit 0 clear) reports FIFO A "empty" (bit 1)
// every sample. 804 idle = $0E, IRQ storm at idle with the writable
// per-FIFO enables ($809/$829) at their reset-0 (= enabled) state.
class AscSonora {
public:
    static constexpr int kSampleRate = 22257;

    // cpuHz: the Spice ASC ticks at C15M (= the Color Classic CPU clock);
    // the LC III passes Sonora's 25 MHz CPU clock.
    explicit AscSonora(int64_t cpuHz = 15667200) : cpuHz_(cpuHz) {}

    void reset();
    uint8_t read(uint32_t offset);
    void write(uint32_t offset, uint8_t v);
    void tick(int cpuCycles);

    bool irqAsserted() const { return irq_; }
    std::function<void(bool)> onIrq;
    std::function<void(uint32_t, uint8_t)> onWrite;  // diagnostic taps
    std::function<void(uint32_t, uint8_t)> onRead;

    // Audio host pull — mono L+R mix so the V8-family host path (LC II
    // LcMachine::drain) works unchanged; popStereo for stereo hosts.
    int available() const { return int((outWr_ - outRd_) & (kOutSize - 1)); }
    int16_t pop() {
        if (outRd_ == outWr_) return 0;
        uint32_t i = outRd_++ & (kOutSize - 1);
        return int16_t((int(outL_[i]) + int(outR_[i])) / 2);
    }
    bool popStereo(int16_t& left, int16_t& right) {
        if (outRd_ == outWr_) { left = right = 0; return false; }
        uint32_t i = outRd_++ & (kOutSize - 1);
        left = outL_[i]; right = outR_[i];
        return true;
    }
    int fifoCap(int channel) const { return cap_[channel & 1]; }

    // ── Save states (SaveState.h) ───────────────────────────────────────
    // Stereo FIFO pair, register blocks and drain accumulator. As in AscV8,
    // the host output ring is dropped rather than carried.
    template <class Ar> void visit(Ar& ar) {
        ar(fifo_, rd_, wr_, cap_, regs_, xtraRegs_,
           fifoStat_, fifoIrqEn_, irq_, drainAcc_);
        if constexpr (Ar::loading) outRd_ = outWr_ = 0;
    }

private:
    enum : uint8_t {
        STAT_HALF_A = 0x01, STAT_EMPTY_OR_FULL_A = 0x02,
        STAT_HALF_B = 0x04, STAT_EMPTY_OR_FULL_B = 0x08
    };
    void setIrq(bool s) {
        if (s != irq_) { irq_ = s; if (onIrq) onIrq(s); }
    }
    void clearFifos() {
        rd_[0] = rd_[1] = wr_[0] = wr_[1] = 0;
        cap_[0] = cap_[1] = 0;
    }

    const int64_t cpuHz_;
    uint8_t fifo_[2][0x400] = {};
    uint16_t rd_[2] = {}, wr_[2] = {};
    int cap_[2] = {};
    uint8_t regs_[0x40] = {};                // sparse $800-$83F block
    uint8_t xtraRegs_[0x100] = {};           // $F00-$FFF CD-XA/rate block
    uint8_t fifoStat_ = STAT_EMPTY_OR_FULL_A | STAT_EMPTY_OR_FULL_B;  // A&B empty
    uint8_t fifoIrqEn_[2] = { 0, 0 };        // bit 0: 0 = enabled (reset)
    bool irq_ = false;
    int64_t drainAcc_ = 0;

    static constexpr int kOutSize = 8192;
    int16_t outL_[kOutSize] = {}, outR_[kOutSize] = {};
    uint32_t outRd_ = 0, outWr_ = 0;
};

// Discrete EASC 343S1063 — the Enhanced ASC on the Quadra 700 and the
// Eclipse towers (Quadra 900/950). Distinct from the Sonora/IOSB cells:
// version $B0, output clock 44.1 kHz (22.5792 MHz XTAL / 512), a per-channel
// 16.16 sample-rate converter (SRC) and CD-XA ADPCM decoder on BOTH FIFOs,
// and FIFO IRQ enables that reset to 1 = DISABLED (Sonora resets 0 =
// enabled). Source of truth: MAME master asc_easc_device (sound/asc.cpp:
// 1419-1771), hardware-pinned by the in-file ASCTester dump from a real
// Quadra 700 (asc.cpp:1428-1456: version $B0, 804Idle $0F, F09/F29 = $01);
// hookup macquadra700.cpp:805 (ASC_EASC @ 22.5792 MHz).
class AscEasc {
public:
    // asc.cpp:1739 — device_start sets m_sample_rate = 44100.
    static constexpr int kSampleRate = 44100;

    explicit AscEasc(int64_t cpuHz = 25000000) : cpuHz_(cpuHz) {}

    void reset();
    uint8_t read(uint32_t offset);
    void write(uint32_t offset, uint8_t v);
    void tick(int cpuCycles);
    int cyclesToNextEvent() const {
        const int64_t need = cpuHz_ - drainAcc_;
        return int(std::max<int64_t>(1, (need + kSampleRate - 1) / kSampleRate));
    }

    bool irqAsserted() const { return irq_; }
    std::function<void(bool)> onIrq;
    std::function<void(uint32_t, uint8_t)> onWrite;  // diagnostic taps
    std::function<void(uint32_t, uint8_t)> onRead;

    // Audio host pull. The chip drains its FIFOs at 44.1 kHz — that rate is
    // guest-visible (FIFO IRQ cadence) and must not bend — but the host DAC
    // side of POM68K runs at the Mac rate, so the output ring keeps every
    // SECOND sample: 22 050 Hz, the same "no resampler" stance documented
    // at AscV8::drainHz.
    int available() const { return int((outWr_ - outRd_) & (kOutSize - 1)); }
    int16_t pop() {
        if (outRd_ == outWr_) return 0;
        uint32_t i = outRd_++ & (kOutSize - 1);
        return int16_t((int(outL_[i]) + int(outR_[i])) / 2);
    }
    bool popStereo(int16_t& left, int16_t& right) {
        if (outRd_ == outWr_) { left = right = 0; return false; }
        uint32_t i = outRd_++ & (kOutSize - 1);
        left = outL_[i]; right = outR_[i];
        return true;
    }
    int fifoCap(int channel) const { return cap_[channel & 1]; }

    // ── Save states (SaveState.h) ───────────────────────────────────────
    // FIFO pair + register blocks + the SRC phase and CD-XA predictor state
    // (both feed samples the guest hears and pace the FIFO drain). As in the
    // other flavours the host output ring is dropped on restore.
    template <class Ar> void visit(Ar& ar) {
        ar(fifo_, rd_, wr_, cap_, regs_, xtraRegs_, fifoStat_, fifoIrqEn_,
           irq_, srcStep_, srcAccum_, xaS0_, xaS1_, xaParam_, xaPos_,
           xaByte_, xaSubpos_, lastL_, lastR_, drainAcc_, outPhase_);
        if constexpr (Ar::loading) outRd_ = outWr_ = 0;
    }

private:
    enum : uint8_t {
        STAT_HALF_A = 0x01, STAT_EMPTY_OR_FULL_A = 0x02,
        STAT_HALF_B = 0x04, STAT_EMPTY_OR_FULL_B = 0x08
    };
    void setIrq(bool s) {
        if (s != irq_) { irq_ = s; if (onIrq) onIrq(s); }
    }
    void clearFifos() {
        rd_[0] = rd_[1] = wr_[0] = wr_[1] = 0;
        cap_[0] = cap_[1] = 0;
    }
    void pushFifo(int channel, uint8_t v);
    uint8_t popFifo(int channel);            // asc.cpp:1537-1587
    int16_t decodeCdxa(int channel, uint8_t mode);   // asc.cpp:1589-1648

    const int64_t cpuHz_;
    uint8_t fifo_[2][0x400] = {};
    uint16_t rd_[2] = {}, wr_[2] = {};
    int cap_[2] = {};
    uint8_t regs_[0x40] = {};                // sparse $800-$83F block
    uint8_t xtraRegs_[0x100] = {};           // $F00-$FFF EASC block:
                                             // WRPTR/RDPTR/SRC/VOL/CTRL/CD-XA
    uint8_t fifoStat_ = 0;                   // base device_reset zeroes it;
                                             // ASCTester's $0F idle emerges
                                             // from the running FIFO drain
    uint8_t fifoIrqEn_[2] = { 1, 1 };        // bit 0: 1 = disabled (EASC reset)
    bool irq_ = false;
    uint32_t srcStep_[2] = {}, srcAccum_[2] = {};   // 16.16 SRC phase
    // CD-XA ADPCM predictor, per channel (asc.h:209-224)
    int16_t xaS0_[2] = {}, xaS1_[2] = {};    // two previous decoded samples
    uint8_t xaParam_[2] = {};                // block header (filter/shift)
    int32_t xaPos_[2] = {};                  // 0..27 within a block
    uint8_t xaByte_[2] = {};                 // packed byte (4:1 / 8:1 modes)
    int32_t xaSubpos_[2] = {};               // sub-sample within that byte
    int16_t lastL_ = 0, lastR_ = 0;          // 16-bit: CD-XA output is s16
    int64_t drainAcc_ = 0;
    uint8_t outPhase_ = 0;                   // 2:1 host decimation phase

    static constexpr int kOutSize = 8192;
    int16_t outL_[kOutSize] = {}, outR_[kOutSize] = {};
    uint32_t outRd_ = 0, outWr_ = 0;
};

// Audio cell copied into IOSB/PrimeTime (LC 475 / Quadra 605). Despite
// MAME's historical ASC_EASC wiring in iosb.cpp, ASCTester identifies this
// as the distinct $BB IOSB variant: two 1 KB FIFOs drained as stereo at
// 22.257 kHz, with writable FIFO interrupt enables.
//
// PIN — reset state comes from the REAL LC 475, not from MAME. `reset()`
// sets fifoStat_ = $0E and both FIFO IRQ enables to 1 (= disabled), which is
// the in-file ASCTester dump of a real Mac LC 475 (sound/asc.cpp:1130-1136:
// "ASC Version: $BB / F09: 1 ($01) F29: 1 ($01) / 804Idle: $0E"). MAME's own
// `asc_iosb_device` is *unreachable in MAME*: iosb.cpp:89 instantiates
// ASC_EASC instead ("TODO: should use unique IOSB variant, but that needs
// more reverse-engineering"), so a parity diff against a RUNNING MAME LC 475
// / Quadra 605 compares us against the EASC's reset state ($B0, F09/F29 = 1,
// 44.1 kHz) and will look like a divergence. It is not. Do not "fix" toward
// what the running driver reports; the dump is the oracle.
//
// Audit § 2.8(a), DOCUMENT-SKIP — the 16-bit FIFO ports are not modelled.
// The real IOSB audio cell has them (sound/asc.cpp:73: "The IOSB variant has
// some surprising differences that aren't yet understood, including 16-bit
// wide FIFO ports"), and MAME even defines the handlers —
// `asc_iosb_device::read_w` / `write_w` (asc.cpp:1346-1371), which sign-flip
// each byte with ^ $80 and, on the write side, drop the high half entirely.
// NOTHING CALLS THEM: iosb.cpp:60 maps only the byte-wide $14000-$14FFF
// window, and no MAME driver references read_w/write_w. Our decode matches
// that map exactly (`Q605Memory.cpp:390`/`:463`, `addr & 0xFFF` byte-wide).
// Implementing a port with an admittedly-not-understood semantic, no caller
// and no guest would be inventing behaviour. Reopening condition: a guest
// (or MAME driver update) actually issuing 16-bit accesses to the cell.
class AscIosb {
public:
    static constexpr int kSampleRate = 22257;
    static constexpr int64_t kCpuHz = 15667200;

    void reset();
    uint8_t read(uint32_t offset);
    void write(uint32_t offset, uint8_t v);
    void tick(int cpuCycles);
    int cyclesToNextEvent() const {
        const int64_t need = kCpuHz - drainAcc_;
        return int((need + kSampleRate - 1) / kSampleRate);
    }

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

    // ── Save states (SaveState.h) ───────────────────────────────────────
    // Stereo FIFO pair + registers + drain phase; the host output ring is
    // dropped on restore, as in the other ASC flavours.
    template <class Ar> void visit(Ar& ar) {
        ar(fifo_, rd_, wr_, cap_, mode_, fifoStat_, playRec_,
           fifoIrqEn_, irq_, lastL_, lastR_, drainAcc_);
        if constexpr (Ar::loading) outRd_ = outWr_ = 0;
    }

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
