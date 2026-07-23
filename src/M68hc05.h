// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Motorola 68HC05E1 MCU core (Egret/Cuda firmware LLE, step 10 / #3) ──
// An 8-bit M68HC05 interpreter with the E1's on-chip peripherals, built to
// run the REAL Cuda firmware dumps under roms/cuda/ (341S0417 = Cuda 2.35,
// 341S0788 = 2.37, 341S0060 = 2.40 — 0x1100 bytes each, mapped at $0F00).
// Oracle: MAME src/devices/cpu/m6805/m6805.cpp (s_hc_cycles table,
// SP mask $FF floor $C0, 13-bit space) + m68hc05e1.cpp (memory map
// :225-236, port/DDR/pullup semantics :103-133, PLL :135-165, timer
// :167-227, vectors: reset $1FFE, IRQ $1FFA, TIMER $1FF8, CPI $1FF6,
// priority IRQ > TIMER > CPI per interrupt_vector :66-84) and
// mame/apple/cuda.cpp for the port wiring the integrator provides.
//
// Standalone by design (blueprint step 1, TODO): the machine wiring
// (VIA shim on PB, AdbLine on PA6, POM68K_CUDA_LLE flag) is step 2+.
// Gate: tests/m68hc05_test.cpp — executes the real reset vector.

#pragma once
#include <cstdint>
#include <functional>
#include <vector>

class M68hc05 {
public:
    // Port callbacks (port 0 = A, 1 = B, 2 = C). read returns the INPUT
    // levels on the pins; the core applies DDR/pullup mixing like MAME
    // ports_r/ports_w. Unset callbacks read as all-ones (pulled up).
    std::function<uint8_t(int)> readPort;
    std::function<void(int, uint8_t)> writePort;
    void setPullups(int port, uint8_t mask) { pullups_[port & 3] = mask; }

    bool loadRom(const std::vector<uint8_t>& data);  // 0x1100 bytes @ $0F00
    void reset();                                    // PC ← [$1FFE]

    // Execute up to `budget` MCU cycles (2 MCU clocks each on a real E1);
    // returns the cycles actually consumed. Runs the programmable timer
    // (overflow every 512 cycles — clock/1024 Hz at 2 clocks/cycle) and
    // the one-second timer alongside the instruction stream.
    int run(int budget);

    void setIrqLine(bool asserted);                  // external /IRQ ($1FFA)

    // ── Debug / gate accessors ──
    uint16_t pc() const { return pc_; }
    uint8_t a() const { return a_; }
    uint8_t x() const { return x_; }
    uint8_t sp() const { return sp_; }
    uint8_t cc() const { return cc_ | 0xE0; }
    bool waiting() const { return waiting_; }
    bool illegal() const { return illegal_; }        // hit an undefined opcode
    uint16_t illegalPc() const { return illegalPc_; }
    uint8_t illegalOp() const { return illegalOp_; }
    uint8_t ddr(int p) const { return ddrs_[p & 3]; }
    uint8_t portLatch(int p) const { return ports_[p & 3]; }
    uint8_t pll() const { return pllCtrl_; }
    uint8_t ramByte(int off) const { return ram_[off & 0x1FF]; }
    long portWrites = 0, ddrWrites = 0, pllWrites = 0;   // gate counters
    long instructions = 0;

private:
    // CCR bits (M6805: CC = 111HINZC).
    enum { CC_H = 0x10, CC_I = 0x08, CC_N = 0x04, CC_Z = 0x02, CC_C = 0x01 };
    enum { INT_IRQ = 0x01, INT_TIMER = 0x02, INT_CPI = 0x04 };

    uint8_t read8(uint16_t addr);
    void write8(uint16_t addr, uint8_t v);
    uint16_t read16(uint16_t addr) { return uint16_t(read8(addr) << 8) | read8(addr + 1); }

    uint8_t fetch8() { uint8_t v = read8(pc_); pc_ = (pc_ + 1) & 0x1FFF; return v; }
    uint16_t fetch16() { uint16_t v = fetch8(); return uint16_t((v << 8) | fetch8()); }

    void push8(uint8_t v) { write8(0x00C0 | (sp_ & 0x3F), v); sp_--; }
    uint8_t pop8() { sp_++; return read8(0x00C0 | (sp_ & 0x3F)); }
    void pushState();                                // PCL,PCH,X,A,CC
    void serviceInterrupts();

    void setNZ(uint8_t v) {
        cc_ = uint8_t((cc_ & ~(CC_N | CC_Z)) | (v & 0x80 ? CC_N : 0) | (v ? 0 : CC_Z));
    }
    uint8_t aluRmw(int op, uint8_t v);               // NEG..CLR group
    void aluOp(int op, uint16_t ea, bool imm);       // SUB..STX group
    int execOne();                                   // one instruction → cycles

    void sendPort(int p, uint8_t data);              // DDR/pullup mix out

    // Registers.
    uint16_t pc_ = 0;
    uint8_t a_ = 0, x_ = 0, sp_ = 0xFF, cc_ = CC_I;

    // Memory: E1 map — RAM $0090-$01FF (stack $C0-$FF), ROM $0F00-$1FFF.
    uint8_t ram_[0x200] = {};
    uint8_t rom_[0x1100] = {};
    bool romLoaded_ = false;

    // On-chip peripherals (m68hc05e1.cpp).
    uint8_t ports_[4] = {}, ddrs_[4] = {}, pullups_[4] = {};
    uint8_t pllCtrl_ = 0;
    uint8_t timerCtrl_ = 0;
    uint8_t onesec_ = 0;
    int64_t cycles_ = 0;                             // total cycles (timer ctr)
    int64_t progTimerAcc_ = 0;                       // → overflow per 512 cyc
    int64_t onesecAcc_ = 0;

    // Interrupts / execution state.
    uint8_t pending_ = 0;
    bool irqLine_ = false;
    bool waiting_ = false;                           // WAIT/STOP until int
    bool illegal_ = false;
    uint16_t illegalPc_ = 0;
    uint8_t illegalOp_ = 0;

    static const uint8_t kCycles[256];               // MAME s_hc_cycles
};
