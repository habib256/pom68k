// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2000-2026 — GPLv3 (see LICENSE)
// Portions Copyright (C) 2012 John D. Corrado (POM2 M6502 lineage)
//
// ── R65C02 CPU core (Apple PIC IOP brick, docs/IOP_BRINGUP.md M1) ────────
// The 65C02 inside the Apple 343S1021 PIC (Mac IIfx / Quadra 900-950 I/O
// processors — MAME `machine/applepic.h:9` instantiates R65C02). Also the
// core family of the M50753-adjacent portables, should that day come.
//
// Vendored from the sibling POM2's `M6502` (src/M6502.cpp, GPL, same
// author) with two deliberate reductions — diff against POM2 to pull
// upstream fixes:
//   * CMOS-only. POM2's runtime NMOS/CMOS switch and the NMOS opcode
//     remap are gone; POM68K has no NMOS 6502 target. The dispatch table
//     IS the 65C02 table, Rockwell RMB/SMB/BBR/BBS included (cycle costs
//     cited against MAME `ow65c02.lst` in the .cpp).
//   * Bus by callback. POM2's `Memory*` becomes `read8`/`write8`
//     std::function members (the `M68hc05` pattern): the PIC maps RAM +
//     its own registers, the core does not care.
// Apple II diagnostics (PC-trace ring, BRK dumps, env-var traps) are
// stripped — they held process-global state that two PIC instances would
// have raced on.
//
// WAI/STP ($CB/$DB) are kept from POM2 (WDC behaviour, Klaus-gated).
// MAME's r65c02 decodes them as NOPs; whether the NCR 65CX02 cell has
// them is settled in M2 by scanning the uploaded IOP firmware — see
// docs/IOP_BRINGUP.md §6.
//
// Timing: architectural cycle counts (MAME om6502/ow65c02 lists). The
// integrator owns the clock ratio (PIC core = host clock / 8).
// Gate: tests/r65c02_test.cpp — Klaus Dormann's 6502 functional +
// 65C02 extended-opcodes images to their success traps.

#pragma once
#include "SaveState.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>

class R65c02
{
public:
    /// P register bits. B (bit 4) and the always-1 bit 5 exist only on
    /// pushed copies (PHP/BRK push them set; IRQ/NMI push B clear).
    struct Status {
        static constexpr uint8_t N = 0x80;
        static constexpr uint8_t V = 0x40;
        static constexpr uint8_t B = 0x10;
        static constexpr uint8_t D = 0x08;
        static constexpr uint8_t I = 0x04;
        static constexpr uint8_t Z = 0x02;
        static constexpr uint8_t C = 0x01;
    };

    // ── Bus (set both before any reset/step) ─────────────────────────────
    std::function<uint8_t(uint16_t)> read8;
    std::function<void(uint16_t, uint8_t)> write8;

    R65c02();

    /// Reset per MAME `ow65c02.lst:814`: I set, D cleared, SP -= 3 (the
    /// faked-BRK pushes are reads on real silicon), PC ← ($FFFC), halt
    /// latch cleared. Requires the bus callbacks to be wired.
    void softReset();
    /// softReset + A/X/Y zeroed, SP snapped to $FF (power-on).
    void hardReset();

    void step();                 // one instruction (+ pending IRQ/NMI entry)
    int  run(int maxCycles);     // >= maxCycles architectural cycles; returns actual
    void stop() { running = 0; } // makes an in-flight run() return

    // ── IRQ / NMI ────────────────────────────────────────────────────────
    /// Wire-OR IRQ line: each source (0..31) owns a bit; the CPU's IRQ
    /// input is the OR of all of them, so one device deasserting cannot
    /// drop another's pending interrupt. The PIC's interrupt unit is a
    /// single source; the id space is kept for integrators with several.
    void setIrqLine(int sourceId, bool asserted);
    uint32_t getIrqSourceMask() const { return irqSourceMask.load(std::memory_order_relaxed); }
    void setNMI() { NMI = 1; }

    // ── Register file (gates, debugger, snapshot) ────────────────────────
    uint8_t  getAccumulator()    const { return accumulator; }
    uint8_t  getXRegister()      const { return xRegister; }
    uint8_t  getYRegister()      const { return yRegister; }
    uint8_t  getStatusRegister() const { return statusRegister; }
    uint8_t  getStackPointer()   const { return stackPointer; }
    uint16_t getProgramCounter() const { return programCounter; }
    void setAccumulator(uint8_t v)    { accumulator = v; }
    void setXRegister(uint8_t v)      { xRegister = v; }
    void setYRegister(uint8_t v)      { yRegister = v; }
    void setStatusRegister(uint8_t v) { statusRegister = v; }
    void setStackPointer(uint8_t v)   { stackPointer = v; }
    /// Jump without a RESET — the Klaus harness enters at $0400 directly.
    void setProgramCounter(uint16_t pc) { programCounter = pc; }

    /// STP ($DB) halt latch — only reset clears it (MAME
    /// `ow65c02.lst:715-718`); step() burns cycles and ignores IRQ/NMI
    /// while set. Guest-visible, so it travels in the snapshot.
    bool isHalted() const  { return halted; }
    void setHalted(bool v) { halted = v; }

    // ── Bring-up trace (off by default, one predicted branch when off) ──
    // A firmware that ends up in its own BRK handler jumped somewhere it
    // never meant to; the only useful evidence is where it came FROM.
    // `setTrace(true)` records a ring of executed PCs, and `onBrk` fires
    // with the address of the `$00` about to execute (Q900 M7).
    void setTrace(bool on) { trace_ = on; }
    std::function<void(uint16_t)> onBrk;
    /// The last executed PCs, oldest first (empty unless setTrace(true)).
    std::vector<uint16_t> pcTrail() const {
        std::vector<uint16_t> out;
        if (!trace_) return out;
        for (int i = 0; i < kTrail; i++) {
            uint16_t pc = pcRing_[(pcRingIdx_ + i) % kTrail];
            if (pc || !out.empty()) out.push_back(pc);
        }
        return out;
    }

    /// Cycles consumed by the current/last instruction.
    int getCurrentInstructionCycles() const { return cycles; }
    /// Total architectural cycles since construction/restore. The
    /// integrator slaves the PIC's timer and DMA cadence to this.
    int64_t cycleCount() const { return cycleCount_; }

    // ── Save states (SaveState.h) ────────────────────────────────────────
    // `cycleCount_` fixes the IOP's PHASE against the host — the Cuda↔VIA
    // lesson (`pom68k-mactv-gate-broken`) applies to any MCU↔host
    // transport, so the counter travels verbatim. Bus callbacks are
    // re-bound by the owning device, never serialized.
    template <class Ar> void visit(Ar& ar) {
        uint32_t irqMask = irqSourceMask.load(std::memory_order_relaxed);
        int irq = IRQ.load(std::memory_order_relaxed);
        ar(accumulator, xRegister, yRegister, statusRegister, stackPointer,
           programCounter, halted, NMI, irqMask, irq, cycleCount_);
        irqSourceMask.store(irqMask, std::memory_order_relaxed);
        IRQ.store(irq, std::memory_order_relaxed);
    }

private:
    uint8_t accumulator = 0, xRegister = 0, yRegister = 0;
    uint8_t statusRegister = 0x24, stackPointer = 0xFF;
    uint16_t programCounter = 0;
    // Atomics so an off-thread integrator can assert IRQ concurrently
    // with the CPU thread (relaxed RMW; plain mov on the hot path).
    std::atomic<int> IRQ{0};
    std::atomic<uint32_t> irqSourceMask{0};
    int NMI = 0;
    uint16_t op = 0;
    int tmp = 0;
    int cycles = 0;
    int running = 0;
    int64_t cycleCount_ = 0;
    bool halted = false;

    // Debug ring (see setTrace). Out of the save-state chunk: it is
    // developer scaffolding, not guest-visible machine state.
    static constexpr int kTrail = 256;
    bool trace_ = false;
    uint16_t pcRing_[kTrail] = {};
    int pcRingIdx_ = 0;

    uint16_t memReadAbsolute(uint16_t adr);
    void pushProgramCounter();
    void popProgramCounter();
    void handleIRQ();
    void handleNMI();

    // Addressing modes — each leaves the effective address in `op` and
    // charges its share of the cycle budget (the fetch seeds cycles=1).
    void Imp(); void Imm(); void Zero(); void ZeroX(); void ZeroY();
    void Abs(); void AbsX(); void AbsY(); void Ind(); void IndZero();
    void IndAbsX(); void IndZeroX(); void IndZeroY(); void Rel();
    void WAbsX(); void RmwAbsX(); void WAbsY(); void WIndZeroY();

    void setStatusRegisterNZ(uint8_t val);
    void setFlagCarry(int val);
    void setFlagBorrow(int val);
    /// The intermediate RMW bus cycle: a dummy read on CMOS
    /// (MAME `ow65c02.lst`; NMOS re-wrote the original value instead).
    void rmwSecondBusCycle(uint16_t addr, uint8_t origValue);

    void LDA(); void LDX(); void LDY(); void STA(); void STX(); void STY();
    void ADC(); void SBC(); void CMP(); void CPX(); void CPY();
    void AND(); void ORA(); void EOR();
    void ASL(); void ASL_A(); void LSR(); void LSR_A();
    void ROL(); void ROL_A(); void ROR(); void ROR_A();
    void INC(); void DEC(); void INX(); void INY(); void DEX(); void DEY();
    void BIT(); void PHA(); void PHP(); void PLA(); void PLP();
    void BRK(); void RTI(); void JMP(); void RTS(); void JSR();
    void branch();
    void BNE(); void BEQ(); void BVC(); void BVS();
    void BCC(); void BCS(); void BPL(); void BMI();
    void TAX(); void TXA(); void TAY(); void TYA(); void TXS(); void TSX();
    void CLC(); void SEC(); void CLI(); void SEI(); void CLV();
    void CLD(); void SED(); void NOP();

    // 65C02 additions.
    void BRA(); void STZ(); void INA(); void DEA();
    void PHX(); void PHY(); void PLX(); void PLY();
    void BIT_imm(); void TSB(); void TRB();

    // Rockwell bit ops — one instantiation per bit so the dispatch table
    // targets them directly. SMB0=$07..SMB7=$77, RMB0=$87..RMB7=$F7,
    // BBR0=$0F..BBR7=$7F, BBS0=$8F..BBS7=$FF... (column layout in .cpp).
    template <int N> void SMBn();
    template <int N> void RMBn();
    template <int N> void BBRn();
    template <int N> void BBSn();

    void WAI(); void STP();

    // 65C02 reserved-NOP classes (all defined lengths/timings).
    void Unoff();     // 1 byte, 1 cycle ($x3/$xB columns)
    void Unoff2();    // 2 bytes, 3 cycles ($44)
    void UnoffImm();  // 2 bytes, 2 cycles ($02/$22/$42/$62/$82/$C2/$E2)
    void UnoffZpX();  // 2 bytes, 4 cycles ($54/$D4/$F4)
    void UnoffAbs4(); // 3 bytes, 4 cycles ($DC/$FC)
    void Unoff5C();   // 3 bytes, 8 cycles (the $5C oddball)

    void executeOpcode();

    struct OpcodeEntry {
        void (R65c02::*addrMode)();
        void (R65c02::*operation)();
    };
    /// The 65C02 dispatch table (single-function opcodes carry a null
    /// operation). CMOS-only build: dispatched directly, never mutated.
    static const OpcodeEntry kCmosTable[256];
};
