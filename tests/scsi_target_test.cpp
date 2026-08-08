// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Gate for the hard-disk target's SCSI-2 surface: the MODE SENSE page set,
// the Apple identity, and the command coverage a guest-side formatter needs
// (SEEK / VERIFY / SYNCHRONIZE CACHE / READ DEFECT DATA / REASSIGN BLOCKS /
// MODE SELECT(10)). Nothing here needs a ROM or a real disk image — the test
// builds its own — so it runs everywhere, which is the point: the boot
// etalons only prove the ROM can READ a volume, and every command below is
// one the ROM never issues and Drive Setup / HD SC Setup / Silverlining do.
//
// Two oracles, and they are not the same one:
//   • the disk mode pages and the page $30 signature come from RaSCSI
//     (RASCSI-X68k src/raspberrypi/disk.cpp:1473-1616 + 2857-2918,
//     BSD-3-Clause) — MAME's nscsi_hd answers MODE SENSE with a bare header
//     and models none of this;
//   • the command semantics (sense keys, out-of-range behaviour) are SCSI-2.

#include "ScsiDisk.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

static int failures = 0;
static void check(bool c, const char* what) {
    if (!c) { std::fprintf(stderr, "FAIL: %s\n", what); failures++; }
}

// Sense key + ASC of the last CHECK CONDITION, drained through REQUEST SENSE.
static void sense(ScsiDisk& d, uint8_t& key, uint8_t& asc) {
    std::vector<uint8_t> out, none;
    const uint8_t rs[6] = { 0x03, 0, 0, 0, 18, 0 };
    d.command(rs, 6, out, none);
    key = out.size() > 2 ? uint8_t(out[2] & 0x0F) : 0xFF;
    asc = out.size() > 12 ? out[12] : 0xFF;
}

static bool contains(const std::vector<uint8_t>& v, const char* s) {
    const size_t n = std::strlen(s);
    if (v.size() < n) return false;
    for (size_t i = 0; i + n <= v.size(); i++)
        if (std::memcmp(v.data() + i, s, n) == 0) return true;
    return false;
}

// Find a mode page by code inside a MODE SENSE reply, skipping the header
// and the block descriptor. Returns the offset of the page, or 0.
static size_t findPage(const std::vector<uint8_t>& v, bool ten, uint8_t code) {
    size_t p = ten ? 8 : 4;
    const size_t bdLen = ten ? (v.size() > 7 ? size_t((v[6] << 8) | v[7]) : 0)
                             : (v.size() > 3 ? size_t(v[3]) : 0);
    p += bdLen;
    while (p + 1 < v.size()) {
        const uint8_t c = v[p] & 0x3F;
        const size_t len = size_t(v[p + 1]) + 2;
        if (c == code) return p;
        if (len < 2) break;
        p += len;
    }
    return 0;
}

int main() {
    // ── A disk image with no filesystem on it: 2 MB of a recognisable
    // pattern, which is enough for every command below and small enough to
    // build in the test. All-zero would also work — what must NOT happen is
    // 'LK' at 0 or 'BD' at $400, which would trip the flat-HFS façade and
    // renumber every LBA under the test's feet.
    const uint32_t kBlocks = 4096;
    const std::string path = "scsi_target_test.img";
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f) { std::fprintf(stderr, "SKIP: cannot write %s\n", path.c_str()); return 0; }
        std::vector<uint8_t> blk(512);
        for (uint32_t b = 0; b < kBlocks; b++) {
            for (size_t i = 0; i < blk.size(); i++)
                blk[i] = uint8_t(b + i);
            f.write(reinterpret_cast<const char*>(blk.data()), std::streamsize(blk.size()));
        }
    }

    ScsiDisk disk;
    check(disk.open(path), "open the synthetic image");
    check(disk.blocks() == kBlocks, "no façade was applied");
    check(!disk.flatHfsFacade(), "image is served raw");

    std::vector<uint8_t> out, none;
    uint8_t key = 0, asc = 0;

    // ── 1. INQUIRY: the Apple identity ──────────────────────────────────
    // Vendor is bytes 8-15, product 16-31, revision 32-35. Apple's own tools
    // read these next to the page $30 signature below.
    {
        const uint8_t cdb[6] = { 0x12, 0, 0, 0, 36, 0 };
        check(disk.command(cdb, 6, out, none) == 0, "INQUIRY GOOD");
        check(out.size() == 36, "INQUIRY returns 36 bytes");
        check(out[0] == 0x00, "INQUIRY: direct-access device");
        check(std::memcmp(&out[8], " SEAGATE", 8) == 0, "INQUIRY vendor");
        check(std::memcmp(&out[16], "          ST225N", 16) == 0, "INQUIRY product");
    }

    // ── 2. MODE SENSE(6) page $30: the Apple signature ──────────────────
    // This string is what HD SC Setup and Drive Setup read before they will
    // work on a drive. Without it the volume still boots and no guest-side
    // tool will touch it.
    {
        const uint8_t cdb[6] = { 0x1A, 0, 0x30, 0, 64, 0 };
        check(disk.command(cdb, 6, out, none) == 0, "MODE SENSE $30 GOOD");
        check(contains(out, "APPLE COMPUTER, INC."), "page $30 carries the Apple signature");
        const size_t p = findPage(out, false, 0x30);
        check(p != 0, "page $30 is present and walkable");
        check(p && out[p + 1] == 0x1C, "page $30 length = $1C");
        check(p && std::memcmp(&out[p + 0x0A], "APPLE COMPUTER, INC.", 20) == 0,
              "signature sits at page offset $0A");
        check(out[0] == uint8_t(out.size() - 1), "mode data length = n-1");
        check(out[2] == 0x00, "a hard disk is not write-protected");
    }

    // ── 3. The block descriptor: 512-byte blocks, real block count ──────
    {
        const uint8_t cdb[6] = { 0x1A, 0, 0x30, 0, 64, 0 };
        disk.command(cdb, 6, out, none);
        check(out[3] == 8, "block descriptor present (DBD clear)");
        const uint32_t n = (uint32_t(out[5]) << 16) | (uint32_t(out[6]) << 8) | out[7];
        const uint32_t bs = (uint32_t(out[9]) << 16) | (uint32_t(out[10]) << 8) | out[11];
        check(n == kBlocks, "descriptor reports the block COUNT");
        check(bs == 512, "descriptor reports 512-byte blocks");

        const uint8_t dbd[6] = { 0x1A, 0x08, 0x30, 0, 64, 0 };   // DBD set
        disk.command(dbd, 6, out, none);
        check(out[3] == 0, "DBD suppresses the block descriptor");
        check(contains(out, "APPLE COMPUTER, INC."), "…and keeps the page");
    }

    // ── 4. Page $3F returns the whole set ───────────────────────────────
    {
        const uint8_t cdb[6] = { 0x1A, 0, 0x3F, 0, 255, 0 };
        check(disk.command(cdb, 6, out, none) == 0, "MODE SENSE $3F GOOD");
        for (uint8_t p : { 0x01, 0x02, 0x03, 0x04, 0x08, 0x30 }) {
            char msg[64];
            std::snprintf(msg, sizeof msg, "page $%02X present in the $3F reply", p);
            check(findPage(out, false, p) != 0, msg);
        }
    }

    // ── 5. Page 4 geometry is self-consistent ───────────────────────────
    // Invented, and it has to be — an image has no platters. What must hold
    // is that cylinders × heads × sectors lands in the right neighbourhood
    // of the real capacity, or a formatter computes a partition map off the
    // end of the disk.
    {
        const uint8_t cdb[6] = { 0x1A, 0, 0x04, 0, 64, 0 };
        check(disk.command(cdb, 6, out, none) == 0, "MODE SENSE page 4 GOOD");
        const size_t p = findPage(out, false, 0x04);
        check(p != 0, "page 4 walkable");
        if (p) {
            const uint32_t cyl = (uint32_t(out[p + 2]) << 16)
                               | (uint32_t(out[p + 3]) << 8) | out[p + 4];
            const uint32_t heads = out[p + 5];
            check(heads == 8, "page 4 reports 8 heads");
            check(cyl > 0, "page 4 cylinder count is non-zero");
            check(uint64_t(cyl) * heads * 25 <= kBlocks, "geometry fits inside the image");
            check(uint64_t(cyl + 1) * heads * 25 > kBlocks, "geometry is not wildly short");
        }
    }

    // ── 6. PC=1 (changeable) hides what cannot change ───────────────────
    {
        const uint8_t cdb[6] = { 0x1A, 0, 0x40 | 0x30, 0, 64, 0 };
        check(disk.command(cdb, 6, out, none) == 0, "MODE SENSE changeable GOOD");
        check(!contains(out, "APPLE COMPUTER, INC."),
              "the signature is not offered as changeable");
        const uint8_t fmt[6] = { 0x1A, 0, 0x40 | 0x03, 0, 64, 0 };
        disk.command(fmt, 6, out, none);
        const size_t p = findPage(out, false, 0x03);
        check(p && out[p + 0x0C] == 0xFF && out[p + 0x0D] == 0xFF,
              "page 3 offers only the sector size as changeable");
    }

    // ── 7. An unsupported page is refused, not silently empty ───────────
    {
        const uint8_t cdb[6] = { 0x1A, 0, 0x0C, 0, 64, 0 };   // no such page
        check(disk.command(cdb, 6, out, none) == 2, "unknown page → CHECK CONDITION");
        sense(disk, key, asc);
        check(key == 0x05 && asc == 0x24, "…with ILLEGAL REQUEST / INVALID FIELD IN CDB");
    }

    // ── 8. MODE SENSE(10): same body, 8-byte header ─────────────────────
    {
        const uint8_t cdb[10] = { 0x5A, 0, 0x3F, 0, 0, 0, 0, 0x01, 0x00, 0 };
        check(disk.command(cdb, 10, out, none) == 0, "MODE SENSE(10) GOOD");
        const size_t n = (size_t(out[0]) << 8) | out[1];
        check(n == out.size() - 2, "mode data length = n-2");
        check(out[7] == 8, "block descriptor length in bytes 6-7");
        check(contains(out, "APPLE COMPUTER, INC."), "page $30 in the (10) reply");
        check(findPage(out, true, 0x04) != 0, "page 4 walkable in the (10) reply");
    }

    // ── 9. The command coverage a formatter needs ───────────────────────
    {
        struct { const char* name; uint8_t cdb[10]; int len; } ok[] = {
            { "REZERO UNIT",          { 0x01, 0,0,0,0,0 }, 6 },
            { "SEND DIAGNOSTIC",      { 0x1D, 0x04, 0,0,0,0 }, 6 },
            { "START/STOP UNIT",      { 0x1B, 0,0,0, 0x01, 0 }, 6 },
            { "PREVENT/ALLOW REMOVAL",{ 0x1E, 0,0,0, 0x01, 0 }, 6 },
            { "SYNCHRONIZE CACHE",    { 0x35, 0,0,0,0,0,0,0,0,0 }, 10 },
            { "MODE SELECT(10)",      { 0x55, 0x10, 0,0,0,0,0, 0, 0, 0 }, 10 },
            { "REASSIGN BLOCKS",      { 0x07, 0,0,0,0,0 }, 6 },
        };
        for (auto& c : ok)
            check(disk.command(c.cdb, c.len, out, none) == 0, c.name);
    }

    // SEEK: in range GOOD, past the end CHECK. A driver probing the end of
    // the disk must see the error, not a silent success.
    {
        const uint8_t s6[6] = { 0x0B, 0, 0x01, 0x00, 0, 0 };          // LBA 256
        check(disk.command(s6, 6, out, none) == 0, "SEEK(6) in range");
        const uint8_t s6bad[6] = { 0x0B, 0x1F, 0xFF, 0xFF, 0, 0 };
        check(disk.command(s6bad, 6, out, none) == 2, "SEEK(6) past the end → CHECK");
        sense(disk, key, asc);
        check(key == 0x05 && asc == 0x24, "…ILLEGAL REQUEST / INVALID FIELD");

        const uint8_t s10[10] = { 0x2B, 0, 0,0,0x01,0x00, 0,0,0,0 };
        check(disk.command(s10, 10, out, none) == 0, "SEEK(10) in range");
        const uint8_t s10bad[10] = { 0x2B, 0, 0,0,0xFF,0xFF, 0,0,0,0 };
        check(disk.command(s10bad, 10, out, none) == 2, "SEEK(10) past the end → CHECK");
    }

    // READ DEFECT DATA: an empty list in the requested format.
    {
        const uint8_t cdb[10] = { 0x37, 0, 0x05, 0,0,0,0, 0x00, 0x04, 0 };
        check(disk.command(cdb, 10, out, none) == 0, "READ DEFECT DATA GOOD");
        check(out.size() == 4, "defect list header only");
        check(out[1] == 0x05, "P/G list bits and format echoed");
        check(out[2] == 0 && out[3] == 0, "defect list length = 0");
    }

    // ── 10. WRITE AND VERIFY + VERIFY with BytChk ───────────────────────
    {
        std::vector<uint8_t> payload(1024);
        for (size_t i = 0; i < payload.size(); i++) payload[i] = uint8_t(i * 7 + 3);
        const uint8_t wv[10] = { 0x2E, 0, 0,0,0x00,0x10, 0, 0x00, 0x02, 0 }; // LBA 16, 2
        check(disk.command(wv, 10, out, payload) == 0, "WRITE AND VERIFY(10) GOOD");

        const uint8_t rd[10] = { 0x28, 0, 0,0,0x00,0x10, 0, 0x00, 0x02, 0 };
        disk.command(rd, 10, out, none);
        check(out == payload, "the bytes actually landed");

        // BytChk set + identical data → GOOD
        const uint8_t vfy[10] = { 0x2F, 0x02, 0,0,0x00,0x10, 0, 0x00, 0x02, 0 };
        check(disk.command(vfy, 10, out, payload) == 0, "VERIFY BytChk match → GOOD");

        // BytChk set + one byte different → MISCOMPARE, not a cheerful GOOD
        payload[500] ^= 0xFF;
        check(disk.command(vfy, 10, out, payload) == 2, "VERIFY BytChk mismatch → CHECK");
        sense(disk, key, asc);
        check(key == 0x0E && asc == 0x1D, "…MISCOMPARE / $1D");

        // BytChk clear: a range check and nothing else.
        const uint8_t vfy0[10] = { 0x2F, 0, 0,0,0x00,0x10, 0, 0x00, 0x02, 0 };
        check(disk.command(vfy0, 10, out, none) == 0, "VERIFY without BytChk → GOOD");
        const uint8_t vfyBad[10] = { 0x2F, 0, 0,0,0xFF,0xF0, 0, 0x00, 0x02, 0 };
        check(disk.command(vfyBad, 10, out, none) == 2, "VERIFY past the end → CHECK");
    }

    // ── 11. DATA OUT sizing, which is the controllers' half of all this ──
    // A wrong count here does not corrupt data — it hangs the bus, because
    // initiator and target disagree about whose turn it is to talk.
    {
        auto wbc = [&](std::vector<uint8_t> cdb) {
            return disk.writeByteCount(cdb.data(), int(cdb.size()));
        };
        check(wbc({ 0x0A, 0,0,0, 2, 0 }) == 2 * 512, "WRITE(6) 2 blocks");
        check(wbc({ 0x0A, 0,0,0, 0, 0 }) == 256 * 512, "WRITE(6) count 0 = 256");
        check(wbc({ 0x2A, 0, 0,0,0,0, 0, 0, 3, 0 }) == 3 * 512, "WRITE(10) 3 blocks");
        check(wbc({ 0x2E, 0, 0,0,0,0, 0, 0, 3, 0 }) == 3 * 512, "WRITE AND VERIFY(10)");
        check(wbc({ 0x2F, 0x02, 0,0,0,0, 0, 0, 3, 0 }) == 3 * 512, "VERIFY with BytChk");
        check(wbc({ 0x2F, 0x00, 0,0,0,0, 0, 0, 3, 0 }) == 0, "VERIFY without BytChk");
        check(wbc({ 0x15, 0,0,0, 12, 0 }) == 12, "MODE SELECT(6) parameter list");
        check(wbc({ 0x55, 0x10, 0,0,0,0, 0, 0, 24, 0 }) == 24, "MODE SELECT(10)");
        check(wbc({ 0x1D, 0, 0, 0, 8, 0 }) == 8, "SEND DIAGNOSTIC parameter list");
        check(wbc({ 0x04, 0x10, 0,0,0,0 }) == 4, "FORMAT UNIT FmtData: header first");
        check(wbc({ 0x04, 0x00, 0,0,0,0 }) == 0, "FORMAT UNIT without FmtData");
        check(wbc({ 0x07, 0,0,0,0,0 }) == 4, "REASSIGN BLOCKS: header first");
        check(wbc({ 0x08, 0,0,0, 1, 0 }) == 0, "READ(6) sends nothing");

        // The defect-list header carries the real length of what follows.
        const uint8_t fmt[6] = { 0x04, 0x10, 0, 0, 0, 0 };
        const std::vector<uint8_t> hdr = { 0x00, 0x00, 0x00, 0x08 };
        check(disk.extendDataOut(fmt, 6, hdr, 4) == 12, "defect header extends the gather");
        const std::vector<uint8_t> empty = { 0x00, 0x00, 0x00, 0x00 };
        check(disk.extendDataOut(fmt, 6, empty, 4) == 4, "empty defect list ends it");
        const uint8_t rd6[6] = { 0x08, 0, 0, 0, 1, 0 };
        check(disk.extendDataOut(rd6, 6, hdr, 4) == 4, "READ(6) is never extended");
    }

    std::remove(path.c_str());
    if (failures) {
        std::fprintf(stderr, "scsi_target_test: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("scsi_target_test: mode pages, Apple signature and the "
                "formatter command set, gate passed\n");
    return 0;
}
