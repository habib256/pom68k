// Mitsubishi-specific execution and binary-ALU differential against the
// independently Klaus-qualified R65C02. No private firmware required.
#include "M740.h"
#include "R65c02.h"
#include <array>
#include <cstdio>
#include <vector>

namespace {
int failures=0;
void check(bool ok,const char* label) {
    std::printf("%s %s\n",ok?"ok":"FAIL",label);if(!ok) ++failures;
}
struct Fixture {
    std::array<uint8_t,65536> ram{};
    M740 cpu;
    Fixture() {
        cpu.read8=[this](uint16_t a){return ram[a];};
        cpu.write8=[this](uint16_t a,uint8_t v){ram[a]=v;};
        cpu.r.pc=0x200;
    }
    void code(std::initializer_list<uint8_t> bytes) {
        uint16_t a=0x200;for(auto b:bytes) ram[a++]=b;
        cpu.r.pc=0x200;
    }
};

void testStackAndVectors() {
    Fixture f;
    f.ram[0xfffe]=0;f.ram[0xffff]=2;
    f.ram[0xfffc]=0;f.ram[0xfffd]=3;
    f.cpu.reset();
    check(f.cpu.r.pc==0x200 && f.cpu.r.sp==0xfc,"reset vector is FFFE, stack starts in page zero");
    f.code({0xa2,0xbf,0x9a,0xa9,0x57,0x48,0xa9,0,0x68,0x20,0,3});
    f.ram[0x300]=0x60;
    for(int i=0;i<5;++i) f.cpu.step();
    check(f.ram[0xbf]==0x57 && f.ram[0x1bf]==0 && f.cpu.r.sp==0xbe,
          "PHA uses zero-page stack without touching page one");
    f.cpu.step();
    check(f.cpu.r.a==0x57 && f.cpu.r.sp==0xbf,"PLA restores the accumulator");
    check(f.cpu.step()==6 && f.cpu.r.pc==0x300,"JSR absolute destination and cycles");
    check(f.cpu.step()==6 && f.cpu.r.pc==0x20c && f.cpu.r.sp==0xbf,"RTS returns after both address bytes");
    f.cpu.r.pc=0x220;f.ram[0x220]=0;f.ram[0x300]=0x40;
    f.cpu.r.p=M740::B|M740::D|M740::T;
    check(f.cpu.step()==7 && f.cpu.r.pc==0x300 && f.ram[0xbd]==(M740::B|M740::D|M740::T),
          "BRK uses FFFC and pushes real transfer/decimal flags");
    f.cpu.step();
    check(f.cpu.r.pc==0x222 && (f.cpu.r.p&(M740::D|M740::T))==(M740::D|M740::T),
          "RTI restores transfer mode and the BRK return address");
    f.ram[0xfffa]=0x40;f.ram[0xfffb]=3;f.ram[0x340]=0x40;
    f.ram[0xfff2]=0x80;f.ram[0xfff3]=3;
    f.cpu.r.p&=~M740::I;f.cpu.setInterrupt(4,true);f.cpu.setInterrupt(0,true);
    check(f.cpu.step()==7 && f.cpu.r.pc==0x340 && !(f.ram[0xbd]&M740::B),
          "INT1 wins priority over INT2; hardware frame clears B");
    f.cpu.setInterrupt(0,false);f.cpu.setInterrupt(4,false);f.cpu.step();
    check(f.cpu.r.pc==0x222,"hardware IRQ returns to the interrupted instruction");
    f.code({0x02,0x40});f.ram[0x40]=0x50;f.ram[0x41]=3;f.ram[0x350]=0x60;
    check(f.cpu.step()==7 && f.cpu.r.pc==0x350,"JSR (zp) resolves its pointer");
    f.cpu.step();check(f.cpu.r.pc==0x202,"JSR (zp) stacks the operand address");
    f.code({0x22,0x90});f.ram[0xff90]=0x60;
    check(f.cpu.step()==5 && f.cpu.r.pc==0xff90,"special-page JSR targets FFxx");
    f.cpu.step();check(f.cpu.r.pc==0x202,"special-page JSR returns after its operand");
}

void testMitsubishiInstructions() {
    Fixture f;
    f.code({0x3c,0x96,0x40,0x82,0x40,0x44,0x40,0x64,0x40});
    check(f.cpu.step()==4 && f.ram[0x40]==0x96,"LDM #imm,zp stores without using A");
    const auto p=f.cpu.r.p;
    check(f.cpu.step()==8 && f.ram[0x40]==0x69 && f.cpu.r.p==p,"RRF swaps nibbles and preserves flags");
    check(f.cpu.step()==5 && f.ram[0x40]==0x96 && (f.cpu.r.p&M740::N),"COM complements memory and updates NZ");
    f.cpu.r.a=0x44;
    check(f.cpu.step()==3 && (f.cpu.r.p&M740::N) && f.cpu.r.a==0x44,"TST tests memory without changing A");
    bool bits=true;
    for(unsigned bit=0;bit<8;++bit) {
        f.cpu.r.p=0xd5;f.cpu.r.a=0;f.code({uint8_t(0x0b+32*bit)});
        bits&=f.cpu.step()==2 && f.cpu.r.a==(1u<<bit) && f.cpu.r.p==0xd5;
        f.code({uint8_t(0x1b+32*bit)});f.cpu.step();bits&=f.cpu.r.a==0;
        f.ram[0x40]=0;f.code({uint8_t(0x0f+32*bit),0x40});
        bits&=f.cpu.step()==5 && f.ram[0x40]==(1u<<bit);
        f.code({uint8_t(0x1f+32*bit),0x40});f.cpu.step();bits&=f.ram[0x40]==0;
        f.cpu.r.a=uint8_t(1u<<bit);f.code({uint8_t(0x03+32*bit),0xfc});
        bits&=f.cpu.step()==6 && f.cpu.r.pc==0x1fe;
        f.cpu.r.a=0;f.code({uint8_t(0x03+32*bit),0xfc});
        bits&=f.cpu.step()==4 && f.cpu.r.pc==0x202;
        f.ram[0x40]=0;f.code({uint8_t(0x17+32*bit),0x40,0xfb});
        bits&=f.cpu.step()==7 && f.cpu.r.pc==0x1fe;
        f.ram[0x40]=uint8_t(1u<<bit);f.code({uint8_t(0x17+32*bit),0x40,0xfb});
        bits&=f.cpu.step()==5 && f.cpu.r.pc==0x203;
    }
    check(bits,"all eight accumulator/memory bit operations and taken/untaken branches");
    f.code({0x80,0xfc});check(f.cpu.step()==4 && f.cpu.r.pc==0x1fe,"BRA has fixed four-cycle timing across a page");
    f.code({0xa9,0x33,0xa2,0x40,0x32,0xa9,0x7f,0x18,0x69,1,0xc9,0x80,0x12});
    f.cpu.r.p=M740::B|M740::I;
    for(int i=0;i<3;++i) f.cpu.step();
    check(f.cpu.step()==4 && f.ram[0x40]==0x7f && f.cpu.r.a==0x33,"SET redirects LDA to zero-page X");
    f.cpu.step();
    check(f.cpu.step()==5 && f.ram[0x40]==0x80 && f.cpu.r.a==0x33 && (f.cpu.r.p&M740::V),
          "transfer ADC updates memory, flags and extra cycles");
    check(f.cpu.step()==3 && (f.cpu.r.p&M740::Z),"transfer CMP reads zero-page X");
    f.cpu.step();check(!(f.cpu.r.p&M740::T),"CLT restores accumulator operations");
    f.code({0xc2,0xea});f.cpu.r.p|=M740::I;
    f.cpu.step();const auto pc=f.cpu.r.pc;f.cpu.step();
    check(f.cpu.waiting() && f.cpu.r.pc==pc,"WIT waits without fetching instructions");
    f.cpu.setInterrupt(0,true);f.cpu.step();
    check(!f.cpu.waiting() && f.cpu.r.pc==pc+1,"a masked IRQ wakes WIT without entering its handler");
    f.cpu.setInterrupt(0,false);f.cpu.r.sp=0xbf;f.code({0x02,0xff});
    f.ram[0xff]=0x33;f.ram[0]=3;f.ram[0x333]=0x60;
    f.cpu.step();check(f.cpu.r.pc==0x333,"JSR (zp) wraps its pointer at FF to 00");
    f.cpu.step();f.code({0x6c,0xff,3});f.ram[0x3ff]=0x44;f.ram[0x300]=4;f.ram[0x400]=5;
    f.cpu.step();check(f.cpu.r.pc==0x444,"JMP indirect keeps the NMOS page-boundary wrap");
    f.code({0x52});f.cpu.step();
    check(f.cpu.illegalOpcode(),"reserved KIL opcodes halt with an observable diagnostic");
    f.ram[0xfffe]=0;f.ram[0xffff]=2;f.cpu.reset();
    check(!f.cpu.illegalOpcode() && f.cpu.r.sp==0xfc,"reset clears the illegal-opcode halt and reinitializes SP");
}

void testBusAndDecimal() {
    Fixture f;
    std::vector<uint16_t> reads;
    f.cpu.read8=[&](uint16_t a){reads.push_back(a);return f.ram[a];};
    f.cpu.r.x=1;f.code({0xbd,0x10,3});f.ram[0x311]=0x42;
    check(f.cpu.step()==5 && reads==std::vector<uint16_t>({0x200,0x201,0x202,0x311,0x311}),
          "indexed read always includes its dummy cycle on the same page");
    reads.clear();f.code({0x85,0x40});
    check(f.cpu.step()==4 && reads==std::vector<uint16_t>({0x200,0x201,0x40}) && f.ram[0x40]==0x42,
          "store performs the Mitsubishi read before write");
    f.cpu.r.a=0x50;f.cpu.r.p=M740::B|M740::D;f.code({0x69,0x50});f.cpu.step();
    check(f.cpu.r.a==0 && (f.cpu.r.p&(M740::C|M740::N|M740::V|M740::Z))==(M740::C|M740::N|M740::V),
          "BCD ADC uses pre-adjust NMOS flags, not CMOS zero flags");
    f.cpu.r.a=0;f.cpu.r.p=M740::B|M740::D|M740::C;f.code({0xe9,1});f.cpu.step();
    check(f.cpu.r.a==0x99 && !(f.cpu.r.p&M740::C) && (f.cpu.r.p&M740::N),"BCD subtract propagates decimal borrow");
}

void differential() {
    Fixture f;
    R65c02 oracle;
    std::array<uint8_t,65536> memory{};
    oracle.read8=[&](uint16_t a){return memory[a];};
    oracle.write8=[&](uint16_t a,uint8_t v){memory[a]=v;};
    // Base-6502 data/ALU instructions whose architectural semantics are common.
    // Stack, decimal, timing and Mitsubishi opcodes have separate explicit tests.
    constexpr uint8_t codes[]={0x01,0x05,0x06,0x09,0x0a,0x0d,0x0e,
        0x11,0x15,0x16,0x19,0x1d,0x1e,0x21,0x24,0x25,0x26,0x29,0x2a,0x2c,0x2d,0x2e,
        0x31,0x35,0x36,0x39,0x3d,0x3e,0x41,0x45,0x46,0x49,0x4a,0x4d,0x4e,
        0x51,0x55,0x56,0x59,0x5d,0x5e,0x61,0x65,0x66,0x69,0x6a,0x6d,0x6e,
        0x71,0x75,0x76,0x79,0x7d,0x7e,0x81,0x84,0x85,0x86,0x88,0x8a,0x8c,0x8d,0x8e,
        0x91,0x94,0x95,0x96,0x98,0x99,0x9a,0x9d,0xa0,0xa1,0xa2,0xa4,0xa5,0xa6,
        0xa8,0xa9,0xaa,0xac,0xad,0xae,0xb1,0xb4,0xb5,0xb6,0xb9,0xba,0xbc,0xbd,0xbe,
        0xc0,0xc1,0xc4,0xc5,0xc6,0xc8,0xc9,0xca,0xcc,0xcd,0xce,
        0xd1,0xd5,0xd6,0xd9,0xdd,0xde,0xe0,0xe1,0xe4,0xe5,0xe6,0xe8,0xe9,0xec,0xed,0xee,
        0xf1,0xf5,0xf6,0xf9,0xfd,0xfe};
    uint32_t seed=0x50753;
    auto random=[&](){seed=seed*1664525+1013904223;return uint8_t(seed>>24);};
    bool ok=true;
    for(auto code:codes) for(int trial=0;trial<64;++trial) {
        for(auto& b:f.ram) b=random();
        f.ram[0x200]=code;memory=f.ram;
        f.cpu.r={random(),random(),random(),uint8_t((random()&0xc7)|M740::B),random(),0x200};
        oracle.setAccumulator(f.cpu.r.a);oracle.setXRegister(f.cpu.r.x);oracle.setYRegister(f.cpu.r.y);
        oracle.setStatusRegister(f.cpu.r.p|0x20);oracle.setStackPointer(f.cpu.r.sp);oracle.setProgramCounter(0x200);
        f.cpu.step();oracle.step();
        const auto& r=f.cpu.r;
        if(r.a!=oracle.getAccumulator() || r.x!=oracle.getXRegister() || r.y!=oracle.getYRegister() ||
           r.sp!=oracle.getStackPointer() || r.pc!=oracle.getProgramCounter() ||
           ((r.p^oracle.getStatusRegister())&0xcf) || f.ram!=memory) {
            std::printf("differential mismatch opcode %02x trial %d\n",code,trial);ok=false;break;
        }
    }
    check(ok,"common binary ALU/data semantics match the independently qualified R65C02 (64 states per opcode)");
}
}
int main() {
    testStackAndVectors();testMitsubishiInstructions();testBusAndDecimal();differential();
    return failures ? 1:0;
}
