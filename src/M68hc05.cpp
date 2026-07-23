// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// 68HC05E1 interpreter — see M68hc05.h for the oracle map. Opcode layout
// (M6805 family, MAME 6805ops.hxx):
//   $00-$0F BRSET/BRCLR n,dir,rel      $10-$1F BSET/BCLR n,dir
//   $20-$2F Bcc rel                    $30-$3F RMW dir
//   $40-$4F RMW A ($42 = MUL)          $50-$5F RMW X
//   $60-$6F RMW ix1                    $70-$7F RMW ix
//   $80 RTI  $81 RTS  $83 SWI  $8E STOP  $8F WAIT
//   $97 TAX $98 CLC $99 SEC $9A CLI $9B SEI $9C RSP $9D NOP $9F TXA
//   $A0-$FF ALU ops × {imm,dir,ext,ix2,ix1,ix} ($AD = BSR)
// Cycle counts = MAME s_hc_cycles (m6805.cpp:327-345) verbatim.

#include "M68hc05.h"
#include <cstring>

// MAME m6805.cpp:327-345 s_hc_cycles — 0 marks an undefined opcode.
const uint8_t M68hc05::kCycles[256] = {
    /*0*/ 5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
    /*1*/ 5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
    /*2*/ 3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
    /*3*/ 5,0,0,5,5,0,5,5,5,5,5,0,5,4,0,5,
    /*4*/ 3,0,11,3,3,0,3,3,3,3,3,0,3,3,0,3,
    /*5*/ 3,0,0,3,3,0,3,3,3,3,3,0,3,3,0,3,
    /*6*/ 6,0,0,6,6,0,6,6,6,6,6,0,6,5,0,6,
    /*7*/ 5,0,0,5,5,0,5,5,5,5,5,0,5,4,0,5,
    /*8*/ 9,6,0,10,0,0,0,0,0,0,0,0,0,0,2,2,
    /*9*/ 0,0,0,0,0,0,0,2,2,2,2,2,2,2,0,2,
    /*A*/ 2,2,2,2,2,2,2,0,2,2,2,2,0,6,2,0,
    /*B*/ 3,3,3,3,3,3,3,4,3,3,3,3,2,5,3,4,
    /*C*/ 4,4,4,4,4,4,4,5,4,4,4,4,3,6,4,5,
    /*D*/ 5,5,5,5,5,5,5,6,5,5,5,5,4,7,5,6,
    /*E*/ 4,4,4,4,4,4,4,5,4,4,4,4,3,6,4,5,
    /*F*/ 3,3,3,3,3,3,3,4,3,3,3,3,2,5,3,4,
};

bool M68hc05::loadRom(const std::vector<uint8_t>& data) {
    if (data.size() != sizeof(rom_)) return false;   // 0x1100 @ $0F00
    std::memcpy(rom_, data.data(), sizeof(rom_));
    romLoaded_ = true;
    return true;
}

void M68hc05::reset() {
    a_ = x_ = 0;
    sp_ = 0xFF;
    cc_ = CC_I;                                      // interrupts masked
    std::memset(ports_, 0, sizeof(ports_));          // all ports input
    std::memset(ddrs_, 0, sizeof(ddrs_));
    pllCtrl_ = 0; timerCtrl_ = 0; onesec_ = 0;
    pending_ = 0; waiting_ = false;
    illegal_ = false; illegalPc_ = 0; illegalOp_ = 0;
    cycles_ = 0; progTimerAcc_ = 0; onesecAcc_ = 0;
    progArmed_ = onesecArmed_ = false;
    pc_ = read16(0x1FFE);                            // device_reset rm16
}

void M68hc05::setIrqLine(bool asserted) {
    if (asserted && !irqLine_) pending_ |= INT_IRQ;  // latch on assert edge
    irqLine_ = asserted;
    if (asserted) waiting_ = false;                  // WAIT/STOP wake even
}                                                    // with I set (HC05 UM)

// ── E1 on-chip bus (m68hc05e1.cpp:225-236 m68hc05e1_map) ───────────────
uint8_t M68hc05::read8(uint16_t addr) {
    addr &= 0x1FFF;
    if (addr <= 0x0002) {                            // ports (ports_r :107)
        uint8_t in = readPort ? readPort(addr) : 0xFF;
        in &= uint8_t(~ddrs_[addr]);
        in |= ports_[addr] & ddrs_[addr];
        return in;
    }
    if (addr >= 0x0004 && addr <= 0x0006) return ddrs_[addr - 4];
    if (addr == 0x0007) return pllCtrl_;
    if (addr == 0x0008) return timerCtrl_;
    if (addr == 0x0009) return uint8_t((cycles_ / 4) & 0xFF);  // free-running
    if (addr == 0x0012) return onesec_;
    if (addr >= 0x0090 && addr <= 0x01FF) return ram_[addr];
    if (addr >= 0x0F00) return rom_[addr - 0x0F00];
    return 0;                                        // unmapped reads 0
}

void M68hc05::sendPort(int p, uint8_t data) {
    if (writePort) writePort(p, data);
    portWrites++;
}

void M68hc05::write8(uint16_t addr, uint8_t v) {
    addr &= 0x1FFF;
    if (addr <= 0x0002) {                            // ports_w :117
        int p = addr;
        sendPort(p, uint8_t((v & ddrs_[p]) | (pullups_[p] & ~ddrs_[p])));
        ports_[p] = v;
        return;
    }
    if (addr >= 0x0004 && addr <= 0x0006) {          // ddrs_w :129
        int p = addr - 4;
        v &= uint8_t(~forcedIn_[p]);                 // cuda.cpp:146-152 tap
        sendPort(p, uint8_t((ports_[p] & v) | (pullups_[p] & ~v)));
        ddrs_[p] = v;
        ddrWrites++;
        return;
    }
    if (addr == 0x0007) {                            // pll_w :140 (rate-3 cheat)
        if ((v & 3) == 2) v |= 3;
        if (pllCtrl_ != v) { progArmed_ = true; progTimerAcc_ = 0; }
        pllCtrl_ = v;                                // :158 prog_timer adjust
        pllWrites++;
        return;
    }
    if (addr == 0x0008) {                            // timer_ctrl_w :171
        if ((timerCtrl_ & 0x80) && !(v & 0x80)) { pending_ &= ~INT_TIMER; timerCtrl_ &= 0x7F; }
        else if ((timerCtrl_ & 0x40) && !(v & 0x40)) { pending_ &= ~INT_TIMER; timerCtrl_ &= 0xBF; }
        timerCtrl_ = uint8_t((timerCtrl_ & 0xC0) | (v & 0x3F));
        return;
    }
    if (addr == 0x0012) {                            // onesec_w :199
        // Every write re-arms the one-second timer (:201 m_timer->adjust).
        if (!onesecArmed_) { onesecArmed_ = true; onesecAcc_ = 0; }
        if ((onesec_ & 0x40) && !(v & 0x40)) pending_ &= ~INT_CPI;
        onesec_ = v;
        return;
    }
    if (addr >= 0x0090 && addr <= 0x01FF) { ram_[addr] = v; return; }
    // ROM/unmapped writes drop.
}

// ── Interrupt entry (interrupt_vector :66-84, priority IRQ>TIMER>CPI) ──
void M68hc05::pushState() {
    push8(uint8_t(pc_));                             // PCL
    push8(uint8_t(pc_ >> 8));                        // PCH
    push8(x_);
    push8(a_);
    push8(cc() /* 111HINZC */);
}

void M68hc05::serviceInterrupts() {
    if (!pending_) return;
    if (cc_ & CC_I) return;
    pushState();
    cc_ |= CC_I;
    if (pending_ & INT_IRQ)        { pending_ &= ~INT_IRQ;   pc_ = read16(0x1FFA); }
    else if (pending_ & INT_TIMER) { pending_ &= ~INT_TIMER; pc_ = read16(0x1FF8); }
    else                           { pending_ &= ~INT_CPI;   pc_ = read16(0x1FF6); }
    waiting_ = false;
}

// ── RMW group ($30 dir / $40 A / $50 X / $60 ix1 / $70 ix) ─────────────
uint8_t M68hc05::aluRmw(int op, uint8_t v) {
    uint8_t r = v;
    switch (op) {
        case 0x0:                                    // NEG
            r = uint8_t(-v);
            cc_ = uint8_t((cc_ & ~CC_C) | (v ? CC_C : 0));
            setNZ(r);
            break;
        case 0x3: r = uint8_t(~v); cc_ |= CC_C; setNZ(r); break;      // COM
        case 0x4:                                    // LSR
            cc_ = uint8_t((cc_ & ~CC_C) | (v & 1));
            r = v >> 1; setNZ(r);
            break;
        case 0x6: {                                  // ROR
            uint8_t cin = uint8_t((cc_ & CC_C) << 7);
            cc_ = uint8_t((cc_ & ~CC_C) | (v & 1));
            r = uint8_t((v >> 1) | cin); setNZ(r);
            break;
        }
        case 0x7:                                    // ASR
            cc_ = uint8_t((cc_ & ~CC_C) | (v & 1));
            r = uint8_t((v >> 1) | (v & 0x80)); setNZ(r);
            break;
        case 0x8:                                    // LSL/ASL
            cc_ = uint8_t((cc_ & ~CC_C) | (v >> 7));
            r = uint8_t(v << 1); setNZ(r);
            break;
        case 0x9: {                                  // ROL
            uint8_t cin = cc_ & CC_C;
            cc_ = uint8_t((cc_ & ~CC_C) | (v >> 7));
            r = uint8_t((v << 1) | cin); setNZ(r);
            break;
        }
        case 0xA: r = uint8_t(v - 1); setNZ(r); break;                // DEC
        case 0xC: r = uint8_t(v + 1); setNZ(r); break;                // INC
        case 0xD: setNZ(v); break;                                    // TST
        case 0xF: r = 0; cc_ = uint8_t((cc_ & ~CC_N) | CC_Z); break;  // CLR
        default: break;                              // holes guarded by kCycles
    }
    return r;
}

// ── ALU group ($A0-$FF): op = low nibble ───────────────────────────────
void M68hc05::aluOp(int op, uint16_t ea, bool imm) {
    // STA/STX/JMP/JSR write or jump; everything else reads the operand.
    switch (op) {
        case 0x7:                                    // STA
            write8(ea, a_); setNZ(a_); return;
        case 0xF:                                    // STX
            write8(ea, x_); setNZ(x_); return;
        case 0xC: pc_ = ea & 0x1FFF; return;         // JMP
        case 0xD:                                    // JSR
            push8(uint8_t(pc_));
            push8(uint8_t(pc_ >> 8));
            pc_ = ea & 0x1FFF;
            return;
        default: break;
    }
    uint8_t m = imm ? uint8_t(ea) : read8(ea);
    switch (op) {
        case 0x0: {                                  // SUB
            int r = a_ - m;
            cc_ = uint8_t((cc_ & ~CC_C) | (r & 0x100 ? CC_C : 0));
            a_ = uint8_t(r); setNZ(a_);
            break;
        }
        case 0x1: {                                  // CMP
            int r = a_ - m;
            cc_ = uint8_t((cc_ & ~CC_C) | (r & 0x100 ? CC_C : 0));
            setNZ(uint8_t(r));
            break;
        }
        case 0x2: {                                  // SBC
            int r = a_ - m - (cc_ & CC_C);
            cc_ = uint8_t((cc_ & ~CC_C) | (r & 0x100 ? CC_C : 0));
            a_ = uint8_t(r); setNZ(a_);
            break;
        }
        case 0x3: {                                  // CPX
            int r = x_ - m;
            cc_ = uint8_t((cc_ & ~CC_C) | (r & 0x100 ? CC_C : 0));
            setNZ(uint8_t(r));
            break;
        }
        case 0x4: a_ &= m; setNZ(a_); break;         // AND
        case 0x5: setNZ(uint8_t(a_ & m)); break;     // BIT
        case 0x6: a_ = m; setNZ(a_); break;          // LDA
        case 0x8: a_ ^= m; setNZ(a_); break;         // EOR
        case 0x9: {                                  // ADC
            int c = cc_ & CC_C;
            int r = a_ + m + c;
            cc_ = uint8_t((cc_ & ~(CC_H | CC_C))
                          | (((a_ & 0xF) + (m & 0xF) + c) > 0xF ? CC_H : 0)
                          | (r > 0xFF ? CC_C : 0));
            a_ = uint8_t(r); setNZ(a_);
            break;
        }
        case 0xA: a_ |= m; setNZ(a_); break;         // ORA
        case 0xB: {                                  // ADD
            int r = a_ + m;
            cc_ = uint8_t((cc_ & ~(CC_H | CC_C))
                          | (((a_ & 0xF) + (m & 0xF)) > 0xF ? CC_H : 0)
                          | (r > 0xFF ? CC_C : 0));
            a_ = uint8_t(r); setNZ(a_);
            break;
        }
        case 0xE: x_ = m; setNZ(x_); break;          // LDX
        default: break;
    }
}

int M68hc05::execOne() {
    uint8_t op = fetch8();
    int cyc = kCycles[op];
    if (cyc == 0) {                                  // undefined opcode
        illegal_ = true;
        illegalPc_ = uint16_t((pc_ - 1) & 0x1FFF);
        illegalOp_ = op;
        return 2;
    }
    instructions++;
    const int hi = op >> 4, lo = op & 0xF;

    switch (hi) {
        case 0x0: {                                  // BRSET/BRCLR n,dir,rel
            int bit = lo >> 1;
            uint8_t m = read8(fetch8());
            int8_t rel = int8_t(fetch8());
            bool set = (m >> bit) & 1;
            cc_ = uint8_t((cc_ & ~CC_C) | (set ? CC_C : 0));  // C ← bit
            bool take = (lo & 1) ? !set : set;
            if (take) pc_ = uint16_t((pc_ + rel) & 0x1FFF);
            break;
        }
        case 0x1: {                                  // BSET/BCLR n,dir
            int bit = lo >> 1;
            uint16_t ea = fetch8();
            uint8_t m = read8(ea);
            if (lo & 1) m &= uint8_t(~(1u << bit));
            else        m |= uint8_t(1u << bit);
            write8(ea, m);
            break;
        }
        case 0x2: {                                  // Bcc rel
            int8_t rel = int8_t(fetch8());
            const bool C = cc_ & CC_C, Z = cc_ & CC_Z, N = cc_ & CC_N;
            const bool H = cc_ & CC_H, I = cc_ & CC_I;
            bool take = false;
            switch (lo) {
                case 0x0: take = true; break;                   // BRA
                case 0x1: take = false; break;                  // BRN
                case 0x2: take = !(C || Z); break;              // BHI
                case 0x3: take = C || Z; break;                 // BLS
                case 0x4: take = !C; break;                     // BCC
                case 0x5: take = C; break;                      // BCS
                case 0x6: take = !Z; break;                     // BNE
                case 0x7: take = Z; break;                      // BEQ
                case 0x8: take = !H; break;                     // BHCC
                case 0x9: take = H; break;                      // BHCS
                case 0xA: take = !N; break;                     // BPL
                case 0xB: take = N; break;                      // BMI
                case 0xC: take = !I; break;                     // BMC
                case 0xD: take = I; break;                      // BMS
                case 0xE: take = irqLine_; break;               // BIL (/IRQ low)
                case 0xF: take = !irqLine_; break;              // BIH
            }
            if (take) pc_ = uint16_t((pc_ + rel) & 0x1FFF);
            break;
        }
        case 0x3: {                                  // RMW dir
            uint16_t ea = fetch8();
            uint8_t r = aluRmw(lo, read8(ea));
            if (lo != 0xD) write8(ea, r);            // TST doesn't write back
            break;
        }
        case 0x4:                                    // RMW A / MUL
            if (op == 0x42) {                        // MUL: X:A = X × A
                uint16_t r = uint16_t(x_) * a_;
                x_ = uint8_t(r >> 8);
                a_ = uint8_t(r);
                cc_ &= uint8_t(~(CC_H | CC_C));
            } else {
                a_ = aluRmw(lo, a_);
            }
            break;
        case 0x5: x_ = aluRmw(lo, x_); break;        // RMW X
        case 0x6: {                                  // RMW ix1
            uint16_t ea = uint16_t((x_ + fetch8()) & 0x1FFF);
            uint8_t r = aluRmw(lo, read8(ea));
            if (lo != 0xD) write8(ea, r);
            break;
        }
        case 0x7: {                                  // RMW ix
            uint16_t ea = x_;
            uint8_t r = aluRmw(lo, read8(ea));
            if (lo != 0xD) write8(ea, r);
            break;
        }
        case 0x8:
            switch (op) {
                case 0x80:                           // RTI
                    cc_ = pop8();
                    a_ = pop8();
                    x_ = pop8();
                    pc_ = uint16_t(pop8() << 8);
                    pc_ = uint16_t((pc_ | pop8()) & 0x1FFF);
                    break;
                case 0x81:                           // RTS
                    pc_ = uint16_t(pop8() << 8);
                    pc_ = uint16_t((pc_ | pop8()) & 0x1FFF);
                    break;
                case 0x83:                           // SWI
                    pushState();
                    cc_ |= CC_I;
                    pc_ = read16(0x1FFC);
                    break;
                case 0x8E:                           // STOP (clears I, sleeps)
                case 0x8F:                           // WAIT
                    cc_ &= uint8_t(~CC_I);
                    waiting_ = true;
                    break;
            }
            break;
        case 0x9:
            switch (op) {
                case 0x97: x_ = a_; break;                      // TAX
                case 0x98: cc_ &= uint8_t(~CC_C); break;        // CLC
                case 0x99: cc_ |= CC_C; break;                  // SEC
                case 0x9A: cc_ &= uint8_t(~CC_I); break;        // CLI
                case 0x9B: cc_ |= CC_I; break;                  // SEI
                case 0x9C: sp_ = 0xFF; break;                   // RSP
                case 0x9D: break;                               // NOP
                case 0x9F: a_ = x_; break;                      // TXA
            }
            break;
        case 0xA:                                    // ALU imm / BSR
            if (op == 0xAD) {                        // BSR rel
                int8_t rel = int8_t(fetch8());
                push8(uint8_t(pc_));
                push8(uint8_t(pc_ >> 8));
                pc_ = uint16_t((pc_ + rel) & 0x1FFF);
            } else {
                aluOp(lo, fetch8(), true);
            }
            break;
        case 0xB: aluOp(lo, fetch8(), false); break;             // dir
        case 0xC: aluOp(lo, fetch16(), false); break;            // ext
        case 0xD: aluOp(lo, uint16_t(x_ + fetch16()), false); break;  // ix2
        case 0xE: aluOp(lo, uint16_t(x_ + fetch8()), false); break;   // ix1
        case 0xF: aluOp(lo, x_, false); break;                   // ix
    }
    return cyc;
}

int M68hc05::run(int budget) {
    if (!romLoaded_ || illegal_) return budget;
    int used = 0;
    while (used < budget) {
        serviceInterrupts();
        int cyc;
        if (waiting_) {
            cyc = 4;                                 // idle in WAIT/STOP
        } else {
            cyc = execOne();
            if (illegal_) { used += cyc; break; }
        }
        used += cyc;
        cycles_ += cyc;

        // Programmable timer: overflow flag every 512 cycles (clock/1024 Hz
        // at 2 clocks per cycle — m68hc05e1.cpp pll_w/timer_tick). TOF sets
        // bit 7; interrupt when enabled (bit 5). Armed by the first pll_w
        // like MAME's m_prog_timer->adjust — it does NOT free-run.
        if (progArmed_) {
            progTimerAcc_ += cyc;
            while (progTimerAcc_ >= 512) {
                progTimerAcc_ -= 512;
                timerCtrl_ |= 0x80;
                if (timerCtrl_ & 0x20) { pending_ |= INT_TIMER; waiting_ = false; }
            }
        }
        // One-second timer (seconds_tick): flag bit 6, CPI int if bit 4.
        // Armed by the first onesec_w; cyclesPerSecond = pllClock/2.
        if (onesecArmed_) {
            static const int64_t kPllHz[4] = { 524288, 1048576, 2097152, 4194304 };
            const int64_t cps = kPllHz[pllCtrl_ & 3] / 2;
            onesecAcc_ += cyc;
            if (onesecAcc_ >= cps) {
                onesecAcc_ -= cps;
                onesec_ |= 0x40;
                if (onesec_ & 0x10) { pending_ |= INT_CPI; waiting_ = false; }
            }
        }
    }
    return used;
}
