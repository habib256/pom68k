// Guest input and deterministic raw board/CPU replay beyond a drawn desktop.
#pragma once
#include "PortableCpu.h"
#include "SaveStateMachines.h"
#include <array>
#include <cstdio>

namespace portableprobe {
inline uint32_t longword(PortableMemory& mem,uint32_t a) {
    uint32_t result=0;for(int i=0;i<4;++i) result=(result<<8)|mem.peek8(a+i);return result;
}
inline bool check(PortableMemory& mem,PortableCpu& cpu,pom68k::SnapMachine profile) {
    auto frame=[&](){cpu.runCycles(PortableMemory::kCpuHz/60);};
    auto keyMap=[&](){
        std::array<uint32_t,4> map{};
        for(unsigned i=0;i<4;++i) map[i]=longword(mem,0x174+4*i);
        return map;
    };
    const auto idle=keyMap();mem.keyEvent(5,true);
    bool down=false,up=false,moved=false;
    for(int f=0;f<120 && !down;++f) {frame();down=keyMap()!=idle;}
    mem.keyEvent(5,false);
    for(int f=0;f<120 && !up;++f) {frame();up=keyMap()==idle;}
    const auto pointer=longword(mem,0x830);
    for(int f=0;f<120 && !moved;++f) {
        if(!(f%10)) mem.mouseMove(int16_t(pointer)<320?8:-8,int16_t(pointer>>16)<200?8:-8);
        frame();moved=longword(mem,0x830)!=pointer;
    }
    auto save=[&](){std::vector<uint8_t> b;pom68k::save(mem,cpu,profile,b);return b;};
    const auto state=save();const auto tick=longword(mem,0x16a);
    const auto other=profile==pom68k::SnapMachine::Portable?pom68k::SnapMachine::Pb100:pom68k::SnapMachine::Portable;
    std::string error;
    const bool refused=!pom68k::load(mem,cpu,other,state.data(),state.size(),error) && save()==state;
    for(int f=0;f<20;++f) frame();
    const auto expected=save();const auto later=longword(mem,0x16a);
    const bool loaded=pom68k::load(mem,cpu,profile,state.data(),state.size(),error) && longword(mem,0x16a)==tick;
    for(int f=0;f<20;++f) frame();
    const bool replay=loaded && save()==expected;
    std::printf("Portable keyboard=%s/%s mouse=%08x->%08x ticks=%u->%u public replay=%s\n",
        down?"down":"MISSING",up?"up":"STUCK",pointer,longword(mem,0x830),tick,later,replay?"exact":"FAILED");
    return down && up && moved && later>tick && replay && refused && !cpu.isHalted();
}
}
