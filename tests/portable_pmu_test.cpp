// Portable-specific wiring and clock/restore contracts, without private assets.
#include "PortablePmu.h"
#include <algorithm>
#include <cstdio>

namespace {
int failures=0;
void check(bool ok,const char* text) { std::printf("%s %s\n",ok?"ok":"FAIL",text);if(!ok) ++failures; }
std::vector<uint8_t> save(PortablePmu& p) {
    std::vector<uint8_t> b;sav::Writer ar(b);ar(p);return b;
}
bool load(PortablePmu& p,const std::vector<uint8_t>& b) {
    sav::Reader ar(b.data(),b.size());ar(p);return ar.ok() && !ar.remaining();
}
std::vector<uint8_t> firmware() {
    std::vector<uint8_t> b(M50753::kRomSize,0xea);
    const uint8_t code[]={0xa2,0xbf,0x9a,0x3c,0x72,0xe9,0x3c,0x72,0xe8,
        0x3c,4,0xeb,0x3c,4,0xea,0xe6,0x40,0x80,0xfc};
    std::copy(std::begin(code),std::end(code),b.begin());
    b[0x17fe]=0;b[0x17ff]=0xe8;return b;
}
void wiring() {
    PortablePmu p(15667200);
    check(p.cpuHeld(),"missing firmware holds the 68000");
    check(p.loadFirmware(firmware()),"synthetic firmware accepted");p.reset();
    check(p.mcu().read(0xee)==0x20 && p.mcu().read(0xe2)==8,
          "Portable straps select normal boot and charger, without Jaws input bits");
    p.keyEvent(0,true);check(p.mcu().read(0xe2)==10,"any-key-down reaches PMU port 1");
    p.keyEvent(0,false);check(p.mcu().read(0xe2)==8,"releasing the last key clears any-key-down");
    p.setData(0x96);check(p.mcu().read(0xe4)==0x96,"host data reaches port 2 inputs");
    p.mcu().write(0xe5,0xff);p.mcu().write(0xe4,0x69);
    check(p.data()==0x69,"PMU output data reaches host");
    p.setRequest(false);check(!(p.mcu().read(0xe8)&0x80),"host REQ low reaches port 3 bit 7");
    p.setRequest(true);check((p.mcu().read(0xe8)&0x80)!=0,"host REQ high reaches port 3 bit 7");
    bool second=false,irq=false;int resetEdges=0;
    p.onSecondPin=[&](bool high){second=high;};p.onInterruptPin=[&](bool high){irq=high;};
    p.onResetPin=[&](bool){++resetEdges;};
    p.mcu().write(0xe9,0x72);p.mcu().write(0xe8,0x72);
    check(!p.cpuHeld() && p.ack() && second && irq && resetEdges==1,
          "SYS_RST bit 1 alone releases CPU and publishes physical VIA CA2/CB1 levels");
    p.mcu().write(0xe8,0);
    check(p.cpuHeld() && !p.ack() && !second && !irq && resetEdges==2,
          "reset assertion and low VIA/ACK levels propagate");
    p.mcu().write(0xeb,0x0f);p.mcu().write(0xea,4);
    check(!p.displayBlank() && p.adbLine().line(),"display enable and released ADB line");
    p.mcu().write(0xea,1);
    check(p.displayBlank() && !p.adbLine().line(),"display blanking and asserted ADB line");
    for(unsigned channel=0;channel<8;++channel) {
        p.mcu().write(0xf3,channel);
        check(p.mcu().read(0xef)==0xff,"Portable analog channel reads full scale");
    }
}
void clockAndRestore() {
    PortablePmu one(15667200),split(15667200);
    const auto fw=firmware();one.loadFirmware(fw);split.loadFirmware(fw);
    one.reset();split.reset();one.tick(100001);
    for(int i=0;i<100001;++i) split.tick(1);
    check(save(one)==save(split) && !one.cpuHeld(),"host tick partitioning preserves MCU debt and ADB phase");
    auto snapshot=save(one);one.tick(12345);auto expected=save(one);
    PortablePmu fresh(15667200);fresh.loadFirmware(fw);fresh.reset();
    int resets=0;bool ca2=false,cb1=false;
    fresh.onResetPin=[&](bool){++resets;};fresh.onSecondPin=[&](bool v){ca2=v;};
    fresh.onInterruptPin=[&](bool v){cb1=v;};
    check(load(fresh,snapshot) && !resets && ca2 && cb1,
          "fresh-instance restore republishes VIA levels without resetting CPU");
    fresh.tick(12345);check(save(fresh)==expected,"fresh restored MCU continues byte-identically");
    auto wrong=fw;wrong[32]^=1;PortablePmu other(15667200);other.loadFirmware(wrong);other.reset();
    const auto before=save(other);
    check(!load(other,snapshot) && save(other)==before,"different firmware is refused before mutation");
    PortablePmu wrongClock(16000000);wrongClock.loadFirmware(fw);wrongClock.reset();
    const auto initial=save(wrongClock);
    check(!load(wrongClock,snapshot) && save(wrongClock)==initial,"different host clock is refused before mutation");
    int assertions=0;one.onResetPin=[&](bool high){if(!high) ++assertions;};
    one.mcu().write(0x40,0x5a);one.reset();
    check(one.mcu().read(0x40)==0x5a && one.cpuHeld() && assertions==1,
          "controller reset retains battery RAM and asserts the host reset pin");
}
}
int main() {wiring();clockAndRestore();return failures?1:0;}
