// POM68K — declrom_test gate: format block, CRC, Display sResource.
//
// Two gates in one binary, told apart by the argument:
//   declrom_test        the synthetic declaration ROM — asset-none, never
//                       skips a check;
//   toby_declrom_test   `toby`: the real Toby 342-0008-a video card ROM —
//                       asset-optional, soft-skips LOUDLY (« SKIP: ») when
//                       the dump is absent, so the census counts it.
// Until 2026-09-14 one gate carried both halves and exited 0 with the three
// Toby checks silently skipped: green, counted executed, proving nothing
// about the card (TODO § Preuve, noted 2026-09-02).

#include "AssetFingerprint.h"
#include "DeclRom.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

static int fails = 0;
static void check(bool ok, const char* msg) {
    std::printf("  %-55s %s\n", msg, ok ? "ok" : "FAIL");
    if (!ok) fails++;
}

static std::string findDeclRom() {
    // The lock's own path first (assets.lock: declaration-rom, Toby video
    // 342-0008-a), then the flat locations a dump may be dropped in.
    for (const char* p : { "roms/archive/macroms/Misc/Video cards/Apple Macintosh II Video Card/342-0008-a.bin",
                           "tests/data/342-0008-a.bin", "../tests/data/342-0008-a.bin",
                           "roms/342-0008-a.bin" }) {
        if (std::ifstream(p, std::ios::binary)) return p;
    }
    return {};
}

int main(int argc, char** argv) {
    const bool toby = argc > 1 && std::string(argv[1]) == "toby";
    if (toby) {
        const std::string path = findDeclRom();
        if (path.empty()) {
            std::printf("SKIP: needs the Toby 342-0008-a.bin video card ROM\n");
            return 0;
        }
        std::printf("toby_declrom_test — the real Toby declaration ROM\n");
        testasset::report({ path });
        std::ifstream f(path, std::ios::binary);
        std::vector<uint8_t> file((std::istreambuf_iterator<char>(f)), {});
        check(file.size() == 4096, "Toby file size 4096");
        check(DeclRom::validateFormatBlock(file.data(), file.size()), "Toby format block");
        auto installed = DeclRom::installRaw(file.data(), file.size());
        check(installed.size() == 16384, "Toby NuBus install size (lane 0 → ×4)");
        std::printf("%s\n", fails ? "FAILED" : "PASSED");
        return fails ? 1 : 0;
    }
    std::printf("declrom_test — declaration ROM builder (synthetic)\n");

    auto syn = DeclRom::buildSynthetic(0xF9000000);
    check(syn.size() > 64, "synthetic ROM non-trivial size");
    check(DeclRom::validateFormatBlock(syn.data(), syn.size()), "synthetic format block");
    check(DeclRom::dirOffset(syn.data(), syn.size()) < syn.size(), "directory in bounds");
    // Block $2A: an 18-byte header whose five routine offsets land inside
    // the block, then five two-instruction stubs; the first is Open's noErr.
    const std::array<uint8_t, 26> driverHeader = {
        0x00, 0x00, 0x00, 0x2A, 0x4C, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x12, 0x00, 0x16,
        0x00, 0x1A, 0x00, 0x1E, 0x00, 0x22, 0x70, 0x00,
        0x4E, 0x75,
    };
    check(std::search(syn.begin(), syn.end(), driverHeader.begin(),
                      driverHeader.end()) != syn.end(),
          "synthetic video driver has in-block routine offsets");

    uint32_t crcField = uint32_t(syn[syn.size() - 12]) << 24
                      | uint32_t(syn[syn.size() - 11]) << 16
                      | uint32_t(syn[syn.size() - 10]) << 8
                      | syn[syn.size() - 11 + 3];
    (void)crcField;
    std::vector<uint8_t> tmp = syn;
    tmp[tmp.size() - 12] = tmp[tmp.size() - 11] = tmp[tmp.size() - 10] = tmp[tmp.size() - 9] = 0;
    uint32_t expect = DeclRom::computeCrc(tmp.data(), tmp.size());
    uint32_t got = uint32_t(syn[syn.size() - 12]) << 24 | uint32_t(syn[syn.size() - 11]) << 16
                 | uint32_t(syn[syn.size() - 10]) << 8 | syn[syn.size() - 9];
    check(got == expect, "CRC matches recomputation");

    std::printf("%s\n", fails ? "FAILED" : "PASSED");
    return fails ? 1 : 0;
}
