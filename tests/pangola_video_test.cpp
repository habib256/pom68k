// WD90C26/Pangola register and packed pixel contracts without private assets.
#include "PangolaVideo.h"
#include "Pb030Cpu.h"
#include "Pb030Profile.h"
#include <cstdio>
#include "SaveStateMachines.h"
#include <filesystem>
#include <fstream>
int main() {
    int fails=0;
    auto check=[&](bool ok,const char* what){std::printf("%s %s\n",ok?"ok":"FAIL",what);fails+=!ok;};
    PangolaVideo video;video.configure(480);video.reset();
    std::vector<uint8_t> rom(0x8000,0x5a);check(video.loadRom(rom) && video.romRead(0x8001)==0x5a,"32 KB video ROM is immutable mapped data");
    for(unsigned mode=0;mode<4;++mode) {
        video.controlWrite(0,0);video.controlWrite(1,uint8_t(mode<<5));
        video.memoryWrite(0x7ffff,0x1b);
        check(video.memoryRead(0x7ffff)==0x1b,"packed 8/4/2/1 bpp reads invert Pangola expansion at the VRAM boundary");
    }
    video.controlWrite(1,0x60);video.memoryWrite(0,0xa5);video.controlWrite(1,0);
    check(video.memoryRead(0)==1 && video.memoryRead(1)==0 && video.memoryRead(2)==1 && video.memoryRead(7)==1,
          "1 bpp expands eight MSB-first pixel indices into linear VGA bytes");
    video.modeSetup(0x10);video.ioWrite(0x3c2,1);
    auto reg=[&](uint8_t r,uint8_t v){video.ioWrite(0x3d4,r);video.ioWrite(0x3d5,v);};
    video.ioWrite(0x3d4,0x2a);check(video.ioRead(0x3d5)==0xff,"PR11 reads stay locked until PR10 read unlock");
    reg(0x29,0x80);video.ioWrite(0x3d4,0x2a);
    check(video.ioRead(0x3d5)==0x70,"180c CNF15 strap is low during setup");
    check(video.ioRead(0x3c2)==0x60,"input status zero exposes CNF15 selected by the VGA clock bits");
    video.ioWrite(0x3c2,5);check(video.ioRead(0x3c2)==0x70,"input sense selects another latched CNF strap");video.ioWrite(0x3c2,1);
    PangolaVideo passive;passive.configure(400);passive.reset();passive.modeSetup(0x10);passive.ioWrite(0x3c2,1);
    passive.ioWrite(0x3d4,0x29);passive.ioWrite(0x3d5,0x80);passive.ioWrite(0x3d4,0x2a);
    check(passive.ioRead(0x3d5)==0xf0,"165c CNF15 strap is high during setup");
    reg(0x29,0x85);reg(0,95);reg(1,79);reg(6,11);reg(7,0x22);reg(0x12,0xdf);
    reg(0x13,80);reg(0x14,0x40);reg(0x17,0x80);
    video.ioWrite(0x3c4,4);video.ioWrite(0x3c5,8);
    video.ioWrite(0x3c8,0);for(auto c:{63,63,63,63,0,0}) video.ioWrite(0x3c9,uint8_t(c));
    video.ioWrite(0x3c7,1);
    check(video.ioRead(0x3c7)==3 && video.ioRead(0x3c9)==63 && video.ioRead(0x3c9)==0 && video.ioRead(0x3c9)==0,
          "RAMDAC read index, RGB phase and state follow standard VGA ports");
    video.ioWrite(0x3c8,2);for(auto c:{255,255,255}) video.ioWrite(0x3c9,uint8_t(c));
    video.ioWrite(0x3c7,2);check(video.ioRead(0x3c9)==255,"DAC preserves register bytes and masks to six bits at scanout");
    video.memoryWrite(0,1);video.memoryWrite(640,0);
    std::vector<uint32_t> out;video.decode(false,out);
    check(out.size()==640*480 && out[0]==0xff0000 && out[640]==0xffffff,"LCD renderer uses CRTC row offset and the programmed DAC");
    reg(0x0d,1);video.decode(false,out);check(out[0]==0xffffff,"CRTC start address changes scanout independently of Pangola mode");reg(0x0d,0);
    check(video.ioRead(0x3da)==0 && video.ioRead(0x3da)==0,"status polling alone cannot advance the video beam");
    video.tick(int((384000ll*33000000+25174999)/25175000),33000000);
    check((video.ioRead(0x3da)&9)==9,"machine cycles advance scanout into vertical blank");
    std::vector<uint8_t> state;sav::Writer w(state);w(video);
    video.memoryWrite(0,0);video.controlWrite(1,0x40);reg(0x13,1);
    sav::Reader r(state.data(),state.size());r(video);video.decode(false,out);
    check(r.ok() && out[0]==0xff0000 && (video.ioRead(0x3da)&8),"single visitor restores pixels, palette, indexed latches and beam phase");
    rom[0]^=1;video.loadRom(rom);video.memoryWrite(0,0x42);
    sav::Reader mismatch(state.data(),state.size());mismatch(video);
    check(!mismatch.ok() && video.memoryRead(0)==0x42,"different video firmware refuses before mutating device state");
    const auto core=pom68k::defaultCoreConfig();Pb030Memory mem(core,8u<<20,33000000,true,480);
    mem.colorVideo().loadRom(rom);mem.colorVideo().controlWrite(1,0);
    mem.write8(0xfc380000,0x27);
    check(mem.read8(0xfc000000)==0x27 && mem.read8(0xfcff8001)==0x5a && mem.read8(0xfe0fe004)==0x70,
          "board maps Pangola aliases and bounds declaration ROM without hiding external monitor sense");
    bool fault=false;try{mem.read8(0x50f20001);}catch(const moira::MmuBusError&){fault=true;}
    check(fault,"color board faults the grayscale GSC probe");
    Pb030Cpu cpu(mem,jit::defaultResolvedConfig(),core.cpu,true);mem.setCpu(&cpu);
    mem.loadRom(std::vector<uint8_t>(Pb030Memory::kRomSize,0));cpu.hardReset();
    mem.read8(0x40000000);mem.write8(0x1234,0x56);mem.write8(0xfc000000,0x31);
    std::vector<uint8_t> session;pom68k::save(mem,cpu,pom68k::SnapMachine::Pb180c,session);
    mem.write8(0x1234,0xa5);mem.write8(0xfc000000,0x42);rom[0]^=1;mem.colorVideo().loadRom(rom);
    std::string error;
    check(!pom68k::load(mem,cpu,pom68k::SnapMachine::Pb180c,session.data(),session.size(),error) &&
          error.find("Color video ROM differs")!=std::string::npos && mem.peek8(0x1234)==0xa5 && mem.read8(0xfc000000)==0x42,
          "public color session rolls back earlier RAM and VRAM on firmware mismatch");
    rom[0]^=1;mem.colorVideo().loadRom(rom);
    check(pom68k::load(mem,cpu,pom68k::SnapMachine::Pb180c,session.data(),session.size(),error) &&
          mem.peek8(0x1234)==0x56 && mem.read8(0xfc000000)==0x31,"matching video firmware restores the public session");
    const auto panel=pom68k::pb030Profile(pom68k::SnapMachine::Pb165c);
    Pb030Memory sibling(core,8u<<20,panel.cpuHz,true,panel.colorHeight);
    Pb030Cpu siblingCpu(sibling,jit::defaultResolvedConfig(),core.cpu,panel.fpu);sibling.setCpu(&siblingCpu);
    sibling.loadRom(std::vector<uint8_t>(Pb030Memory::kRomSize,0));sibling.colorVideo().loadRom(rom);siblingCpu.hardReset();
    sibling.read8(0x40000000);sibling.write8(0x1234,0xa5);sibling.write8(0xfc000000,0x42);
    sibling.decode(out);
    check(panel.cpuHz==33000000 && panel.boxFlag==44 && siblingCpu.getFPUModel()==moira::FPUModel::M68882 &&
          sibling.screenHeight()==400 && out.size()==640*400,"165c profile composes a 33 MHz 030/68882 with a 640x400 panel");
    check(!pom68k::load(sibling,siblingCpu,pom68k::SnapMachine::Pb165c,session.data(),session.size(),error) &&
          error=="save state is for another machine profile" && sibling.peek8(0x1234)==0xa5 && sibling.read8(0xfc000000)==0x42,
          "180c session refuses on 165c before changing RAM or VRAM");
    std::vector<uint8_t> small;pom68k::save(sibling,siblingCpu,pom68k::SnapMachine::Pb165c,small);
    check(!pom68k::load(mem,cpu,pom68k::SnapMachine::Pb180c,small.data(),small.size(),error) &&
          error=="save state is for another machine profile" && mem.peek8(0x1234)==0x56,
          "165c session refuses on 180c with the same main and video ROMs");
    sibling.write8(0x1234,0);sibling.write8(0xfc000000,0);
    check(pom68k::load(sibling,siblingCpu,pom68k::SnapMachine::Pb165c,small.data(),small.size(),error) &&
          sibling.peek8(0x1234)==0xa5 && sibling.read8(0xfc000000)==0x42,"165c accepts its own public session");
    auto config=core.firmware;config.colorVideoLle=false;
    check(!mem.selectColorFirmware(config),"required video firmware cannot select HLE");
    const auto dir=std::filesystem::temp_directory_path()/"pom68k-pangola-video-test";
    std::filesystem::create_directories(dir);config.colorVideoLle=true;
    config.colorVideoPath=(dir/"video.bin").string();
    check(!mem.selectColorFirmware(config),"explicit missing video ROM cannot fall back to private fixtures");
    {std::ofstream f(*config.colorVideoPath,std::ios::binary);f.put(0);}
    check(!mem.selectColorFirmware(config),"explicit truncated video ROM refuses");
    {std::ofstream f(*config.colorVideoPath,std::ios::binary);f.write(reinterpret_cast<const char*>(rom.data()),rom.size());}
    check(mem.selectColorFirmware(config),"explicit valid 32 KB video ROM loads");
    const auto devices=mem.lleRegistry().devices();
    const auto found=std::find_if(devices.begin(),devices.end(),[](const auto& d){return d.target==pom68k::FirmwareTarget::ColorVideo;});
    check(found!=devices.end() && found->mode==pom68k::lle::Mode::Lle && found->firmware==*config.colorVideoPath,
          "video provenance reports the actual selected path");
    std::filesystem::remove_all(dir);
    return fails?1:0;
}
