// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Gate: the SCSI CD-ROM personality of `ScsiDisk` (openCdrom). Pins the
// pieces a classic Mac actually depends on, against MAME
// bus/nscsi/cd.cpp: INQUIRY type $05 + removable, 2048-byte READ
// CAPACITY, READ(10) at 2048-byte blocks, a single-data-track READ TOC in
// both LBA and MSF form, WRITE refused with DATA PROTECT, and — the one
// that decides whether a disc ever mounts — the Apple magic MODE SENSE
// page $30 carrying "APPLE COMPUTER, INC" (cd.cpp:604-618).
// Self-contained: builds its own tiny ISO image, no assets needed.

#include "ScsiDisk.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) fails++;
}

int main() {
    std::printf("scsi_cdrom_test — SCSI CD-ROM target\n");

    // A 64-sector MODE1 image with a recognisable pattern per sector.
    const uint32_t kSectors = 64;
    std::vector<uint8_t> iso(size_t(kSectors) * 2048, 0);
    for (uint32_t s = 0; s < kSectors; s++)
        for (int i = 0; i < 2048; i++)
            iso[size_t(s) * 2048 + i] = uint8_t(s ^ i);
    const std::string path = "scsi_cdrom_test.iso";
    { std::ofstream o(path, std::ios::binary | std::ios::trunc);
      o.write(reinterpret_cast<const char*>(iso.data()),
              std::streamsize(iso.size())); }

    ScsiDisk cd;
    check(cd.openCdrom(path), "openCdrom accepts a 2048-multiple image");
    check(cd.cdrom() && cd.present() && cd.mediumPresent(), "target + medium present");
    check(cd.blocks() == kSectors, "block count = image size / 2048");
    check(cd.blockSize() == 2048, "block size is 2048");

    std::vector<uint8_t> out, in;

    // INQUIRY: a CD-ROM must announce type $05 and removable, or the
    // driver never even looks at the medium.
    const uint8_t inq[6] = { 0x12, 0, 0, 0, 36, 0 };
    check(cd.command(inq, 6, out, in) == 0 && out.size() == 36, "INQUIRY returns 36 bytes");
    check(out[0] == 0x05, "INQUIRY device type = $05 (CD-ROM)");
    check((out[1] & 0x80) != 0, "INQUIRY removable bit set");
    check(out[8] != 0 && out[16] != 0, "vendor/product strings filled");

    // READ CAPACITY reports 2048-byte blocks — a 512 here makes every
    // file on the disc land at the wrong offset.
    const uint8_t rc[10] = { 0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    check(cd.command(rc, 10, out, in) == 0 && out.size() == 8, "READ CAPACITY replies");
    uint32_t last = uint32_t(out[0]) << 24 | uint32_t(out[1]) << 16
                  | uint32_t(out[2]) << 8 | out[3];
    uint32_t bs = uint32_t(out[4]) << 24 | uint32_t(out[5]) << 16
                | uint32_t(out[6]) << 8 | out[7];
    check(last == kSectors - 1, "READ CAPACITY last LBA");
    check(bs == 2048, "READ CAPACITY block size = 2048");

    // READ(10) must deliver whole 2048-byte sectors from the right offset.
    const uint8_t rd[10] = { 0x28, 0, 0, 0, 0, 3, 0, 0, 2, 0 };   // LBA 3, 2 blocks
    check(cd.command(rd, 10, out, in) == 0, "READ(10) succeeds");
    check(out.size() == 2 * 2048, "READ(10) returns 2 × 2048 bytes");
    check(!std::memcmp(out.data(), &iso[3 * 2048], 2048) &&
          !std::memcmp(out.data() + 2048, &iso[4 * 2048], 2048),
          "READ(10) data matches the image at LBA 3");

    // The Apple magic page — the gate on whether a Mac mounts the disc.
    const uint8_t ms30[6] = { 0x1A, 0, 0x30, 0, 60, 0 };
    check(cd.command(ms30, 6, out, in) == 0, "MODE SENSE page $30 succeeds");
    bool magic = false;
    for (size_t i = 0; i + 19 <= out.size(); i++)
        if (!std::memcmp(&out[i], "APPLE COMPUTER, INC", 19)) magic = true;
    check(magic, "MODE SENSE $30 carries 'APPLE COMPUTER, INC'");
    check(out.size() > 2 && (out[2] & 0x80), "MODE SENSE reports write-protected");

    // MODE SENSE must carry a BLOCK DESCRIPTOR unless DBD is set. This is
    // how the driver learns the disc is 2048 bytes/block; omitting it made
    // Mac OS 8.1 ask for the Apple page once and never speak again
    // (MAME cd.cpp:527-538).
    const uint8_t ms3f[6] = { 0x1A, 0, 0x3F, 0, 60, 0 };
    check(cd.command(ms3f, 6, out, in) == 0, "MODE SENSE page $3F succeeds");
    check(out.size() > 3 && out[3] == 0x08, "block descriptor present (8 bytes)");
    check(out.size() > 11 && out[9] == 0x00 && out[10] == 0x08 && out[11] == 0x00,
          "block descriptor block length = 2048");
    const uint8_t msDbd[6] = { 0x1A, 0x08, 0x3F, 0, 60, 0 };   // DBD set
    check(cd.command(msDbd, 6, out, in) == 0 && out.size() > 3 && out[3] == 0,
          "DBD set → no block descriptor");
    // The CD audio control page: Mac OS asks for it right after accepting
    // the disc and stalls if it does not come back (cd.cpp:587-604).
    const uint8_t ms0e[6] = { 0x1A, 0, 0x0E, 0, 28, 0 };
    check(cd.command(ms0e, 6, out, in) == 0, "MODE SENSE page $0E succeeds");
    {
        bool found = false;
        for (size_t i = 0; i + 1 < out.size(); i++)
            if ((out[i] & 0x3F) == 0x0E && out[i + 1] == 0x0E) found = true;
        check(found, "page $0E returned with length $0E");
    }

    // READ TOC: one data track + lead-out, in LBA and in MSF.
    const uint8_t toc[10] = { 0x43, 0, 0, 0, 0, 0, 0, 0, 20, 0 };
    check(cd.command(toc, 10, out, in) == 0 && out.size() == 20, "READ TOC replies");
    check(out[2] == 1 && out[3] == 1, "READ TOC first/last track = 1");
    check(out[5] == 0x14, "track 1 is an ADR-1 data track");
    check(out[14] == 0xAA, "lead-out entry present");
    uint32_t leadout = uint32_t(out[16]) << 24 | uint32_t(out[17]) << 16
                     | uint32_t(out[18]) << 8 | out[19];
    check(leadout == kSectors, "lead-out LBA = block count");
    const uint8_t tocMsf[10] = { 0x43, 0x02, 0, 0, 0, 0, 0, 0, 20, 0 };
    check(cd.command(tocMsf, 10, out, in) == 0, "READ TOC (MSF) replies");
    // LBA 0 in MSF is 00:02:00 — the 150-frame pre-gap.
    check(out[9] == 0 && out[10] == 2 && out[11] == 0, "MSF track 1 = 00:02:00");

    // Session-info format (1): Mac OS asks for it during the mount.
    const uint8_t tocSess[10] = { 0x43, 0x02, 0x01, 0, 0, 0, 0, 0, 12, 0 };
    check(cd.command(tocSess, 10, out, in) == 0 && out.size() == 12,
          "READ TOC format 1 (session info) replies");
    check(out[2] == 1 && out[3] == 1, "one session, first = last = 1");
    // Full TOC (2) is unhandled in MAME too, and answering honestly beats
    // inventing a reply (cd.cpp:890-900).
    const uint8_t tocFull[10] = { 0x43, 0x02, 0x02, 0, 0, 0, 0, 0, 48, 0 };
    check(cd.command(tocFull, 10, out, in) == 2,
          "READ TOC format 2 (full TOC) → CHECK CONDITION, as MAME does");

    // A CD is read-only, and must say so the way drivers expect.
    const uint8_t wr[10] = { 0x2A, 0, 0, 0, 0, 0, 0, 0, 1, 0 };
    in.assign(2048, 0xAB);
    check(cd.command(wr, 10, out, in) == 2, "WRITE(10) → CHECK CONDITION");
    const uint8_t rs[6] = { 0x03, 0, 0, 0, 18, 0 };
    check(cd.command(rs, 6, out, in) == 0 && out.size() > 2 &&
          (out[2] & 0x0F) == 0x07, "REQUEST SENSE reports DATA PROTECT");

    // Eject: the drive stays a target, the medium goes away, and reads
    // fail as NOT READY rather than silently returning zeros.
    const uint8_t stopEject[6] = { 0x1B, 0, 0, 0, 0x02, 0 };
    check(cd.command(stopEject, 6, out, in) == 0, "START/STOP UNIT (eject) accepted");
    check(cd.present() && !cd.mediumPresent(), "drive still present, medium gone");
    check(cd.command(rd, 10, out, in) == 2, "READ after eject → CHECK CONDITION");
    check(cd.command(rs, 6, out, in) == 0 && out.size() > 2 &&
          (out[2] & 0x0F) == 0x02, "sense key = NOT READY");

    // Hot insert: an EMPTY drive attached at boot (attachCdromEmpty), media
    // arriving mid-run (openCdrom on the same target). The driver's view:
    // NOT READY while empty; after the change, exactly one CHECK CONDITION
    // with UNIT ATTENTION / $28 (not-ready-to-ready) — that is the edge the
    // Mac CD extension mounts on — then business as usual.
    {
        ScsiDisk hot;
        hot.attachCdromEmpty();
        check(hot.cdrom() && hot.present() && !hot.mediumPresent(),
              "empty drive: target present, no medium");
        const uint8_t tur[6] = { 0x00, 0, 0, 0, 0, 0 };
        std::vector<uint8_t> o3, i3;
        check(hot.command(tur, 6, o3, i3) == 2, "TUR on empty drive → CHECK");
        check(hot.command(rs, 6, o3, i3) == 0 && (o3[2] & 0x0F) == 0x02,
              "empty drive sense = NOT READY");
        check(hot.openCdrom(path), "media hot-inserted into the drive");
        check(hot.command(tur, 6, o3, i3) == 2,
              "first TUR after insert → CHECK (unit attention)");
        check(hot.command(rs, 6, o3, i3) == 0 && (o3[2] & 0x0F) == 0x06 &&
              o3.size() > 12 && o3[12] == 0x28,
              "sense = UNIT ATTENTION, ASC $28 (medium changed)");
        check(hot.command(tur, 6, o3, i3) == 0, "second TUR → GOOD");
        // INQUIRY must never be blocked by a pending attention (SCSI-2):
        ScsiDisk hot2;
        hot2.attachCdromEmpty();
        check(hot2.openCdrom(path), "second drive, media inserted");
        const uint8_t inq2[6] = { 0x12, 0, 0, 0, 36, 0 };
        check(hot2.command(inq2, 6, o3, i3) == 0,
              "INQUIRY passes through a pending attention");
        check(hot2.command(tur, 6, o3, i3) == 2,
              "…which stays pending for the next command");
    }

    // A raw 2352-byte rip must be refused, not mis-read as MODE1.
    {
        const std::string raw = "scsi_cdrom_test_raw.bin";
        std::vector<uint8_t> r(2352 * 4, 0);
        { std::ofstream o(raw, std::ios::binary | std::ios::trunc);
          o.write(reinterpret_cast<const char*>(r.data()), std::streamsize(r.size())); }
        ScsiDisk bad;
        check(!bad.openCdrom(raw), "2352-byte raw image refused, not mis-read");
        std::remove(raw.c_str());
    }

    // MODE1/2352 raw rips: de-framed to user data, not served raw.
    {
        const std::string rawPath = "scsi_cdrom_test_2352.bin";
        std::vector<uint8_t> raw(2352 * 4, 0);
        for (int s2 = 0; s2 < 4; s2++) {
            uint8_t* sec = &raw[size_t(s2) * 2352];
            sec[0] = 0x00;
            for (int i = 1; i <= 10; i++) sec[i] = 0xFF;
            sec[11] = 0x00;                       // 12-byte sync
            sec[15] = 0x01;                       // MODE1
            for (int i = 0; i < 2048; i++) sec[16 + i] = uint8_t(s2 * 7 + i);
        }
        { std::ofstream o(rawPath, std::ios::binary | std::ios::trunc);
          o.write(reinterpret_cast<const char*>(raw.data()),
                  std::streamsize(raw.size())); }
        ScsiDisk r;
        check(r.openCdrom(rawPath), "MODE1/2352 rip accepted");
        check(r.blocks() == 4, "2352 rip -> 4 user-data blocks");
        const uint8_t rd2[10] = { 0x28, 0, 0, 0, 0, 2, 0, 0, 1, 0 };
        std::vector<uint8_t> o2, i2;
        check(r.command(rd2, 10, o2, i2) == 0 && o2.size() == 2048,
              "READ(10) on a de-framed rip");
        bool same = true;
        for (int i = 0; i < 2048; i++)
            if (o2[i] != uint8_t(2 * 7 + i)) same = false;
        check(same, "de-framed data is the payload, not the frame");

        const std::string cuePath = "scsi_cdrom_test.cue";
        { std::ofstream o(cuePath);
          o << "FILE \"" << rawPath << "\" BINARY\n"
            << "  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n"; }
        ScsiDisk c2;
        check(c2.openCdrom(cuePath), ".cue sheet resolves its FILE");
        check(c2.blocks() == 4, ".cue disc has the data track blocks");
        std::remove(cuePath.c_str());
        std::remove(rawPath.c_str());
    }

    std::remove(path.c_str());
    if (fails) { std::printf("FAILED (%d)\n", fails); return 1; }
    std::printf("PASS\n");
    return 0;
}
