// POM68K — declrom_test gate: format block, CRC, Display sResource.

#include "AssetFingerprint.h"
#include "DeclRom.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>

static int fails = 0;
static void check(bool ok, const char* msg) {
    std::printf("  %-55s %s\n", msg, ok ? "ok" : "FAIL");
    if (!ok) fails++;
}

static std::string findDeclRom() {
    for (const char* p : { "tests/data/342-0008-a.bin", "../tests/data/342-0008-a.bin",
                           "roms/342-0008-a.bin" }) {
        if (std::ifstream(p, std::ios::binary)) return p;
    }
    return {};
}

int main() {
    std::printf("declrom_test — declaration ROM builder\n");

    auto syn = DeclRom::buildSynthetic(0xF9000000);
    check(syn.size() > 64, "synthetic ROM non-trivial size");
    check(DeclRom::validateFormatBlock(syn.data(), syn.size()), "synthetic format block");
    check(DeclRom::dirOffset(syn.data(), syn.size()) < syn.size(), "directory in bounds");
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

    std::string path = findDeclRom();
    if (!path.empty()) {
        testasset::report({ path });
        std::ifstream f(path, std::ios::binary);
        std::vector<uint8_t> file((std::istreambuf_iterator<char>(f)), {});
        check(file.size() == 4096, "Toby file size 4096");
        check(DeclRom::validateFormatBlock(file.data(), file.size()), "Toby format block");
        auto installed = DeclRom::installRaw(file.data(), file.size());
        check(installed.size() == 16384, "Toby NuBus install size (lane 0 → ×4)");
    } else {
        std::printf("  (Toby 342-0008-a.bin absent — synthetic-only checks)\n");
    }

    std::printf("%s\n", fails ? "FAILED" : "PASSED");
    return fails ? 1 : 0;
}
