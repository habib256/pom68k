// Jaws/Misc GLU bring-up on the real 420DBFF3 ROM and M50753 firmware.
#include "Pb030Cpu.h"
#include "Pb030Profile.h"
#include "AssetFingerprint.h"
#include "JitTestConfig.h"
#include "InfiniteHdCompanion.h"
#include "Pb030SessionProbe.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <cstring>

static std::vector<uint8_t> bytes(const std::string& path) {
    std::ifstream f(path,std::ios::binary);
    return {(std::istreambuf_iterator<char>(f)),{}};
}
int main(int argc,char** argv) {
    const std::string_view argument=argc>1?argv[1]:"--pb140";
    const std::string_view slug=argument.starts_with("--")?argument.substr(2):argument;
    const pom68k::MachineProfile* profile=nullptr;
    for(const auto& candidate:pom68k::kMachineProfiles)
        if(candidate.slug==slug && candidate.platform==pom68k::PlatformKind::Jaws) profile=&candidate;
    if(!profile) {std::fprintf(stderr,"Unknown Jaws profile\n");return 1;}
    const auto board=pom68k::pb030Profile(profile->snapshot);
    const auto rom=testasset::find("roms/1MB ROMs/1991-10 - 420DBFF3 - Quadra 700&900 & PB140&170.ROM");
    const auto fw=testasset::find("roms/archive/macroms/pmu/Powerbook-140-145-170__PMU.bin");
    const auto disk=testasset::find("hdv/System 7.5.5 HD.dsk");
    if(rom.empty() || fw.empty() || disk.empty()) {std::puts("SKIP: PowerBook ROM/PMU/System 7.5.5 required");return 0;}
    testasset::report({rom,fw,disk});
    auto config=pom68k::defaultCoreConfig();
    Pb030Memory mem(config,8u<<20,board.cpuHz);
    if(!mem.loadRom(bytes(rom)) || !mem.pmu().loadFirmware(bytes(fw)) || !mem.attachScsi(disk)) return 1;
    if(!infinitehd::attach(mem,disk)) return 1;
    if(!agentboot::install(mem)) return 1;
    Pb030Cpu cpu(mem,testjit::resolveFromEnvironment(),config.cpu,board.fpu);
    mem.setCpu(&cpu);cpu.hardReset();
    std::printf("Jaws engine=%s\n",cpu.engine()?"jit":"interp");
    const int frames=std::getenv("POM68K_FRAMES")?std::atoi(std::getenv("POM68K_FRAMES")):2400;
    for(int frame=0;frame<frames && !cpu.isHalted();++frame) {
        cpu.runCycles(mem.cpuHz()/60);
        if(!(frame%600)) std::fprintf(stderr,"f=%d pc=%08x pmu=%04x held=%d scsi=%ld box=%d ticks=%02x%02x%02x%02x\n",
            frame,cpu.getPC0(),mem.pmu().mcu().cpu().r.pc,mem.cpuHeld(),mem.scsi().commands,mem.peek8(0xcb3),
            mem.peek8(0x16a),mem.peek8(0x16b),mem.peek8(0x16c),mem.peek8(0x16d));
    }
    std::vector<uint32_t> fb;mem.decode(fb);
    if(std::getenv("POM68K_DUMP")) {
        FILE* f=std::fopen("pb140_screen.ppm","wb");
        if(f) {std::fprintf(f,"P6\n640 400\n255\n");for(auto p:fb){uint8_t c[]={uint8_t(p>>16),uint8_t(p>>8),uint8_t(p)};std::fwrite(c,1,3,f);}std::fclose(f);}
    }
    int menu=0,desk=0;
    for(int y=2;y<18;++y) for(int x=0;x<640;++x) menu+=fb[y*640+x]==0;
    for(int y=60;y<360;++y) for(int x=320;x<600;++x) desk+=fb[y*640+x]==0;
    const double menuRatio=menu/double(640*16),deskRatio=desk/double(280*300);
    std::printf("%s pc=%08x halted=%d box=%u menu=%.3f desktop=%.3f SCSI=%ld\n",
        profile->slug,cpu.getPC0(),cpu.isHalted(),mem.peek8(0xcb3),menuRatio,deskRatio,mem.scsi().commands);
    bool ok=!cpu.isHalted() && mem.peek8(0xcb3)==board.boxFlag && mem.scsi().commands>500 &&
        menuRatio<0.35 && deskRatio>0.15 && deskRatio<0.90;
    const auto pin=pixelpin::settleAndHash([&](std::vector<uint32_t>& out){mem.decode(out);},
        [&](long n){for(long i=0;i<n;++i) cpu.runCycles(mem.cpuHz()/60);},
        [&](){return mem.scsi().commands;},640,400,20);
    ok=pixelpin::check((std::string(profile->slug)+"_boot_etalon").c_str(),pin) && ok;
    if(ok) ok=pb030probe::check(mem,cpu,profile->snapshot);
    ok=agentboot::check(mem,cpu,mem.cpuHz()/60,ok);
    std::puts(ok?"PASS: Jaws Finder, PMU input and state replay":"FAIL: Jaws qualification");
    return ok?0:1;
}
