// Required Pangola declaration ROM selection; explicit choices are authoritative.
#include "Pb030Memory.h"
#include <filesystem>
#include <fstream>
#include <iterator>

bool Pb030Memory::selectColorFirmware(const pom68k::CoreFirmwareConfig& config) {
    if(!colorHeight_) return true;
    if(!config.colorVideoLle) {
        std::fprintf(stderr,"PowerBook color: video declaration ROM is required; no substitute is available.\n");
        return false;
    }
    pom68k::lle::Device d;
    d.module=pom68k::lle::PbVideoRom;d.target=pom68k::FirmwareTarget::ColorVideo;
    d.name="Pangola / WD90C26 — color LCD declaration ROM";
    d.mode=pom68k::lle::Mode::Lle;d.why=pom68k::lle::Why::LleFirmware;
    d.pathKnob="--firmware-override=pbvideo:lle:<path>";
    d.firmwareForced=config.colorVideoPath.value_or("");
    if(config.root) d.candidates.push_back((std::filesystem::path(*config.root)/"pbvideo/pb180c.bin").string());
    for(const char* base:{"","../"}) {
        d.candidates.push_back(std::string(base)+"roms/pbvideo/pb180c.bin");
        d.candidates.push_back(std::string(base)+"roms/archive/macroms/68k/1M/Powerbook 180c video card.bin");
    }
    auto tryPath=[&](const std::string& path){
        std::ifstream f(path,std::ios::binary);
        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),{});
        if(!color_.loadRom(bytes)) return false;
        d.firmware=path;return true;
    };
    if(config.colorVideoPath && !tryPath(*config.colorVideoPath)) {
        std::fprintf(stderr,"PowerBook color: cannot load video ROM %s (expected 32768 bytes).\n",config.colorVideoPath->c_str());
        return false;
    }
    if(d.firmware.empty()) for(const auto& p:d.candidates) if(tryPath(p)) break;
    if(d.firmware.empty()) {
        std::fprintf(stderr,"PowerBook color: supply roms/pbvideo/pb180c.bin or --firmware-override=pbvideo:lle:<path>.\n");
        return false;
    }
    lle_->report(d);
    std::printf("PowerBook color video ROM: %s\n",d.firmware.c_str());
    return true;
}
