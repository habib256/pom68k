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

static std::vector<uint8_t> snapshot(AtaDisk& disk) {
    std::vector<uint8_t> bytes;
    sav::Writer writer(bytes);
    writer(disk);
    return bytes;
}

static bool restore(AtaDisk& disk, const std::vector<uint8_t>& bytes) {
    sav::Reader reader(bytes.data(), bytes.size());
    reader(disk);
    return reader.ok() && !reader.remaining();
}

static void transfer(AtaDisk& disk, uint8_t command, uint8_t lba, uint8_t count = 1) {
    disk.writeRegister(AtaDisk::kDevice, 0x40);
    disk.writeRegister(AtaDisk::kLbaLow, lba);
    disk.writeRegister(AtaDisk::kLbaMid, 0);
    disk.writeRegister(AtaDisk::kLbaHigh, 0);
    disk.writeRegister(AtaDisk::kSectorCount, count);
    disk.writeRegister(AtaDisk::kCommand, command);
}

static void snapshots() {
    AtaDisk disk, fresh;
    check(disk.open("ata_disk_test.img") && fresh.open("ata_disk_test.img"),
          "snapshot tests open the same unmodified backing medium");
    transfer(disk, 0x20, 3, 2);
    disk.readData();
    const auto reading = snapshot(disk);
    check(restore(fresh, reading) && fresh.irq(),
          "restoring into a fresh drive preserves the pending interrupt");
    bool equal = true;
    for (int i = 1; i < 512; ++i) equal &= disk.readData() == fresh.readData();
    check(equal && !(fresh.readRegister(AtaDisk::kStatus) & AtaDisk::kDrq),
          "a mid-read snapshot resumes both sectors at the saved word");

    const auto beforeWriting = snapshot(disk);
    transfer(disk, 0x30, 2, 2);
    for (int i = 0; i < 19; ++i) disk.writeData(0x1234);
    const auto writing = snapshot(disk);
    for (int i = 19; i < 512; ++i) disk.writeData(0x5678);
    check(restore(fresh, writing), "a partial two-sector write restores");
    for (int i = 19; i < 512; ++i) fresh.writeData(0x5678);
    transfer(disk, 0x20, 2, 2);
    transfer(fresh, 0x20, 2, 2);
    equal = true;
    for (int i = 0; i < 512; ++i) equal &= disk.readData() == fresh.readData();
    check(equal, "resumed PIO writes commit the saved prefix and both sectors");

    const auto written = snapshot(disk);
    check(restore(fresh, written) && snapshot(fresh) == written,
          "dirty sectors replay into a freshly opened base with byte-identical state");
    transfer(disk, 0x30, 2);
    for (int i = 0; i < 256; ++i) disk.writeData(0xEEEE);
    transfer(disk, 0x30, 5);
    for (int i = 0; i < 256; ++i) disk.writeData(0xFFFF);
    check(restore(disk, written), "restoring replays sectors present at snapshot time");
    transfer(disk, 0x20, 2);
    check(disk.readData() == 0x1234, "later overwrites of a saved sector are undone");
    transfer(disk, 0x20, 5);
    check(disk.readData() == 0x5A05, "writes to other sectors after the snapshot are undone");
    check(restore(disk, beforeWriting), "an earlier snapshot can be restored repeatedly");
    transfer(disk, 0x20, 2);
    check(disk.readData() == 0x5A02, "restoring the clean snapshot returns the original medium");

    disk.writeRegister(AtaDisk::kDevice, 3);
    disk.writeRegister(AtaDisk::kSectorCount, 2);
    disk.writeRegister(AtaDisk::kCommand, 0x91);
    const auto geometry = snapshot(disk);
    check(restore(fresh, geometry), "command $91 geometry restores into a fresh drive");
    fresh.writeRegister(AtaDisk::kCommand, 0xEC);
    std::vector<uint16_t> identify(256);
    for (auto& word : identify) word = fresh.readData();
    check(identify[54] == 1 && identify[55] == 4 && identify[56] == 2,
          "IDENTIFY reports the saved current CHS geometry");

    disk.writeRegister(AtaDisk::kCommand, 0xE8);
    for (int i = 0; i < 256; ++i) disk.writeData(0xBEEF);
    const auto buffer = snapshot(disk);
    check(restore(fresh, buffer), "WRITE BUFFER contents survive a snapshot");
    fresh.writeRegister(AtaDisk::kCommand, 0xE4);
    check(fresh.readData() == 0xBEEF, "READ BUFFER returns the saved drive latch");
    auto truncated = buffer;
    truncated.pop_back();
    check(!restore(fresh, truncated), "a truncated ATA snapshot is rejected");
    disk.close();
    check(disk.open("ata_disk_test.img") && restore(disk, beforeWriting),
          "reopening a drive clears the previous medium's write log");
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

    // The counters are what an investigation reads: "the guest issued READ
    // SECTORS" and "the guest actually pulled the bytes" are different
    // claims, and only the second one proves a data path.
    std::printf("  (counters: %ld commands, %ld sectors read, %ld written, "
                "%ld data words)\n", ata.commands, ata.sectorsRead,
                ata.sectorsWritten, ata.dataWords);
    check(ata.dataWords > ata.sectorsRead * 200 && ata.sectorsWritten == 1,
          "the counters distinguish commands issued from data transferred");

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

    snapshots();
    std::remove("ata_disk_test.img");
    std::printf(failures ? "FAIL\n" : "PASS\n");
    return failures ? 1 : 0;
}
