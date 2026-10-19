// POM68K — M50753 on the PowerBook parallel/ADB buses (GPLv3).
#include "Pb030Pmu.h"
#include <algorithm>

Pb030Pmu::Pb030Pmu(int64_t hostHz):hostHz_(hostHz) {
    mcu_.readInput=[](){return uint8_t(0x30);};
    mcu_.readAnalog=[](unsigned p){return uint8_t(p==0?0x7f:p==1?0xff:p==5?0x40:p==7?0x10:0);};
    mcu_.readPort=[this](unsigned p){
        switch(p) {
        case 1:return uint8_t(0x88|(std::any_of(keys_.begin(),keys_.end(),[](bool v){return v;})?2:0));
        case 2:return fromHost_;
        case 3:return uint8_t(request_?0x80:0);
        case 4:return uint8_t(line_.line()?2:0);
        default:return uint8_t(0);
        }
    };
    mcu_.writePort=[this](unsigned p,uint8_t v){
        if(p==2) toHost_=v;
        if(p==3) {
            const bool rise=!(port3_&2) && (v&2);
            port3_=v;
            if(onInterruptPin) onInterruptPin(!(v&0x20));
            if(rise && !restoring_ && onResetRelease) onResetRelease();
        }
        if(p==4) { port4_=v;line_.setHostDrive(!(v&1)); }
    };
}

void Pb030Pmu::reset() {
    acc_=wireAcc_=0;debt_=0;request_=true;fromHost_=toHost_=port3_=port4_=0;
    keys_.fill(false);line_.reset();mcu_.reset();
}

void Pb030Pmu::tick(int cycles) {
    if(!active_) return;
    // M50753 oscillator / 4. Carry instruction overshoot into the next
    // host quantum; chunking tick() must not speed up the MCU.
    constexpr int64_t mcuHz=983040;
    acc_+=int64_t(cycles)*mcuHz;
    int budget=int(acc_/hostHz_);acc_-=int64_t(budget)*hostHz_;
    budget-=debt_;
    int used=0;
    while(used<budget) {
        const int c=mcu_.step();used+=c;
        // AdbLine's established timing domain is C15M, independent of
        // both the portable's CPU clock and its PMU oscillator.
        wireAcc_+=int64_t(c)*15667200;
        const int wire=int(wireAcc_/mcuHz);wireAcc_-=int64_t(wire)*mcuHz;
        line_.tick(wire);
    }
    debt_=used-budget;
}
