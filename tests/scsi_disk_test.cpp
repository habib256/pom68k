// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// M7 gate: the SCSI target device in isolation (no controller). Opens a
// real Apple SCSI image and checks INQUIRY / READ CAPACITY / READ(6): the
// Driver Descriptor Map ('ER') at block 0 and the partition map ('PM') at
// block 1 must come back through the SCSI command path. Soft-skips without
// hdv/HD20SC.vhd.

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

#define CHECK(c, ...) do { if(!(c)){ std::fprintf(stderr, "FAIL: " __VA_ARGS__); \
    std::fprintf(stderr, "\n"); return 1; } } while (0)

int main() {
    std::string img = find("hdv/HD20SC.vhd");
    if (img.empty()) { std::printf("SKIP: hdv/HD20SC.vhd not found\n"); return 0; }

    ScsiDisk disk;
    CHECK(disk.open(img), "open image");
    std::printf("disk: %u blocks (%u MB)\n", disk.blocks(), disk.blocks() / 2048);

    std::vector<uint8_t> out, none;

    // INQUIRY → direct-access device
    uint8_t inquiry[6] = { 0x12, 0, 0, 0, 36, 0 };
    CHECK(disk.command(inquiry, 6, out, none) == 0, "INQUIRY status");
    CHECK(out.size() == 36 && out[0] == 0x00, "INQUIRY: direct-access type");

    // READ CAPACITY → last LBA + block size 512
    uint8_t rdcap[10] = { 0x25, 0,0,0,0,0,0,0,0,0 };
    CHECK(disk.command(rdcap, 10, out, none) == 0, "READ CAPACITY status");
    uint32_t last = (uint32_t(out[0])<<24)|(uint32_t(out[1])<<16)|(uint32_t(out[2])<<8)|out[3];
    uint32_t bs = (uint32_t(out[6])<<8)|out[7];
    CHECK(last == disk.blocks() - 1, "READ CAPACITY last LBA");
    CHECK(bs == 512, "READ CAPACITY block size 512");

    // READ(6) block 0 → 'ER' Driver Descriptor Map
    uint8_t read0[6] = { 0x08, 0, 0, 0, 1, 0 };
    CHECK(disk.command(read0, 6, out, none) == 0, "READ(6) block 0 status");
    CHECK(out.size() == 512, "READ(6) returns one block");
    CHECK(out[0] == 'E' && out[1] == 'R', "block 0 is the DDM ('ER')");

    // READ(6) block 1 → 'PM' Apple Partition Map
    uint8_t read1[6] = { 0x08, 0, 0, 1, 1, 0 };
    CHECK(disk.command(read1, 6, out, none) == 0, "READ(6) block 1 status");
    CHECK(out[0] == 'P' && out[1] == 'M', "block 1 is the partition map ('PM')");

    // A multi-block read spans blocks contiguously
    uint8_t read2[6] = { 0x08, 0, 0, 0, 2, 0 };
    CHECK(disk.command(read2, 6, out, none) == 0, "READ(6) 2 blocks");
    CHECK(out.size() == 1024 && out[512] == 'P' && out[513] == 'M',
          "second block of a 2-block read is 'PM'");

    std::printf("scsi_disk_test: DDM + partition map read through SCSI, gate passed\n");
    return 0;
}
