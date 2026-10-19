// Niagra/GSC contracts without private ROM or media.
#include "Pb030Cpu.h"
#include "Pb030Profile.h"
#include "SaveStateMachines.h"
#include <cstdio>

int main() {
    int failed=0;
    auto check=[&](bool ok,const char* why){std::printf("%s %s\n",ok?"ok":"FAIL",why);failed+=!ok;};
    auto core=pom68k::defaultCoreConfig();
    Pb030Memory mem(core,8u<<20,25000000,true);
    Pb030Cpu cpu(mem,jit::defaultResolvedConfig(),core.cpu);mem.setCpu(&cpu);
    std::vector<uint8_t> rom(Pb030Memory::kRomSize,0);mem.loadRom(rom);
    cpu.hardReset();mem.read8(0x40000000);
    check(mem.read8(0x50f20001)==5 && !mem.read8(0x50000000) && mem.read8(0x50f00200)==0x93,
          "Niagra has panel 5 and exact 50F I/O selects, without Jaws mirrors");
    check(mem.read8(0x50fb6000)==2 && mem.read8(0x50fb4000)==0xff && !mem.read8(0x50fb6001),
          "CPU speed and access detect registers occupy the upper byte lane");
    mem.write8(0x6ffe0000,0x81);
    check(mem.read8(0x60000000)==0x81 && !mem.read8(0xfee00000),"GSC VRAM aliases 128 KB across the 60000000 select; DDC is absent");
    check(mem.read8(0xfe0fe004)==0x70,"external video reports no connected monitor");
    mem.write8(0xfe100000,0x42);check(mem.read8(0xfe100000)==0x42,"external video RAM is independent storage");
    mem.write8(0x50f96000,0x77);check(mem.read8(0x50f96000)==0x77,"Ponti modem latch reads back");
    mem.write8(0x50f14000,0);
    check(mem.read8(0x50f96400)&8,"sound write sets Ponti activity latch");
    mem.write8(0x50f96400,4);check(!(mem.read8(0x50f96400)&8),"sound latch clears on the clear bit's rising edge");
    mem.write8(0x50f14000,0);mem.write8(0x50f96400,4);
    check(mem.read8(0x50f96400)&8,"held clear bit does not invent a rising edge");
    Pb030Niagra video;std::vector<uint8_t> vram(0x20000);std::vector<uint32_t> out;
    video.gscWrite(4,0x20);vram[0]=0x80;video.decode(vram,false,out);
    check(out.size()==640*400 && out[0]==0x0f0f0f && out[1]==0xffffff,"GSC 1 bpp uses MSB first and the inverted grayscale palette");
    video.gscWrite(4,0x21);vram[0]=0x1b;video.decode(vram,false,out);
    check(out[0]==0xffffff && out[1]==0xbfbfbf && out[2]==0x7f7f7f && out[3]==0x3f3f3f,"GSC 2 bpp produces four ordered gray levels");
    video.gscWrite(4,0x22);vram[0]=0x0f;video.decode(vram,false,out);
    check(out[0]==0xffffff && out[1]==0x0f0f0f,"GSC 4 bpp converts high then low nibble");
    video.decode(vram,true,out);check(out[0]==0x0f0f0f && out.back()==0x0f0f0f,"PMU blank overrides enabled GSC output");
    mem.write8(0x50f20004,0x21);
    std::vector<uint8_t> saved;pom68k::save(mem,cpu,pom68k::SnapMachine::Pb160,saved);
    mem.write8(0x60000000,0x11);mem.write8(0x50f20004,0x22);mem.write8(0xfe100000,0x33);
    std::string error;
    check(pom68k::load(mem,cpu,pom68k::SnapMachine::Pb160,saved.data(),saved.size(),error) &&
          mem.read8(0x60000000)==0x81 && mem.read8(0x50f20004)==0x21 && mem.read8(0xfe100000)==0x42 && mem.read8(0x50f96000)==0x77,
          "public state restores GSC mode, both VRAM banks and Ponti latches");
    for(auto profile:{pom68k::SnapMachine::Pb165,pom68k::SnapMachine::Pb180}) {
        const auto board=pom68k::pb030Profile(profile);
        Pb030Memory sibling(core,8u<<20,board.cpuHz,true);
        Pb030Cpu siblingCpu(sibling,jit::defaultResolvedConfig(),core.cpu,board.fpu);
        sibling.setCpu(&siblingCpu);sibling.loadRom(rom);siblingCpu.hardReset();sibling.read8(0x40000000);
        check(sibling.read8(0x50fb6000)==3 && sibling.read8(0x50f20001)==5 &&
              siblingCpu.getFPUModel()==(profile==pom68k::SnapMachine::Pb180?moira::FPUModel::M68882:moira::FPUModel::NONE),
              "33 MHz Niagra speed register and real CPU FPU model distinguish 165/180");
        std::vector<uint8_t> session;pom68k::save(sibling,siblingCpu,profile,session);
        sibling.write8(0x1234,0xa5);
        const auto other=profile==pom68k::SnapMachine::Pb165?pom68k::SnapMachine::Pb180:pom68k::SnapMachine::Pb165;
        check(!pom68k::load(sibling,siblingCpu,other,session.data(),session.size(),error) &&
              error=="save state is for another machine profile" && sibling.peek8(0x1234)==0xa5,
              "165/180 cross-profile state refusal leaves RAM intact");
        check(pom68k::load(sibling,siblingCpu,profile,session.data(),session.size(),error),"33 MHz profile accepts its own session");
    }
    std::vector<uint8_t> fw(M50753::kRomSize,0xea);fw[0x17fe]=0;fw[0x17ff]=0xe8;
    Pb030Pmu whole(33000000),split(33000000);whole.loadFirmware(fw);split.loadFirmware(fw);
    whole.reset();split.reset();whole.tick(330000);
    for(int i=0;i<3300;++i) split.tick(100);
    std::vector<uint8_t> a,b;sav::Writer wa(a),wb(b);wa(whole);wb(split);
    check(a==b && whole.mcu().cpu().cycleCount()>9800 && whole.mcu().cpu().cycleCount()<9840,
          "33 MHz host timing keeps the independent PMU clock invariant under partitioning");
    return failed?1:0;
}
