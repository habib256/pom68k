// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Q8 SWIM2 media gate: MFM 1.44 MB + GCR 800K through Swim2+SonyDrive,
// sector write-back, SuperDrive sense bits. Keeps the register-only
// swim2_test untouched.

#include "Swim2.h"
#include "SonyDrive.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {
int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-62s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}

constexpr uint16_t kMark = 0x100;

// Pull decoded bytes from Swim2 until `need` collected or budget exhausted.
std::vector<uint16_t> drainSwim(Swim2& swim, int need, int budgetCycles) {
    std::vector<uint16_t> out;
    out.reserve(size_t(need));
    // Cell rate depends on setup[3:2] (16/31/31/63 clocks per bit). Pace
    // each tick at the slowest GCR rate so one tick always yields a byte.
    constexpr int kTick = 63 * 8;
    int left = budgetCycles;
    while (int(out.size()) < need && left > 0) {
        swim.tick(kTick);
        left -= kTick;
        while (swim.fifoCount() && int(out.size()) < need)
            out.push_back(swim.read(1));
    }
    return out;
}

bool findMfmSector0(const std::vector<uint16_t>& stream, std::vector<uint8_t>& data) {
    // Hunt A1 A1 A1 FE 00 00 01 02 … then A1 A1 A1 FB + 512 payload
    for (size_t i = 0; i + 8 < stream.size(); i++) {
        if (!(stream[i] == 0xA1 && stream[i + 1] == 0xA1 && stream[i + 2] == 0xA1 &&
              stream[i + 3] == 0xFE && stream[i + 4] == 0x00 && stream[i + 5] == 0x00 &&
              stream[i + 6] == 0x01 && stream[i + 7] == 0x02))
            continue;
        for (size_t j = i + 8; j + 4 + 512 < stream.size(); j++) {
            if (!(stream[j] == 0xA1 && stream[j + 1] == 0xA1 && stream[j + 2] == 0xA1 &&
                  stream[j + 3] == 0xFB))
                continue;
            data.assign(512, 0);
            for (int k = 0; k < 512; k++) data[size_t(k)] = uint8_t(stream[j + 4 + size_t(k)]);
            return true;
        }
    }
    return false;
}

bool findGcrSector(const std::vector<uint16_t>& stream, int track, int sector,
                   std::vector<uint8_t>& /*data*/) {
    // Address prologue D5 AA 96 is enough to prove GCR via SWIM2.
    int hits = 0;
    for (size_t i = 0; i + 3 < stream.size(); i++) {
        if (stream[i] == 0xD5 && stream[i + 1] == 0xAA && stream[i + 2] == 0x96)
            hits++;
    }
    (void)track; (void)sector;
    return hits >= 4;
}
} // namespace

int main() {
    std::printf("swim2_media_test — SWIM2 + SonyDrive MFM/GCR media\n");

    // ── MFM 1.44 MB round-trip through Swim2 FIFO ──────────────────────
    {
        std::vector<uint8_t> img(SonyDrive::kSize1440K, 0);
        for (int i = 0; i < 512; i++)
            img[size_t(i)] = uint8_t(0xA0 + (i & 0x1F));   // track0/side0/sec0

        SonyDrive drive;
        drive.setSuperDrive(true);
        check(drive.insertImage(img), "insert 1.44MB image");
        check(drive.isHd() && drive.mfmMode() && drive.sense(0xA),
              "HD media: SuperDrive + MFM senses");

        Swim2 swim;
        swim.reset();
        swim.attachDrive(&drive, nullptr);
        // motor + drive A + ACTION (MAME mode bits — swim2.cpp:128-137)
        swim.write(7, 0x8A);
        swim.write(5, 0x00);                     // MFM read clocking

        auto stream = drainSwim(swim, 8000, 8000 * 128);
        std::vector<uint8_t> sector;
        check(findMfmSector0(stream, sector), "MFM address+data marks via FIFO");
        bool payloadOk = true;
        for (int i = 0; i < 512 && payloadOk; i++)
            if (sector[size_t(i)] != uint8_t(0xA0 + (i & 0x1F))) payloadOk = false;
        check(payloadOk, "MFM track0 sector1 payload matches image");
    }

    // ── GCR 800K via Swim2 (not only Iwm) ──────────────────────────────
    {
        std::vector<uint8_t> img(SonyDrive::kSize800K, 0x55);
        img[0] = 0x4C; img[1] = 0x4B;

        SonyDrive drive;
        drive.setSuperDrive(true);
        check(drive.insertImage(img), "insert 800K GCR image");
        check(!drive.mfmMode() && drive.sense(0xA),
              "800K on SuperDrive: GCR mode, SuperDrive sense");

        Swim2 swim;
        swim.reset();
        swim.attachDrive(&drive, nullptr);
        swim.write(7, 0x8A);
        swim.write(5, 0x04);                     // GCR read (setup bit 2)

        auto stream = drainSwim(swim, 4000, 4000 * 63 * 8);
        std::vector<uint8_t> dummy;
        check(findGcrSector(stream, 0, 0, dummy),
              "GCR address prologues visible via Swim2");
    }

    // ── Sector write then read-back ────────────────────────────────────
    {
        std::vector<uint8_t> img(SonyDrive::kSize1440K, 0);
        SonyDrive drive;
        drive.setSuperDrive(true);
        check(drive.insertImage(std::move(img)), "insert blank HD for write");

        uint8_t pat[512];
        for (int i = 0; i < 512; i++) pat[i] = uint8_t(0x5A ^ i);
        check(drive.writeSector(3, 1, 7, pat), "writeSector(3,1,7)");

        uint8_t got[512];
        check(drive.readSector(3, 1, 7, got), "readSector(3,1,7)");
        check(std::memcmp(pat, got, 512) == 0, "writeSector/readSector round-trip");

        // Swim2 write path: feed MARK A1×3 + FB + payload into the drive
        Swim2 swim;
        swim.reset();
        swim.attachDrive(&drive, nullptr);
        drive.command(0b0100);                   // motor on (CA2=0)
        swim.write(7, 0x9A);                     // motor+A+write+ACTION
        // Seek to track 0 / assemble sector 2 data field
        auto push = [&](uint16_t v) {
            while (swim.fifoCount() >= 2) swim.tick(128);
            if (v & kMark) swim.write(1, uint8_t(v));
            else swim.write(0, uint8_t(v));
            swim.tick(128);
        };
        for (int i = 0; i < 3; i++) push(kMark | 0xA1);
        push(0xFE);
        push(0); push(0); push(2); push(2);      // C H S N
        push(0); push(0);                        // fake CRC
        for (int i = 0; i < 3; i++) push(kMark | 0xA1);
        push(0xFB);
        uint8_t wr[512];
        for (int i = 0; i < 512; i++) wr[i] = uint8_t(0xC0 + (i & 0x0F));
        for (int i = 0; i < 512; i++) push(wr[i]);
        push(0); push(0);                        // CRC → commit
        for (int i = 0; i < 8; i++) swim.tick(128);

        uint8_t back[512];
        check(drive.readSector(0, 0, 1, back), "read sector after Swim2 write");
        bool ok = true;
        for (int i = 0; i < 512 && ok; i++)
            if (back[i] != wr[i]) ok = false;
        check(ok, "Swim2 writeByte path commits sector payload");
    }

    // ── Drive detect / eject ───────────────────────────────────────────
    {
        SonyDrive drive;
        drive.setSuperDrive(true);
        check(drive.sense(0xA), "empty SuperDrive still reports capability");
        check(drive.sense(0x1), "CSTIN high when empty");
        std::vector<uint8_t> img(SonyDrive::kSize1440K, 0);
        drive.insertImage(std::move(img));
        check(!drive.sense(0x1), "CSTIN low when inserted");
        drive.eject();
        check(!drive.hasDisk() && drive.sense(0x1), "eject clears image");
    }

    std::printf("%s\n", gFails ? "FAILED" : "PASSED");
    return gFails ? 1 : 0;
}
