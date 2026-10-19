// Portable/PB100 public containers, battery validation and required firmware.
#include "PortableCpu.h"
#include "SaveStateMachines.h"
#include "PortableEnv.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cstdio>

namespace {
int failures=0;
void check(bool ok,const char* label) {std::printf("%s %s\n",ok?"ok":"FAIL",label);failures+=!ok;}
std::vector<uint8_t> rom(uint32_t id=0x96ca3846) {
    std::vector<uint8_t> bytes(PortableMemory::kRomSize,0);
    for(unsigned i=0;i<4;++i) bytes[i]=uint8_t(id>>(24-8*i));
    bytes[5]=0x90;bytes[6]=1;bytes[0x100]=0x60;bytes[0x101]=0xfe;return bytes;
}
std::vector<uint8_t> firmware() {
    std::vector<uint8_t> bytes(M50753::kRomSize,0xea);
    const uint8_t code[]={0xa2,0xbf,0x9a,0x3c,0x72,0xe9,0x3c,0x72,0xe8,
        0xe6,0x40,0x80,0xfc};
    std::copy(std::begin(code),std::end(code),bytes.begin());
    bytes[0x17fe]=0;bytes[0x17ff]=0xe8;return bytes;
}
void write(const std::string& path,const std::vector<uint8_t>& bytes) {
    std::ofstream f(path,std::ios::binary|std::ios::trunc);
    f.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
}
}
int main() {
    using pom68k::SnapMachine;
    auto config=pom68k::defaultCoreConfig();
    PortableMemory mem(config);PortableCpu cpu(mem,jit::defaultResolvedConfig());mem.setCpu(&cpu);
    auto fw=firmware();mem.loadRom(rom());mem.pmu().loadFirmware(fw);cpu.hardReset();
    mem.seedRtc(0x12345678);cpu.runCycles(10000);
    check(!mem.cpuHeld() && mem.pmu().mcu().read(0x25)==0x12 && mem.pmu().mcu().read(0x28)==0x78,
          "injected host time reaches the PMU clock once at host reset release");
    mem.write8(0x1234,0x56);mem.pmu().mcu().write(0x30,0x77);
    auto save=[&](SnapMachine profile){std::vector<uint8_t> out;pom68k::save(mem,cpu,profile,out);return out;};
    std::string error;
    for(auto profile:{SnapMachine::Portable,SnapMachine::Pb100}) {
        const auto state=save(profile);
        const auto other=profile==SnapMachine::Portable?SnapMachine::Pb100:SnapMachine::Portable;
        mem.write8(0x1234,0xa5);const auto before=save(profile);
        check(!pom68k::load(mem,cpu,other,state.data(),state.size(),error) &&
              error=="save state is for another machine profile" && save(profile)==before,
              "Portable/PB100 public profile refusal is atomic in both directions");
        check(pom68k::load(mem,cpu,profile,state.data(),state.size(),error) && mem.peek8(0x1234)==0x56,
              "each Portable-family public container restores RAM");
    }
    const auto state=save(SnapMachine::Portable);
    mem.write8(0x1234,0xa5);fw[32]^=1;mem.pmu().loadFirmware(fw);const auto before=save(SnapMachine::Portable);
    check(!pom68k::load(mem,cpu,SnapMachine::Portable,state.data(),state.size(),error) &&
          error.find("Power Manager firmware differs")!=std::string::npos && save(SnapMachine::Portable)==before,
          "firmware mismatch rolls back all earlier machine mutations");
    fw[32]^=1;mem.pmu().loadFirmware(fw);
    check(pom68k::load(mem,cpu,SnapMachine::Portable,state.data(),state.size(),error),"matching firmware restores the public state");
    const auto stable=save(SnapMachine::Portable);
    check(!pom68k::load(mem,cpu,SnapMachine::Portable,state.data(),state.size()-7,error) &&
          save(SnapMachine::Portable)==stable,"truncated public state is refused without mutation");
    mem.loadRom(rom(0x96645f9c));
    check(!pom68k::load(mem,cpu,SnapMachine::Portable,state.data(),state.size(),error) && mem.peek8(0x1234)==0x56,
          "different main ROM cannot restore a same-size board's state");mem.loadRom(rom());
    PortableMemory small(config,4u<<20);small.loadRom(rom());small.pmu().loadFirmware(fw);
    PortableCpu smallCpu(small,jit::defaultResolvedConfig());small.setCpu(&smallCpu);smallCpu.hardReset();
    check(!pom68k::load(small,smallCpu,SnapMachine::Portable,state.data(),state.size(),error),"RAM-size mismatch refuses before loading chunks");

    const auto directory=std::filesystem::path(pom68kProcessTempPath("portable_product",""));
    std::filesystem::create_directories(directory);
    const auto battery=(directory/"battery.pram").string();
    mem.savePram(battery);mem.pmu().mcu().write(0x30,0);
    check(mem.loadPram(battery) && mem.pmu().mcu().read(0x30)==0x77 &&
          mem.pmu().mcu().read(0x25)==0x12,"battery file restores PMU PRAM and RTC RAM together");
    std::ifstream input(battery,std::ios::binary);std::vector<uint8_t> record((std::istreambuf_iterator<char>(input)),{});input.close();
    mem.pmu().mcu().write(0x30,0x99);record[30]^=1;write(battery,record);
    check(!mem.loadPram(battery) && mem.pmu().mcu().read(0x30)==0x99,"corrupt battery checksum is refused atomically");
    record[30]^=1;write(battery,record);fw[32]^=1;mem.pmu().loadFirmware(fw);
    check(!mem.loadPram(battery) && mem.pmu().mcu().read(0x30)==0x99,"battery firmware identity is enforced");
    fw[32]^=1;mem.pmu().loadFirmware(fw);mem.loadRom(rom(0x96645f9c));
    check(!mem.loadPram(battery) && mem.pmu().mcu().read(0x30)==0x99,"Portable battery cannot silently seed PB100");mem.loadRom(rom());
    record.pop_back();write(battery,record);
    check(!mem.loadPram(battery) && mem.pmu().mcu().read(0x30)==0x99,"truncated battery file is refused atomically");
    auto policy=config.firmware;policy.pmuLle=false;
    check(!mem.selectPmuFirmware(policy),"required PMU cannot select HLE");
    policy.pmuLle=true;policy.pmuPath=(directory/"absent.bin").string();
    check(!mem.selectPmuFirmware(policy),"an explicit missing firmware never falls back to archive fixtures");
    policy.pmuPath=(directory/"pmu.bin").string();write(*policy.pmuPath,{1,2,3});
    check(!mem.selectPmuFirmware(policy),"an explicit malformed firmware never falls back");
    write(*policy.pmuPath,fw);check(mem.selectPmuFirmware(policy),"explicit valid PMU firmware loads");
    const auto devices=mem.lleRegistry().devices();
    const auto found=std::find_if(devices.begin(),devices.end(),[](const auto& d){return d.target==pom68k::FirmwareTarget::Pmu;});
    check(found!=devices.end() && found->mode==pom68k::lle::Mode::Lle && found->firmware==*policy.pmuPath,
          "firmware provenance reports the selected path to the GUI");
    check(mem.attachCdromEmpty(3) && mem.bayIsCdrom(3) && !mem.detachScsi(3),"product composes the empty CD bay and refuses HDD detachment of it");
    std::filesystem::remove_all(directory);return failures?1:0;
}
