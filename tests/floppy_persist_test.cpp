// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Gate: floppy write persistence. The IWM/SWIM write engines commit
// sectors into the in-memory image (iwm_write_test / swim2_test); this
// gate covers the new host-file layer: with write-back enabled, committed
// sectors reach the .dsk file on eject (temp + rename), DiskCopy 4.2
// images get their header + data checksum regenerated, and WITHOUT
// write-back the file stays untouched (the etalon default).

#include "SonyDrive.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

static int fails = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::printf("FAIL: %s\n", msg); fails++; } \
    else std::printf("ok: %s\n", msg); } while (0)

static std::vector<uint8_t> readAll(const std::string& p) {
    std::ifstream in(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
}
static void writeAll(const std::string& p, const std::vector<uint8_t>& d) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(d.data()),
              std::streamsize(d.size()));
}

int main() {
    const std::string raw = "floppy_persist_tmp.dsk";
    const std::string dc42 = "floppy_persist_tmp.image";
    uint8_t sec[512];
    for (int i = 0; i < 512; i++) sec[i] = uint8_t(i * 7 + 3);

    {   // ── raw .dsk round trip ──
        writeAll(raw, std::vector<uint8_t>(SonyDrive::kSize800K, 0));
        SonyDrive drv;
        drv.reset();
        CHECK(drv.insert(raw), "insert raw 800K image");
        drv.setWriteBack(true);
        CHECK(!drv.dirty(), "clean after insert");
        CHECK(drv.writeSector(7, 1, 3, sec), "writeSector(7,1,3)");
        CHECK(drv.dirty(), "dirty after write");
        drv.eject();                          // flushes
        CHECK(!drv.hasDisk(), "ejected");
        auto file = readAll(raw);
        CHECK(file.size() == SonyDrive::kSize800K, "file size preserved");
        // Recompute the sector offset independently: track 7 is zone 0
        // (12 sectors/track, both sides).
        SonyDrive probe;
        probe.reset();
        CHECK(probe.insert(raw), "re-insert flushed image");
        uint8_t back[512] = {};
        CHECK(probe.readSector(7, 1, 3, back) &&
              std::memcmp(back, sec, 512) == 0,
              "sector survived eject → reload");
    }

    {   // ── write-back OFF leaves the file untouched (etalon default) ──
        writeAll(raw, std::vector<uint8_t>(SonyDrive::kSize800K, 0));
        SonyDrive drv;
        drv.reset();
        drv.insert(raw);
        drv.writeSector(0, 0, 0, sec);
        drv.eject();
        auto file = readAll(raw);
        bool untouched = true;
        for (uint8_t b : file) if (b) { untouched = false; break; }
        CHECK(untouched, "no write-back without opt-in");
    }

    {   // ── DiskCopy 4.2 round trip + checksum regeneration ──
        std::vector<uint8_t> img(0x54 + SonyDrive::kSize800K, 0);
        img[0x40] = uint8_t(SonyDrive::kSize800K >> 24);
        img[0x41] = uint8_t(SonyDrive::kSize800K >> 16);
        img[0x42] = uint8_t(SonyDrive::kSize800K >> 8);
        img[0x43] = uint8_t(SonyDrive::kSize800K);
        img[0x52] = 0x01; img[0x53] = 0x00;   // magic
        writeAll(dc42, img);
        SonyDrive drv;
        drv.reset();
        CHECK(drv.insert(dc42), "insert DC42 image");
        drv.setWriteBack(true);
        CHECK(drv.writeSector(0, 0, 1, sec), "writeSector into DC42");
        CHECK(drv.flushToFile(), "explicit flush (exit path)");
        auto file = readAll(dc42);
        CHECK(file.size() == 0x54 + SonyDrive::kSize800K,
              "DC42 header preserved");
        CHECK(file[0x52] == 0x01 && file[0x53] == 0x00, "DC42 magic intact");
        // Data checksum: rolling add + ror32 over big-endian words.
        uint32_t sum = 0;
        for (size_t i = 0x54; i + 1 < file.size(); i += 2) {
            sum += uint32_t(file[i] << 8 | file[i + 1]);
            sum = (sum >> 1) | (sum << 31);
        }
        uint32_t stored = uint32_t(file[0x48]) << 24 | uint32_t(file[0x49]) << 16
                        | uint32_t(file[0x4A]) << 8 | file[0x4B];
        CHECK(sum == stored, "DC42 data checksum regenerated");
        CHECK(std::memcmp(&file[0x54 + 512], sec, 512) == 0,
              "DC42 sector data landed");
        CHECK(!drv.dirty(), "clean after flush");
    }

    {   // ── DC42 WITH a tag block: insert() strips the tags, so the written
        //    back header must not keep claiming they are there. The old
        //    fixture only ever built tagSize == 0, so a file declaring N tag
        //    bytes that are not in it went unnoticed — Disk Copy / MAME /
        //    Mini vMac then fail the tag checksum or read past EOF.
        const uint32_t kTagSize = 800 * 12;            // 800 sectors x 12 B
        std::vector<uint8_t> img(0x54 + SonyDrive::kSize800K + kTagSize, 0);
        img[0x40] = uint8_t(SonyDrive::kSize800K >> 24);
        img[0x41] = uint8_t(SonyDrive::kSize800K >> 16);
        img[0x42] = uint8_t(SonyDrive::kSize800K >> 8);
        img[0x43] = uint8_t(SonyDrive::kSize800K);
        img[0x44] = uint8_t(kTagSize >> 24);           // tagSize
        img[0x45] = uint8_t(kTagSize >> 16);
        img[0x46] = uint8_t(kTagSize >> 8);
        img[0x47] = uint8_t(kTagSize);
        img[0x4C] = 0xDE; img[0x4D] = 0xAD;            // tagChecksum
        img[0x4E] = 0xBE; img[0x4F] = 0xEF;
        img[0x52] = 0x01; img[0x53] = 0x00;            // magic
        writeAll(dc42, img);
        SonyDrive drv;
        drv.reset();
        CHECK(drv.insert(dc42), "insert DC42 image with tags");
        drv.setWriteBack(true);
        CHECK(drv.writeSector(0, 0, 1, sec), "writeSector into tagged DC42");
        CHECK(drv.flushToFile(), "explicit flush (tagged)");
        auto file = readAll(dc42);
        CHECK(file.size() == 0x54 + SonyDrive::kSize800K,
              "tagged DC42 written back without the tag block");
        const uint32_t tagSize = uint32_t(file[0x44]) << 24 | uint32_t(file[0x45]) << 16
                               | uint32_t(file[0x46]) << 8 | file[0x47];
        CHECK(tagSize == 0, "tagSize zeroed to match the file we actually wrote");
        const uint32_t tagCk = uint32_t(file[0x4C]) << 24 | uint32_t(file[0x4D]) << 16
                             | uint32_t(file[0x4E]) << 8 | file[0x4F];
        CHECK(tagCk == 0, "tagChecksum zeroed alongside tagSize");
    }

    std::remove(raw.c_str());
    std::remove(dc42.c_str());
    std::printf(fails ? "FAILED (%d)\n" : "PASSED — floppy write persistence\n",
                fails);
    return fails ? 1 : 0;
}
