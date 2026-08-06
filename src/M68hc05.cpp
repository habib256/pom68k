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
        // Acking the flag WITHDRAWS a pending-but-untaken request (level
        // semantics: request = flag AND enable). m68hc05e1.cpp's
        // set_input_line(CLEAR_LINE) intends exactly this but the generic
        // m6805 base defeats it — its external-IRQ-pin latch (m6805.cpp
        // :541-546) never clears pending until taken. MAME's own detailed
        // HC05 model is the arbiter: m68hc05.cpp:313-318 clears
        // M68HC05_INT_TIMER from pending on flag ack. Keep this as-is
        // (MAME-parity audit #46, refuted 2026-08-06).
        if ((timerCtrl_ & 0x80) && !(v & 0x80)) { pending_ &= ~INT_TIMER; timerCtrl_ &= 0x7F; }
        else if ((timerCtrl_ & 0x40) && !(v & 0x40)) { pending_ &= ~INT_TIMER; timerCtrl_ &= 0xBF; }
        timerCtrl_ = uint8_t((timerCtrl_ & 0xC0) | (v & 0x3F));
        return;
    }
    if (addr == 0x0012) {                            // onesec_w :199
        // EVERY write re-arms the 1 Hz phase: the next seconds_tick lands
        // one second after this write (m68hc05e1.cpp:201, unconditional
        // m_timer->adjust(from_seconds(1), 0, from_seconds(1))). Arming
        // only once let the tick free-run from the first write instead
        // (MAME-parity audit #47).
        onesecArmed_ = true;
        onesecAcc_ = 0;
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
    // MAME-parity audit §2.10 (cosmetic, POM68K is MORE faithful — PIN,
    // 2026-08-06): the 6805 CCR is 5 bits wide and its top three bits read
    // as 1 on silicon (MC68HC05 family manual, CCR = 1 1 1 H I N Z C), so
    // the byte the interrupt sequence stacks has $E0 set. MAME pushes its
    // raw m_cc (m6805.cpp:554/561), whose bits 7-5 are whatever they were
    // initialised to — i.e. 0. Keep cc()'s `| 0xE0`. A future parity diff
    // must NOT "fix" this toward MAME: the stacked byte is what RTI pops
    // back and what a firmware that walks its own stack frame sees.
    push8(cc() /* 111HINZC */);
}

// Returns the cycles the interrupt sequence burned, 0 if none was taken.
// A real 6805 charges 11 cycles for the push + vector fetch (MAME
// m6805.cpp:541-573, `m_icount -= 11`) and then fetches the handler's first
// opcode in the same execute_run iteration. This cost used to be suppressed
// after it exposed a Cuda/VIA scheduling-phase bug on the Macintosh TV. The
// transport now advances from event deadlines instead of a coarse fixed
// peripheral batch, so the hardware cost is both affordable and stable.
int M68hc05::serviceInterrupts() {
    if (!pending_) return 0;
    if (cc_ & CC_I) return 0;
    pushState();
    cc_ |= CC_I;
    if (pending_ & INT_IRQ)        { pending_ &= ~INT_IRQ;   pc_ = read16(0x1FFA); }
    else if (pending_ & INT_TIMER) { pending_ &= ~INT_TIMER; pc_ = read16(0x1FF8); }
    else                           { pending_ &= ~INT_CPI;   pc_ = read16(0x1FF6); }
    waiting_ = false;
    return 11;                       // M6805 interrupt entry cost (m6805.cpp:570)
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
        // MAME-parity audit §2.10 (cosmetic, DOCUMENT-SKIP 2026-08-06):
        // MAME's `illegal` handler (m6805.cpp:587-590, dispatched from the
        // s_hmos/s_cmos tables) only logerrors and falls through to the next
        // opcode. POM68K latches instead: run() short-circuits while
        // illegal_ is set, freezing the MCU at the offending PC/opcode. That
        // is a debugging aid and it is load-bearing — a 6805 that fetches an
        // undefined opcode has lost its PC, and "continue quietly" turns a
        // one-line diagnostic into an MCU wandering through PRAM with the
        // host handshake half-open. It is also unreachable on the shipped
        // firmware: m68hc05_test asserts !illegal() over the whole Cuda
        // 2.37 boot, and egret_lle_test/cuda_lle_test do the same.
        // Additionally covered by the ABSOLUTE rule on this file — halting
        // vs continuing changes the MCU's instruction rate, and a 2 % shift
        // deadlocks the Mac TV (`pom68k-mactv-gate-broken`).
        // Reopening condition: a firmware dump that legitimately executes an
        // undefined opcode (some 6805 masks alias them onto real ones).
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
                // MAME-parity audit §2.10 (POM68K is MORE than MAME — PIN,
                // 2026-08-06): MAME's `stop`/`wait` handlers are
                // `fatalerror("unimplemented")` (6805ops.hxx:527-539), so
                // there is nothing to be in parity WITH. POM68K implements
                // both as the WAIT state: I cleared, core idle, on-chip
                // timers still running. STOP would additionally gate the
                // oscillator (timers frozen); approximating it as WAIT is
                // the conservative direction — a firmware that STOPs still
                // gets woken by its timer instead of hanging forever.
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
        // Shaped like MAME's execute_run (m6805.cpp:167-183): the interrupt
        // sequence PRECEDES an instruction, it never replaces one — taking a
        // vector and then falling straight through to the next opcode fetch
        // in the same iteration. (Letting it consume the iteration instead
        // costs the MCU one instruction per interrupt.)
        int cyc = serviceInterrupts();
        if (waiting_) {
            cyc += 4;                                // idle in WAIT/STOP
        } else {
            const int icyc = execOne();
            if (illegal_) { used += cyc + icyc; break; }
            cyc += icyc;
        }
        used += cyc;
        cycles_ += cyc;
        if (onCycles) onCycles(cyc);     // slaved wire advances per instr

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
            // The integrator clocks this core at a FIXED 2.097152 MHz
            // (CudaLle::kMcuHz) and the programmable timer twelve lines above
            // assumes the same, so deriving the one-second period from the PLL
            // selector was the only place with a different clock model: with
            // rate 0/1 the CPI heartbeat fired 8x/4x per second and the guest
            // RTC ran that much fast. MAME arms this timer from wall time and
            // scales only the prog timer.
            const int64_t cps = 2097152;
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
