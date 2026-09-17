// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The ATA drive, driven through its task file exactly as the Quadra 630's
// ROM driver does: the software-reset pulse, the post-reset signature, the
// Status poll, IDENTIFY DEVICE, then READ and WRITE SECTORS. Asset-free —
// the image is a few sectors built here.
//
// The sequence is not invented: it is what `q630_boot_etalon` was observed
// doing at every boot (CHANGELOG 2026-09-17) — Device Control $0E then $0A,
// 3 994 Status reads, then registers 1-6 for the signature.

#include "AtaDisk.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

int main() {
    std::printf("ATA: the task file a Quadra 630's ROM driver speaks\n");

    // Eight sectors, each filled with its own number so a mis-addressed
    // read is visible rather than plausible.
    const uint32_t kSectors = 8;
    {
        std::ofstream f("ata_disk_test.img", std::ios::binary);
        for (uint32_t s = 0; s < kSectors; s++) {
            std::vector<uint8_t> sector(512, uint8_t(0xA0 + s));
            sector[0] = uint8_t(s);              // low byte of word 0
            sector[1] = 0x5A;                    // high byte of word 0
            f.write(reinterpret_cast<const char*>(sector.data()), 512);
        }
    }

    AtaDisk bare;
    check(!bare.present(), "a drive with no image is absent");
    check(bare.readRegister(AtaDisk::kStatus) == 0,
          "and reads back 0 — BSY=0, DRDY=0, which is 'no device'");

    AtaDisk ata;
    check(ata.open("ata_disk_test.img"), "a 512-byte-sector image opens");
    check(ata.sectorCount() == kSectors, "all of its sectors are addressable");

    // ── The reset pulse, byte for byte as the guest sends it ────────────
    ata.writeRegister(AtaDisk::kDeviceControl, 0x0E);   // SRST asserted
    ata.writeRegister(AtaDisk::kDeviceControl, 0x0A);   // released
    check((ata.readRegister(AtaDisk::kStatus) & AtaDisk::kBsy) == 0,
          "after SRST the drive is not busy");
    check((ata.readRegister(AtaDisk::kStatus) & AtaDisk::kDrdy) != 0,
          "and reports DRDY, which is what the Status poll waits for");
    check(ata.readRegister(AtaDisk::kSectorCount) == 1 &&
          ata.readRegister(AtaDisk::kLbaLow) == 1 &&
          ata.readRegister(AtaDisk::kLbaMid) == 0 &&
          ata.readRegister(AtaDisk::kLbaHigh) == 0,
          "the signature is 01 01 00 00: an ATA device, not ATAPI");

    // ── IDENTIFY DEVICE ─────────────────────────────────────────────────
    ata.writeRegister(AtaDisk::kCommand, 0xEC);
    check((ata.readRegister(AtaDisk::kStatus) & AtaDisk::kDrq) != 0,
          "IDENTIFY DEVICE raises DRQ: 256 words are waiting");
    std::vector<uint16_t> id;
    for (int i = 0; i < 256; i++) id.push_back(ata.readData());
    check(id[0] == 0x0040, "word 0 says non-removable ATA device");
    check(id[49] & 0x0200, "word 49 says LBA is supported");
    check((uint32_t(id[61]) << 16 | id[60]) == kSectors,
          "words 60-61 report the real sector count");
    // The model string is byte-swapped pairs, as ATA specifies.
    char model[41] = {};
    for (int i = 0; i < 20; i++) {
        model[i * 2] = char(id[27 + i] >> 8);
        model[i * 2 + 1] = char(id[27 + i] & 0xFF);
    }
    check(std::strncmp(model, "POM68K ATA Disk", 15) == 0,
          "and the model string reads back unswapped");
    check((ata.readRegister(AtaDisk::kStatus) & AtaDisk::kDrq) == 0,
          "DRQ drops once the buffer is drained");

    // ── READ SECTORS ────────────────────────────────────────────────────
    ata.writeRegister(AtaDisk::kDevice, 0x40);          // LBA mode
    ata.writeRegister(AtaDisk::kSectorCount, 1);
    ata.writeRegister(AtaDisk::kLbaLow, 3);
    ata.writeRegister(AtaDisk::kLbaMid, 0);
    ata.writeRegister(AtaDisk::kLbaHigh, 0);
    ata.writeRegister(AtaDisk::kCommand, 0x20);
    check((ata.readRegister(AtaDisk::kStatus) & AtaDisk::kDrq) != 0,
          "READ SECTORS raises DRQ");
    const uint16_t first = ata.readData();
    check((first & 0xFF) == 3 && (first >> 8) == 0x5A,
          "the word carries sector 3, low byte first as ATA orders it");
    for (int i = 1; i < 256; i++) ata.readData();
    check((ata.readRegister(AtaDisk::kStatus) & AtaDisk::kDrq) == 0,
          "and DRQ drops at the end of the sector");

    // Two sectors in one command: the second must follow without a new CDB.
    ata.writeRegister(AtaDisk::kSectorCount, 2);
    ata.writeRegister(AtaDisk::kLbaLow, 5);
    ata.writeRegister(AtaDisk::kCommand, 0x20);
    uint16_t w5 = ata.readData();
    for (int i = 1; i < 256; i++) ata.readData();
    uint16_t w6 = ata.readData();
    for (int i = 1; i < 256; i++) ata.readData();
    check((w5 & 0xFF) == 5 && (w6 & 0xFF) == 6,
          "a two-sector read delivers both, in order");
    check((ata.readRegister(AtaDisk::kStatus) & AtaDisk::kDrq) == 0,
          "then stops asking for more");

    // ── WRITE SECTORS ───────────────────────────────────────────────────
    ata.writeRegister(AtaDisk::kSectorCount, 1);
    ata.writeRegister(AtaDisk::kLbaLow, 7);
    ata.writeRegister(AtaDisk::kCommand, 0x30);
    check((ata.readRegister(AtaDisk::kStatus) & AtaDisk::kDrq) != 0,
          "WRITE SECTORS asks for the data");
    for (int i = 0; i < 256; i++) ata.writeData(uint16_t(0x1234));
    check((ata.readRegister(AtaDisk::kStatus) & AtaDisk::kDrq) == 0,
          "and takes it a sector at a time");
    ata.writeRegister(AtaDisk::kSectorCount, 1);
    ata.writeRegister(AtaDisk::kLbaLow, 7);
    ata.writeRegister(AtaDisk::kCommand, 0x20);
    check(ata.readData() == 0x1234, "what was written reads back");

    // ── Refusals ────────────────────────────────────────────────────────
    ata.writeRegister(AtaDisk::kSectorCount, 1);
    ata.writeRegister(AtaDisk::kLbaLow, uint8_t(kSectors));   // one past the end
    ata.writeRegister(AtaDisk::kCommand, 0x20);
    check((ata.readRegister(AtaDisk::kStatus) & AtaDisk::kErr) != 0 &&
          (ata.readRegister(AtaDisk::kError) & AtaDisk::kIdnf) != 0,
          "a read past the last sector is ID NOT FOUND, not zeroes");
    ata.writeRegister(AtaDisk::kCommand, 0xB0);               // SMART
    check((ata.readRegister(AtaDisk::kStatus) & AtaDisk::kErr) != 0 &&
          (ata.readRegister(AtaDisk::kError) & AtaDisk::kAbrt) != 0,
          "an unsupported command is ABORTED, which is how a driver probes");

    // ── The interrupt line ──────────────────────────────────────────────
    // Device Control bit 1 is nIEN, and it DISABLES interrupts: the $0E/$0A
    // pulse the guest sends keeps it set, so its reset is polled, not
    // interrupt-driven. $08 is bit 3 alone — interrupts enabled.
    ata.writeRegister(AtaDisk::kDeviceControl, 0x08);
    ata.writeRegister(AtaDisk::kSectorCount, 1);
    ata.writeRegister(AtaDisk::kLbaLow, 0);
    ata.writeRegister(AtaDisk::kCommand, 0x20);
    check(ata.irq(), "a completed read raises INTRQ");
    ata.readRegister(AtaDisk::kStatus);
    check(!ata.irq(), "and reading Status clears it");
    ata.writeRegister(AtaDisk::kSectorCount, 1);
    ata.writeRegister(AtaDisk::kLbaLow, 0);
    ata.writeRegister(AtaDisk::kCommand, 0x20);
    ata.readRegister(AtaDisk::kAltStatus);
    check(ata.irq(), "while Alternate Status leaves it asserted");

    std::remove("ata_disk_test.img");
    std::printf(failures ? "FAIL\n" : "PASS\n");
    return failures ? 1 : 0;
}
