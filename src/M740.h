// POM68K — Mitsubishi 740 CPU for the M50753 Power Manager.
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
// Instruction/addressing reference: MAME m6502/{dm740,om740}.lst,
// m740.cpp (Olivier Galibert, BSD-3-Clause). See docs/PB030_BRINGUP.md.
#pragma once
#include "SaveState.h"
#include <cstdint>
#include <functional>

class M740 {
public:
    enum Flag : uint8_t { C=1, Z=2, I=4, D=8, B=16, T=32, V=64, N=128 };
    struct Registers {
        uint8_t a=0, x=0, y=0, p=B|I, sp=0xff;
        uint16_t pc=0;
        template<class Ar> void visit(Ar& ar) { ar(a,x,y,p,sp,pc); }
    } r;
    std::function<uint8_t(uint16_t)> read8;
    std::function<void(uint16_t,uint8_t)> write8;
    void reset();                  // vector $FFFE; stack is in page ZERO
    int step();                    // instruction boundary; architectural cycles
    int run(int cycles);           // may overshoot by one instruction
    void setInterrupt(unsigned source, bool level); // INT1, X, ADC/T1, T2, INT2
    uint16_t interruptVector() const;
    int64_t cycleCount() const { return total_; }
    bool waiting() const { return wait_; }
    bool illegalOpcode() const { return illegal_; }
    template<class Ar> void visit(Ar& ar) { ar(r,total_,irq_,wait_,illegal_); }
private:
    enum class Mode { imp,acc,imm,zpg,zpx,zpy,aba,abx,aby,idx,idy,adr,ind,zpi,spg,imz,bar,bzr,rel,non };
    enum class Op { brk,ora,jsr,bbs,nop,asl,php,seb,bpl,clt,bbc,clc,dec,clb,
        and_,bit,rol,plp,bmi,set,sec,inc,ldm,rti,eor,stp,com,lsr,pha,jmp,
        bvc,kil,cli,rts,adc,tst,ror,pla,bvs,sei,bra,sta,rrf,sty,stx,dey,
        txa,bcc,tya,txs,shy,shx,ldy,lda,ldx,tay,tax,bcs,clv,tsx,cpy,cmp,
        wit,iny,dex,bne,cld,cpx,sbc,inx,beq,sed };
    struct Instruction { Op op; Mode mode; };
    static const Instruction table_[256];
    int64_t total_=0;
    int cycles_=0;
    uint8_t irq_=0;
    bool wait_=false, illegal_=false;
    uint8_t read(uint16_t a) { ++cycles_; return read8(a); }
    void write(uint16_t a,uint8_t v) { ++cycles_; write8(a,v); }
    uint8_t fetch() { return read(r.pc++); }
    uint16_t word();
    uint16_t address(Mode mode);
    void push(uint8_t v) { write(r.sp--,v); }
    uint8_t pull() { return read(++r.sp); }
    void nz(uint8_t v) { r.p=(r.p&~(N|Z))|(v&N)|(v==0 ? Z:0); }
    void flag(uint8_t f,bool on) { r.p=on ? r.p|f : r.p&~f; }
    uint8_t arithmetic(uint8_t a,uint8_t b,bool subtract);
    void compare(uint8_t a,uint8_t b);
    void interrupt(bool hardware);
    void branch(bool take);
};
