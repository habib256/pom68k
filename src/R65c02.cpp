// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2000-2026 — GPLv3 (see LICENSE)
// Portions Copyright (C) 2012 John D. Corrado (POM2 M6502 lineage)
//
// R65C02 core body — vendored from POM2 `src/M6502.cpp` (see R65c02.h for
// the vendoring contract: CMOS-only, bus-by-callback, diagnostics
// stripped). Cycle comments cite MAME `m6502/ow65c02.lst` (CMOS) — POM2
// carries the full derivation for each count; keep the two files
// diffable when pulling fixes either way.

#include "R65c02.h"

R65c02::R65c02()
{
    statusRegister = 0x24 | Status::I;
}

uint16_t R65c02::memReadAbsolute(uint16_t adr)
{
    return read8(adr) | (read8(static_cast<uint16_t>(adr + 1)) << 8);
}

void R65c02::pushProgramCounter()
{
    write8(static_cast<uint16_t>(stackPointer + 0x100),
           static_cast<uint8_t>(programCounter >> 8));
    stackPointer--;
    write8(static_cast<uint16_t>(stackPointer + 0x100),
           static_cast<uint8_t>(programCounter));
    stackPointer--;
    cycles += 2;
}

void R65c02::popProgramCounter()
{
    // Push order is high then low, so pop low then high.
    stackPointer++;
    const uint8_t lowByte = read8(static_cast<uint16_t>(stackPointer + 0x100));
    stackPointer++;
    const uint8_t highByte = read8(static_cast<uint16_t>(stackPointer + 0x100));
    programCounter = lowByte | (highByte << 8);
    cycles += 2;
}

void R65c02::handleIRQ()
{
    pushProgramCounter();
    write8(static_cast<uint16_t>(0x100 + stackPointer),
           static_cast<uint8_t>((statusRegister & ~0x10) | 0x20));
    stackPointer--;
    // 65C02 interrupt entry sets I and clears D (MAME ow65c02.lst:259
    // brk_c_imp: `m_P = (m_P | F_I) & ~F_D;`).
    statusRegister = (statusRegister | Status::I) & static_cast<uint8_t>(~Status::D);
    programCounter = memReadAbsolute(0xFFFE);
    cycles += 5;
}

void R65c02::handleNMI()
{
    pushProgramCounter();
    write8(static_cast<uint16_t>(0x100 + stackPointer),
           static_cast<uint8_t>((statusRegister & ~0x10) | 0x20));
    stackPointer--;
    statusRegister = (statusRegister | Status::I) & static_cast<uint8_t>(~Status::D);
    NMI = 0;
    programCounter = memReadAbsolute(0xFFFA);
    cycles += 5;
}

// ── Addressing modes ──────────────────────────────────────────────────────

void R65c02::Imp()
{
    cycles++;
}

void R65c02::Imm()
{
    // `op` points at the immediate byte so the operation's read8(op)
    // fetches it.
    op = programCounter++;
}

void R65c02::Zero()
{
    op = read8(programCounter++);
    cycles++;
}

void R65c02::ZeroX()
{
    // zp,X = 2 bus cycles here (zp fetch + the dummy read at the
    // unindexed address) so LDA $zp,X totals 4 like MAME's tables.
    op = (read8(programCounter++) + xRegister) & 0xFF;
    cycles += 2;
}

void R65c02::ZeroY()
{
    op = (read8(programCounter++) + yRegister) & 0xFF;
    cycles += 2;
}

void R65c02::Abs()
{
    op = memReadAbsolute(programCounter);
    programCounter += 2;
    cycles += 2;
}

void R65c02::AbsX()
{
    uint16_t base = read8(programCounter++);
    base |= static_cast<uint16_t>(read8(programCounter++)) << 8;
    op = base + xRegister;
    cycles += 2;
    if ((base & 0xFF00) != (op & 0xFF00))
        cycles++;
}

void R65c02::AbsY()
{
    uint16_t base = read8(programCounter++);
    base |= static_cast<uint16_t>(read8(programCounter++)) << 8;
    op = base + yRegister;
    cycles += 2;
    if ((base & 0xFF00) != (op & 0xFF00))
        cycles++;
}

void R65c02::Ind()
{
    // JMP ($abs). The 65C02 fixes the NMOS page-wrap bug at the cost of
    // one extra cycle (MAME `ow65c02.lst:387-395` jmp_c_ind: 6 total).
    const uint8_t  lo = read8(programCounter++);
    const uint16_t hi = static_cast<uint16_t>(read8(programCounter++)) << 8;
    const uint16_t ptrLo = static_cast<uint16_t>(hi | lo);
    const uint16_t ptrHi = static_cast<uint16_t>(ptrLo + 1);  // linear carry
    cycles += 5;
    op = read8(ptrLo);
    op |= static_cast<uint16_t>(read8(ptrHi)) << 8;
}

void R65c02::IndZeroX()
{
    const uint8_t zp = (read8(programCounter++) + xRegister) & 0xFF;
    op = read8(zp);
    op |= static_cast<uint16_t>(read8(static_cast<uint8_t>(zp + 1))) << 8;
    // 6 total for (zp,X) ops: the silicon dummy-reads the unindexed
    // pointer before adding X.
    cycles += 4;
}

// 65C02 zero-page indirect (zp) — (zp,X) without the X offset.
void R65c02::IndZero()
{
    const uint8_t zp = read8(programCounter++);
    op = read8(zp);
    op |= static_cast<uint16_t>(read8(static_cast<uint8_t>(zp + 1))) << 8;
    cycles += 3;
}

// 65C02 (abs,X) for JMP — no page-wrap bug. MAME `ow65c02.lst:377-386`: 6.
void R65c02::IndAbsX()
{
    uint16_t base = read8(programCounter++);
    base |= static_cast<uint16_t>(read8(programCounter++)) << 8;
    base = static_cast<uint16_t>(base + xRegister);
    op = read8(base);
    op |= static_cast<uint16_t>(read8(static_cast<uint16_t>(base + 1))) << 8;
    cycles += 5;
}

void R65c02::IndZeroY()
{
    const uint8_t zp = read8(programCounter++);
    uint16_t base = read8(zp);
    base |= static_cast<uint16_t>(read8(static_cast<uint8_t>(zp + 1))) << 8;
    op = base + yRegister;
    cycles += 3;
    if ((base & 0xFF00) != (op & 0xFF00))
        cycles++;
}

void R65c02::Rel()
{
    const uint8_t offset = read8(programCounter++);
    if (offset & 0x80)
        op = (programCounter + offset - 256) & 0xFFFF;
    else
        op = (programCounter + offset) & 0xFFFF;
    cycles++;
}

void R65c02::WAbsX()
{
    uint16_t base = read8(programCounter++);
    base |= static_cast<uint16_t>(read8(programCounter++)) << 8;
    op = base + xRegister;
    cycles += 3;
}

// abs,X for the 65C02 RMW shifts/rotates: 6 cycles, 7 only on a page
// cross (INC/DEC abs,X stay a fixed 7 through WAbsX).
void R65c02::RmwAbsX()
{
    uint16_t base = read8(programCounter++);
    base |= static_cast<uint16_t>(read8(programCounter++)) << 8;
    op = base + xRegister;
    cycles += 2;
    if ((base & 0xFF00) != (op & 0xFF00)) cycles++;
}

void R65c02::WAbsY()
{
    uint16_t base = read8(programCounter++);
    base |= static_cast<uint16_t>(read8(programCounter++)) << 8;
    op = base + yRegister;
    cycles += 3;
}

void R65c02::WIndZeroY()
{
    const uint8_t zp = read8(programCounter++);
    uint16_t base = read8(zp);
    base |= static_cast<uint16_t>(read8(static_cast<uint8_t>(zp + 1))) << 8;
    op = base + yRegister;
    cycles += 4;
}

// ── Flags ─────────────────────────────────────────────────────────────────

void R65c02::setStatusRegisterNZ(uint8_t val)
{
    if (val & 0x80)
        statusRegister |= Status::N;
    else
        statusRegister &= ~Status::N;

    if (!val)
        statusRegister |= Status::Z;
    else
        statusRegister &= ~Status::Z;
}

void R65c02::setFlagCarry(int val)
{
    if (val & 0x100)
        statusRegister |= Status::C;
    else
        statusRegister &= ~Status::C;
}

void R65c02::setFlagBorrow(int val)
{
    if (!(val & 0x100))
        statusRegister |= Status::C;
    else
        statusRegister &= ~Status::C;
}

// ── Operations ────────────────────────────────────────────────────────────

void R65c02::LDA()
{
    accumulator = read8(op);
    setStatusRegisterNZ(accumulator);
    cycles++;
}

void R65c02::LDX()
{
    xRegister = read8(op);
    setStatusRegisterNZ(xRegister);
    cycles++;
}

void R65c02::LDY()
{
    yRegister = read8(op);
    setStatusRegisterNZ(yRegister);
    cycles++;
}

void R65c02::STA()
{
    write8(op, accumulator);
    cycles++;
}

void R65c02::STX()
{
    write8(op, xRegister);
    cycles++;
}

void R65c02::STY()
{
    write8(op, yRegister);
    cycles++;
}

void R65c02::ADC()
{
    const uint8_t Op1 = accumulator, Op2 = read8(op);
    cycles++;

    if (statusRegister & Status::D)
    {
        // CMOS decimal ADC — POM2 carries the full silicon derivation
        // (invalid-BCD nibble repack, carry-out ≥ $100, high-sum V);
        // validated there against Tom Harte `wdc65c02/v1/69`.
        if (!((Op1 + Op2 + (statusRegister & Status::C ? 1 : 0)) & 0xFF))
            statusRegister |= Status::Z;
        else
            statusRegister &= ~Status::Z;

        tmp = (Op1 & 0x0F) + (Op2 & 0x0F) + (statusRegister & Status::C ? 1 : 0);
        accumulator = tmp < 0x0A ? tmp : ((tmp + 6) & 0x0F) + 0x10;
        tmp = (Op1 & 0xF0) + (Op2 & 0xF0) + (accumulator & 0xF0);

        if (tmp & 0x80)
            statusRegister |= Status::N;
        else
            statusRegister &= ~Status::N;

        if (((Op1 ^ tmp) & ~(Op1 ^ Op2)) & 0x80)
            statusRegister |= Status::V;
        else
            statusRegister &= ~Status::V;

        tmp = (accumulator & 0x0F) | (tmp < 0xA0 ? tmp : tmp + 0x60);

        if (tmp >= 0x100)
            statusRegister |= Status::C;
        else
            statusRegister &= ~Status::C;

        accumulator = tmp & 0xFF;
        // 65C02: N/Z recomputed from the adjusted result + 1 extra cycle
        // (MAME `ow65c02.lst:11-14` adc_c_aba).
        setStatusRegisterNZ(accumulator);
        cycles++;
    }
    else
    {
        tmp = Op1 + Op2 + (statusRegister & Status::C ? 1 : 0);
        accumulator = tmp & 0xFF;

        if (((Op1 ^ accumulator) & ~(Op1 ^ Op2)) & 0x80)
            statusRegister |= Status::V;
        else
            statusRegister &= ~Status::V;

        setFlagCarry(tmp);
        setStatusRegisterNZ(accumulator);
    }
}

void R65c02::SBC()
{
    const uint8_t Op1 = accumulator, Op2 = read8(op);
    cycles++;

    if (statusRegister & Status::D)
    {
        // WDC 65C02 decimal SBC (MAME `w65c02.cpp:28-46` do_sbc_cd):
        // pack both nibble differences first, then apply the -6/-$60
        // adjustments to the whole byte so a low-nibble borrow can
        // propagate ("interdigit carry"). Divergence from NMOS is
        // confined to invalid BCD digits.
        const int borrow = (statusRegister & Status::C) ? 0 : 1;
        const uint8_t al = static_cast<uint8_t>((Op1 & 0x0F) - (Op2 & 0x0F) - borrow);
        const uint8_t ah = static_cast<uint8_t>((Op1 >> 4) - (Op2 >> 4) -
                                                (static_cast<int8_t>(al) < 0 ? 1 : 0));
        uint8_t res = static_cast<uint8_t>((ah << 4) | (al & 0x0F));
        if (static_cast<int8_t>(al) < 0) res = static_cast<uint8_t>(res - 0x06);
        if (static_cast<int8_t>(ah) < 0) res = static_cast<uint8_t>(res - 0x60);
        accumulator = res;

        tmp = Op1 - Op2 - borrow;
        setFlagBorrow(tmp);
        // N/Z from the final adjusted accumulator, V from the binary
        // difference, + 1 extra cycle (sbc_c_aba mirrors adc_c_aba).
        setStatusRegisterNZ(accumulator);
        if (((Op1 ^ Op2) & (Op1 ^ static_cast<uint8_t>(tmp))) & 0x80)
            statusRegister |= Status::V;
        else
            statusRegister &= ~Status::V;
        cycles++;
    }
    else
    {
        tmp = Op1 - Op2 - (statusRegister & Status::C ? 0 : 1);
        accumulator = tmp & 0xFF;

        if (((Op1 ^ Op2) & (Op1 ^ accumulator)) & 0x80)
            statusRegister |= Status::V;
        else
            statusRegister &= ~Status::V;

        setFlagBorrow(tmp);
        setStatusRegisterNZ(accumulator);
    }
}

void R65c02::CMP()
{
    tmp = accumulator - read8(op);
    cycles++;
    setFlagBorrow(tmp);
    setStatusRegisterNZ(static_cast<uint8_t>(tmp));
}

void R65c02::CPX()
{
    tmp = xRegister - read8(op);
    cycles++;
    setFlagBorrow(tmp);
    setStatusRegisterNZ(static_cast<uint8_t>(tmp));
}

void R65c02::CPY()
{
    tmp = yRegister - read8(op);
    cycles++;
    setFlagBorrow(tmp);
    setStatusRegisterNZ(static_cast<uint8_t>(tmp));
}

void R65c02::AND()
{
    accumulator &= read8(op);
    cycles++;
    setStatusRegisterNZ(accumulator);
}

void R65c02::ORA()
{
    accumulator |= read8(op);
    cycles++;
    setStatusRegisterNZ(accumulator);
}

void R65c02::EOR()
{
    accumulator ^= read8(op);
    cycles++;
    setStatusRegisterNZ(accumulator);
}

void R65c02::rmwSecondBusCycle(uint16_t addr, uint8_t origValue)
{
    // CMOS: a dummy read of the same address (NMOS wrote the original
    // back instead — MAME `om6502.lst:161-164` vs `ow65c02.lst`).
    (void)origValue;
    (void)read8(addr);
}

void R65c02::ASL()
{
    const uint8_t orig = read8(op);
    rmwSecondBusCycle(op, orig);

    if (orig & 0x80) statusRegister |= Status::C;
    else             statusRegister &= ~Status::C;

    const uint8_t val = static_cast<uint8_t>(orig << 1);
    setStatusRegisterNZ(val);
    write8(op, val);
    cycles += 3;
}

void R65c02::ASL_A()
{
    tmp = accumulator << 1;
    accumulator = tmp & 0xFF;
    setFlagCarry(tmp);
    setStatusRegisterNZ(accumulator);
}

void R65c02::LSR()
{
    const uint8_t orig = read8(op);
    rmwSecondBusCycle(op, orig);

    if (orig & 1) statusRegister |= Status::C;
    else          statusRegister &= ~Status::C;

    const uint8_t val = static_cast<uint8_t>(orig >> 1);
    setStatusRegisterNZ(val);
    write8(op, val);
    cycles += 3;
}

void R65c02::LSR_A()
{
    if (accumulator & 1)
        statusRegister |= Status::C;
    else
        statusRegister &= ~Status::C;

    accumulator >>= 1;
    setStatusRegisterNZ(accumulator);
}

void R65c02::ROL()
{
    const uint8_t orig = read8(op);
    rmwSecondBusCycle(op, orig);
    const uint8_t newCarry = orig & 0x80;
    const uint8_t val = static_cast<uint8_t>(
        (orig << 1) | (statusRegister & Status::C ? 1 : 0));

    if (newCarry) statusRegister |= Status::C;
    else          statusRegister &= ~Status::C;

    setStatusRegisterNZ(val);
    write8(op, val);
    cycles += 3;
}

void R65c02::ROL_A()
{
    tmp = (accumulator << 1) | (statusRegister & Status::C ? 1 : 0);
    accumulator = tmp & 0xFF;
    setFlagCarry(tmp);
    setStatusRegisterNZ(accumulator);
}

void R65c02::ROR()
{
    const uint8_t orig = read8(op);
    rmwSecondBusCycle(op, orig);
    const int newCarry = orig & 1;
    const uint8_t val = static_cast<uint8_t>(
        (orig >> 1) | (statusRegister & Status::C ? 0x80 : 0));

    if (newCarry) statusRegister |= Status::C;
    else          statusRegister &= ~Status::C;

    setStatusRegisterNZ(val);
    write8(op, val);
    cycles += 3;
}

void R65c02::ROR_A()
{
    tmp = accumulator | (statusRegister & Status::C ? 0x100 : 0);

    if (accumulator & 1)
        statusRegister |= Status::C;
    else
        statusRegister &= ~Status::C;

    accumulator = tmp >> 1;
    setStatusRegisterNZ(accumulator);
}

void R65c02::INC()
{
    // RMW: read + dummy + write. MAME: INC mem = 5/6/6/7 (zp/abs/zp,X/abs,X).
    const uint8_t orig = read8(op);
    rmwSecondBusCycle(op, orig);
    const uint8_t val = static_cast<uint8_t>(orig + 1);
    setStatusRegisterNZ(val);
    write8(op, val);
    cycles += 3;
}

void R65c02::DEC()
{
    const uint8_t orig = read8(op);
    rmwSecondBusCycle(op, orig);
    const uint8_t val = static_cast<uint8_t>(orig - 1);
    setStatusRegisterNZ(val);
    write8(op, val);
    cycles += 3;
}

void R65c02::INX()
{
    xRegister++;
    setStatusRegisterNZ(xRegister);
}

void R65c02::INY()
{
    yRegister++;
    setStatusRegisterNZ(yRegister);
}

void R65c02::DEX()
{
    xRegister--;
    setStatusRegisterNZ(xRegister);
}

void R65c02::DEY()
{
    yRegister--;
    setStatusRegisterNZ(yRegister);
}

void R65c02::BIT()
{
    const uint8_t val = read8(op);

    if (val & 0x40)
        statusRegister |= Status::V;
    else
        statusRegister &= ~Status::V;

    if (val & 0x80)
        statusRegister |= Status::N;
    else
        statusRegister &= ~Status::N;

    if (!(val & accumulator))
        statusRegister |= Status::Z;
    else
        statusRegister &= ~Status::Z;

    cycles++;
}

void R65c02::PHA()
{
    write8(static_cast<uint16_t>(0x100 + stackPointer), accumulator);
    stackPointer--;
    cycles++;
}

void R65c02::PHP()
{
    // PHP pushes P with B (bit 4) and the always-1 bit 5 both set; the
    // two bits exist only on pushed copies (IRQ/NMI push B clear).
    write8(static_cast<uint16_t>(0x100 + stackPointer),
           statusRegister | Status::B | 0x20);
    stackPointer--;
    cycles++;
}

void R65c02::PLA()
{
    stackPointer++;
    accumulator = read8(static_cast<uint16_t>(stackPointer + 0x100));
    setStatusRegisterNZ(accumulator);
    cycles += 2;
}

void R65c02::PLP()
{
    stackPointer++;
    // Force U=1 and B=1 on every P pop (MAME om6502.lst:959 +
    // m6502.cpp:408) — they are not physical flags.
    statusRegister = read8(static_cast<uint16_t>(stackPointer + 0x100)) | 0x30;
    cycles += 2;
}

void R65c02::BRK()
{
    // BRK is 2 bytes: opcode + signature byte. The pushed return address
    // must skip past the signature byte.
    programCounter++;
    pushProgramCounter();
    write8(static_cast<uint16_t>(0x100 + stackPointer),
           statusRegister | Status::B | 0x20);
    stackPointer--;
    statusRegister = (statusRegister | Status::I) & static_cast<uint8_t>(~Status::D);
    programCounter = memReadAbsolute(0xFFFE);
    // 7 total (MAME `ow65c02.lst:234-261`): fetch(1)+Imp(1)+pushPC(2)+3.
    cycles += 3;
}

void R65c02::RTI()
{
    // 6 total: fetch(1)+Imp(1)+PLP body(2)+popPC body(2).
    PLP();
    popProgramCounter();
}

void R65c02::JMP()
{
    programCounter = op;
}

void R65c02::RTS()
{
    popProgramCounter();
    programCounter++;
    cycles += 2;
}

void R65c02::JSR()
{
    const uint8_t lo = read8(programCounter++);
    pushProgramCounter();
    programCounter = lo + (read8(programCounter) << 8);
    cycles += 3;
}

void R65c02::branch()
{
    cycles++;
    if ((programCounter & 0xFF00) != (op & 0xFF00))
        cycles++;
    programCounter = op;
}

void R65c02::BNE()
{
    if (!(statusRegister & Status::Z))
        branch();
}

void R65c02::BEQ()
{
    if (statusRegister & Status::Z)
        branch();
}

void R65c02::BVC()
{
    if (!(statusRegister & Status::V))
        branch();
}

void R65c02::BVS()
{
    if (statusRegister & Status::V)
        branch();
}

void R65c02::BCC()
{
    if (!(statusRegister & Status::C))
        branch();
}

void R65c02::BCS()
{
    if (statusRegister & Status::C)
        branch();
}

void R65c02::BPL()
{
    if (!(statusRegister & Status::N))
        branch();
}

void R65c02::BMI()
{
    if (statusRegister & Status::N)
        branch();
}

void R65c02::TAX()
{
    xRegister = accumulator;
    setStatusRegisterNZ(accumulator);
}

void R65c02::TXA()
{
    accumulator = xRegister;
    setStatusRegisterNZ(accumulator);
}

void R65c02::TAY()
{
    yRegister = accumulator;
    setStatusRegisterNZ(accumulator);
}

void R65c02::TYA()
{
    accumulator = yRegister;
    setStatusRegisterNZ(accumulator);
}

void R65c02::TXS()
{
    stackPointer = xRegister;
}

void R65c02::TSX()
{
    xRegister = stackPointer;
    setStatusRegisterNZ(xRegister);
}

void R65c02::CLC()
{
    statusRegister &= ~Status::C;
}

void R65c02::SEC()
{
    statusRegister |= Status::C;
}

void R65c02::CLI()
{
    statusRegister &= ~Status::I;
}

void R65c02::SEI()
{
    statusRegister |= Status::I;
}

void R65c02::CLV()
{
    statusRegister &= ~Status::V;
}

void R65c02::CLD()
{
    statusRegister &= ~Status::D;
}

void R65c02::SED()
{
    statusRegister |= Status::D;
}

void R65c02::NOP()
{
}

// ── 65C02 additions ───────────────────────────────────────────────────────

void R65c02::BRA()
{
    cycles++;
    if ((programCounter & 0xFF00) != (op & 0xFF00)) cycles++;
    programCounter = op;
}

void R65c02::STZ()
{
    write8(op, 0);
    cycles++;
}

// INC A / DEC A ($1A/$3A): 2-cycle implied ops — fetch + Imp, nothing more.
void R65c02::INA()
{
    accumulator++;
    setStatusRegisterNZ(accumulator);
}

void R65c02::DEA()
{
    accumulator--;
    setStatusRegisterNZ(accumulator);
}

void R65c02::PHX()
{
    write8(static_cast<uint16_t>(0x100 + stackPointer), xRegister);
    stackPointer--;
    cycles++;
}

void R65c02::PHY()
{
    write8(static_cast<uint16_t>(0x100 + stackPointer), yRegister);
    stackPointer--;
    cycles++;
}

void R65c02::PLX()
{
    stackPointer++;
    xRegister = read8(static_cast<uint16_t>(stackPointer + 0x100));
    setStatusRegisterNZ(xRegister);
    cycles += 2;
}

void R65c02::PLY()
{
    stackPointer++;
    yRegister = read8(static_cast<uint16_t>(stackPointer + 0x100));
    setStatusRegisterNZ(yRegister);
    cycles += 2;
}

void R65c02::BIT_imm()
{
    // BIT #imm affects only Z — V and N stay put. MAME
    // `ow65c02.lst:210-217`: 2 cycles.
    const uint8_t val = read8(op);
    if (!(val & accumulator)) statusRegister |= Status::Z;
    else                      statusRegister &= ~Status::Z;
    cycles += 1;
}

void R65c02::TSB()
{
    // Z = (mem AND A == 0); mem |= A.
    const uint8_t orig = read8(op);
    rmwSecondBusCycle(op, orig);
    if (!(orig & accumulator)) statusRegister |= Status::Z;
    else                       statusRegister &= ~Status::Z;
    write8(op, static_cast<uint8_t>(orig | accumulator));
    cycles += 3;
}

void R65c02::TRB()
{
    // Z = (mem AND A == 0); mem &= ~A.
    const uint8_t orig = read8(op);
    rmwSecondBusCycle(op, orig);
    if (!(orig & accumulator)) statusRegister |= Status::Z;
    else                       statusRegister &= ~Status::Z;
    write8(op, static_cast<uint8_t>(orig & ~accumulator));
    cycles += 3;
}

// ── Rockwell SMBn / RMBn / BBRn / BBSn ────────────────────────────────────
// 5 cycles each on real silicon (MAME `ow65c02.lst:497-504, 700-707` for
// RMB/SMB, `:168-181` for BBR/BBS not-taken); BBR/BBS-taken adds +1, +1
// more on page cross. The fetch seeded cycles=1, so bodies add 4.

template <int N>
void R65c02::SMBn()
{
    const uint8_t zp = read8(programCounter++);
    uint8_t v = read8(zp);
    v |= static_cast<uint8_t>(1u << N);
    write8(zp, v);
    cycles += 4;
}

template <int N>
void R65c02::RMBn()
{
    const uint8_t zp = read8(programCounter++);
    uint8_t v = read8(zp);
    v &= static_cast<uint8_t>(~(1u << N));
    write8(zp, v);
    cycles += 4;
}

template <int N>
void R65c02::BBRn()
{
    const uint8_t zp     = read8(programCounter++);
    const int8_t  offset = static_cast<int8_t>(read8(programCounter++));
    const uint8_t v      = read8(zp);
    cycles += 4;
    if ((v & (1u << N)) == 0) {
        const uint16_t old = programCounter;
        programCounter = static_cast<uint16_t>(programCounter + offset);
        cycles += 1;
        if ((old & 0xFF00u) != (programCounter & 0xFF00u)) cycles += 1;
    }
}

template <int N>
void R65c02::BBSn()
{
    const uint8_t zp     = read8(programCounter++);
    const int8_t  offset = static_cast<int8_t>(read8(programCounter++));
    const uint8_t v      = read8(zp);
    cycles += 4;
    if ((v & (1u << N)) != 0) {
        const uint16_t old = programCounter;
        programCounter = static_cast<uint16_t>(programCounter + offset);
        cycles += 1;
        if ((old & 0xFF00u) != (programCounter & 0xFF00u)) cycles += 1;
    }
}

// Explicit instantiations so the function pointers in kCmosTable resolve.
template void R65c02::SMBn<0>(); template void R65c02::SMBn<1>();
template void R65c02::SMBn<2>(); template void R65c02::SMBn<3>();
template void R65c02::SMBn<4>(); template void R65c02::SMBn<5>();
template void R65c02::SMBn<6>(); template void R65c02::SMBn<7>();
template void R65c02::RMBn<0>(); template void R65c02::RMBn<1>();
template void R65c02::RMBn<2>(); template void R65c02::RMBn<3>();
template void R65c02::RMBn<4>(); template void R65c02::RMBn<5>();
template void R65c02::RMBn<6>(); template void R65c02::RMBn<7>();
template void R65c02::BBRn<0>(); template void R65c02::BBRn<1>();
template void R65c02::BBRn<2>(); template void R65c02::BBRn<3>();
template void R65c02::BBRn<4>(); template void R65c02::BBRn<5>();
template void R65c02::BBRn<6>(); template void R65c02::BBRn<7>();
template void R65c02::BBSn<0>(); template void R65c02::BBSn<1>();
template void R65c02::BBSn<2>(); template void R65c02::BBSn<3>();
template void R65c02::BBSn<4>(); template void R65c02::BBSn<5>();
template void R65c02::BBSn<6>(); template void R65c02::BBSn<7>();

void R65c02::WAI()
{
    // WDC WAI suspends until IRQ/NMI — and wakes even with I=1 (MAME
    // `ow65c02.lst:797-803`): the wake is unconditional on the line, only
    // the vectoring honours I. Modelled as "consume 3 cycles, fall
    // through": the next step() vectors (NMI / IRQ+I=0) or continues at
    // PC+1 (IRQ+I=1). See POM2 for the full derivation.
    cycles += 2;
}

void R65c02::STP()
{
    // STP halts until RESET (MAME `ow65c02.lst:715-718`: only reset_c
    // exits — NMI does NOT wake STP). Sticky latch; step()
    // short-circuits while set, soft/hardReset clear it.
    halted = true;
    cycles += 2;
}

void R65c02::Unoff()
{
    // 65C02 1-byte reserved NOPs ($x3/$xB columns): 1 cycle (fetch only).
}

void R65c02::Unoff2()
{
    programCounter++;
    cycles += 2;
}

void R65c02::UnoffImm()   // 2 bytes, 2 cycles: NOP #imm class
{
    programCounter++;
    cycles += 1;
}

void R65c02::UnoffZpX()   // 2 bytes, 4 cycles: NOP zp,X ($54/$D4/$F4)
{
    programCounter++;
    cycles += 3;
}

void R65c02::UnoffAbs4()  // 3 bytes, 4 cycles: NOP abs class ($DC/$FC)
{
    programCounter += 2;
    cycles += 3;
}

void R65c02::Unoff5C()    // 3 bytes, 8 cycles: the 65C02 oddball $5C
{
    // Fetches its operand then burns 5 more cycles reading $FFxx (MAME
    // `ow65c02.lst` nop_5c). The dummy bus reads are not replayed.
    programCounter += 2;
    cycles += 7;
}

// The 65C02 dispatch table: {addressingMode, operation}; single-function
// opcodes carry a null operation.
const R65c02::OpcodeEntry R65c02::kCmosTable[256] = {
    /* 0x00 */ {&R65c02::Imp,       &R65c02::BRK},
    /* 0x01 */ {&R65c02::IndZeroX,  &R65c02::ORA},
    /* 0x02 */ {&R65c02::UnoffImm,  nullptr},          // NOP #imm (2 bytes)
    /* 0x03 */ {&R65c02::Unoff,     nullptr},
    /* 0x04 */ {&R65c02::Zero,      &R65c02::TSB},     // TSB zp
    /* 0x05 */ {&R65c02::Zero,      &R65c02::ORA},
    /* 0x06 */ {&R65c02::Zero,      &R65c02::ASL},
    /* 0x07 */ {&R65c02::RMBn<0>,   nullptr},
    /* 0x08 */ {&R65c02::Imp,       &R65c02::PHP},
    /* 0x09 */ {&R65c02::Imm,       &R65c02::ORA},
    /* 0x0A */ {&R65c02::Imp,       &R65c02::ASL_A},
    /* 0x0B */ {&R65c02::Unoff,     nullptr},
    /* 0x0C */ {&R65c02::Abs,       &R65c02::TSB},     // TSB abs
    /* 0x0D */ {&R65c02::Abs,       &R65c02::ORA},
    /* 0x0E */ {&R65c02::Abs,       &R65c02::ASL},
    /* 0x0F */ {&R65c02::BBRn<0>,   nullptr},

    /* 0x10 */ {&R65c02::Rel,       &R65c02::BPL},
    /* 0x11 */ {&R65c02::IndZeroY,  &R65c02::ORA},
    /* 0x12 */ {&R65c02::IndZero,   &R65c02::ORA},     // ORA (zp)
    /* 0x13 */ {&R65c02::Unoff,     nullptr},
    /* 0x14 */ {&R65c02::Zero,      &R65c02::TRB},     // TRB zp
    /* 0x15 */ {&R65c02::ZeroX,     &R65c02::ORA},
    /* 0x16 */ {&R65c02::ZeroX,     &R65c02::ASL},
    /* 0x17 */ {&R65c02::RMBn<1>,   nullptr},
    /* 0x18 */ {&R65c02::Imp,       &R65c02::CLC},
    /* 0x19 */ {&R65c02::AbsY,      &R65c02::ORA},
    /* 0x1A */ {&R65c02::Imp,       &R65c02::INA},     // INC A
    /* 0x1B */ {&R65c02::Unoff,     nullptr},
    /* 0x1C */ {&R65c02::Abs,       &R65c02::TRB},     // TRB abs
    /* 0x1D */ {&R65c02::AbsX,      &R65c02::ORA},
    /* 0x1E */ {&R65c02::RmwAbsX,   &R65c02::ASL},
    /* 0x1F */ {&R65c02::BBRn<1>,   nullptr},

    /* 0x20 */ {&R65c02::JSR,       nullptr},
    /* 0x21 */ {&R65c02::IndZeroX,  &R65c02::AND},
    /* 0x22 */ {&R65c02::UnoffImm,  nullptr},
    /* 0x23 */ {&R65c02::Unoff,     nullptr},
    /* 0x24 */ {&R65c02::Zero,      &R65c02::BIT},
    /* 0x25 */ {&R65c02::Zero,      &R65c02::AND},
    /* 0x26 */ {&R65c02::Zero,      &R65c02::ROL},
    /* 0x27 */ {&R65c02::RMBn<2>,   nullptr},
    /* 0x28 */ {&R65c02::Imp,       &R65c02::PLP},
    /* 0x29 */ {&R65c02::Imm,       &R65c02::AND},
    /* 0x2A */ {&R65c02::Imp,       &R65c02::ROL_A},
    /* 0x2B */ {&R65c02::Unoff,     nullptr},
    /* 0x2C */ {&R65c02::Abs,       &R65c02::BIT},
    /* 0x2D */ {&R65c02::Abs,       &R65c02::AND},
    /* 0x2E */ {&R65c02::Abs,       &R65c02::ROL},
    /* 0x2F */ {&R65c02::BBRn<2>,   nullptr},

    /* 0x30 */ {&R65c02::Rel,       &R65c02::BMI},
    /* 0x31 */ {&R65c02::IndZeroY,  &R65c02::AND},
    /* 0x32 */ {&R65c02::IndZero,   &R65c02::AND},     // AND (zp)
    /* 0x33 */ {&R65c02::Unoff,     nullptr},
    /* 0x34 */ {&R65c02::ZeroX,     &R65c02::BIT},     // BIT zp,X
    /* 0x35 */ {&R65c02::ZeroX,     &R65c02::AND},
    /* 0x36 */ {&R65c02::ZeroX,     &R65c02::ROL},
    /* 0x37 */ {&R65c02::RMBn<3>,   nullptr},
    /* 0x38 */ {&R65c02::Imp,       &R65c02::SEC},
    /* 0x39 */ {&R65c02::AbsY,      &R65c02::AND},
    /* 0x3A */ {&R65c02::Imp,       &R65c02::DEA},     // DEC A
    /* 0x3B */ {&R65c02::Unoff,     nullptr},
    /* 0x3C */ {&R65c02::AbsX,      &R65c02::BIT},     // BIT abs,X
    /* 0x3D */ {&R65c02::AbsX,      &R65c02::AND},
    /* 0x3E */ {&R65c02::RmwAbsX,   &R65c02::ROL},
    /* 0x3F */ {&R65c02::BBRn<3>,   nullptr},

    /* 0x40 */ {&R65c02::Imp,       &R65c02::RTI},
    /* 0x41 */ {&R65c02::IndZeroX,  &R65c02::EOR},
    /* 0x42 */ {&R65c02::UnoffImm,  nullptr},
    /* 0x43 */ {&R65c02::Unoff,     nullptr},
    /* 0x44 */ {&R65c02::Unoff2,    nullptr},          // NOP zp (3 cyc)
    /* 0x45 */ {&R65c02::Zero,      &R65c02::EOR},
    /* 0x46 */ {&R65c02::Zero,      &R65c02::LSR},
    /* 0x47 */ {&R65c02::RMBn<4>,   nullptr},
    /* 0x48 */ {&R65c02::Imp,       &R65c02::PHA},
    /* 0x49 */ {&R65c02::Imm,       &R65c02::EOR},
    /* 0x4A */ {&R65c02::Imp,       &R65c02::LSR_A},
    /* 0x4B */ {&R65c02::Unoff,     nullptr},
    /* 0x4C */ {&R65c02::Abs,       &R65c02::JMP},
    /* 0x4D */ {&R65c02::Abs,       &R65c02::EOR},
    /* 0x4E */ {&R65c02::Abs,       &R65c02::LSR},
    /* 0x4F */ {&R65c02::BBRn<4>,   nullptr},

    /* 0x50 */ {&R65c02::Rel,       &R65c02::BVC},
    /* 0x51 */ {&R65c02::IndZeroY,  &R65c02::EOR},
    /* 0x52 */ {&R65c02::IndZero,   &R65c02::EOR},     // EOR (zp)
    /* 0x53 */ {&R65c02::Unoff,     nullptr},
    /* 0x54 */ {&R65c02::UnoffZpX,  nullptr},          // NOP zp,X (4 cyc)
    /* 0x55 */ {&R65c02::ZeroX,     &R65c02::EOR},
    /* 0x56 */ {&R65c02::ZeroX,     &R65c02::LSR},
    /* 0x57 */ {&R65c02::RMBn<5>,   nullptr},
    /* 0x58 */ {&R65c02::Imp,       &R65c02::CLI},
    /* 0x59 */ {&R65c02::AbsY,      &R65c02::EOR},
    /* 0x5A */ {&R65c02::Imp,       &R65c02::PHY},     // PHY
    /* 0x5B */ {&R65c02::Unoff,     nullptr},
    /* 0x5C */ {&R65c02::Unoff5C,   nullptr},          // oddball: 3 bytes, 8 cyc
    /* 0x5D */ {&R65c02::AbsX,      &R65c02::EOR},
    /* 0x5E */ {&R65c02::RmwAbsX,   &R65c02::LSR},
    /* 0x5F */ {&R65c02::BBRn<5>,   nullptr},

    /* 0x60 */ {&R65c02::Imp,       &R65c02::RTS},
    /* 0x61 */ {&R65c02::IndZeroX,  &R65c02::ADC},
    /* 0x62 */ {&R65c02::UnoffImm,  nullptr},
    /* 0x63 */ {&R65c02::Unoff,     nullptr},
    /* 0x64 */ {&R65c02::Zero,      &R65c02::STZ},     // STZ zp
    /* 0x65 */ {&R65c02::Zero,      &R65c02::ADC},
    /* 0x66 */ {&R65c02::Zero,      &R65c02::ROR},
    /* 0x67 */ {&R65c02::RMBn<6>,   nullptr},
    /* 0x68 */ {&R65c02::Imp,       &R65c02::PLA},
    /* 0x69 */ {&R65c02::Imm,       &R65c02::ADC},
    /* 0x6A */ {&R65c02::Imp,       &R65c02::ROR_A},
    /* 0x6B */ {&R65c02::Unoff,     nullptr},
    /* 0x6C */ {&R65c02::Ind,       &R65c02::JMP},
    /* 0x6D */ {&R65c02::Abs,       &R65c02::ADC},
    /* 0x6E */ {&R65c02::Abs,       &R65c02::ROR},
    /* 0x6F */ {&R65c02::BBRn<6>,   nullptr},

    /* 0x70 */ {&R65c02::Rel,       &R65c02::BVS},
    /* 0x71 */ {&R65c02::IndZeroY,  &R65c02::ADC},
    /* 0x72 */ {&R65c02::IndZero,   &R65c02::ADC},     // ADC (zp)
    /* 0x73 */ {&R65c02::Unoff,     nullptr},
    /* 0x74 */ {&R65c02::ZeroX,     &R65c02::STZ},     // STZ zp,X
    /* 0x75 */ {&R65c02::ZeroX,     &R65c02::ADC},
    /* 0x76 */ {&R65c02::ZeroX,     &R65c02::ROR},
    /* 0x77 */ {&R65c02::RMBn<7>,   nullptr},
    /* 0x78 */ {&R65c02::Imp,       &R65c02::SEI},
    /* 0x79 */ {&R65c02::AbsY,      &R65c02::ADC},
    /* 0x7A */ {&R65c02::Imp,       &R65c02::PLY},     // PLY
    /* 0x7B */ {&R65c02::Unoff,     nullptr},
    /* 0x7C */ {&R65c02::IndAbsX,   &R65c02::JMP},     // JMP (abs,X)
    /* 0x7D */ {&R65c02::AbsX,      &R65c02::ADC},
    /* 0x7E */ {&R65c02::RmwAbsX,   &R65c02::ROR},
    /* 0x7F */ {&R65c02::BBRn<7>,   nullptr},

    /* 0x80 */ {&R65c02::Rel,       &R65c02::BRA},     // BRA
    /* 0x81 */ {&R65c02::IndZeroX,  &R65c02::STA},
    /* 0x82 */ {&R65c02::UnoffImm,  nullptr},
    /* 0x83 */ {&R65c02::Unoff,     nullptr},
    /* 0x84 */ {&R65c02::Zero,      &R65c02::STY},
    /* 0x85 */ {&R65c02::Zero,      &R65c02::STA},
    /* 0x86 */ {&R65c02::Zero,      &R65c02::STX},
    /* 0x87 */ {&R65c02::SMBn<0>,   nullptr},
    /* 0x88 */ {&R65c02::Imp,       &R65c02::DEY},
    /* 0x89 */ {&R65c02::Imm,       &R65c02::BIT_imm}, // BIT #imm
    /* 0x8A */ {&R65c02::Imp,       &R65c02::TXA},
    /* 0x8B */ {&R65c02::Unoff,     nullptr},
    /* 0x8C */ {&R65c02::Abs,       &R65c02::STY},
    /* 0x8D */ {&R65c02::Abs,       &R65c02::STA},
    /* 0x8E */ {&R65c02::Abs,       &R65c02::STX},
    /* 0x8F */ {&R65c02::BBSn<0>,   nullptr},

    /* 0x90 */ {&R65c02::Rel,       &R65c02::BCC},
    /* 0x91 */ {&R65c02::WIndZeroY, &R65c02::STA},
    /* 0x92 */ {&R65c02::IndZero,   &R65c02::STA},     // STA (zp)
    /* 0x93 */ {&R65c02::Unoff,     nullptr},
    /* 0x94 */ {&R65c02::ZeroX,     &R65c02::STY},
    /* 0x95 */ {&R65c02::ZeroX,     &R65c02::STA},
    /* 0x96 */ {&R65c02::ZeroY,     &R65c02::STX},
    /* 0x97 */ {&R65c02::SMBn<1>,   nullptr},
    /* 0x98 */ {&R65c02::Imp,       &R65c02::TYA},
    /* 0x99 */ {&R65c02::WAbsY,     &R65c02::STA},
    /* 0x9A */ {&R65c02::Imp,       &R65c02::TXS},
    /* 0x9B */ {&R65c02::Unoff,     nullptr},
    /* 0x9C */ {&R65c02::Abs,       &R65c02::STZ},     // STZ abs
    /* 0x9D */ {&R65c02::WAbsX,     &R65c02::STA},
    /* 0x9E */ {&R65c02::WAbsX,     &R65c02::STZ},     // STZ abs,X
    /* 0x9F */ {&R65c02::BBSn<1>,   nullptr},

    /* 0xA0 */ {&R65c02::Imm,       &R65c02::LDY},
    /* 0xA1 */ {&R65c02::IndZeroX,  &R65c02::LDA},
    /* 0xA2 */ {&R65c02::Imm,       &R65c02::LDX},
    /* 0xA3 */ {&R65c02::Unoff,     nullptr},
    /* 0xA4 */ {&R65c02::Zero,      &R65c02::LDY},
    /* 0xA5 */ {&R65c02::Zero,      &R65c02::LDA},
    /* 0xA6 */ {&R65c02::Zero,      &R65c02::LDX},
    /* 0xA7 */ {&R65c02::SMBn<2>,   nullptr},
    /* 0xA8 */ {&R65c02::Imp,       &R65c02::TAY},
    /* 0xA9 */ {&R65c02::Imm,       &R65c02::LDA},
    /* 0xAA */ {&R65c02::Imp,       &R65c02::TAX},
    /* 0xAB */ {&R65c02::Unoff,     nullptr},
    /* 0xAC */ {&R65c02::Abs,       &R65c02::LDY},
    /* 0xAD */ {&R65c02::Abs,       &R65c02::LDA},
    /* 0xAE */ {&R65c02::Abs,       &R65c02::LDX},
    /* 0xAF */ {&R65c02::BBSn<2>,   nullptr},

    /* 0xB0 */ {&R65c02::Rel,       &R65c02::BCS},
    /* 0xB1 */ {&R65c02::IndZeroY,  &R65c02::LDA},
    /* 0xB2 */ {&R65c02::IndZero,   &R65c02::LDA},     // LDA (zp)
    /* 0xB3 */ {&R65c02::Unoff,     nullptr},
    /* 0xB4 */ {&R65c02::ZeroX,     &R65c02::LDY},
    /* 0xB5 */ {&R65c02::ZeroX,     &R65c02::LDA},
    /* 0xB6 */ {&R65c02::ZeroY,     &R65c02::LDX},
    /* 0xB7 */ {&R65c02::SMBn<3>,   nullptr},
    /* 0xB8 */ {&R65c02::Imp,       &R65c02::CLV},
    /* 0xB9 */ {&R65c02::AbsY,      &R65c02::LDA},
    /* 0xBA */ {&R65c02::Imp,       &R65c02::TSX},
    /* 0xBB */ {&R65c02::Unoff,     nullptr},
    /* 0xBC */ {&R65c02::AbsX,      &R65c02::LDY},
    /* 0xBD */ {&R65c02::AbsX,      &R65c02::LDA},
    /* 0xBE */ {&R65c02::AbsY,      &R65c02::LDX},
    /* 0xBF */ {&R65c02::BBSn<3>,   nullptr},

    /* 0xC0 */ {&R65c02::Imm,       &R65c02::CPY},
    /* 0xC1 */ {&R65c02::IndZeroX,  &R65c02::CMP},
    /* 0xC2 */ {&R65c02::UnoffImm,  nullptr},
    /* 0xC3 */ {&R65c02::Unoff,     nullptr},
    /* 0xC4 */ {&R65c02::Zero,      &R65c02::CPY},
    /* 0xC5 */ {&R65c02::Zero,      &R65c02::CMP},
    /* 0xC6 */ {&R65c02::Zero,      &R65c02::DEC},
    /* 0xC7 */ {&R65c02::SMBn<4>,   nullptr},
    /* 0xC8 */ {&R65c02::Imp,       &R65c02::INY},
    /* 0xC9 */ {&R65c02::Imm,       &R65c02::CMP},
    /* 0xCA */ {&R65c02::Imp,       &R65c02::DEX},
    /* 0xCB */ {&R65c02::WAI,       nullptr},          // WDC WAI — see header
    /* 0xCC */ {&R65c02::Abs,       &R65c02::CPY},
    /* 0xCD */ {&R65c02::Abs,       &R65c02::CMP},
    /* 0xCE */ {&R65c02::Abs,       &R65c02::DEC},
    /* 0xCF */ {&R65c02::BBSn<4>,   nullptr},

    /* 0xD0 */ {&R65c02::Rel,       &R65c02::BNE},
    /* 0xD1 */ {&R65c02::IndZeroY,  &R65c02::CMP},
    /* 0xD2 */ {&R65c02::IndZero,   &R65c02::CMP},     // CMP (zp)
    /* 0xD3 */ {&R65c02::Unoff,     nullptr},
    /* 0xD4 */ {&R65c02::UnoffZpX,  nullptr},          // NOP zp,X (4 cyc)
    /* 0xD5 */ {&R65c02::ZeroX,     &R65c02::CMP},
    /* 0xD6 */ {&R65c02::ZeroX,     &R65c02::DEC},
    /* 0xD7 */ {&R65c02::SMBn<5>,   nullptr},
    /* 0xD8 */ {&R65c02::Imp,       &R65c02::CLD},
    /* 0xD9 */ {&R65c02::AbsY,      &R65c02::CMP},
    /* 0xDA */ {&R65c02::Imp,       &R65c02::PHX},     // PHX
    /* 0xDB */ {&R65c02::STP,       nullptr},          // WDC STP — see header
    /* 0xDC */ {&R65c02::UnoffAbs4, nullptr},          // NOP abs,X (4 cyc)
    /* 0xDD */ {&R65c02::AbsX,      &R65c02::CMP},
    /* 0xDE */ {&R65c02::WAbsX,     &R65c02::DEC},
    /* 0xDF */ {&R65c02::BBSn<5>,   nullptr},

    /* 0xE0 */ {&R65c02::Imm,       &R65c02::CPX},
    /* 0xE1 */ {&R65c02::IndZeroX,  &R65c02::SBC},
    /* 0xE2 */ {&R65c02::UnoffImm,  nullptr},
    /* 0xE3 */ {&R65c02::Unoff,     nullptr},
    /* 0xE4 */ {&R65c02::Zero,      &R65c02::CPX},
    /* 0xE5 */ {&R65c02::Zero,      &R65c02::SBC},
    /* 0xE6 */ {&R65c02::Zero,      &R65c02::INC},
    /* 0xE7 */ {&R65c02::SMBn<6>,   nullptr},
    /* 0xE8 */ {&R65c02::Imp,       &R65c02::INX},
    /* 0xE9 */ {&R65c02::Imm,       &R65c02::SBC},
    /* 0xEA */ {&R65c02::Imp,       &R65c02::NOP},
    /* 0xEB */ {&R65c02::Unoff,     nullptr},
    /* 0xEC */ {&R65c02::Abs,       &R65c02::CPX},
    /* 0xED */ {&R65c02::Abs,       &R65c02::SBC},
    /* 0xEE */ {&R65c02::Abs,       &R65c02::INC},
    /* 0xEF */ {&R65c02::BBSn<6>,   nullptr},

    /* 0xF0 */ {&R65c02::Rel,       &R65c02::BEQ},
    /* 0xF1 */ {&R65c02::IndZeroY,  &R65c02::SBC},
    /* 0xF2 */ {&R65c02::IndZero,   &R65c02::SBC},     // SBC (zp)
    /* 0xF3 */ {&R65c02::Unoff,     nullptr},
    /* 0xF4 */ {&R65c02::UnoffZpX,  nullptr},          // NOP zp,X (4 cyc)
    /* 0xF5 */ {&R65c02::ZeroX,     &R65c02::SBC},
    /* 0xF6 */ {&R65c02::ZeroX,     &R65c02::INC},
    /* 0xF7 */ {&R65c02::SMBn<7>,   nullptr},
    /* 0xF8 */ {&R65c02::Imp,       &R65c02::SED},
    /* 0xF9 */ {&R65c02::AbsY,      &R65c02::SBC},
    /* 0xFA */ {&R65c02::Imp,       &R65c02::PLX},     // PLX
    /* 0xFB */ {&R65c02::Unoff,     nullptr},
    /* 0xFC */ {&R65c02::UnoffAbs4, nullptr},          // NOP abs,X (4 cyc)
    /* 0xFD */ {&R65c02::AbsX,      &R65c02::SBC},
    /* 0xFE */ {&R65c02::WAbsX,     &R65c02::INC},
    /* 0xFF */ {&R65c02::BBSn<7>,   nullptr},
};

// ── Dispatch / control ────────────────────────────────────────────────────

void R65c02::executeOpcode()
{
    // Count the opcode fetch so per-instruction totals match the lists.
    cycles = 1;
    if (trace_) {
        pcRing_[pcRingIdx_] = programCounter;
        pcRingIdx_ = (pcRingIdx_ + 1) % kTrail;
    }
    const uint8_t opcode = read8(programCounter++);
    if (opcode == 0x00 && onBrk) onBrk(uint16_t(programCounter - 1));
    const OpcodeEntry& entry = kCmosTable[opcode];
    (this->*entry.addrMode)();
    if (entry.operation)
        (this->*entry.operation)();
}

void R65c02::hardReset()
{
    statusRegister = 0x24 | Status::I;
    stackPointer = 0xFF;
    accumulator = 0;
    xRegister = 0;
    yRegister = 0;
    halted = false;
    programCounter = memReadAbsolute(0xFFFC);
}

void R65c02::softReset()
{
    // 65C02 reset: I set, D cleared (MAME `ow65c02.lst:814`), SP -= 3
    // (the faked-BRK pushes read instead of write), vector fetch.
    statusRegister = (statusRegister | Status::I) & static_cast<uint8_t>(~Status::D);
    halted = false;
    stackPointer = static_cast<uint8_t>(stackPointer - 3);
    programCounter = memReadAbsolute(0xFFFC);
}

void R65c02::setIrqLine(int sourceId, bool asserted)
{
    // Wire-OR semantics; atomic RMW so an off-thread source and the CPU
    // thread cannot lose each other's update.
    const uint32_t bit = 1u << (sourceId & 31);
    const uint32_t newMask = asserted
        ? (irqSourceMask.fetch_or(bit,  std::memory_order_relaxed) | bit)
        : (irqSourceMask.fetch_and(~bit, std::memory_order_relaxed) & ~bit);
    IRQ.store(newMask != 0 ? 1 : 0, std::memory_order_relaxed);
}

void R65c02::step()
{
    // STP-halted: burn cycles, ignore IRQ/NMI — only reset wakes.
    if (halted) {
        cycles = 2;
        cycleCount_ += cycles;
        return;
    }

    // NMI outranks IRQ. The 7-cycle entry sequence is captured before
    // executeOpcode() reseeds `cycles`, then folded back in.
    // Owned deviation (inherited from POM2, full derivation there):
    // interrupt sampling is instruction-granular, so an IRQ pending
    // across a CLI is taken one instruction earlier than real silicon.
    int interruptCycles = 0;
    if (NMI) {
        cycles = 0;
        handleNMI();
        interruptCycles = cycles;
    } else if (!(statusRegister & Status::I) &&
               IRQ.load(std::memory_order_relaxed)) {
        cycles = 0;
        handleIRQ();
        interruptCycles = cycles;
    }

    executeOpcode();
    cycles += interruptCycles;
    cycleCount_ += cycles;
}

int R65c02::run(int maxCycles)
{
    int cyclesExecuted = 0;
    running = 1;

    while (running && cyclesExecuted < maxCycles) {
        step();
        cyclesExecuted += cycles;
    }
    return cyclesExecuted;
}
