// Public Jaws persistence contracts, using synthetic ROMs only.
#include "Pb030Cpu.h"
#include "SaveStateMachines.h"
#include <filesystem>
#include <fstream>
#include <cstdio>

int main() {
    int failures=0;
    auto check=[&](bool ok,const char* label){std::printf("%s %s\n",ok?"ok":"FAIL",label);failures+=!ok;};
    auto core=pom68k::defaultCoreConfig();
    Pb030Memory mem(core,2u<<20);Pb030Cpu cpu(mem,jit::defaultResolvedConfig(),core.cpu);
    mem.setCpu(&cpu);
    std::vector<uint8_t> rom(Pb030Memory::kRomSize,0);
    std::vector<uint8_t> firmware(M50753::kRomSize,0xea);
    firmware[0x17fe]=0;firmware[0x17ff]=0xe8;
    mem.loadRom(rom);mem.pmu().loadFirmware(firmware);cpu.hardReset();
    mem.read8(0x40000000);mem.write8(0x1234,0x56);
    mem.rtc().setXpram(0x42,0x77);mem.pmu().mcu().write(0x25,0x5b);
    std::vector<uint8_t> state;pom68k::save(mem,cpu,pom68k::SnapMachine::Pb140,state);
    mem.write8(0x1234,0xa5);
    std::string error;
    check(!pom68k::load(mem,cpu,pom68k::SnapMachine::Pb170,state.data(),state.size(),error) && mem.peek8(0x1234)==0xa5,
          "profile mismatch refuses without mutation");
    firmware[12]^=1;mem.pmu().loadFirmware(firmware);
    check(!pom68k::load(mem,cpu,pom68k::SnapMachine::Pb140,state.data(),state.size(),error) &&
          error.find("Power Manager firmware differs")!=std::string::npos && mem.peek8(0x1234)==0xa5,
          "PMU mismatch rolls back earlier RAM mutations");
    firmware[12]^=1;mem.pmu().loadFirmware(firmware);
    check(pom68k::load(mem,cpu,pom68k::SnapMachine::Pb140,state.data(),state.size(),error) && mem.peek8(0x1234)==0x56,
          "matching public container restores RAM");
    const auto dir=std::filesystem::temp_directory_path()/"pom68k-pb030-product-test";
    std::filesystem::create_directories(dir);const auto battery=(dir/"battery.pram").string();
    mem.savePram(battery);mem.rtc().setXpram(0x42,0);mem.pmu().mcu().write(0x25,0);
    check(mem.loadPram(battery) && mem.rtc().xpram(0x42)==0x77 && mem.pmu().mcu().read(0x25)==0x5b,
          "battery file restores RTC and PMU RAM together");
    firmware[12]^=1;mem.pmu().loadFirmware(firmware);mem.rtc().setXpram(0x42,0x99);
    check(!mem.loadPram(battery) && mem.rtc().xpram(0x42)==0x99,"battery firmware mismatch is atomic");
    {std::ofstream out(battery,std::ios::binary|std::ios::trunc);out<<"P68PBPR1";}
    check(!mem.loadPram(battery) && mem.rtc().xpram(0x42)==0x99,"truncated battery file is atomic");
    auto config=core.firmware;config.pmuLle=false;
    check(!mem.selectPmuFirmware(config),"required PMU cannot select HLE");
    config.pmuLle=true;config.pmuPath=(dir/"missing.bin").string();
    check(!mem.selectPmuFirmware(config),"explicit missing firmware cannot fall back to private fixtures");
    config.pmuPath=(dir/"pmu.bin").string();
    {std::ofstream out(*config.pmuPath,std::ios::binary);out.write(reinterpret_cast<const char*>(firmware.data()),firmware.size());}
    check(mem.selectPmuFirmware(config),"explicit valid required firmware loads");
    const auto devices=mem.lleRegistry().devices();
    const auto found=std::find_if(devices.begin(),devices.end(),[](const auto& d){return d.target==pom68k::FirmwareTarget::Pmu;});
    check(found!=devices.end() && found->mode==pom68k::lle::Mode::Lle && found->firmware==*config.pmuPath,
          "required firmware provenance reports the actual selected path");
    std::filesystem::remove_all(dir);
    return failures?1:0;
}
