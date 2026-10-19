// POM68K — Mitsubishi 740 instruction interpreter (GPLv3).
// Architectural instruction boundaries, including dummy bus accesses.
// Reference/provenance: M740.h and docs/PB030_BRINGUP.md.
#include "M740.h"
#include "M740Table.h"

uint16_t M740::word() {
    const uint8_t lo=fetch();
    return uint16_t(lo | (uint16_t(fetch())<<8));
}

void M740::reset() {
    cycles_=0;
    irq_=0; wait_=illegal_=false;
    r.p|=I;
    r.sp=0xff;
    for(int i=0;i<3;++i) read(r.sp--);
    const uint8_t lo=read(0xfffe);
    r.pc=uint16_t(lo | (uint16_t(read(0xffff))<<8));
    total_+=cycles_;
}

void M740::setInterrupt(unsigned source,bool level) {
    if(source<5) {
        const uint8_t bit=uint8_t(1u<<source);
        irq_=level ? irq_|bit : irq_&~bit;
    }
}

uint16_t M740::interruptVector() const {
    for(unsigned i=0;i<5;++i)
        if(irq_&(1u<<i)) return uint16_t(0xfffa-2*i);
    return 0xfffc; // BRK with no pending interrupt
}

void M740::interrupt(bool hardware) {
    read(r.pc);
    if(!hardware) ++r.pc; else read(r.pc); // discarded opcode fetch
    push(uint8_t(r.pc>>8)); push(uint8_t(r.pc));
    push(hardware ? r.p&~B : r.p);
    const uint16_t vector=interruptVector();
    const uint8_t lo=read(vector);
    r.pc=uint16_t(lo | (uint16_t(read(vector+1))<<8));
    r.p|=I; // T and D survive IRQ entry on this CPU
}

uint16_t M740::address(Mode mode) {
    switch(mode) {
    case Mode::imm: return r.pc++;
    case Mode::zpg: return fetch();
    case Mode::zpx: case Mode::zpy: {
        const uint8_t base=fetch();
        read(uint8_t(r.pc-1)); // Mitsubishi dummy cycle is PC low, not base
        return uint8_t(base+(mode==Mode::zpx ? r.x:r.y));
    }
    case Mode::aba: return word();
    case Mode::abx: case Mode::aby: {
        const uint16_t base=word();
        const uint16_t a=uint16_t(base+(mode==Mode::abx ? r.x:r.y));
        read(uint16_t((base&0xff00)|(a&0xff))); // unconditional, even same page
        return a;
    }
    case Mode::idx: {
        const uint8_t base=fetch();
        read(uint8_t(r.pc-1));
        const uint8_t p=uint8_t(base+r.x);
        const uint8_t lo=read(p);
        return uint16_t(lo | (uint16_t(read(uint8_t(p+1)))<<8));
    }
    case Mode::idy: {
        const uint8_t p=fetch(), lo=read(p);
        const uint16_t base=uint16_t(lo | (uint16_t(read(uint8_t(p+1)))<<8));
        const uint16_t a=uint16_t(base+r.y);
        read(uint16_t((base&0xff00)|(a&0xff)));
        return a;
    }
    default: return 0;
    }
}

void M740::branch(bool take) {
    const uint16_t operand=r.pc;
    const int8_t displacement=int8_t(fetch());
    if(take) {
        read(uint16_t((operand&0xff00)|(r.pc&0xff)));
        read(uint16_t((r.pc&0xff00)|((r.pc+displacement)&0xff)));
        r.pc=uint16_t(r.pc+displacement);
    }
}

void M740::compare(uint8_t a,uint8_t b) {
    flag(C,a>=b); nz(uint8_t(a-b));
}

uint8_t M740::arithmetic(uint8_t a,uint8_t b,bool subtract) {
    const int carry=(r.p&C)!=0;
    if(subtract) {
        const int diff=int(a)-int(b)-(1-carry);
        nz(uint8_t(diff)); flag(C,diff>=0);
        flag(V,((a^b)&(a^diff)&0x80)!=0);
        if(!(r.p&D)) return uint8_t(diff);
        int lo=int(a&15)-int(b&15)-(1-carry);
        const bool borrow=lo<0;
        if(borrow) lo-=6;
        int hi=int(a>>4)-int(b>>4)-int(borrow);
        if(hi<0) hi-=6;
        return uint8_t((unsigned(hi)<<4)|(lo&15));
    }
    const int sum=int(a)+int(b)+carry;
    if(!(r.p&D)) {
        nz(uint8_t(sum)); flag(C,sum>255);
        flag(V,(~(a^b)&(a^sum)&0x80)!=0);
        return uint8_t(sum);
    }
    int lo=(a&15)+(b&15)+carry;
    if(lo>9) lo+=6;
    int hi=(a>>4)+(b>>4)+(lo>15);
    flag(Z,uint8_t(sum)==0); flag(N,(hi&8)!=0);
    flag(V,(~(a^b)&(a^(hi<<4))&0x80)!=0);
    if(hi>9) hi+=6;
    flag(C,hi>15);
    return uint8_t((hi<<4)|(lo&15));
}

int M740::step() {
    cycles_=0;
    if(illegal_) { ++total_; return 1; }
    if(wait_) {
        if(!irq_) { ++total_; return 1; }
        wait_=false;
    }
    if(irq_ && !(r.p&I)) {
        interrupt(true); total_+=cycles_; return cycles_;
    }
    const uint8_t code=fetch();
    const auto [op,mode]=table_[code];
    const uint8_t mask=uint8_t(1u<<(code>>5));
    switch(op) {
    case Op::brk: interrupt(false); break;
    case Op::kil: illegal_=true; break;
    case Op::stp: case Op::wit: read(r.pc); wait_=true; break;
    case Op::set: read(r.pc); r.p|=T; break;
    case Op::clt: read(r.pc); r.p&=~T; break;
    case Op::clc: case Op::sec: case Op::cli: case Op::sei:
    case Op::cld: case Op::sed: case Op::clv: {
        read(r.pc);
        const uint8_t f=op==Op::clc || op==Op::sec ? C :
            op==Op::cli || op==Op::sei ? I : op==Op::clv ? V:D;
        flag(f,op==Op::sec || op==Op::sei || op==Op::sed); break;
    }
    case Op::php: case Op::pha:
        read(r.pc); push(op==Op::php ? r.p:r.a); break;
    case Op::plp: case Op::pla:
        read(r.pc); read(r.sp);
        if(op==Op::plp) r.p=pull()|B; else { r.a=pull(); nz(r.a); }
        break;
    case Op::rti: case Op::rts: {
        read(r.pc); read(r.sp);
        if(op==Op::rti) r.p=pull()|B;
        const uint8_t lo=pull();
        r.pc=uint16_t(lo | (uint16_t(pull())<<8));
        if(op==Op::rts) { read(r.pc); ++r.pc; }
        break;
    }
    case Op::jsr: {
        const uint8_t lo=fetch();
        read(r.sp);
        const uint16_t ret=mode==Mode::adr ? r.pc:uint16_t(r.pc-1);
        push(uint8_t(ret>>8)); push(uint8_t(ret));
        if(mode==Mode::adr) r.pc=uint16_t(lo | (uint16_t(fetch())<<8));
        else if(mode==Mode::spg) r.pc=uint16_t(0xff00|lo);
        else {
            const uint8_t targetLo=read(lo);
            r.pc=uint16_t(targetLo | (uint16_t(read(uint8_t(lo+1)))<<8));
        }
        break;
    }
    case Op::jmp: {
        if(mode==Mode::adr) r.pc=word();
        else {
            const uint16_t pointer=mode==Mode::zpi ? fetch():word();
            const uint8_t lo=read(pointer);
            const uint16_t hi=mode==Mode::zpi ? uint8_t(pointer+1):
                uint16_t((pointer&0xff00)|uint8_t(pointer+1));
            r.pc=uint16_t(lo | (uint16_t(read(hi))<<8));
        }
        break;
    }
    case Op::ldm: {
        const uint8_t v=fetch(), a=fetch(); write(a,v); break;
    }
    case Op::bbs: case Op::bbc: {
        uint8_t v;
        if(mode==Mode::bar) { v=r.a; read(r.pc); read(r.pc); }
        else { const uint8_t a=fetch(); v=read(a); read(r.pc); }
        const bool take=((v&mask)!=0)==(op==Op::bbs);
        branch(take); break;
    }
    case Op::bra: case Op::bpl: case Op::bmi: case Op::bvc: case Op::bvs:
    case Op::bcc: case Op::bcs: case Op::bne: case Op::beq: {
        const bool take=op==Op::bra || (op==Op::bpl && !(r.p&N)) ||
            (op==Op::bmi && (r.p&N)) || (op==Op::bvc && !(r.p&V)) ||
            (op==Op::bvs && (r.p&V)) || (op==Op::bcc && !(r.p&C)) ||
            (op==Op::bcs && (r.p&C)) || (op==Op::bne && !(r.p&Z)) ||
            (op==Op::beq && (r.p&Z));
        branch(take); break;
    }
    case Op::tax: case Op::tay: case Op::txa: case Op::tya:
    case Op::tsx: case Op::txs: case Op::inx: case Op::iny:
    case Op::dex: case Op::dey: {
        read(r.pc);
        switch(op) {
        case Op::tax:r.x=r.a;nz(r.x);break; case Op::tay:r.y=r.a;nz(r.y);break;
        case Op::txa:r.a=r.x;nz(r.a);break; case Op::tya:r.a=r.y;nz(r.a);break;
        case Op::tsx:r.x=r.sp;nz(r.x);break; case Op::txs:r.sp=r.x;break;
        case Op::inx:++r.x;nz(r.x);break; case Op::iny:++r.y;nz(r.y);break;
        case Op::dex:--r.x;nz(r.x);break; default:--r.y;nz(r.y);break;
        }
        break;
    }
    default: {
        if(mode==Mode::imp) { read(r.pc); break; } // NOP
        const bool acc=mode==Mode::acc;
        const uint16_t a=acc ? 0:address(mode);
        if(op==Op::sta || op==Op::stx || op==Op::sty || op==Op::shx || op==Op::shy) {
            read(a); // Mitsubishi stores have an extra read before the write
            const uint8_t v=op==Op::sta ? r.a : op==Op::sty ? r.y :
                op==Op::shy ? uint8_t(r.y&((a>>8)+1)) :
                op==Op::shx ? uint8_t(r.x&((a>>8)+1)) : r.x;
            write(a,v); break;
        }
        uint8_t v=acc ? r.a:read(a);
        if(acc) read(r.pc);
        if(op==Op::nop) break;
        const bool rmw=op==Op::asl || op==Op::lsr || op==Op::rol ||
            op==Op::ror || op==Op::inc || op==Op::dec || op==Op::seb ||
            op==Op::clb || op==Op::com || op==Op::rrf;
        if(rmw) {
            if(!acc) read(a);
            const bool carry=(r.p&C)!=0;
            switch(op) {
            case Op::asl:flag(C,(v&128)!=0);v=uint8_t(v<<1);nz(v);break;
            case Op::lsr:flag(C,(v&1)!=0);v>>=1;nz(v);break;
            case Op::rol:flag(C,(v&128)!=0);v=uint8_t((v<<1)|carry);nz(v);break;
            case Op::ror:flag(C,(v&1)!=0);v=uint8_t((v>>1)|(carry?128:0));nz(v);break;
            case Op::inc:++v;nz(v);break; case Op::dec:--v;nz(v);break;
            case Op::seb:v|=mask;break; case Op::clb:v&=~mask;break;
            case Op::com:v^=255;nz(v);break;
            default:read(a);read(a);read(a);v=uint8_t((v<<4)|(v>>4));break;
            }
            if(acc) r.a=v; else write(a,v);
            break;
        }
        if(op==Op::ldx) { r.x=v;nz(v);break; }
        if(op==Op::ldy) { r.y=v;nz(v);break; }
        if(op==Op::cpx) { compare(r.x,v);break; }
        if(op==Op::cpy) { compare(r.y,v);break; }
        if(op==Op::tst) { nz(v);break; }
        if(op==Op::bit) { flag(Z,(r.a&v)==0);r.p=(r.p&~(N|V))|(v&(N|V));break; }
        const bool transfer=(r.p&T)!=0;
        uint8_t lhs=r.a;
        if(transfer && op!=Op::lda) lhs=read(r.x);
        if(transfer && op!=Op::cmp) read(r.x);
        switch(op) {
        case Op::ora:v=lhs|v;nz(v);break; case Op::and_:v=lhs&v;nz(v);break;
        case Op::eor:v=lhs^v;nz(v);break; case Op::lda:nz(v);break;
        case Op::adc:v=arithmetic(lhs,v,false);break;
        case Op::sbc:v=arithmetic(lhs,v,true);break;
        case Op::cmp:compare(lhs,v);break;
        default:illegal_=true;break;
        }
        if(op!=Op::cmp) { if(transfer) write(r.x,v); else r.a=v; }
        break;
    }
    }
    total_+=cycles_;
    return cycles_;
}

int M740::run(int cycles) {
    int elapsed=0;
    while(elapsed<cycles) elapsed+=step();
    return elapsed;
}
