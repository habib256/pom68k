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
#include <filesystem>
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

// Read a complete physical GCR field, then exercise the same decoder used
// by IWM nibble writes and flux reconstruction. No tag access test hook.
static std::vector<uint8_t> field(SonyDrive& drive, uint8_t sectorCode = 0x96) {
    std::vector<uint8_t> bytes;
    for (int i = 0; i < 24000; ++i) bytes.push_back(drive.nextNibble(false));
    for (size_t i = 0; i + 709 <= bytes.size(); ++i)
        if (bytes[i] == 0xD5 && bytes[i + 1] == 0xAA && bytes[i + 2] == 0xAD &&
            bytes[i + 3] == sectorCode)
            return {bytes.begin() + i, bytes.begin() + i + 709};
    return {};
}
static void writeField(SonyDrive& drive, const std::vector<uint8_t>& bytes) {
    for (auto byte : bytes) drive.writeNibble(byte);
    drive.flushWrite(false);
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

    {   // DC42 retains all twelve physical tag bytes per GCR sector.
        const uint32_t kTagSize = 1600 * 12;           // 800K = 1600 sectors
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
        for (size_t i = 0; i < kTagSize; ++i)
            img[0x54 + SonyDrive::kSize800K + i] = uint8_t(i * 7 + 1);
        writeAll(dc42, img);
        SonyDrive drv;
        drv.reset();
        CHECK(drv.insert(dc42), "insert DC42 image with tags");
        drv.setWriteBack(true);
        CHECK(drv.writeSector(0, 0, 1, sec), "writeSector into tagged DC42");
        CHECK(drv.flushToFile(), "explicit flush (tagged)");
        auto file = readAll(dc42);
        CHECK(file.size() == img.size(), "tagged DC42 keeps its complete tag block");
        CHECK(std::memcmp(file.data() + 0x54 + SonyDrive::kSize800K,
                          img.data() + 0x54 + SonyDrive::kSize800K, kTagSize) == 0,
              "a data-sector edit preserves every original tag byte");
        const uint32_t tagSize = uint32_t(file[0x44]) << 24 | uint32_t(file[0x45]) << 16
                               | uint32_t(file[0x46]) << 8 | file[0x47];
        CHECK(tagSize == kTagSize, "tagSize matches the preserved physical sector tags");
        const uint32_t tagCk = uint32_t(file[0x4C]) << 24 | uint32_t(file[0x4D]) << 16
                             | uint32_t(file[0x4E]) << 8 | file[0x4F];
        uint32_t sum = 0;
        for (size_t i = 12; i < kTagSize; i += 2) {
            const size_t at = 0x54 + SonyDrive::kSize800K + i;
            sum += uint32_t(file[at]) * 256 + file[at + 1];
            sum = (sum >> 1) | (sum << 31);
        }
        CHECK(tagCk == sum && sum != 0, "tag checksum regenerated, excluding the first twelve bytes");

        const auto physical = field(drv);
        CHECK(!physical.empty(), "nonzero tags are present in the physical read field");
        auto blank = img;
        blank.resize(0x54 + SonyDrive::kSize800K);
        for (size_t i = 0x44; i < 0x48; ++i) blank[i] = 0;
        for (size_t i = 0x4C; i < 0x50; ++i) blank[i] = 0;
        const std::string targetPath = "floppy_tags_target.image";
        writeAll(targetPath, blank);
        SonyDrive target;
        CHECK(target.insert(targetPath), "insert tagless DC42 destination");
        target.setWriteBack(true);
        writeField(target, physical);
        CHECK(target.dirty(), "a tag-only GCR write marks the medium dirty");
        CHECK(field(target) == physical, "GCR write decoder retains the complete physical field");

        std::vector<uint8_t> state;
        sav::Writer writer(state);
        writer(target);
        target.insertImage(std::vector<uint8_t>(SonyDrive::kSize800K, 0));
        sav::Reader reader(state.data(), state.size());
        reader(target);
        CHECK(reader.ok() && !reader.remaining(), "snapshot restores the tagged medium");
        CHECK(field(target) == physical, "restored medium exposes the same tag bytes on the read path");
        CHECK(target.flushToFile(), "tag-only write persists as a DC42 tag block");
        auto updated = readAll(targetPath);
        CHECK(updated.size() == img.size(), "formerly tagless DC42 gains a complete tag block");
        CHECK(std::memcmp(updated.data() + 0x54 + SonyDrive::kSize800K,
                          img.data() + 0x54 + SonyDrive::kSize800K, 12) == 0,
              "guest-written first-sector tags reach the backing file");
        bool otherTagsZero = true;
        for (size_t i = 0x54 + SonyDrive::kSize800K + 12; i < updated.size(); ++i)
            otherTagsZero &= updated[i] == 0;
        CHECK(otherTagsZero && updated[0x4C] == 0 && updated[0x4D] == 0 &&
              updated[0x4E] == 0 && updated[0x4F] == 0,
              "first-sector tags are preserved but excluded from the DC42 checksum");
        SonyDrive reopened;
        CHECK(reopened.insert(targetPath) && field(reopened) == physical,
              "nonzero physical tags survive file close and reopen");
        auto damaged = physical;
        if (damaged.size() > 5) damaged[5] = 0; // illegal GCR symbol
        writeField(reopened, damaged);
        CHECK(!reopened.dirty() && field(reopened) == physical,
              "a bad-checksum field cannot modify tags independently of data");
        const auto unchanged = field(reopened, 0x97);
        reopened.setWriteProtected(true);
        writeField(reopened, field(drv, 0x97));
        CHECK(field(reopened, 0x97) == unchanged,
              "write protection applies to the tag bytes too");
        std::remove(targetPath.c_str());

        const std::vector<uint8_t> rawBase(SonyDrive::kSize800K, 0);
        writeAll(raw, rawBase);
        SonyDrive rawTarget;
        CHECK(rawTarget.insert(raw), "insert raw destination");
        rawTarget.setWriteBack(true);
        writeField(rawTarget, physical);
        CHECK(rawTarget.writeSector(0, 0, 1, sec) && rawTarget.flushToFile(),
              "raw media retains its sector-only write-back contract");
        const auto rawWritten = readAll(raw);
        CHECK(rawWritten.size() == rawBase.size() &&
              std::memcmp(rawWritten.data() + 512, sec, 512) == 0 &&
              field(rawTarget) == physical,
              "raw export writes data without clearing the mounted medium's physical tags");

        auto malformed = img;
        malformed.pop_back();
        writeAll(dc42, malformed);
        CHECK(!drv.insert(dc42), "truncated tag payload rejected");
        malformed = img;
        malformed[0x47] ^= 1;
        writeAll(dc42, malformed);
        CHECK(!drv.insert(dc42), "tag count inconsistent with sector geometry rejected");
    }

    std::remove(raw.c_str());
    std::remove(dc42.c_str());
    {   // ── a disks35/ref/ fixture is never written in place ──
        // Both orders the runners use: write-back on before the insert
        // (V8/Sonora/Toby) routes the insert to the work clone; write-back
        // on after the insert (DAFB) routes the flush. The reference bytes
        // assets.lock pins survive either way.
        namespace fs = std::filesystem;
        const fs::path root = "floppy_persist_ref_tmp";
        const fs::path ref = root / "disks35" / "ref" / "System.dsk";
        const fs::path work = root / "disks35" / "work" / "System.dsk";
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(ref.parent_path(), ec);
        writeAll(ref.string(), std::vector<uint8_t>(SonyDrive::kSize400K, 0));
        const auto pristine = readAll(ref.string());

        SonyDrive before;
        before.reset();
        before.setWriteBack(true);
        CHECK(before.insert(ref.string()), "insert a reference floppy, write-back on");
        CHECK(fs::is_regular_file(work, ec), "write-back before insert: work clone made");
        CHECK(before.writeSector(3, 0, 1, sec), "write a sector on the clone");
        before.eject();
        CHECK(readAll(ref.string()) == pristine, "reference untouched (routed at insert)");
        auto cloned = readAll(work.string());
        CHECK(cloned.size() == SonyDrive::kSize400K && cloned != pristine,
              "work clone carries the guest's sector");

        fs::remove_all(root / "disks35" / "work", ec);
        SonyDrive after;
        after.reset();
        CHECK(after.insert(ref.string()), "insert a reference floppy, write-back off");
        after.setWriteBack(true);             // the DAFB runner's order
        CHECK(after.writeSector(3, 0, 1, sec), "write a sector after enabling write-back");
        after.eject();
        CHECK(readAll(ref.string()) == pristine, "reference untouched (routed at flush)");
        CHECK(fs::is_regular_file(work, ec) && readAll(work.string()) != pristine,
              "the late flush lands on the work clone");
        fs::remove_all(root, ec);
    }

    std::printf(fails ? "FAILED (%d)\n" : "PASSED — floppy write persistence\n",
                fails);
    return fails ? 1 : 0;
}
