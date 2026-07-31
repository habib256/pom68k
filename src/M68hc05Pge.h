// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Motorola M68HC05PGE "PG&E" power manager (PowerBook Duo / PB150) ──
// The Apple semi-custom 68HC05 PMU of the MSC PowerBooks: 512 B boot mask
// ROM (bankable, OPTION bit 7), 960 B internal RAM ($40-$3FF), 32 KB SRAM
// at $8000-$FFFF (CSCR bit 5) that receives the MAIN firmware — uploaded
// by the system ROM over SPI at every boot — eleven GPIO ports (A-L),
// SPI master, 4-ch ADC, PWM/PLM analog outs, hardware keyboard-matrix
// scanner, trackball quadrature counters, 5.86 ms + 1 s fixed timers, a
// Mac-epoch uint32 RTC and an ADB modem cell.
// Oracle: MAME src/devices/cpu/m6805/m68hc05pge.cpp (R. Belmont,
// BSD-3-Clause) — register map :339-364, SPI :383-505, keyscan :536-585,
// ADB cell :613-700, vectors $FFFA-(irq×2) priority IRQ>ADB>RTI>CPI>SPI>
// KEY (:218-231), reset $FFFE, SWI $FFFC. Clock 4.194304 MHz = 2.097152 M
// MCU cycles/s (2 clocks per cycle), the Cuda's exact rate.
//
// This is a SEPARATE interpreter clone of M68hc05 (the E1), not a
// subclass: the E1's Cuda/Egret transport is phase-fragile
// (pom68k-mactv-gate-broken) and stays untouched; the two cores share the
// M6805 opcode set but differ in address width (16 vs 13 bits), stack
// window ($40-$FF vs $C0-$FF), vector table, memory map and every
// peripheral. Unify only with gates green on both sides.
// Gate: tests/m68hc05pge_test.cpp (boot ROM executes, SPI upload works).

#pragma once
#include "SaveState.h"
#include <cstdint>
#include <functional>
#include <vector>

class M68hc05Pge {
public:
    enum Port { A = 0, B, C, D, E, F, G, H, J, K, L, kPorts };

    // Port callbacks: read returns pin INPUT levels, core applies
    // DDR/pullup mixing (MAME ports_r/ports_w). Unset reads = all-ones.
    std::function<uint8_t(int)> readPort;
    std::function<void(int, uint8_t)> writePort;
    // SPI master pins (host side = the MSC VIA1 shifter):
    std::function<void(bool)> spiClock;              // SCK  → VIA1 CB1
    std::function<void(bool)> spiMosi;               // MOSI → VIA1 CB2
    void spiMisoIn(bool level) { spiMiso_ = level; } // MISO ← VIA1 CB2 out
    // ADC channels 0-15 (battery volts/current/temps — board supplies).
    std::function<uint8_t(int)> adcIn;
    // Trackball counters (signed deltas) + button, read by the firmware.
    std::function<uint8_t()> tbX, tbY;
    std::function<bool()> tbButton;
    // Same instruction-slaved hook as M68hc05::onCycles.
    std::function<void(int)> onCycles;

    void setPullups(int port, uint8_t mask) { pullups_[port] = mask; }

    bool loadBootRom(const std::vector<uint8_t>& data);   // 512 B @ $FE00
    void reset();

    int run(int budget);                             // MCU cycles
    void setIrqLine(bool asserted);                  // external /IRQ
    // MAME's spin_until_time on the PMU side (msc.cpp pmu_ack_w: 20 µs
    // after an ACK edge). Transport pacing, not an architectural feature —
    // but the Cuda experience says the MCU/host interleave is load-bearing
    // (pom68k-mactv-gate-broken), so the oracle's is reproduced.
    void spinCycles(int c) { if (c > spin_) spin_ = c; }

    // ── Debug / gate accessors ──
    uint16_t pc() const { return pc_; }
    uint8_t a() const { return a_; }
    uint8_t x() const { return x_; }
    uint8_t sp() const { return sp_; }
    bool waiting() const { return waiting_; }
    bool illegal() const { return illegal_; }
    uint16_t illegalPc() const { return illegalPc_; }
    uint8_t illegalOp() const { return illegalOp_; }
    int64_t cycleCount() const { return cycles_; }
    long instructions = 0;
    long spiTransfers = 0;                           // SPDR writes (master)
    uint8_t option() const { return option_; }       // bit 7 = boot ROM in
    uint8_t lastSpiIn() const { return spiIn_; }
    // Last 64 instruction PCs (diagnostic ring, oldest first via walk).
    uint16_t pcHistory(int back) const {             // back = 0 → newest
        return pcRing_[(pcRingPos_ + 63 - (back & 63)) & 63];
    }
    // Last 64 completed SPI exchanges, (out << 8) | in — newest at back 0.
    uint16_t spiHistory(int back) const {
        return spiRing_[(spiRingPos_ + 63 - (back & 63)) & 63];
    }
    uint8_t portLatch(int p) const { return ports_[p]; }
    uint8_t ddr(int p) const { return ddrs_[p]; }
    uint8_t sramByte(int off) const { return sram_[off & 0x7FFF]; }
    uint8_t ramByte(int off) const { return ram_[off & 0x3FF]; }
    void setRtc(uint32_t s) { rtc_ = s; }
    uint32_t rtc() const { return rtc_; }

    // ── Save states ─────────────────────────────────────────────────────
    // sram_ carries the host-uploaded firmware AND the PMU-side PRAM;
    // ram_ the working state incl. the power flag. Both are guest state.
    // bootRom_ is identity (reloaded by the machine).
    template <class Ar> void visit(Ar& ar) {
        ar(pc_, a_, x_, sp_, cc_);
        ar.bytes(ram_, sizeof ram_);
        ar.bytes(sram_, sizeof sram_);
        ar.bytes(ports_, sizeof ports_);
        ar.bytes(ddrs_, sizeof ddrs_);
        ar(pllCtrl_, option_, cscr_, cpicsr_, kcsr_, adcsr_, tbcs_);
        ar(spcr_, spsr_, spiIn_, spiOut_, spiBit_, spiClk_, spiMiso_, spiEdgeAcc_);
        ar(adbcr_, adbsr_, adbdr_, adbTimerAcc_, adbTimerMode_);
        ar(pwmacr_, pwma0_, pwma1_, pwmbcr_, pwmb0_, pwmb1_);
        ar(plmcr_, plmt1_, plmt2_);
        ar(rtc_, cycles_, secAcc_, cpiAcc_, keyscanAcc_, keyscanPeriod_);
        ar(pending_, irqLine_, waiting_, illegal_, illegalPc_, illegalOp_);
        ar(instructions, spin_);
    }

private:
    enum { CC_H = 0x10, CC_I = 0x08, CC_N = 0x04, CC_Z = 0x02, CC_C = 0x01 };
    // Interrupt sources, priority order (m68hc05pge.cpp:218-231).
    enum { INT_IRQ = 0x01, INT_ADB = 0x02, INT_RTI = 0x04,
           INT_CPI = 0x08, INT_SPI = 0x10, INT_KEY = 0x20 };

    uint8_t read8(uint16_t addr);
    void write8(uint16_t addr, uint8_t v);
    uint16_t read16(uint16_t addr) { return uint16_t(read8(addr) << 8) | read8(addr + 1); }

    uint8_t fetch8() { uint8_t v = read8(pc_); pc_++; return v; }
    uint16_t fetch16() { uint16_t v = fetch8(); return uint16_t((v << 8) | fetch8()); }

    // Stack: SP floor $40, mask $FF (m68hc05pge ctor params).
    void push8(uint8_t v) { write8(0x0040 | sp_, v); sp_--; }
    uint8_t pop8() { sp_++; return read8(0x0040 | sp_); }
    void pushState();
    int  serviceInterrupts();

    void setNZ(uint8_t v) {
        cc_ = uint8_t((cc_ & ~(CC_N | CC_Z)) | (v & 0x80 ? CC_N : 0) | (v ? 0 : CC_Z));
    }
    uint8_t aluRmw(int op, uint8_t v);
    void aluOp(int op, uint16_t ea, bool imm);
    int execOne();

    uint8_t portsIn(int p);
    void sendPort(int p, uint8_t data);
    void updateAdbIrq();
    void spiEdge();

    // Registers.
    uint16_t pc_ = 0;
    uint8_t a_ = 0, x_ = 0, sp_ = 0xFF, cc_ = CC_I;

    // Memory.
    uint8_t ram_[0x3C0] = {};                        // $40-$3FF
    uint8_t sram_[0x8000] = {};                      // $8000-$FFFF (CSCR-gated)
    uint8_t bootRom_[0x200] = {};                    // $FE00 when OPTION bit 7
    bool romLoaded_ = false;

    // Peripheral registers.
    uint8_t ports_[kPorts] = {}, ddrs_[kPorts] = {}, pullups_[kPorts] = {};
    uint8_t pllCtrl_ = 0;
    uint8_t option_ = 0x80;                          // OPTION_RESET: boot ROM in
    uint8_t cscr_ = 0x01;                            // CSCR_RESET
    uint8_t cpicsr_ = 0;
    uint8_t kcsr_ = 0;
    uint8_t adcsr_ = 0;
    uint8_t tbcs_ = 0;
    uint8_t spcr_ = 0, spsr_ = 0;
    uint8_t spiIn_ = 0, spiOut_ = 0;
    int spiBit_ = 0;                                 // edges remaining (16..0)
    bool spiClk_ = false, spiMiso_ = false;
    int64_t spiEdgeAcc_ = 0;
    uint8_t adbcr_ = 0, adbsr_ = 0x80, adbdr_ = 0;   // TDRE set at reset
    int64_t adbTimerAcc_ = 0;
    int adbTimerMode_ = -1;                          // -1 idle, 0 TDRE, 1 TC
    uint8_t pwmacr_ = 0, pwma0_ = 0, pwma1_ = 0;
    uint8_t pwmbcr_ = 0, pwmb0_ = 0, pwmb1_ = 0;
    uint8_t plmcr_ = 0, plmt1_ = 0, plmt2_ = 0;
    uint32_t rtc_ = 0;                               // Mac-epoch seconds

    // Timing (2.097152 M cycles/s; both fixed timers FREE-RUN from reset —
    // unlike the E1, whose timers arm on first register write).
    int64_t cycles_ = 0;
    int64_t secAcc_ = 0;
    int64_t cpiAcc_ = 0;
    int64_t keyscanAcc_ = 0;
    int64_t keyscanPeriod_ = 0;                      // 0 = scanner off

    uint8_t pending_ = 0;
    bool irqLine_ = false;
    bool waiting_ = false;
    bool illegal_ = false;
    uint16_t illegalPc_ = 0;
    uint8_t illegalOp_ = 0;
    int spin_ = 0;                                   // pending idle cycles
    uint16_t pcRing_[64] = {};                       // diagnostic, not state
    int pcRingPos_ = 0;
    uint16_t spiRing_[64] = {};                      // diagnostic, not state
    int spiRingPos_ = 0;
    uint8_t spiOutLatch_ = 0;                        // SPDR as written

    static const uint8_t kCycles[256];
    static constexpr int64_t kHz = 2097152;          // 4.194304 MHz / 2
};
