// Finder Sleep -> real PMU reset hold -> key -> ROM warm resume.
// Apple PowerMgr.a: GoToSleep/TakeASnooze/WakeUp; MAME macprtb.cpp: SYS_RST.
#pragma once
#include "PortableBootProbe.h"
#include <algorithm>
#include <cstdlib>

namespace portablesleep {
inline bool check(PortableMemory& mem,PortableCpu& cpu,pom68k::SnapMachine profile) {
    auto run=[&](int n){for(int f=0;f<n;++f) cpu.runCycles(PortableMemory::kCpuHz/60);};
    auto peek=[&](uint32_t a){return portableprobe::longword(mem,a);};
    auto key=[&](uint8_t code){mem.keyEvent(code,true);run(10);mem.keyEvent(code,false);};
    auto save=[&](){std::vector<uint8_t> out;pom68k::save(mem,cpu,profile,out);return out;};
    auto dump=[&](const char* name){
        if(!std::getenv("POM68K_DUMP")) return;
        std::vector<uint32_t> pixels;mem.decode(pixels);
        const auto path=std::string("/tmp/")+(profile==pom68k::SnapMachine::Pb100?"pb100":"portable")+"-sleep-"+name+".ppm";
        FILE* out=std::fopen(path.c_str(),"wb");if(!out) return;
        std::fprintf(out,"P6\n640 400\n255\n");
        for(auto p:pixels) {uint8_t c[]={uint8_t(p>>16),uint8_t(p>>8),uint8_t(p)};std::fwrite(c,1,3,out);}
        std::fclose(out);
    };
    auto steer=[&](int x,int y){
        for(int i=0;i<120;++i) {
            const auto pos=peek(0x830);const int dx=x-int16_t(pos),dy=y-int16_t(pos>>16);
            if(std::abs(dx)<=3 && std::abs(dy)<=3) return true;
            auto step=[](int d){return std::abs(d)<=3?0:std::clamp(d/2,-8,8);};
            mem.mouseMove(step(dx),step(dy));run(4);
        }
        std::printf("FAIL sleep pointer: %08x target %d,%d\n",peek(0x830),x,y);return false;
    };
    // The pinned System 7.1 fixture opens an unsupported-QuickTime alert.
    // Dismiss it through ADB, then let Finder open its startup window.
    key(0x24);run(120);
    if(!steer(230,10)) return false;
    mem.mouseButton(true);run(120);dump("menu");
    if(!steer(230,170)) {mem.mouseButton(false);return false;}
    mem.mouseButton(false);run(120);dump("confirmation");
    // Confirm Finder's AppleTalk warning using the default Sleep button.
    const auto beforeTicks=peek(0x16a);
    key(0x24);
    for(int f=0;f<600 && !mem.cpuHeld() && !cpu.isHalted();++f) run(1);
    if(!mem.cpuHeld() || cpu.isHalted() || !mem.pmu().displayBlank()) {
        std::printf("FAIL sleep entry: held=%d blank=%d halted=%d pc=%08x\n",
            mem.cpuHeld(),mem.pmu().displayBlank(),cpu.isHalted(),cpu.getPC0());return false;
    }
    // Machine cycles must advance the continuously powered PMU while the
    // host executes nothing. PC, registers, Ticks and the SCSI bus stay idle.
    const auto pc=cpu.getPC0(),ticks=peek(0x16a);
    auto registers=[&](){
        std::array<uint32_t,17> out{};
        for(int i=0;i<8;++i) {out[i]=cpu.getD(i);out[8+i]=cpu.getA(i);}
        out[16]=cpu.getSR();return out;
    };
    const auto sleepingRegisters=registers();
    const auto commands=mem.scsi().commands;
    const auto sleeping=save();
    run(600);const auto later=save();
    const bool held=mem.cpuHeld() && mem.pmu().displayBlank() && cpu.getPC0()==pc &&
        peek(0x16a)==ticks && registers()==sleepingRegisters && mem.scsi().commands==commands;
    bool busIdle=true;
    for(int id=0;id<7;++id) busIdle=busIdle && !mem.scsi().sessionOn(id);
    dump("asleep");
    std::string error;
    bool replay=pom68k::load(mem,cpu,profile,sleeping.data(),sleeping.size(),error);
    if(replay) {run(600);replay=save()==later;}
    // Replay the wake from the same asleep container as well. Neither the
    // ROM nor the System is patched, and no PMgr command is injected.
    auto wake=[&](){key(0x31);run(600);};
    wake();const auto awake=save();
    bool wakeReplay=pom68k::load(mem,cpu,profile,later.data(),later.size(),error);
    if(wakeReplay) {wake();wakeReplay=save()==awake;}
    std::string app;const auto length=mem.peek8(0x910);
    if(length<32) for(unsigned i=0;i<length;++i) app+=char(mem.peek8(0x911+i));
    const bool resumed=!mem.cpuHeld() && !mem.pmu().displayBlank() && !cpu.isHalted() &&
        app=="Finder" && peek(0x16a)>beforeTicks;
    dump("awake");
    std::printf("sleep: idle=%d asleep replay=%d wake replay=%d resumed=%d Ticks=%u->%u app=%s\n",
        held,replay,wakeReplay,resumed,beforeTicks,peek(0x16a),app.c_str());
    // Keyboard, trackball and public snapshots must still work after wake.
    return held && busIdle && replay && wakeReplay && resumed && portableprobe::check(mem,cpu,profile);
}
}
