// POM68K — flat HFS ('LK') → in-memory SCSI DDM façade.
// Soft-skips without an Infinite Mac-style .dsk + HD20SC/boot template.

#include "ScsiDisk.h"
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

static std::string find(const char* rel) {
    for (const std::string base : { std::string(), std::string("../") }) {
        std::string p = base + rel;
        if (std::ifstream(p, std::ios::binary)) return p;
    }
    return {};
}

int main() {
    std::string dsk = find("hdv/System 6.0.8 HD.dsk");
    if (dsk.empty()) dsk = find("hdv/System 5.1 HD.dsk");
    std::string tpl = find("hdv/HD20SC.vhd");
    if (tpl.empty()) tpl = find("hdv/boot.vhd");
    if (dsk.empty() || tpl.empty()) {
        std::printf("SKIP: needs flat HFS .dsk + HD20SC/boot.vhd template\n");
        return 0;
    }

    // Confirm source is still bare HFS on disk.
    {
        std::ifstream in(dsk, std::ios::binary);
        char sig[2] = {};
        in.read(sig, 2);
        if (sig[0] != 'L' || sig[1] != 'K') {
            std::fprintf(stderr, "FAIL: %s is not bare HFS ('LK')\n", dsk.c_str());
            return 1;
        }
    }

    ScsiDisk disk;
    if (!disk.open(dsk)) {
        std::fprintf(stderr, "FAIL: open %s\n", dsk.c_str());
        return 1;
    }
    if (!disk.flatHfsFacade() || disk.hfsPrefixBlocks() != 96) {
        std::fprintf(stderr, "FAIL: façade not applied (flat=%d prefix=%u)\n",
                     int(disk.flatHfsFacade()), disk.hfsPrefixBlocks());
        return 1;
    }
    auto& img = disk.image();
    if (img.size() < 512 || img[0] != 'E' || img[1] != 'R') {
        std::fprintf(stderr, "FAIL: LBA0 is not DDM 'ER'\n");
        return 1;
    }
    // HFS payload still at LBA 96.
    size_t hfs0 = size_t(disk.hfsPrefixBlocks()) * 512;
    if (img.size() <= hfs0 + 2 || img[hfs0] != 'L' || img[hfs0 + 1] != 'K') {
        std::fprintf(stderr, "FAIL: HFS 'LK' missing at LBA %u\n", disk.hfsPrefixBlocks());
        return 1;
    }
    // ddType $6A present for LC II.
    int count = (img[0x10] << 8) | img[0x11];
    bool has6A = false, has0001 = false;
    for (int i = 0; i < count; i++) {
        int e = 0x12 + i * 8;
        int typ = (img[e + 6] << 8) | img[e + 7];
        if (typ == 0x6A) has6A = true;
        if (typ == 0x0001) has0001 = true;
    }
    if (!has6A || !has0001) {
        std::fprintf(stderr, "FAIL: need DDM types $0001 and $6A (count=%d)\n", count);
        return 1;
    }

    // READ CAPACITY reflects wrapped size.
    std::vector<uint8_t> out, in;
    uint8_t cdb[10] = { 0x25 };
    if (disk.command(cdb, 10, out, in) != 0 || out.size() < 8) {
        std::fprintf(stderr, "FAIL: READ CAPACITY\n");
        return 1;
    }
    uint32_t last = (uint32_t(out[0]) << 24) | (uint32_t(out[1]) << 16) |
                    (uint32_t(out[2]) << 8) | out[3];
    if (last + 1 != disk.blocks()) {
        std::fprintf(stderr, "FAIL: capacity %u vs blocks %u\n", last + 1, disk.blocks());
        return 1;
    }

    // ER image must remain untouched (no façade).
    ScsiDisk er;
    if (!er.open(tpl) || er.flatHfsFacade()) {
        std::fprintf(stderr, "FAIL: ER template should not get a façade\n");
        return 1;
    }

    std::printf("PASSED — flat HFS façade (%u blocks, prefix %u)\n",
                disk.blocks(), disk.hfsPrefixBlocks());
    return 0;
}
