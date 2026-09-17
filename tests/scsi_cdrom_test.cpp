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
        // The exact post-insert probe Mac OS 8.1 sends (traced 2026-08-04):
        // READ SUB-CHANNEL, MSF, SubQ, current position. A CHECK here
        // aborts the mount the driver had already started.
        const uint8_t subch[10] = { 0x42, 0x02, 0x40, 0x01, 0, 0, 0, 0, 0x10, 0 };
        check(hot.command(subch, 10, o3, i3) == 0 && o3.size() == 16 &&
              o3[1] == 0x15 && o3[4] == 0x01,
              "READ SUB-CHANNEL answers, no audio status");
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

    {
        std::vector<uint8_t> er(1026, 0);
        er[0] = 'E'; er[1] = 'R';
        er[2] = 0x02; er[3] = 0x00;
        check(scsiAppleImageBlockSize(er.data(), er.size()) == 512,
              "ER + sbBlkSize 512 is a disk dump");
        er[2] = 0x08; er[3] = 0x00;
        check(scsiAppleImageBlockSize(er.data(), er.size()) == 2048,
              "ER + sbBlkSize 2048 is a CD");
        std::vector<uint8_t> bare(1026, 0);
        bare[1024] = 'B'; bare[1025] = 'D';
        check(scsiAppleImageBlockSize(bare.data(), bare.size()) == 512,
              "bare HFS BD at 1024 is a disk dump");
        check(scsiAppleImageBlockSize(iso.data(), iso.size()) == 0,
              "unlabelled 2048 image declares no Apple block size");
    }

    {
        std::vector<uint8_t> toast(8 * 512, 0);
        toast[0] = 'E'; toast[1] = 'R';
        toast[2] = 0x02; toast[3] = 0x00;
        toast[0x200] = 'P'; toast[0x201] = 'M';
        toast[0x207] = 2;
        toast[0x400] = 'P'; toast[0x401] = 'M';
        toast[0x407] = 2;
        toast[0x40b] = 4;
        toast[0x40f] = 4;
        std::memcpy(toast.data() + 0x430, "Apple_HFS", 9);
        toast[4 * 512 + 0x400] = 'B';
        toast[4 * 512 + 0x401] = 'D';
        const std::string tp = "scsi_toast512_test.img";
        { std::ofstream o(tp, std::ios::binary | std::ios::trunc);
          o.write(reinterpret_cast<const char*>(toast.data()),
                  std::streamsize(toast.size())); }
        ScsiDisk hd;
        check(hd.open(tp), "open a driverless 512 Toast dump");
        const auto& img = hd.image();
        bool hfs = false;
        if (hd.flatHfsFacade()) {
            const size_t o = size_t(hd.hfsPrefixBlocks()) * 512;
            hfs = img.size() > o + 0x401 && img[o + 0x400] == 'B'
               && img[o + 0x401] == 'D';
        } else {
            hfs = img.size() > 0x401 && img[0x400] == 'B' && img[0x401] == 'D'
               && !(img[0] == 'E' && img[1] == 'R');
        }
        check(hfs, "512 dump unwraps to an HFS volume");
        std::remove(tp.c_str());
    }

    std::remove(path.c_str());
    // ── A mixed-mode disc: the TOC must show its audio tracks ────────
    // Synthesized here rather than found: a flat 2048-byte image cannot
    // carry an audio track at all, and real mixed discs are other people's
    // music (tools/make_mixed_cd.py builds the full-size twin).
    {
        const uint32_t kData = 8, kAudio = 4, kPregap = 150;
        std::vector<uint8_t> bin;
        auto raw = [&](bool audio, uint32_t lba, uint8_t fill) {
            std::vector<uint8_t> s(2352, fill);
            if (!audio) {
                static const uint8_t sync[12] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
                std::memcpy(s.data(), sync, 12);
                const uint32_t f = lba + 150;
                s[12] = uint8_t((((f / (60 * 75)) / 10) << 4) | ((f / (60 * 75)) % 10));
                s[13] = uint8_t(((((f / 75) % 60) / 10) << 4) | (((f / 75) % 60) % 10));
                s[14] = uint8_t((((f % 75) / 10) << 4) | ((f % 75) % 10));
                s[15] = 0x01;
            }
            bin.insert(bin.end(), s.begin(), s.end());
        };
        uint32_t lba = 0;
        for (uint32_t i = 0; i < kData; i++, lba++) raw(false, lba, uint8_t(0xA0 + i));
        for (uint32_t i = 0; i < kPregap; i++, lba++) raw(true, lba, 0);
        const uint32_t audioStart = lba;
        for (uint32_t i = 0; i < kAudio; i++, lba++) raw(true, lba, 0x5A);
        { std::ofstream f("scsi_cdrom_mixed.bin", std::ios::binary);
          f.write(reinterpret_cast<const char*>(bin.data()), std::streamsize(bin.size())); }
        const uint32_t am = audioStart;   // cue times are file-relative
        char cue[512];
        std::snprintf(cue, sizeof cue,
            "FILE \"scsi_cdrom_mixed.bin\" BINARY\n"
            "  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n"
            "  TRACK 02 AUDIO\n    INDEX 01 %02u:%02u:%02u\n",
            am / (60 * 75), (am / 75) % 60, am % 75);
        { std::ofstream f("scsi_cdrom_mixed.cue"); f << cue; }

        ScsiDisk mixed;
        check(mixed.openCdrom("scsi_cdrom_mixed.cue"), "a mixed-mode .cue mounts");
        check(mixed.trackCount() == 2, "both tracks are read from the sheet");
        if (mixed.trackCount() == 2) {
            check(!mixed.trackIsAudio(0) && mixed.trackIsAudio(1),
                  "track 1 is data, track 2 is audio");
            check(mixed.trackStartLba(1) == audioStart,
                  "the audio track starts where INDEX 01 put it");
        }
        std::vector<uint8_t> o, i2;
        const uint8_t toc[10] = { 0x43, 0, 0, 0, 0, 0, 0, 0, 40, 0 };
        check(mixed.command(toc, 10, o, i2) == 0 && o.size() == 28,
              "READ TOC returns two tracks and a lead-out");
        if (o.size() == 28) {
            check(o[2] == 1 && o[3] == 2, "first track 1, last track 2");
            check(o[5] == 0x14 && o[6] == 1, "track 1 is flagged data (control $4)");
            check(o[13] == 0x10 && o[14] == 2, "track 2 is flagged AUDIO (control $0)");
            check(o[22] == 0xAA, "the lead-out closes the TOC");
        }
        const uint8_t rd10[10] = { 0x28, 0, 0, 0, 0, 0, 0, 0, 1, 0 };
        check(mixed.command(rd10, 10, o, i2) == 0 && o.size() == 2048 && o[0] == 0xA0,
              "the data track still reads as 2048-byte user data");
        // ── The CD-DA transport ──────────────────────────────────────
        // PLAY AUDIO starts it, advanceAudio() moves it on MACHINE time
        // (75 sectors a second), READ SUBCHANNEL is what the AppleCD Audio
        // Player watches. No samples leave the drive yet.
        {
            const uint8_t stopped[10] = { 0x42, 0x02, 0x40, 0x01, 0, 0, 0, 0, 16, 0 };
            check(mixed.command(stopped, 10, o, i2) == 0 && o.size() >= 2 && o[1] == 0x15,
                  "an idle drive reports audio status $15 (stopped)");

            // Play the whole audio track: PLAY AUDIO(10) from its start.
            const uint32_t len = 4;
            const uint8_t play[10] = { 0x45, 0,
                uint8_t(audioStart >> 24), uint8_t(audioStart >> 16),
                uint8_t(audioStart >> 8), uint8_t(audioStart),
                0, uint8_t(len >> 8), uint8_t(len), 0 };
            check(mixed.command(play, 10, o, i2) == 0, "PLAY AUDIO (10) is accepted");
            check(mixed.audioState() == 1 && mixed.audioLba() == audioStart,
                  "the transport is playing, at the track's first sector");
            check(mixed.command(stopped, 10, o, i2) == 0 && o[1] == 0x11,
                  "READ SUBCHANNEL reports $11 (playing)");
            if (o.size() >= 16) check(o[6] == 2, "and names track 2 under the head");

            mixed.advanceAudio(1000000ull / 75 * 2);       // two sectors
            check(mixed.audioLba() == audioStart + 2, "the position advances at 75 sectors/s");

            const uint8_t pause[10] = { 0x4B, 0, 0, 0, 0, 0, 0, 0, 0x00, 0 };
            check(mixed.command(pause, 10, o, i2) == 0 && mixed.audioState() == 2,
                  "PAUSE holds the transport");
            mixed.advanceAudio(1000000ull);                 // a paused disc does not run
            check(mixed.audioLba() == audioStart + 2, "a paused disc does not advance");
            check(mixed.command(stopped, 10, o, i2) == 0 && o[1] == 0x12,
                  "READ SUBCHANNEL reports $12 (paused)");

            const uint8_t resume[10] = { 0x4B, 0, 0, 0, 0, 0, 0, 0, 0x01, 0 };
            check(mixed.command(resume, 10, o, i2) == 0 && mixed.audioState() == 1,
                  "RESUME sets it playing again");
            mixed.advanceAudio(1000000ull);                 // past the end
            check(mixed.audioState() == 3, "the play completes at the end address");
            check(mixed.command(stopped, 10, o, i2) == 0 && o[1] == 0x13,
                  "READ SUBCHANNEL reports $13 (completed)");

            // A play aimed at the data track is refused, not faked.
            const uint8_t bad[10] = { 0x45, 0, 0, 0, 0, 0, 0, 0, 1, 0 };
            check(mixed.command(bad, 10, o, i2) == 2,
                  "PLAY AUDIO on a data track is refused");
        }

        std::remove("scsi_cdrom_mixed.bin");
        std::remove("scsi_cdrom_mixed.cue");
    }

    if (fails) { std::printf("FAILED (%d)\n", fails); return 1; }
    std::printf("PASS\n");
    return 0;
}
