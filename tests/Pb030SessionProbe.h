// Jaws qualification: running System, real PMU input and public session replay.
#pragma once
#include "Pb030Cpu.h"
#include "Mmu030Peek.h"
#include "PixelPin.h"
#include "SaveStateMachines.h"
#include "AgentBootProbe.h"
#include <array>

namespace pb030probe {
inline bool check(Pb030Memory& mem,Pb030Cpu& cpu,pom68k::SnapMachine profile) {
    auto frame=[&](){cpu.runCycles(mem.cpuHz()/60);};
    auto logical=[&](uint32_t a,unsigned n,uint32_t& value){
        value=0;
        for(unsigned i=0;i<n;++i) {
            uint32_t physical=0;
            if(!mmu030peek::translate(cpu.getTC(),cpu.getCRP(),cpu.getSRP(),a+i,5,
                [&](uint32_t p){return mem.peek8(p);},&physical)) return false;
            value=(value<<8)|mem.peek8(physical);
        }
        return true;
    };
    auto keyMap=[&](){
        std::array<uint32_t,4> map{};
        for(unsigned i=0;i<4;++i) if(!logical(0x174+4*i,4,map[i])) map.fill(0xffffffff);
        return map;
    };
    auto pointer=[&](){uint32_t p=0;return logical(0x830,4,p)?p:0xffffffff;};
    uint32_t length=0;std::string app;
    if(logical(0x910,1,length) && length<32)
        for(unsigned i=0;i<length;++i) {uint32_t c=0;if(logical(0x911+i,1,c)) app+=char(c);}
    const auto idle=keyMap();mem.pmu().keyEvent(0x05,true);
    bool down=false,up=false,moved=false;
    for(int f=0;f<120 && !down;++f) {frame();down=keyMap()!=idle;}
    mem.pmu().keyEvent(0x05,false);
    for(int f=0;f<120 && !up;++f) {frame();up=keyMap()==idle;}
    const uint32_t beforePointer=pointer();
    for(int f=0;f<120 && !moved;++f) {
        if(!(f%10)) mem.pmu().mouseMove(int16_t(beforePointer)<320?8:-8,
                                       int16_t(beforePointer>>16)<200?8:-8);
        frame();moved=pointer()!=beforePointer;
    }
    uint32_t tick=0;const bool readable=logical(0x16a,4,tick);
    const auto clock=cpu.getClock(),machineClock=cpu.machineClock();
    std::vector<uint32_t> pixels;mem.decode(pixels);
    const auto pixelHash=pixelpin::hashRegion(pixels,640,400,20);
    std::vector<uint8_t> bytes;
    pom68k::save(mem,cpu,profile,bytes);
    std::string error;
    const auto other=profile==pom68k::SnapMachine::Pb145?pom68k::SnapMachine::Pb145B:
        profile==pom68k::SnapMachine::Pb145B?pom68k::SnapMachine::Pb145:
        profile==pom68k::SnapMachine::Pb140?pom68k::SnapMachine::Pb170:pom68k::SnapMachine::Pb140;
    const bool refused=!pom68k::load(mem,cpu,other,bytes.data(),bytes.size(),error) &&
        error=="save state is for another machine profile";
    for(int f=0;f<20;++f) frame();
    const bool loaded=pom68k::load(mem,cpu,profile,bytes.data(),bytes.size(),error);
    mem.decode(pixels);uint32_t restoredTick=0;
    const bool restored=loaded && cpu.getClock()==clock &&
        cpu.machineClock()==machineClock && logical(0x16a,4,restoredTick) && restoredTick==tick &&
        pixelpin::hashRegion(pixels,640,400,20)==pixelHash;
    for(int f=0;f<60;++f) frame();
    uint32_t laterTick=0;const bool running=logical(0x16a,4,laterTick) && laterTick>tick;
    std::printf("Jaws app=%s keyboard=%s/%s pointer=%08x->%08x restore=%s ticks=%u->%u TC=%08x\n",
        app.c_str(),down?"down":"MISSING",up?"up":"STUCK",beforePointer,pointer(),
        restored?"exact":"FAILED",tick,laterTick,cpu.getTC());
    return agentboot::finderOrAgent(app) && down && up && moved && beforePointer!=0xffffffff && readable && restored && running && refused;
}
}
