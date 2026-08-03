// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Apple 343S1021 PIC — I/O processor (docs/IOP_BRINGUP.md M2) ──────────
// The NCR standard-cell ASIC the Mac IIfx carries twice (SCC IOP + SWIM/
// ADB IOP) and the Quadra 900/950 carry twice more: an R65C02 core at
// input-clock/8, 32 KB of internal RAM (mirrored across the 64 KB space),
// one timer, two DMA channels (1 byte per channel per 8 input clocks),
// a host mailbox window and a peripheral register port with a host-bypass
// mode. Oracle: MAME `machine/applepic.cpp` (AJR, BSD-3-Clause) — every
// register behaviour below cites it; deviations are called out loud.
//
// The 65C02 firmware is NOT a dump: the host ROM downloads it through the
// shared-RAM window at boot (the whole internal map is RAM + registers),
// then releases /RSTPIC. See `docs/IOP_BRINGUP.md` § 1.
//
// Internal map (`applepic.cpp:63-77`):
//   $0000-$6FFF RAM (mirror $8000-$EFFF)      $F010-$F013 timer
//   $7000-$77FF RAM (no mirror)               $F020-$F02F DMA ×2
//   $7800-$7FFF RAM (mirror $F800-$FFFF —     $F030 SCC control (bit0 =
//               the 6502 vectors live here)         host bypass, bit7→gpout1)
//                                             $F031 I/O control (storage)
//   $F040-$F04F device regs → peripheral      $F032 timer/DPLL (bit0 = timer
//               callbacks (non-bypass only)         continuous, bit1→gpout0,
//   $F000-$F7FF otherwise open bus ($FF)            bits2-3 read gpin)
//                                             $F033/$F034 int mask / flags
//                                             $F035 host interrupt requests
//
// Host window (32 bytes, `applepic.cpp:125-217`), decoded by offset bits:
//   bit4 → device regs $0-$F (bypass mode only — the boot path talks to
//          the SCC straight through the idle PIC)
//   bit2 → shared-RAM data port at the address register, auto-increment
//          when status bit1 is set. Reaches the WHOLE internal space —
//          registers included (MAME goes through the 6502 address space).
//   bit1 → status/control: write bit2 = /RSTPIC (1 releases the 65C02,
//          which then runs its reset sequence), bit3 = interrupt the IOP,
//          bits 4/5 = ack INTHST0/1; read = PINT//REQ + host-int flags.
//   bit0/none → RAM address register lo/hi.
//
// Timing contract: `tick(clocks)` advances the device by input clocks
// (C15M on every known board). The 65C02 executes one instruction per
// 8×cycles clocks, the DMA engine moves one byte per channel per 8
// clocks, the timer counts raw clocks (arm = latch*8+12, continuous
// period (latch+2)*8 — `applepic.cpp:282-322`). Remainders carry as debt
// (`pom68k-mcu-lle-clock-drift`), and the timer/DMA phase advances with
// instruction granularity, slaved to the CPU's own cycle count.
//
// Gate: tests/applepic_test.cpp — uploads a hand-assembled 65C02 program
// through the window and proves reset-release, mailbox interrupts both
// ways, timer one-shot + continuous cadence, DMA in both directions and
// the bypass path.

#pragma once
#include "R65c02.h"
#include "SaveState.h"
#include <array>
#include <cstdint>
#include <functional>

class ApplePic {
public:
    // ── Board wiring (integrator installs; all optional) ─────────────────
    std::function<uint8_t(int)> readPeriph;        // prd: peripheral reg read
    std::function<void(int, uint8_t)> writePeriph; // pwr: peripheral reg write
    std::function<void(bool)> hostInt;             // hint → OSS/VIA2 input
    std::function<uint8_t()> gpIn;                 // 2 input pins (ADB in on the SWIM PIC)
    std::function<void(int, bool)> gpOut;          // gpout0/1 (ADB out on the SWIM PIC)

    ApplePic();

    /// MAME device_reset (`applepic.cpp:108-123`): 65C02 held in reset
    /// until the host releases /RSTPIC, host-int flags dropped, DMA
    /// disabled, interrupt mask cleared.
    void reset();

    // ── Host interface (5-bit offset — the machine map extracts it) ──────
    uint8_t hostRead(uint32_t offset);
    void hostWrite(uint32_t offset, uint8_t data);

    // ── Peripheral wires ─────────────────────────────────────────────────
    void pintW(bool state);   // peripheral /INT (SCC out_int)
    void reqaW(bool state);   // DMA channel A request (SCC WREQA / SWIM dat1byte)
    void reqbW(bool state);   // DMA channel B request

    /// Advance by `clocks` input clocks (see the timing contract above).
    void tick(int clocks);

    // ── Gate / debug accessors ───────────────────────────────────────────
    // Bring-up watchpoint: a firmware that returns to a bogus address had
    // its stack frame overwritten by SOMEONE, and the IOP's RAM has three
    // writers (the 65C02, the host window, the DMA engine). `watch` names
    // which one touched a byte — set it to a negative value to disarm.
    int watch = -1;
    R65c02& cpu() { return cpu_; }
    bool cpuHeld() const { return !(statusReg_ & 0x04); }
    uint8_t ramByte(uint16_t a) const { return ram_[a & 0x7FFF]; }
    uint8_t intFlags() const { return intReg_; }      // unmasked (the 6502
    uint8_t intMask() const { return intMask_; }      //  reads flags & mask)
    uint8_t statusReg() const { return statusReg_; }
    uint16_t ramAddr() const { return ramAddr_; }
    int64_t clockNow() const { return clockNow_; }

    // ── Save states ──────────────────────────────────────────────────────
    // The RAM is the firmware + its variables (there is no ROM to reload);
    // `clockNow_`/`budget_`/`dmaPhase_` fix the IOP's phase against the
    // host — the Cuda↔VIA lesson (`pom68k-mactv-gate-broken`). Callbacks
    // are re-bound by the owning machine.
    template <class Ar> void visit(Ar& ar) {
        cpu_.visit(ar);
        ar(ram_, ramAddr_, statusReg_, sccCtl_, ioCtl_, dpllCtl_,
           intMask_, intReg_,
           timerLatch_, timerArmed_, timerExpiry_, timerLastExpired_,
           clockNow_, budget_, dmaPhase_);
        for (DmaChannel& ch : dma_)
            ar(ch.control, ch.map, ch.tc, ch.req);
    }

private:
    // 6502 interrupt sources (`applepic.cpp:22-26`).
    static constexpr int kIrqDma1       = 1;
    static constexpr int kIrqDma2       = 2;
    static constexpr int kIrqPeripheral = 3;
    static constexpr int kIrqHost       = 4;
    static constexpr int kIrqTimer      = 5;

    // DMA control bits (`applepic.cpp:28-32`); bits 7-4 = I/O reg offset.
    static constexpr uint8_t kDmaEnable = 0x01;
    static constexpr uint8_t kDmaReq    = 0x02;
    static constexpr uint8_t kDmaDir    = 0x04;   // 1 = I/O → RAM
    static constexpr uint8_t kDmaChain  = 0x08;   // enable on other channel's completion

    struct DmaChannel {
        uint8_t control = 0;
        uint16_t map = 0;
        uint16_t tc = 0;     // 11-bit transfer count
        bool req = false;
    };

    uint8_t read8(uint16_t a);
    void write8(uint16_t a, uint8_t v);
    uint8_t regRead(uint16_t a);
    void regWrite(uint16_t a, uint8_t v);

    uint16_t timerCount() const;
    void runTimer();
    void dmaTick();

    void setInterrupt(int which);
    void resetInterrupt(int which);
    void updateIrqLine();

    R65c02 cpu_;
    std::array<uint8_t, 0x8000> ram_{};

    uint16_t ramAddr_ = 0;
    uint8_t statusReg_ = 0x80;    // MAME construction value (`applepic.cpp:46`)
    uint8_t sccCtl_ = 0;
    uint8_t ioCtl_ = 0;
    uint8_t dpllCtl_ = 0;
    uint8_t intMask_ = 0;
    uint8_t intReg_ = 0;

    uint16_t timerLatch_ = 0;
    bool timerArmed_ = false;
    int64_t timerExpiry_ = 0;        // in input clocks (clockNow_ domain)
    int64_t timerLastExpired_ = 0;

    DmaChannel dma_[2];

    const char* writer_ = "cpu";     // watchpoint tag, see `watch`
    int64_t clockNow_ = 0;           // input clocks elapsed
    int budget_ = 0;                 // tick() debt carry
    int dmaPhase_ = 0;               // clocks toward the next 8-clock DMA slot
};
