// Real Portable/PB100 board bring-up, with immutable host-owned media.
#include "PortableCpu.h"
#include "AssetFingerprint.h"
#include "JitTestConfig.h"
#include "InfiniteHdCompanion.h"
#include "PixelPin.h"
#include "PortableBootProbe.h"
#include "AgentBootProbe.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

static std::vector<uint8_t> bytes(const std::string& path) {
    std::ifstream f(path,std::ios::binary);return {(std::istreambuf_iterator<char>(f)),{}};
}
int main(int argc,char** argv) {
    if(argc>2 || (argc==2 && std::string_view(argv[1])!="--pb100")) {
        std::fprintf(stderr,"Usage: portable_boot_etalon [--pb100]\n");return 1;
    }
    const bool pb100=argc>1 && std::string_view(argv[1])=="--pb100";
    const auto rom=testasset::find(pb100?"roms/256KB ROMs/1991-10 - 96645F9C - PowerBook 100.ROM":
        "roms/256KB ROMs/1989-02 - 96CA3846 - Mac Portable.ROM");
    const auto pmu=testasset::find("roms/archive/macroms/pmu/Macintosh-Portable__PMU.bin");
    const auto disk=testasset::find("hdv/System 7.1 HD.dsk");
    if(rom.empty() || pmu.empty() || disk.empty()) {std::puts("SKIP: Portable ROM/PMU/System 7.1 required");return 0;}
    testasset::report({rom,pmu,disk});
    const auto config=pom68k::defaultCoreConfig();PortableMemory mem(config);
    auto firmware=config.firmware;firmware.pmuPath=pmu;
    if(!mem.loadRom(bytes(rom)) || !mem.selectPmuFirmware(firmware) || !mem.attachScsi(disk) ||
       !infinitehd::attach(mem,disk)) return 1;
    if(!agentboot::install(mem)) return 1;
    PortableCpu cpu(mem,testjit::resolveFromEnvironment());mem.setCpu(&cpu);cpu.hardReset();
    const int frames=std::getenv("POM68K_FRAMES")?std::atoi(std::getenv("POM68K_FRAMES")):3600;
    for(int f=0;f<frames && !cpu.isHalted();++f) {
        cpu.runCycles(PortableMemory::kCpuHz/60);
        if(!(f%300)) std::fprintf(stderr,"f=%d pc=%08x pmu=%04x held=%d scsi=%ld box=%u ticks=%02x%02x%02x%02x\n",
            f,cpu.getPC0(),mem.pmu().mcu().cpu().r.pc,mem.cpuHeld(),mem.scsi().commands,mem.peek8(0xcb3),
            mem.peek8(0x16a),mem.peek8(0x16b),mem.peek8(0x16c),mem.peek8(0x16d));
    }
    std::vector<uint32_t> pixels;mem.decode(pixels);
    if(std::getenv("POM68K_DUMP")) {
        FILE* f=std::fopen(pb100?"/tmp/pom68k-pb100.ppm":"/tmp/pom68k-portable.ppm","wb");
        if(f) {std::fprintf(f,"P6\n640 400\n255\n");for(auto p:pixels) {uint8_t c[]={uint8_t(p>>16),uint8_t(p>>8),uint8_t(p)};std::fwrite(c,1,3,f);}std::fclose(f);}
    }
    std::string app;const auto length=mem.peek8(0x910);
    if(length<32) for(unsigned i=0;i<length;++i) app+=char(mem.peek8(0x911+i));
    std::printf("%s engine=%s app=%s pc=%08x halted=%d scsi=%ld box=%u\n",pb100?"pb100":"portable",
        cpu.engine()?"jit":"interp",app.c_str(),cpu.getPC0(),cpu.isHalted(),mem.scsi().commands,mem.peek8(0xcb3));
    bool ok=!cpu.isHalted() && agentboot::finderOrAgent(app) && mem.scsi().commands>100 && mem.peek8(0xcb3)==(pb100?18:4);
    int menu=0,desktop=0;
    for(int y=2;y<18;++y) for(int x=0;x<640;++x) menu+=(pixels[y*640+x]&255)<128;
    for(int y=60;y<360;++y) for(int x=320;x<600;++x) desktop+=(pixels[y*640+x]&255)<128;
    if(!agentboot::enabled()) ok=ok && menu<double(640*16)*0.35 && desktop>double(280*300)*0.15 && desktop<double(280*300)*0.9;
    const auto pin=pixelpin::settleAndHash([&](std::vector<uint32_t>& out){mem.decode(out);},
        [&](long n){for(long f=0;f<n;++f) cpu.runCycles(PortableMemory::kCpuHz/60);},
        [&](){return mem.scsi().commands;},640,400,20);
    ok=pixelpin::check(pb100?"pb100_boot_etalon":"portable_boot_etalon",pin) && ok;
    if(ok) ok=portableprobe::check(mem,cpu,pb100?pom68k::SnapMachine::Pb100:pom68k::SnapMachine::Portable);
    ok=agentboot::check(mem,cpu,PortableMemory::kCpuHz/60,ok);
    std::puts(ok?"PASS: Portable Finder, PMU input and public state replay":"FAIL: Portable board qualification");
    return ok?0:1;
}
