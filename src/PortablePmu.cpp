// MAME-derived board pin wiring: see docs/PORTABLE_BRINGUP.md for notice.
#include "PortablePmu.h"
#include <algorithm>
#include <stdexcept>

PortablePmu::PortablePmu(int64_t hostHz):hostHz_(hostHz) {
    if(hostHz<=0) throw std::invalid_argument("Portable PMU host clock must be positive");
    mcu_.readInput=[](){return uint8_t(0x20);}; // normal boot, not target disk mode
    mcu_.readAnalog=[](unsigned){return uint8_t(0xff);};
    mcu_.readPort=[this](unsigned p){
        switch(p) {
        case 1:return uint8_t(0x08|(std::any_of(keys_.begin(),keys_.end(),[](bool v){return v;})?2:0));
        case 2:return fromHost_;
        case 3:return uint8_t(request_?0x80:0);
        case 4:return uint8_t(line_.line()?2:0);
        default:return uint8_t(0);
        }
    };
    mcu_.writePort=[this](unsigned p,uint8_t v){
        if(p==2) toHost_=v;
        if(p==3) {
            const bool resetChanged=((port3_^v)&2)!=0;
            port3_=v;
            if(onSecondPin) onSecondPin((v&0x10)!=0);
            if(onInterruptPin) onInterruptPin((v&0x20)!=0);
            if(resetChanged && !restoring_ && onResetPin) onResetPin((v&2)!=0);
        }
        if(p==4) { port4_=v;line_.setHostDrive(!(v&1)); }
    };
    mcu_.setPullups(2,0xff);
    mcu_.setPullups(4,0x0f);
}

void PortablePmu::reset() {
    acc_=wireAcc_=0;debt_=0;request_=true;fromHost_=toHost_=0;
    keys_.fill(false);line_.reset();mcu_.reset();
}

void PortablePmu::tick(int cycles) {
    if(!active_ || cycles<=0) return;
    // Oscillator / 4; carry instruction overshoot across host quanta.
    constexpr int64_t mcuHz=983040;
    acc_+=int64_t(cycles)*mcuHz;
    int budget=int(acc_/hostHz_);acc_-=int64_t(budget)*hostHz_;
    budget-=debt_;
    int used=0;
    while(used<budget) {
        const int c=mcu_.step();used+=c;
        // ADB wire timing is C15M, independently of the 68000 clock.
        wireAcc_+=int64_t(c)*15667200;
        const int wire=int(wireAcc_/mcuHz);wireAcc_-=int64_t(wire)*mcuHz;
        line_.tick(wire);
    }
    debt_=used-budget;
}
