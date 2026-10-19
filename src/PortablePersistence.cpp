// Required firmware provenance and atomic Portable/PB100 battery records (GPLv3).
#include "PortableMemory.h"
#include "AtomicReplace.h"
#include <filesystem>
#include <fstream>
#include <iterator>

namespace {
uint32_t batteryHash(const uint8_t* bytes,size_t size) {
    uint32_t value=2166136261u;
    for(size_t i=0;i<size;++i) {value^=bytes[i];value*=16777619u;}
    return value;
}
constexpr uint8_t magic[]={'P','6','8','P','T','P','R','1'};
}

bool PortableMemory::selectPmuFirmware(const pom68k::CoreFirmwareConfig& config) {
    if(!config.pmuLle) {
        std::fprintf(stderr,"Portable/PB100: M50753 firmware is required; no software substitute is available.\n");return false;
    }
    pom68k::lle::Device d;
    d.module=pom68k::lle::PmuFirmware;d.target=pom68k::FirmwareTarget::Pmu;
    d.name="M50753 Power Manager — keyboard / trackball / clock / power";
    d.mode=pom68k::lle::Mode::Lle;d.why=pom68k::lle::Why::LleFirmware;
    d.pathKnob="--firmware-override=pmu:lle:<path>";
    d.firmwareForced=config.pmuPath.value_or("");
    if(config.root) d.candidates.push_back((std::filesystem::path(*config.root)/"pmu/portable.bin").string());
    for(const char* base:{"","../"}) {
        for(const char* path:{"roms/pmu/portable.bin","roms/archive/macroms/pmu/Macintosh-Portable__PMU.bin",
                              "roms/archive/macroms/pmu/Powerbook-100__PMU.bin"})
            d.candidates.push_back(std::string(base)+path);
    }
    auto tryPath=[&](const std::string& path){
        std::ifstream f(path,std::ios::binary);
        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),{});
        if(!pmu_.loadFirmware(bytes)) return false;
        d.firmware=path;return true;
    };
    if(config.pmuPath && !tryPath(*config.pmuPath)) {
        std::fprintf(stderr,"Portable/PB100: cannot load required M50753 firmware %s (expected 6144 bytes).\n",config.pmuPath->c_str());return false;
    }
    if(d.firmware.empty()) for(const auto& path:d.candidates) if(tryPath(path)) break;
    if(d.firmware.empty()) {
        std::fprintf(stderr,"Portable/PB100: required M50753 firmware missing. Supply roms/pmu/portable.bin or --firmware-override=pmu:lle:<path>.\n");return false;
    }
    lle_->report(d);std::printf("Portable/PB100 M50753 firmware: %s\n",d.firmware.c_str());return true;
}

bool PortableMemory::loadPram(const std::string& path) {
    std::ifstream f(path,std::ios::binary);
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),{});
    if(bytes.size()!=216 || !std::equal(std::begin(magic),std::end(magic),bytes.begin())) return false;
    uint32_t rom=0,checksum=0;uint64_t firmware=0;std::array<uint8_t,192> ram{};
    sav::Reader ar(bytes.data()+8,bytes.size()-8);ar(rom,firmware,ram,checksum);
    if(!ar.ok() || ar.remaining() || rom!=romChecksum() || firmware!=pmu_.mcu().romIdentity() ||
       !pmu_.active() || checksum!=batteryHash(bytes.data(),bytes.size()-4)) return false;
    // All identity, integrity and length checks precede battery RAM mutation.
    for(unsigned i=0;i<ram.size();++i) pmu_.mcu().write(uint16_t(i),ram[i]);
    return true;
}
void PortableMemory::savePram(const std::string& path) {
    if(!pmu_.active()) return;
    std::vector<uint8_t> bytes(std::begin(magic),std::end(magic));
    uint32_t rom=romChecksum();uint64_t firmware=pmu_.mcu().romIdentity();std::array<uint8_t,192> ram{};
    for(unsigned i=0;i<ram.size();++i) ram[i]=pmu_.mcu().read(uint16_t(i));
    sav::Writer ar(bytes);ar(rom,firmware,ram);
    uint32_t checksum=batteryHash(bytes.data(),bytes.size());ar(checksum);
    std::ofstream f(path+".tmp",std::ios::binary|std::ios::trunc);
    if(!f) return;
    f.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));f.close();
    if(!f || !atomicReplaceFile(path+".tmp",path))
        std::fprintf(stderr,"Portable/PB100: could not save battery file %s\n",path.c_str());
}
void PortableMemory::debugDevices(std::vector<pom68k::dev::Snapshot>& out) const {
    using pom68k::dev::add;
    add(out,"VIA","VIA1",via_);add(out,"SCSI","5380",scsi_);add(out,"SCC","SCC",scc_);
    add(out,"Floppy","SWIM1",swim_);add(out,"ADB","PMU wire",pmu_.adbLine());
}
