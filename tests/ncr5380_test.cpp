// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// M7 gate: drive the NCR 5380 through a full initiator transaction exactly
// as the Mac ROM's SCSI Manager does — arbitration, selection, COMMAND
// (send a READ(6) CDB byte by byte with REQ/ACK), DATA IN (read a block),
// STATUS, MESSAGE IN — and check block 0 comes back as the 'ER' Driver
// Descriptor Map. Also exercises the pseudo-DMA fast path. Independent of
// the ROM, so it pins the controller even while the boot trigger is sorted.

#include "Ncr5380.h"
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

// Read the live Current-Bus-Status: REQ asserted?
static bool req(Ncr5380& s) { return s.read(Ncr5380::R_CSR) & Ncr5380::CBS_REQ; }

// One COMMAND-phase byte: place on ODR, pulse ACK.
static void sendByte(Ncr5380& s, uint8_t b) {
    s.write(Ncr5380::R_DATA, b);
    s.write(Ncr5380::R_ICR, Ncr5380::ICR_ACK);   // ACK rising
    s.write(Ncr5380::R_ICR, 0);                  // ACK falling
}
// One read-phase byte: read data, pulse ACK.
static uint8_t recvByte(Ncr5380& s) {
    uint8_t b = s.read(Ncr5380::R_DATA);
    s.write(Ncr5380::R_ICR, Ncr5380::ICR_ACK);
    s.write(Ncr5380::R_ICR, 0);
    return b;
}

static int doRead(Ncr5380& scsi, uint32_t lba, std::vector<uint8_t>& block, bool dma) {
    // Arbitration
    scsi.write(Ncr5380::R_DATA, 0x80);                       // own ID 7
    scsi.write(Ncr5380::R_MODE, Ncr5380::MODE_ARBITRATE);
    // Selection: target ID 0 bit + own ID, assert SEL, drop BSY
    scsi.write(Ncr5380::R_DATA, 0x81);                       // own(7)|target(0)
    scsi.write(Ncr5380::R_MODE, 0);                          // clear arbitrate
    scsi.write(Ncr5380::R_ICR, Ncr5380::ICR_SEL);            // SEL, BSY released
    CHECK(req(scsi), "COMMAND REQ after selection");
    // COMMAND: READ(6) lba, 1 block
    uint8_t cdb[6] = { 0x08, uint8_t((lba >> 16) & 0x1F), uint8_t(lba >> 8),
                       uint8_t(lba), 1, 0 };
    for (int i = 0; i < 6; i++) sendByte(scsi, cdb[i]);
    // DATA IN: 512 bytes
    block.clear();
    if (dma) {
        scsi.write(Ncr5380::R_MODE, Ncr5380::MODE_DMA);
        for (int i = 0; i < 512; i++) block.push_back(scsi.dmaRead());
    } else {
        for (int i = 0; i < 512; i++) { CHECK(req(scsi), "DATA REQ byte %d", i);
            block.push_back(recvByte(scsi)); }
    }
    // STATUS + MESSAGE IN
    uint8_t status = dma ? scsi.dmaRead() : recvByte(scsi);
    uint8_t msg = dma ? scsi.dmaRead() : recvByte(scsi);
    CHECK(status == 0x00, "GOOD status, got %02X", status);
    CHECK(msg == 0x00, "COMMAND COMPLETE message, got %02X", msg);
    // Back to BUS FREE
    CHECK(!(scsi.read(Ncr5380::R_CSR) & Ncr5380::CBS_BSY), "bus free after transfer");
    return 0;
}

int main() {
    std::string img = find("hdv/HD20SC.vhd");
    if (img.empty()) { std::printf("SKIP: hdv/HD20SC.vhd not found\n"); return 0; }
    ScsiDisk disk;
    CHECK(disk.open(img), "open image");
    Ncr5380 scsi;
    scsi.reset();
    scsi.attach(&disk);

    std::vector<uint8_t> block;
    // Polled (REQ/ACK) read of block 0 → 'ER'
    CHECK(doRead(scsi, 0, block, false) == 0, "polled read block 0");
    CHECK(block.size() == 512 && block[0] == 'E' && block[1] == 'R',
          "block 0 DDM via polled SCSI ('%c%c')", block[0], block[1]);

    // Pseudo-DMA read of block 1 → 'PM'
    CHECK(doRead(scsi, 1, block, true) == 0, "DMA read block 1");
    CHECK(block[0] == 'P' && block[1] == 'M', "block 1 partition map via pseudo-DMA");

    // Partition entry for the driver (block 2) is another 'PM'
    CHECK(doRead(scsi, 2, block, false) == 0, "read block 2");
    CHECK(block[0] == 'P' && block[1] == 'M', "block 2 is a partition entry");

    std::printf("ncr5380_test: full SCSI transaction (polled + pseudo-DMA) OK\n");
    return 0;
}
