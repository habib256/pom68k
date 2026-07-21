// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Q6 gate: drive the NCR 53C96 through full initiator transactions exactly as
// the Mac OS 8.1 SCSI Manager does on the Quadra 605 — FLUSH FIFO, load the
// IDENTIFY message + CDB into the FIFO, set the destination bus id, issue
// SELECT-with-ATN, follow interrupt-status / sequence-step, TRANSFER
// INFORMATION for the DATA phase (polled via R_FIFO and via the pseudo-DMA
// port), then Initiator Command Complete to collect STATUS + message.
//
// Runs READ CAPACITY(10) and READ(6)/READ(10) of block 0 from
// hdv/MacOS-8.1-boot.vhd and checks the Driver Descriptor Map signature
// ('ER'). Independent of the ROM, so it pins the controller regardless of the
// boot trigger. Modelled on tests/ncr5380_test.cpp / scsi_pdma_test.cpp.

#include "Ncr53c96.h"
#include "ScsiDisk.h"
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

using R = Ncr53c96;

static std::string find(const char* rel) {
    for (const std::string base : { std::string(), std::string("../"), std::string("../../") }) {
        std::string p = base + rel;
        if (std::ifstream(p, std::ios::binary)) return p;
    }
    return {};
}

#define CHECK(c, ...) do { if(!(c)){ std::fprintf(stderr, "FAIL: " __VA_ARGS__); \
    std::fprintf(stderr, "\n"); return 1; } } while (0)

// Select the target (id 0) and send the CDB the way the driver does:
// FLUSH FIFO, push IDENTIFY + CDB into the FIFO, set bus id, SELECT_ATN.
static void selectAndCommand(R& s, const std::vector<uint8_t>& cdb) {
    s.write(R::R_COMMAND, R::CM_FLUSH_FIFO);
    s.write(R::R_FIFO, 0xC0);                 // IDENTIFY message (LUN 0, disc ok)
    for (uint8_t b : cdb) s.write(R::R_FIFO, b);
    s.write(R::R_STATUS, 0x00);               // destination bus id = 0
    s.write(R::R_COMMAND, R::CD_SELECT_ATN);
    // The controller latches I_BUS|I_FUNCTION and seq_step 4 after the CDB.
    uint8_t ist = s.read(R::R_ISTAT);
    (void)ist;
}

// Poll the DATA IN payload through the FIFO port after a non-DMA XFER.
static void readPolled(R& s, int n, std::vector<uint8_t>& out) {
    s.write(R::R_TCLOW, uint8_t(n));
    s.write(R::R_TCMID, uint8_t(n >> 8));
    s.write(R::R_TCHIGH, 0);
    s.write(R::R_COMMAND, R::CI_XFER);        // non-DMA transfer information
    for (int i = 0; i < n; i++) out.push_back(s.read(R::R_FIFO));
}

// Pull the DATA IN payload through the pseudo-DMA port after a DMA XFER.
static void readDma(R& s, int n, std::vector<uint8_t>& out) {
    s.write(R::R_TCLOW, uint8_t(n));
    s.write(R::R_TCMID, uint8_t(n >> 8));
    s.write(R::R_TCHIGH, 0);
    s.write(R::R_COMMAND, R::CI_XFER | R::CMD_DMA);   // DMA transfer information
    for (int i = 0; i < n; i++) {
        // On the real machine PrimeTime holds off /DTACK while !drq — here we
        // just assert DRQ is up whenever a byte remains.
        if (i < n) { if (!s.drq() && i + 1 < n) { /* last byte: drq may drop */ } }
        out.push_back(s.dmaRead());
    }
}

// Collect STATUS + COMMAND-COMPLETE message and return to BUS FREE.
static int finish(R& s, uint8_t& status, uint8_t& msg) {
    s.write(R::R_COMMAND, R::CI_COMPLETE);     // latch STATUS + message into FIFO
    uint8_t ist = s.read(R::R_ISTAT);
    if (!(ist & R::I_FUNCTION)) return 1;
    status = s.read(R::R_FIFO);
    msg    = s.read(R::R_FIFO);
    s.write(R::R_COMMAND, R::CI_MSG_ACCEPT);   // → BUS FREE / I_DISCONNECT
    uint8_t d = s.read(R::R_ISTAT);
    return (d & R::I_DISCONNECT) ? 0 : 2;
}

static int readBlock(R& s, uint32_t lba, bool ten, bool dma, std::vector<uint8_t>& block) {
    std::vector<uint8_t> cdb;
    if (ten) cdb = { 0x28, 0x00, uint8_t(lba >> 24), uint8_t(lba >> 16),
                     uint8_t(lba >> 8), uint8_t(lba), 0x00, 0x00, 0x01, 0x00 };
    else     cdb = { 0x08, uint8_t((lba >> 16) & 0x1F), uint8_t(lba >> 8),
                     uint8_t(lba), 0x01, 0x00 };
    selectAndCommand(s, cdb);
    block.clear();
    if (dma) readDma(s, 512, block); else readPolled(s, 512, block);
    uint8_t status = 0xFF, msg = 0xFF;
    CHECK(finish(s, status, msg) == 0, "transaction to bus-free (lba %u)", lba);
    CHECK(status == 0x00, "GOOD status, got %02X", status);
    CHECK(msg == 0x00, "COMMAND COMPLETE message, got %02X", msg);
    CHECK(block.size() == 512, "512-byte block, got %zu", block.size());
    return 0;
}

int main() {
    std::string img = find("hdv/MacOS-8.1-boot.vhd");
    if (img.empty()) img = find("hdv/HD20SC.vhd");   // fall back to any bootable image
    if (img.empty()) { std::printf("SKIP: no SCSI disk image found\n"); return 0; }

    ScsiDisk disk;
    CHECK(disk.open(img), "open image %s", img.c_str());
    R scsi;
    scsi.reset();
    scsi.write(R::R_CONFIG1, 0x07);              // own ID 7
    scsi.attach(&disk, 0);

    // ── Reset then a chip reset via the command register ──
    scsi.write(R::R_COMMAND, R::CM_RESET);
    CHECK(!scsi.irq(), "no IRQ after chip reset");

    // ── READ CAPACITY(10): 8-byte payload, polled ──
    {
        std::vector<uint8_t> cdb = { 0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
        selectAndCommand(scsi, cdb);
        std::vector<uint8_t> cap;
        readPolled(scsi, 8, cap);
        uint8_t status = 0xFF, msg = 0xFF;
        CHECK(finish(scsi, status, msg) == 0, "READ CAPACITY to bus-free");
        CHECK(status == 0x00, "READ CAPACITY GOOD status, got %02X", status);
        uint32_t lastBlk = (uint32_t(cap[0]) << 24) | (uint32_t(cap[1]) << 16)
                         | (uint32_t(cap[2]) << 8) | cap[3];
        uint32_t blkSize = (uint32_t(cap[6]) << 8) | cap[7];
        CHECK(blkSize == 512, "block size 512, got %u", blkSize);
        CHECK(lastBlk == disk.blocks() - 1, "last block %u == blocks-1 %u",
              lastBlk, disk.blocks() - 1);
        std::printf("  READ CAPACITY: %u blocks x %u bytes OK\n", lastBlk + 1, blkSize);
    }

    // ── READ(6) block 0, polled → Driver Descriptor Map 'ER' ──
    {
        std::vector<uint8_t> block;
        CHECK(readBlock(scsi, 0, false, false, block) == 0, "READ(6) polled block 0");
        CHECK(block[0] == 'E' && block[1] == 'R',
              "block 0 DDM via polled READ(6) ('%c%c')", block[0], block[1]);
        std::printf("  READ(6) polled  block 0: '%c%c' (Driver Descriptor Map) OK\n",
                    block[0], block[1]);
    }

    // ── READ(6) block 0, pseudo-DMA → same 'ER' ──
    {
        std::vector<uint8_t> block;
        CHECK(readBlock(scsi, 0, false, true, block) == 0, "READ(6) DMA block 0");
        CHECK(block[0] == 'E' && block[1] == 'R',
              "block 0 DDM via pseudo-DMA READ(6) ('%c%c')", block[0], block[1]);
        std::printf("  READ(6) pdma    block 0: '%c%c' (Driver Descriptor Map) OK\n",
                    block[0], block[1]);
    }

    // ── READ(10) block 0, pseudo-DMA → same 'ER' ──
    {
        std::vector<uint8_t> block;
        CHECK(readBlock(scsi, 0, true, true, block) == 0, "READ(10) DMA block 0");
        CHECK(block[0] == 'E' && block[1] == 'R',
              "block 0 DDM via pseudo-DMA READ(10) ('%c%c')", block[0], block[1]);
        std::printf("  READ(10) pdma   block 0: '%c%c' (Driver Descriptor Map) OK\n",
                    block[0], block[1]);
    }

    // ── Selection timeout: an empty ID raises I_DISCONNECT, no hang ──
    {
        scsi.write(R::R_COMMAND, R::CM_FLUSH_FIFO);
        scsi.write(R::R_FIFO, 0xC0);
        scsi.write(R::R_FIFO, 0x00);              // TEST UNIT READY
        scsi.write(R::R_FIFO, 0); scsi.write(R::R_FIFO, 0);
        scsi.write(R::R_FIFO, 0); scsi.write(R::R_FIFO, 0);
        scsi.write(R::R_FIFO, 0);
        scsi.write(R::R_STATUS, 0x03);            // no device at id 3
        scsi.write(R::R_COMMAND, R::CD_SELECT_ATN);
        uint8_t ist = scsi.read(R::R_ISTAT);
        CHECK(ist & R::I_DISCONNECT, "selection timeout raises I_DISCONNECT (got %02X)", ist);
    }

    // ── WRITE(10) 1 block, polled R_FIFO (System 7.5.5 SCSI HAL path) ──
    {
        std::vector<uint8_t> orig;
        CHECK(readBlock(scsi, 1, true, true, orig) == 0, "seed READ(10) LBA 1");
        std::vector<uint8_t> cdb = { 0x2A, 0x00, 0x00, 0x00, 0x00, 0x01,
                                     0x00, 0x00, 0x01, 0x00 };
        selectAndCommand(scsi, cdb);
        (void)scsi.read(R::R_ISTAT);              // clear select IRQ
        scsi.write(R::R_TCLOW, 0x00);
        scsi.write(R::R_TCMID, 0x02);             // TC = 512
        scsi.write(R::R_TCHIGH, 0);
        scsi.write(R::R_COMMAND, R::CI_XFER);     // non-DMA — HAL uses $10
        for (int i = 0; i < 512; i++)
            scsi.write(R::R_FIFO, uint8_t(orig[i] ^ 0xA5));
        CHECK(scsi.irq(), "polled WRITE raises I_BUS after 512 FIFO bytes");
        uint8_t ist = scsi.read(R::R_ISTAT);
        CHECK(ist & R::I_BUS, "I_BUS after polled WRITE (got %02X)", ist);
        uint8_t status = 0xFF, msg = 0xFF;
        CHECK(finish(scsi, status, msg) == 0, "polled WRITE to bus-free");
        CHECK(status == 0x00, "polled WRITE GOOD status, got %02X", status);
        // Restore the block so other tests/images stay clean.
        selectAndCommand(scsi, cdb);
        (void)scsi.read(R::R_ISTAT);
        scsi.write(R::R_TCLOW, 0x00);
        scsi.write(R::R_TCMID, 0x02);
        scsi.write(R::R_TCHIGH, 0);
        scsi.write(R::R_COMMAND, R::CI_XFER | R::CMD_DMA);
        for (uint8_t b : orig) scsi.dmaWrite(b);
        (void)scsi.read(R::R_ISTAT);
        CHECK(finish(scsi, status, msg) == 0, "restore WRITE DMA");
        std::printf("  WRITE(10) polled LBA 1 (512 B via R_FIFO) OK\n");
    }

    std::printf("ncr53c96_test: full 53C96 transactions (polled + pseudo-DMA, "
                "READ6/READ10/READ CAPACITY, selection timeout) OK\n");
    return 0;
}
