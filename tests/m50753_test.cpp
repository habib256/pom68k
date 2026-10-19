// M50753 register/peripheral proof using synthetic Mitsubishi firmware.
#include "M50753.h"
#include <algorithm>
#include <cstdio>
#include <vector>

namespace {
int failures=0;
void check(bool ok,const char* what) {
    std::printf("%s %s\n",ok?"ok":"FAIL",what);if(!ok) ++failures;
}
std::vector<uint8_t> firmware() {
    std::vector<uint8_t> rom(M50753::kRomSize,0xea);
    // Stack below the I/O window, IRQs initially masked, then idle.
    const uint8_t code[]={0xa2,0xbf,0x9a,0x80,0xfe};
    std::copy(std::begin(code),std::end(code),rom.begin());
    rom[0x17fe]=0;rom[0x17ff]=0xe8;
    return rom;
}
std::vector<uint8_t> save(M50753& m) {
    std::vector<uint8_t> out;sav::Writer ar(out);ar(m);return out;
}
bool load(M50753& m,const std::vector<uint8_t>& bytes) {
    sav::Reader ar(bytes.data(),bytes.size());ar(m);
    return ar.ok() && !ar.remaining();
}
void portsAndReset() {
    M50753 m;
    check(!m.loadRom(std::vector<uint8_t>(6143)),"wrong-sized ROM is refused");
    check(m.loadRom(firmware()),"6144-byte M50753 ROM maps at E800");
    uint8_t output[5]={};
    m.readPort=[](unsigned p){return uint8_t(p==2 ? 0xa5:0xff);};
    m.writePort=[&](unsigned p,uint8_t v){output[p]=v;};
    m.readInput=[](){return uint8_t(0x30);};
    m.setPullups(2,0xf0);m.reset();
    check(m.read(0xe5)==0 && m.read(0xe4)==0xa5 && output[2]==0xf0,"reset leaves port inputs with independent external pullups");
    m.write(0xe4,0x3c);m.write(0xe5,0x0f);
    check(m.read(0xe4)==0xac && output[2]==0xfc,"DDR masks input reads and driven output bits separately");
    m.write(0xeb,0xff);m.write(0xea,0xff);
    check(m.read(0xeb)==15 && m.read(0xea)==15 && output[4]==15,"port 4 is four bits wide");
    m.write(0x70,0x5a);m.write(0xe800,0);
    check(m.read(0xe800)==0xa2 && m.read(0xc0)==0xff && m.read(0xee)==0x30,"ROM is immutable; RAM holes and input-only port decode");
    m.write(0xfe,0x83);
    check(m.read(0xfe)==1,"software cannot manufacture external interrupt requests");
    m.setInterrupt(0,true);m.write(0xfe,0xc1);m.setInterrupt(1,true);
    check(m.cpu().interruptVector()==0xfffa,"INT1 has higher priority than INT2");
    m.setInterrupt(0,false);
    check(m.cpu().interruptVector()==0xfff2,"INT2 has its own vector after INT1 deasserts");
    m.reset();
    check(m.read(0x70)==0x5a && m.read(0xe5)==0 && m.read(0xfe)==0 && output[2]==0xf0,
          "reset preserves battery-backed RAM, clears controls and republishes pin levels");
}
void timersAndAdc() {
    M50753 m(4000000);m.loadRom(firmware());m.reset();m.run(8);
    m.write(0xf9,0);m.write(0xfa,2);m.write(0xfb,3);
    m.run(12);check(!(m.read(0xfe)&0x28),"timer prescaler does not fire either timer early");
    m.run(4);check((m.read(0xfe)&0x20) && m.read(0xfa)==2,"timer 1 expires at 16 CPU cycles and reloads");
    m.run(8);check((m.read(0xfe)&0x08) && m.read(0xfb)==3,"timer 2 shares prescaler, retains its own countdown");
    m.write(0xfc,0);m.write(0xfd,1);m.write(0xff,0x60);
    m.run(16);check(!(m.read(0xff)&0x80),"timer X halt inhibits countdown");
    m.write(0xff,0x40);m.run(8);
    check((m.read(0xff)&0xc0)==0xc0 && m.cpu().interruptVector()==0xfff8,"free-running timer X raises its own vector");
    m.write(0xff,0x40);check(!(m.read(0xff)&0x80),"timer X request clears in software");
    m.reset();m.run(8);
    m.readAnalog=[](unsigned p){return uint8_t(p==5 ? 0x40:0x7f);};
    m.write(0xf3,5);m.write(0xf2,0);m.write(0xfe,0x10);
    m.run(68);check(!(m.read(0xfe)&0x20),"ADC conversion is not complete before 72 microseconds");
    auto mid=save(m);m.run(4);
    check((m.read(0xfe)&0x20) && m.cpu().interruptVector()==0xfff6,"ADC completion requests the shared ADC/timer 1 vector");
    check(m.read(0xef)==0x40 && !(m.read(0xfe)&0x20),"ADC result selects the channel and acknowledges its request");
    check(load(m,mid),"snapshot restores a conversion in flight");
    m.run(4);check(m.read(0xfe)&0x20,"restored ADC completes at the original deadline");
}
void firmwareInterruptAndSnapshot() {
    M50753 m;auto rom=firmware();
    // CLI; WIT; BRA back to WIT. ISR increments RAM $40 and acks INT1.
    const uint8_t program[]={0xa2,0xbf,0x9a,0x58,0xc2,0x80,0xfd};
    std::copy(std::begin(program),std::end(program),rom.begin());
    const uint8_t isr[]={0xe6,0x40,0xa9,0x40,0x85,0xfe,0x40};
    std::copy(std::begin(isr),std::end(isr),rom.begin()+0x100);
    rom[0x17fa]=0;rom[0x17fb]=0xe9;
    m.loadRom(rom);m.reset();m.write(0xfe,0x40);m.run(20);
    check(m.cpu().waiting(),"synthetic PMU firmware sleeps at WIT");
    m.setInterrupt(0,true);m.run(50);
    check(m.read(0x40)==1 && m.cpu().waiting() && !(m.read(0xfe)&0x80),"firmware IRQ executes once, acks INT1 and returns to WIT");
    m.write(0xf9,3);m.write(0xfa,7);m.write(0xfb,11);m.write(0xfc,5);m.write(0xfd,13);
    m.run(13);auto before=save(m);m.run(2000);auto after=save(m);
    uint8_t driven=0;m.writePort=[&](unsigned p,uint8_t v){if(p==2) driven=v;};
    m.write(0xe5,255);m.write(0xe4,0x77);
    check(load(m,before) && driven==0,"load republishes restored outputs through the receiving instance's callbacks");
    m.run(2000);
    check(save(m)==after,"RAM, CPU, timer phase, IRQs and wait state replay byte-for-byte after restore");
}
}
int main() { portsAndReset();timersAndAdc();firmwareInterruptAndSnapshot();return failures?1:0; }
