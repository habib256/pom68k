// POM68K — required Power Manager firmware and battery file (GPLv3).
#include "Pb030Memory.h"
#include "AtomicReplace.h"
#include <fstream>
#include <iterator>
#include <filesystem>

bool Pb030Memory::selectPmuFirmware(const pom68k::CoreFirmwareConfig& config) {
    if(!config.pmuLle) {
        std::fprintf(stderr,"PowerBook: M50753 firmware is required; no software substitute is available.\n");
        return false;
    }
    pom68k::lle::Device d;
    d.module=pom68k::lle::PmuFirmware;d.target=pom68k::FirmwareTarget::Pmu;
    d.name="M50753 Power Manager — keyboard / trackball / power";
    d.mode=pom68k::lle::Mode::Lle;d.why=pom68k::lle::Why::LleFirmware;
    d.pathKnob="--firmware-override=pmu:lle:<path>";
    d.firmwareForced=config.pmuPath.value_or("");
    const std::string relative=niagra_?"roms/pmu/pb160.bin":"roms/pmu/pb140.bin";
    if(config.root) d.candidates.push_back((std::filesystem::path(*config.root)/(niagra_?"pmu/pb160.bin":"pmu/pb140.bin")).string());
    for(const char* base:{"","../"}) {
        d.candidates.push_back(std::string(base)+relative);
        d.candidates.push_back(std::string(base)+(niagra_?"roms/archive/macroms/pmu/Powerbook-160-165-180__Powerbook 160 PMU.bin":"roms/archive/macroms/pmu/Powerbook-140-145-170__PMU.bin"));
    }
    auto tryPath=[&](const std::string& path){
        std::ifstream f(path,std::ios::binary);
        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),{});
        if(!pmu_.loadFirmware(bytes)) return false;
        d.firmware=path;return true;
    };
    if(config.pmuPath && !tryPath(*config.pmuPath)) {
        // An explicit required-firmware choice is authoritative. Refusing
        // prevents restoring MCU state against an unnoticed substitute ROM.
        std::fprintf(stderr,"PowerBook: cannot load required M50753 firmware %s (expected 6144 bytes).\n",config.pmuPath->c_str());
        return false;
    }
    if(d.firmware.empty()) for(const auto& p:d.candidates) if(tryPath(p)) break;
    if(d.firmware.empty()) {
        std::fprintf(stderr,"PowerBook: required M50753 firmware missing. Supply %s or --firmware-override=pmu:lle:<path>.\n",relative.c_str());
        return false;
    }
    lle_->report(d);
    std::printf("PowerBook M50753 firmware: %s\n",d.firmware.c_str());
    return true;
}

bool Pb030Memory::loadPram(const std::string& path) {
    std::ifstream f(path,std::ios::binary);
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),{});
    if(bytes.size()!=8+8+256+192) return false;
    constexpr char magic[]="P68PBPR1";
    if(!std::equal(bytes.begin(),bytes.begin()+8,magic)) return false;
    uint64_t identity=0;
    for(unsigned i=0;i<8;++i) identity|=uint64_t(bytes[8+i])<<(8*i);
    if(identity!=pmu_.mcu().romIdentity() || !pmu_.active()) return false;
    // Validate the whole record before changing either battery-backed chip.
    for(unsigned i=0;i<256;++i) rtc_.setXpram(uint8_t(i),bytes[16+i]);
    for(unsigned i=0;i<192;++i) pmu_.mcu().write(uint16_t(i),bytes[272+i]);
    return true;
}
void Pb030Memory::savePram(const std::string& path) {
    if(!pmu_.active()) return;
    std::ofstream f(path+".tmp",std::ios::binary|std::ios::trunc);
    if(!f) return;
    f.write("P68PBPR1",8);
    const uint64_t identity=pmu_.mcu().romIdentity();
    for(unsigned i=0;i<8;++i) f.put(char(identity>>(8*i)));
    for(unsigned i=0;i<256;++i) f.put(char(rtc_.xpram(uint8_t(i))));
    for(unsigned i=0;i<192;++i) f.put(char(pmu_.mcu().read(uint16_t(i))));
    f.close();
    if(!f || !atomicReplaceFile(path+".tmp",path))
        std::fprintf(stderr,"PowerBook: could not save battery file %s\n",path.c_str());
}

void Pb030Memory::debugDevices(std::vector<pom68k::dev::Snapshot>& out) const {
    using pom68k::dev::add;
    add(out,"VIA","VIA1",via_);add(out,"VIA","Misc GLU",pvia_);
    add(out,"SCSI","5380",scsi_);add(out,"SCC","SCC",scc_);
    add(out,"Floppy","SWIM1",swim_);add(out,"ADB","PMU wire",pmu_.adbLine());
}
