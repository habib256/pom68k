// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "MachineFactory.h"

#include "FixtureStore.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <utility>
#if defined(__linux__)
#include <unistd.h>
#endif

namespace pom68k::app {
namespace {

constexpr std::size_t kRom256K = 256u << 10;
constexpr std::size_t kRom512K = 512u << 10;
constexpr std::size_t kRom1M = 1u << 20;

std::uint32_t checksum(const std::vector<std::uint8_t>& rom) {
    if (rom.size() < 4) return 0;
    return std::uint32_t(rom[0]) << 24 | std::uint32_t(rom[1]) << 16 |
           std::uint32_t(rom[2]) << 8 | rom[3];
}

const MachineProfile& bySnapshot(SnapMachine snapshot) {
    if (const MachineProfile* profile = machineProfile(snapshot)) return *profile;
    std::abort();
}

} // namespace

std::vector<std::uint8_t> MachineFactory::readFile(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

std::string MachineFactory::executableDirectory() {
#ifdef __linux__
    char buffer[4096];
    const ssize_t size = ::readlink("/proc/self/exe", buffer,
                                    sizeof buffer - 1);
    if (size > 0) {
        buffer[size] = 0;
        std::string path(buffer);
        const std::size_t slash = path.find_last_of('/');
        if (slash != std::string::npos) return path.substr(0, slash + 1);
    }
#endif
    return {};
}

std::vector<std::uint8_t>
MachineFactory::findResource(const std::string& relative,
                             std::string& matched) {
    const std::string executable = executableDirectory();
    for (const std::string& base :
         {std::string(), executable, executable + "../"}) {
        const std::string path = base + relative;
        auto data = readFile(path);
        if (!data.empty()) {
            matched = path;
            return data;
        }
    }
    matched = relative;
    return {};
}

std::string MachineFactory::findPath(const std::string& relative) {
    const std::string executable = executableDirectory();
    for (const std::string& base :
         {std::string(), executable, executable + "../"}) {
        const std::string path = preferReferenceFixture(base + relative);
        std::ifstream input(path, std::ios::binary);
        if (input) return path;
    }
    return {};
}

std::string
MachineFactory::findRomBySignature(const std::string& signature) {
    namespace fs = std::filesystem;
    static std::map<std::string, std::string> cache;
    if (const auto found = cache.find(signature); found != cache.end())
        return found->second;

    std::string wanted = signature;
    for (char& ch : wanted) ch = char(std::toupper(static_cast<unsigned char>(ch)));
    const std::string executable = executableDirectory();
    std::string result;
    for (const std::string& base :
         {std::string("roms"), executable + "roms", executable + "../roms"}) {
        std::error_code error;
        if (!fs::is_directory(base, error)) continue;
        for (auto it = fs::recursive_directory_iterator(base, error);
             it != fs::recursive_directory_iterator(); it.increment(error)) {
            if (error) break;
            if (!it->is_regular_file(error)) continue;
            std::string name = it->path().filename().string();
            for (char& ch : name)
                ch = char(std::toupper(static_cast<unsigned char>(ch)));
            if (name.find(wanted) != std::string::npos) {
                result = it->path().string();
                break;
            }
        }
        if (!result.empty()) break;
    }
    cache[signature] = result;
    return result;
}

const MachineProfile& MachineFactory::selectProfile(
    const RuntimeConfig& config, const std::vector<std::uint8_t>& rom) {
    const std::uint32_t id = checksum(rom);
    const MachineSelectionConfig& selected = config.machineSelection();

    if (rom.size() == kRom1M) {
        if (id == 0xECD99DC0) return bySnapshot(SnapMachine::ColorClassic);
        if (id == 0xEAF1678D) return bySnapshot(SnapMachine::MacTv);
        if (id == 0xECBBC41C || id == 0xEC904829)
            return bySnapshot(selected.sonora);
        if (id == 0x4957EB49)
            return bySnapshot(selected.vasp);
        if (id == 0xEDE66CBD) return bySnapshot(selected.aio);
        if (id == 0xF1A6F343 || id == 0xF1ACAD13)
            return bySnapshot(selected.djMemc);
        if (id == 0x420DBFF3) return bySnapshot(selected.spike);
        if (id == 0x3DC27823) return bySnapshot(SnapMachine::Quadra950);
        if (id == 0x06684214 || id == 0x064DC91D)
            return bySnapshot(selected.f108);
        if (id == 0xECFA989B) return bySnapshot(SnapMachine::Duo230);
        return bySnapshot(selected.memcJr);
    }

    if (rom.size() >= kRom256K) {
        if (id == 0xB2E362A8) return bySnapshot(SnapMachine::SE);
        if (id == 0xB306E171) return bySnapshot(SnapMachine::SEFDHD);
        if (id == 0xA49F9914) return bySnapshot(SnapMachine::Classic);
    }

    if (rom.size() == kRom512K) {
        if (id == 0x4147DD77) return bySnapshot(SnapMachine::IIfx);
        if (id == 0x36B7FB6C) return bySnapshot(SnapMachine::IIsi);
        if (id == 0x368CADFE) return bySnapshot(SnapMachine::IIci);
        if (id == 0x350EACF0) return bySnapshot(SnapMachine::Lc);
        if (id == 0x3193670E) return bySnapshot(SnapMachine::ClassicII);
        return bySnapshot(SnapMachine::LcII);
    }

    if (rom.size() == kRom256K) {
        if (id != 0x97221136) return bySnapshot(SnapMachine::MacII);
        return bySnapshot(selected.macIi);
    }

    return bySnapshot(SnapMachine::Plus);
}

bool MachineFactory::qualifiesFullLleAarch64(
    const std::vector<std::uint8_t>& rom) noexcept {
    if (rom.size() != kRom1M) return false;
    switch (checksum(rom)) {
    case 0xFF7439EE:
    case 0xF1A6F343:
    case 0xF1ACAD13:
    case 0x420DBFF3:
    case 0x3DC27823:
    case 0x06684214:
    case 0x064DC91D:
        return true;
    default:
        return false;
    }
}

MachineSession MachineFactory::create(
    RuntimeConfig config, std::unique_ptr<MachineSessionRuntime> runtime) {
    std::string matched;
    std::vector<std::uint8_t> rom;
    if (const auto romPath = config.romPath()) {
        matched = *romPath;
        rom = readFile(matched);
    } else {
        rom = findResource("roms/maclcii.rom", matched);
        if (rom.empty()) {
            const std::string path = findRomBySignature("35C28F5F");
            if (!path.empty()) { rom = readFile(path); matched = path; }
        }
        if (rom.empty()) rom = findResource("roms/macplus.rom", matched);
        if (rom.empty()) rom = findResource("roms/macii.rom", matched);
        if (rom.empty()) rom = findResource("roms/quadra605.rom", matched);
        for (const char* signature : {"9779D2C4", "FF7439EE"}) {
            if (!rom.empty()) break;
            const std::string path = findRomBySignature(signature);
            if (!path.empty()) { rom = readFile(path); matched = path; }
        }
    }

    const MachineProfile& profile = selectProfile(config, rom);
    if (config.fullLleAarch64() &&
        (profile.cpu != CpuFamily::M68040 ||
         !qualifiesFullLleAarch64(rom))) {
        return MachineSession::rejected(
            std::move(config), std::move(runtime), 2,
            "Mode LLE AArch64 complet: REFUSÉ — ROM absente ou profil "
            "non-68040 qualifié.");
    }
    return MachineSession(std::move(config), profile, std::move(rom),
                          std::move(matched), std::move(runtime));
}

} // namespace pom68k::app
