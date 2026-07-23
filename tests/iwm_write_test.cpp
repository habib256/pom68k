// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// IWM write-engine gate (LLE floppy write, Plus / LC II SWIM1-GCR):
// the write path is proven as the exact inverse of the read path. A data
// field is harvested from a patterned drive's own nibble stream and
// replayed byte-for-byte through the IWM write registers (MAME iwm.cpp
// write mode: q7-while-enabled entry, handshake bit 7 ready / bit 6
// underrun, async byte cadence) into a blank drive; the sector must
// commit with the payload intact. Underrun and write-protect paths are
// pinned too.

#include "Iwm.h"
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

uint8_t expectedByte(int track, int side, int sector, int i) {
    return uint8_t(track * 7 + side * 31 + sector * 13 + i);
}

std::vector<uint8_t> patternedImage() {
    std::vector<uint8_t> img(SonyDrive::kSize800K);
    size_t off = 0;
    for (int t = 0; t < 80; t++)
        for (int h = 0; h < 2; h++)
            for (int s = 0; s < SonyDrive::sectorsInTrack(t); s++)
                for (int i = 0; i < 512; i++)
                    img[off++] = expectedByte(t, h, s, i);
    return img;
}

// Harvest one complete GCR data field (leading syncs .. DE AA epilogue)
// for `sector` on track 0 side 0 from the drive's read stream.
std::vector<uint8_t> harvestDataField(SonyDrive& drive, int sector) {
    std::vector<uint8_t> nib;
    for (int i = 0; i < 24000; i++) nib.push_back(drive.nextNibble(false));
    // kGcr6[sector] for the sector byte right after D5 AA AD
    static const uint8_t kGcr6Lo[16] = {
        0x96, 0x97, 0x9a, 0x9b, 0x9d, 0x9e, 0x9f, 0xa6,
        0xa7, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb2, 0xb3,
    };
    for (size_t p = 3; p + 720 < nib.size(); p++) {
        if (!(nib[p] == 0xD5 && nib[p + 1] == 0xAA && nib[p + 2] == 0xAD &&
              nib[p + 3] == kGcr6Lo[sector & 0x0F]))
            continue;
        // 3 marks + sector + 703 payload/checksum nibbles + DE AA
        std::vector<uint8_t> field(nib.begin() + long(p),
                                   nib.begin() + long(p) + 4 + 703 + 2);
        return field;
    }
    return {};
}

// IWM register lines (reg = line*2 + set): ENABLE=4, Q6=6, Q7=7.
constexpr int kEnableOn = 9;
constexpr int kQ6On = 13, kQ6Off = 12;
constexpr int kQ7On = 15, kQ7Off = 14;

// Write one byte the way the Sony driver does: poll the handshake at the
// q6-off address until bit 7 (register empty), then store at q6-on.
bool feedByte(Iwm& iwm, uint8_t v, int budgetCycles = 4096) {
    while (budgetCycles > 0) {
        uint8_t h = iwm.read(kQ6Off);            // control 0x80: handshake
        if (h & 0x80) { iwm.write(kQ6On, v); return true; }
        iwm.tick(8);
        budgetCycles -= 8;
    }
    return false;
}
} // namespace

int main() {
    std::printf("iwm_write_test — IWM write engine + SonyDrive GCR write-back\n");

    const std::vector<uint8_t> pattern = patternedImage();
    constexpr int kSector = 3;

    // ── Harvest the encoded field from a patterned drive ──────────────
    SonyDrive src;
    src.reset();
    check(src.insertImage(pattern), "insert patterned 800K source");
    std::vector<uint8_t> field = harvestDataField(src, kSector);
    check(field.size() == 709, "harvest data field for sector 3");

    // ── Replay it through the IWM write path into a blank drive ───────
    {
        SonyDrive dst;
        dst.reset();
        check(dst.insertImage(std::vector<uint8_t>(SonyDrive::kSize800K, 0)),
              "insert blank 800K target");
        Iwm iwm;
        iwm.reset();
        iwm.attachDrive(&dst, nullptr);
        // Handshake before ENABLE (q7 set, not write mode): MAME whd 0xBF.
        iwm.read(kQ7On);
        check((iwm.read(kQ6Off) & 0xC0) == 0x80, "handshake idle: ready, b6 low");
        iwm.read(kQ7Off);
        iwm.read(kEnableOn);                     // ENABLE (motor via VIA)
        iwm.read(kQ6On);

        // First byte enters write mode on the same access (control 0xc0).
        iwm.write(kQ7On, 0xFF);
        check((iwm.read(kQ6Off) & 0x40) != 0, "write mode: b6 high (no underrun)");
        bool fed = true;
        for (int i = 0; i < 5 && fed; i++) fed = feedByte(iwm, 0xFF);   // syncs
        for (size_t i = 0; i < field.size() && fed; i++)
            fed = feedByte(iwm, field[i]);
        check(fed, "handshake paces the whole field without stalling");
        check((iwm.read(kQ6Off) & 0x40) != 0, "no underrun while fed");
        iwm.tick(256);                           // drain the last byte
        iwm.read(kQ7Off);                        // leave write mode -> flush

        uint8_t got[512];
        check(dst.readSector(0, 0, kSector, got), "read back written sector");
        bool match = true;
        for (int i = 0; i < 512 && match; i++)
            if (got[i] != expectedByte(0, 0, kSector, i)) match = false;
        check(match, "write path is the inverse of the read path");

        uint8_t other[512];
        dst.readSector(0, 0, kSector + 1, other);
        bool untouched = true;
        for (int i = 0; i < 512 && untouched; i++)
            if (other[i] != 0) untouched = false;
        check(untouched, "neighbour sector stays blank");
    }

    // ── Underrun: starving the shifter drops handshake bit 6 ──────────
    {
        SonyDrive dst;
        dst.reset();
        dst.insertImage(std::vector<uint8_t>(SonyDrive::kSize800K, 0));
        Iwm iwm;
        iwm.reset();
        iwm.attachDrive(&dst, nullptr);
        iwm.read(kEnableOn);
        iwm.write(kQ7On, 0xFF);                  // one byte, then silence
        iwm.tick(1024);
        check((iwm.read(kQ6Off) & 0x40) == 0, "starved shifter: underrun (b6 low)");
        // Re-entering write mode re-arms the engine.
        iwm.read(kQ7Off);
        iwm.write(kQ7On, 0xFF);
        check((iwm.read(kQ6Off) & 0x40) != 0, "write re-entry clears underrun");
    }

    // ── Write-protected media never commits ───────────────────────────
    {
        SonyDrive dst;
        dst.reset();
        dst.insertImage(std::vector<uint8_t>(SonyDrive::kSize800K, 0));
        dst.setWriteProtected(true);
        Iwm iwm;
        iwm.reset();
        iwm.attachDrive(&dst, nullptr);
        iwm.read(kEnableOn);
        iwm.write(kQ7On, 0xFF);
        for (int i = 0; i < 5; i++) feedByte(iwm, 0xFF);
        for (uint8_t b : field) feedByte(iwm, b);
        iwm.tick(256);
        iwm.read(kQ7Off);
        uint8_t got[512];
        dst.readSector(0, 0, kSector, got);
        bool untouched = true;
        for (int i = 0; i < 512 && untouched; i++)
            if (got[i] != 0) untouched = false;
        check(untouched, "write-protected image is untouched");
    }

    std::printf("%s\n", gFails ? "FAILED" : "PASSED");
    return gFails ? 1 : 0;
}
