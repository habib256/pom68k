// Two guest GDevices, slot-E scanout, real PMU input and public-state replay.
#pragma once
#include "Pb030SessionProbe.h"
#include <algorithm>
#include <cstdlib>

namespace pb030external {
inline bool check(Pb030Memory& mem,Pb030Cpu& cpu,const pom68k::MachineProfile& profile) {
    auto run=[&](int n){for(int f=0;f<n;++f) cpu.runCycles(mem.cpuHz()/60);};
    auto logical=[&](uint32_t a,unsigned n){uint32_t value=0;
        for(unsigned i=0;i<n;++i) {uint32_t physical=0;
            if(!mmu030peek::translate(cpu.getTC(),cpu.getCRP(),cpu.getSRP(),a+i,5,
                [&](uint32_t p){return mem.peek8(p);},&physical)) return uint32_t(0xffffffff);
            value=(value<<8)|mem.peek8(physical);
        }return value;
    };
    auto dump=[&](const char* phase){
        if(!std::getenv("POM68K_DUMP")) return;
        std::vector<uint32_t> pixels;mem.decodeDisplay(pixels);
        const auto path=std::string("/tmp/")+profile.slug+"-external-"+phase+".ppm";
        FILE* f=std::fopen(path.c_str(),"wb");if(!f) return;
        std::fprintf(f,"P6\n%d %d\n255\n",mem.displayWidth(),mem.displayHeight());
        for(auto p:pixels) {uint8_t rgb[]={uint8_t(p>>16),uint8_t(p>>8),uint8_t(p)};std::fwrite(rgb,1,3,f);}
        std::fclose(f);
    };
    bool geometry=false;unsigned devices=0;
    for(uint32_t h=logical(0x8a8,4);h && devices<4;++devices) {
        const auto gd=logical(h,4),pm=logical(logical(gd+0x16,4),4);
        const auto base=logical(pm,4),row=logical(pm+4,2)&0x3fff,depth=logical(pm+0x20,2);
        const auto top=logical(pm+6,2),left=logical(pm+8,2),bottom=logical(pm+10,2),right=logical(pm+12,2);
        std::printf("GDevice base=%08x row=%u depth=%u rect=(%u,%u)-(%u,%u)\n",base,row,depth,left,top,right,bottom);
        if(base==0xfe100000) {
            const unsigned expectedLeft=mem.screenHeight()==480?640:0;
            const unsigned expectedTop=mem.screenHeight()==480?0:unsigned(mem.screenHeight());
            geometry=row==640 && depth==8 && left==expectedLeft && right==left+640 &&
                top==expectedTop && bottom==top+480;
        }
        h=logical(gd+0x1e,4);
    }
    if(!geometry || devices!=2 || mem.externalVideo().registers()[8]!=3 || cpu.isHalted()) {
        std::puts("FAIL: two displays and the 512 KB external mode are required");return false;
    }
    if(!pb030probe::check(mem,cpu,profile.snapshot)) return false;
    const auto pin=pixelpin::settleAndHash([&](std::vector<uint32_t>& out){mem.decodeDisplay(out);},
        [&](long n){run(int(n));},[&](){return mem.scsi().commands;},mem.displayWidth(),mem.displayHeight(),20);
    const auto gate=std::string(profile.slug)+"_external_video_etalon";
    bool ok=pixelpin::check(gate.c_str(),pin);dump("desktop");
    std::vector<uint32_t> before;mem.decodeExternal(before);
    bool reached=false;
    const int targetX=mem.screenHeight()==480?840:200,targetY=mem.screenHeight()==480?120:mem.screenHeight()+120;
    for(int i=0;i<120 && !cpu.isHalted();++i) {
        const auto pointer=logical(0x830,4);const int x=int16_t(pointer),y=int16_t(pointer>>16);
        if(std::abs(x-targetX)<12 && std::abs(y-targetY)<12) {reached=true;break;}
        mem.mouseMove(std::clamp((targetX-x)/2,-16,16),std::clamp((targetY-y)/2,-16,16));run(8);
    }
    run(30);std::vector<uint32_t> after;mem.decodeExternal(after);
    const bool drew=reached && before!=after;
    dump("pointer");
    auto save=[&](){std::vector<uint8_t> bytes;pom68k::save(mem,cpu,profile.snapshot,bytes);return bytes;};
    const auto state=save();
    // CPU snapshots deliberately discard caches/ATCs (MoiraSnapshot.h).
    // Compare two restores, so both arms start with that same cold cache.
    std::string error;bool replay=pom68k::load(mem,cpu,profile.snapshot,state.data(),state.size(),error);
    replay=replay && save()==state;
    run(60);const auto expected=save();
    replay=pom68k::load(mem,cpu,profile.snapshot,state.data(),state.size(),error) && replay;
    if(replay) {run(60);replay=save()==expected;}
    std::printf("external: pointer=%08x crossed=%d pixels changed=%d replay=%d\n",logical(0x830,4),reached,drew,replay);
    return ok && drew && replay && !cpu.isHalted();
}
}
