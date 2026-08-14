// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Q8 SWIM2 media gate, LLE cell-engine edition: MFM 1.44 MB + GCR 800K
// through the real bit engines (MAME swim2.cpp read shifter + TSS write
// serializer), CRC-CCITT verified end-to-end, rotational latency from the
// spin counter. Keeps the register-only swim2_test untouched.

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
constexpr uint16_t kCrcOk = 0x200;               // local tag: handshake CRC0

// Pull decoded bytes from the Swim2 read engine. Ticks both the SWIM
// cell clock and the drive spin (same C15M timeline) and pops promptly —
// the real engine, like hardware, drops bytes on FIFO overrun.
std::vector<uint16_t> drainSwim(Swim2& swim, SonyDrive& drive, int need,
                                int byteCycles, int budgetCycles) {
    std::vector<uint16_t> out;
    out.reserve(size_t(need));
    const int step = byteCycles / 2;             // ≤1 byte lands per step
    int left = budgetCycles;
    while (int(out.size()) < need && left > 0) {
        swim.tick(step);
        drive.tick(step);
        left -= step;
        while (swim.fifoCount() && int(out.size()) < need) {
            const uint8_t h = swim.read(7);
            uint16_t v = swim.read(1);
            if (swim.fifoCount() == 0) {         // flags described this byte
                if (h & 0x01) v |= kMark;
                if (!(h & 0x02)) v |= kCrcOk;
            }
            out.push_back(v);
        }
    }
    return out;
}

bool findMfmSector(const std::vector<uint16_t>& s, int sector,
                   std::vector<uint8_t>& data, bool* addrCrcOk = nullptr,
                   bool* dataCrcOk = nullptr) {
    auto marks = [&](size_t i) {
        return s[i] == (kMark | 0xA1) || s[i] == (kMark | kCrcOk | 0xA1);
    };
    for (size_t i = 0; i + 10 < s.size(); i++) {
        if (!(marks(i) && marks(i + 1) && marks(i + 2) &&
              (s[i + 3] & 0xFF) == 0xFE && !(s[i + 3] & kMark) &&
              int(s[i + 6] & 0xFF) == sector))
            continue;
        if (addrCrcOk) *addrCrcOk = (s[i + 9] & kCrcOk) != 0;
        for (size_t j = i + 10; j + 517 < s.size() && j < i + 80; j++) {
            if (!(marks(j) && marks(j + 1) && marks(j + 2) &&
                  (s[j + 3] & 0xFF) == 0xFB && !(s[j + 3] & kMark)))
                continue;
            data.assign(512, 0);
            for (int k = 0; k < 512; k++)
                data[size_t(k)] = uint8_t(s[j + 4 + size_t(k)]);
            if (dataCrcOk) *dataCrcOk = (s[j + 517] & kCrcOk) != 0;
            return true;
        }
        return false;
    }
    return false;
}

int firstMfmSectorNumber(const std::vector<uint16_t>& s) {
    for (size_t i = 0; i + 9 < s.size(); i++) {
        if ((s[i] & (kMark | 0xFF)) == (kMark | 0xA1) &&
            (s[i + 1] & (kMark | 0xFF)) == (kMark | 0xA1) &&
            (s[i + 2] & (kMark | 0xFF)) == (kMark | 0xA1) &&
            (s[i + 3] & 0xFF) == 0xFE && !(s[i + 3] & kMark))
            return int(s[i + 6] & 0xFF);
    }
    return -1;
}
} // namespace

int main() {
    std::printf("swim2_media_test — SWIM2 cell engine + SonyDrive MFM/GCR\n");

    constexpr int kMfmByte = 16 * 16;            // 16 cells × 16 clocks
    constexpr int kGcrByte = 8 * 31;

    // ── MFM 1.44 MB read through the bit engine, CRC verified ─────────
    {
        std::vector<uint8_t> img(SonyDrive::kSize1440K, 0);
        for (int i = 0; i < 512; i++)
            img[size_t(i)] = uint8_t(0xA0 + (i & 0x1F));   // track0/side0/sec0

        SonyDrive drive;
        drive.setSuperDrive(true);
        drive.setSpinClockHz(15667200);          // test ticks in C15M units
        check(drive.insertImage(img), "insert 1.44MB image");
        check(drive.isHd() && drive.mfmMode() && drive.sense(0xA),
              "HD media: SuperDrive + MFM senses");

        Swim2 swim;
        swim.reset();
        swim.attachDrive(&drive, nullptr);
        drive.commandSwim(0x2);                  // spindle on
        swim.write(5, 0x00);                     // MFM read clocking
        swim.write(7, 0x8A);                     // motor + drive A + ACTION

        auto stream = drainSwim(swim, drive, 1200, kMfmByte, 1200 * kMfmByte * 4);
        std::vector<uint8_t> sector;
        bool aCrc = false, dCrc = false;
        check(findMfmSector(stream, 1, sector, &aCrc, &dCrc),
              "MFM address+data marks via the cell engine");
        bool payloadOk = !sector.empty();
        for (int i = 0; i < 512 && payloadOk; i++)
            if (sector[size_t(i)] != uint8_t(0xA0 + (i & 0x1F))) payloadOk = false;
        check(payloadOk, "MFM track0 sector1 payload matches image");
        check(aCrc, "address-field CRC verifies (handshake CRC0)");
        check(dCrc, "data-field CRC verifies (handshake CRC0)");
    }

    // ── Rotational latency: reads start at the spin angle ─────────────
    {
        std::vector<uint8_t> img(SonyDrive::kSize1440K, 0);
        SonyDrive drive;
        drive.setSuperDrive(true);
        drive.setSpinClockHz(15667200);
        drive.insertImage(std::move(img));
        Swim2 swim;
        swim.reset();
        swim.attachDrive(&drive, nullptr);
        drive.commandSwim(0x2);
        drive.tick(15667200 / 10);               // ~half a revolution at 300 RPM
        swim.write(5, 0x00);
        swim.write(7, 0x8A);                     // ACTION resyncs to rotation
        auto stream = drainSwim(swim, drive, 700, kMfmByte, 700 * kMfmByte * 4);
        const int first = firstMfmSectorNumber(stream);
        check(first > 2, "mid-revolution ACTION lands past the early sectors");
    }

    // ── GCR 800K via Swim2 (not only Iwm) ─────────────────────────────
    {
        std::vector<uint8_t> img(SonyDrive::kSize800K, 0x55);
        img[0] = 0x4C; img[1] = 0x4B;

        SonyDrive drive;
        drive.setSuperDrive(true);
        drive.setSpinClockHz(15667200);
        check(drive.insertImage(img), "insert 800K GCR image");
        check(!drive.mfmMode() && drive.sense(0xA),
              "800K on SuperDrive: GCR mode, SuperDrive sense");

        Swim2 swim;
        swim.reset();
        swim.attachDrive(&drive, nullptr);
        drive.commandSwim(0x2);
        swim.write(5, 0x04);                     // GCR read (setup bit 2)
        swim.write(7, 0x8A);

        auto stream = drainSwim(swim, drive, 2000, kGcrByte, 2000 * kGcrByte * 4);
        int prologues = 0;
        for (size_t i = 0; i + 3 < stream.size(); i++)
            if ((stream[i] & 0xFF) == 0xD5 && (stream[i + 1] & 0xFF) == 0xAA &&
                (stream[i + 2] & 0xFF) == 0x96)
                prologues++;
        check(prologues >= 2, "GCR address prologues frame via the cell engine");
    }

    // ── Direct sector write/read helpers ──────────────────────────────
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
    }

    // ── SWIM2 TSS write engine: format-style sector, real CRC ─────────
    {
        std::vector<uint8_t> img(SonyDrive::kSize1440K, 0);
        SonyDrive drive;
        drive.setSuperDrive(true);
        drive.setSpinClockHz(15667200);
        drive.insertImage(std::move(img));

        Swim2 swim;
        swim.reset();
        swim.attachDrive(&drive, nullptr);
        drive.commandSwim(0x2);                  // spindle on
        swim.write(5, 0x00);                     // MFM write, fclk

        uint8_t wr[512];
        for (int i = 0; i < 512; i++) wr[i] = uint8_t(0xC0 + (i & 0x0F));

        auto writeField = [&](Swim2& sw, SonyDrive& dr, bool goodCrc) {
            auto feed = [&](int reg, uint8_t v) {
                while (sw.fifoCount() >= 2) { sw.tick(64); dr.tick(64); }
                sw.write(reg, v);
                sw.tick(kMfmByte / 2);
                dr.tick(kMfmByte / 2);
            };
            sw.write(7, 0x9A);                   // motor + A + write + ACTION
            for (int i = 0; i < 12; i++) feed(0, 0x00);
            for (int i = 0; i < 3; i++) feed(1, 0xA1);   // marks
            feed(0, 0xFE);
            feed(0, 0x00); feed(0, 0x00); feed(0, 0x02); feed(0, 0x02);
            feed(2, 0);                          // CRC token → real CRC
            for (int i = 0; i < 22; i++) feed(0, 0x4E);
            for (int i = 0; i < 12; i++) feed(0, 0x00);
            for (int i = 0; i < 3; i++) feed(1, 0xA1);
            feed(0, 0xFB);
            for (int i = 0; i < 512; i++) feed(0, wr[i]);
            if (goodCrc) feed(2, 0);
            else { feed(0, 0x12); feed(0, 0x34); }       // corrupt CRC
            for (int i = 0; i < 4; i++) feed(0, 0x4E);
            while (sw.fifoCount()) { sw.tick(kMfmByte); dr.tick(kMfmByte); }
            sw.tick(kMfmByte * 2); dr.tick(kMfmByte * 2);
            sw.write(6, 0x18);                   // exit write → flush/commit
        };

        writeField(swim, drive, true);
        uint8_t back[512];
        check(drive.readSector(0, 0, 1, back), "read sector after TSS write");
        check(std::memcmp(back, wr, 512) == 0,
              "TSS write with CRC token commits the sector");

        // Same field with a corrupt data CRC: must NOT be committed.
        std::vector<uint8_t> img2(SonyDrive::kSize1440K, 0);
        SonyDrive drive2;
        drive2.setSuperDrive(true);
        drive2.setSpinClockHz(15667200);
        drive2.insertImage(std::move(img2));
        Swim2 swim2;
        swim2.reset();
        swim2.attachDrive(&drive2, nullptr);
        drive2.commandSwim(0x2);
        swim2.write(5, 0x00);
        writeField(swim2, drive2, false);
        uint8_t back2[512];
        drive2.readSector(0, 0, 1, back2);
        bool untouched = true;
        for (int i = 0; i < 512 && untouched; i++)
            if (back2[i] != 0) untouched = false;
        check(untouched, "corrupt data CRC is rejected by the decoder");
    }

    // ── SWIM2 TSS GCR write: cell write-back commits a valid field ────
    {
        // Patterned source drive: harvest the encoded data field of
        // track 0 side 0 sector 3 straight off the nibble stream — the
        // write test then proves the TSS + cell decoder invert it.
        std::vector<uint8_t> img(SonyDrive::kSize800K);
        for (size_t i = 0; i < img.size(); i++)
            img[i] = uint8_t(0x11 + (i % 251));
        SonyDrive src;
        src.reset();
        check(src.insertImage(img), "insert patterned 800K GCR source");
        constexpr uint8_t kGcrSector3 = 0x9b;    // kGcr6[3]
        std::vector<uint8_t> field;
        {
            std::vector<uint8_t> nib;
            for (int i = 0; i < 24000; i++) nib.push_back(src.nextNibble(false));
            for (size_t p = 3; p + 720 < nib.size(); p++) {
                if (nib[p] == 0xD5 && nib[p + 1] == 0xAA && nib[p + 2] == 0xAD &&
                    nib[p + 3] == kGcrSector3) {
                    field.assign(nib.begin() + long(p),
                                 nib.begin() + long(p) + 4 + 703 + 2);
                    break;
                }
            }
        }
        check(field.size() == 709, "harvest GCR data field for sector 3");

        SonyDrive dst;
        dst.setSuperDrive(true);
        dst.setSpinClockHz(15667200);
        dst.insertImage(std::vector<uint8_t>(SonyDrive::kSize800K, 0));
        check(!dst.mfmMode(), "800K target stays GCR");

        Swim2 swim;
        swim.reset();
        swim.attachDrive(&dst, nullptr);
        dst.commandSwim(0x2);                    // spindle on
        swim.write(5, 0x44);                     // GCR clocking + GCR write

        auto feed = [&](uint8_t v) {
            while (swim.fifoCount() >= 2) { swim.tick(64); dst.tick(64); }
            swim.write(0, v);
            swim.tick(kGcrByte / 2);
            dst.tick(kGcrByte / 2);
        };
        swim.write(7, 0x9A);                     // motor + A + write + ACTION
        for (int i = 0; i < 6; i++) feed(0xFF);  // leading syncs
        for (uint8_t b : field) feed(b);
        while (swim.fifoCount()) { swim.tick(kGcrByte); dst.tick(kGcrByte); }
        swim.tick(kGcrByte * 2); dst.tick(kGcrByte * 2);
        swim.write(6, 0x18);                     // exit write → flush/commit

        uint8_t got[512], want[512];
        check(dst.readSector(0, 0, 3, got), "read back TSS-written GCR sector");
        src.readSector(0, 0, 3, want);
        check(std::memcmp(got, want, 512) == 0,
              "GCR cell write-back is the inverse of the read path");
        uint8_t other[512];
        dst.readSector(0, 0, 4, other);
        bool untouched = true;
        for (int i = 0; i < 512 && untouched; i++)
            if (other[i] != 0) untouched = false;
        check(untouched, "neighbour GCR sector stays blank");
    }

    // ── Cosmetic MAME divergences, pinned (parity audit § 2.4) ────────
    {
        // (d) A write span with NO transition must leave the track alone.
        // MAME's flush_write (swim2.cpp:100-126) hands write_flux a span with
        // zero fluxes, which erases that arc; Swim2::finishWrite makes it a
        // no-op on purpose (see the note there). Pin it: open write ACTION,
        // close it without feeding a single byte, and the medium must read
        // back byte-for-byte — through the real cell engine, not just through
        // readSector, so a future erase-then-canonicalise alignment shows up.
        std::vector<uint8_t> img(SonyDrive::kSize800K);
        for (size_t i = 0; i < img.size(); i++)
            img[i] = uint8_t(0x33 + (i % 241));

        SonyDrive drive;
        drive.setSuperDrive(true);
        drive.setSpinClockHz(15667200);
        check(drive.insertImage(img), "insert patterned 800K for the empty span");

        uint8_t before[512];
        check(drive.readSector(0, 0, 0, before), "baseline read of t0 h0 s0");

        Swim2 swim;
        swim.reset();
        swim.attachDrive(&drive, nullptr);
        drive.commandSwim(0x2);                  // spindle on
        swim.write(5, 0x44);                     // GCR clocking + GCR write
        swim.write(7, 0x9A);                     // motor + A + write + ACTION
        swim.write(6, 0x18);                     // leave write mode at once
        check((swim.read(2) & 0x01) == 0, "empty write span raises no underrun");

        uint8_t after[512];
        check(drive.readSector(0, 0, 0, after), "read t0 h0 s0 after empty span");
        check(std::memcmp(before, after, 512) == 0,
              "empty write span leaves the track intact");
        // The sharp end of the divergence. Aligning would call commitCells,
        // which erases the arc and then runs the whole-track decoder over it;
        // every field that still verifies is re-committed through
        // writeSector(), which sets dirty_ and re-encodes the track. A write
        // that wrote nothing would thus mark the medium dirty and have
        // flushToFile() rewrite the host image on eject.
        check(!drive.dirty(), "empty write span does not dirty the medium");

        // And the track still frames through the read engine.
        swim.write(5, 0x04);                     // GCR read clocking
        swim.write(7, 0x8A);                     // motor + A + read + ACTION
        auto stream = drainSwim(swim, drive, 2000, kGcrByte, 2000 * kGcrByte * 4);
        int prologues = 0;
        for (size_t i = 0; i + 3 < stream.size(); i++)
            if ((stream[i] & 0xFF) == 0xD5 && (stream[i + 1] & 0xFF) == 0xAA &&
                (stream[i + 2] & 0xFF) == 0x96)
                prologues++;
        check(prologues >= 2, "address prologues survive the empty write span");
    }

    {
        // (b) The reset CRC seed is unobservable. Swim2::reset now seeds
        // crc_ = $FFFF to match MAME swim2.cpp:61 instead of the $CDB4
        // crc_clear() value; this pins that the choice cannot matter, by
        // writing the same MFM field twice on ONE controller — the second
        // pass starts with whatever the first left in crc_, and must commit
        // exactly as the first did (the MARK byte reseeds the register).
        SonyDrive first, second;
        for (SonyDrive* d : { &first, &second }) {
            d->setSuperDrive(true);
            d->setSpinClockHz(15667200);
            d->insertImage(std::vector<uint8_t>(SonyDrive::kSize1440K, 0));
        }

        Swim2 swim;
        swim.reset();
        swim.write(5, 0x00);                     // MFM write, fclk

        // The controller is NOT reset between the two passes — only the media
        // changes — so pass 2 starts on the CRC register pass 1 left behind.
        // Both media are fresh, so both splices land at rotation angle 0 and
        // the decoder's "first field under the head wins" rule cannot skew
        // the comparison.
        auto writeSectorField = [&](SonyDrive& drive, uint8_t fill) {
            swim.attachDrive(&drive, nullptr);
            drive.commandSwim(0x2);              // spindle on
            auto feed = [&](int reg, uint8_t v) {
                while (swim.fifoCount() >= 2) { swim.tick(64); drive.tick(64); }
                swim.write(reg, v);
                swim.tick(kMfmByte / 2);
                drive.tick(kMfmByte / 2);
            };
            swim.write(7, 0x9A);
            for (int i = 0; i < 12; i++) feed(0, 0x00);
            for (int i = 0; i < 3; i++) feed(1, 0xA1);
            feed(0, 0xFE);
            feed(0, 0x00); feed(0, 0x00); feed(0, 0x02); feed(0, 0x02);
            feed(2, 0);
            for (int i = 0; i < 22; i++) feed(0, 0x4E);
            for (int i = 0; i < 12; i++) feed(0, 0x00);
            for (int i = 0; i < 3; i++) feed(1, 0xA1);
            feed(0, 0xFB);
            for (int i = 0; i < 512; i++) feed(0, uint8_t(fill + (i & 0x0F)));
            feed(2, 0);
            for (int i = 0; i < 4; i++) feed(0, 0x4E);
            while (swim.fifoCount()) { swim.tick(kMfmByte); drive.tick(kMfmByte); }
            swim.tick(kMfmByte * 2); drive.tick(kMfmByte * 2);
            swim.write(6, 0x18);
        };
        auto payloadOk = [](const uint8_t* got, uint8_t fill) {
            for (int i = 0; i < 512; i++)
                if (got[i] != uint8_t(fill + (i & 0x0F))) return false;
            return true;
        };

        writeSectorField(first, 0x70);           // crc_ still at the reset seed
        uint8_t a[512];
        check(first.readSector(0, 0, 1, a), "first field commits after reset");
        check(payloadOk(a, 0x70), "field written on the reset CRC seed verifies");

        writeSectorField(second, 0x90);          // crc_ carried over from above
        uint8_t b[512];
        check(second.readSector(0, 0, 1, b), "second field commits");
        check(payloadOk(b, 0x90), "CRC state carried across sessions is irrelevant");
    }

    // ── Flux separator (§ 1.3 flux plan, steps 2-3) ───────────────────
    // The read engine runs a FluxPll over SonyDrive's flux view now. The
    // blocks above are the regression half (ideal edges → identical bit
    // stream); these are the properties the ideal-cell path never had.
    {
        // Opt-in means OFF: no env, no setter → ideal edges.
        SonyDrive plain;
        check(plain.fluxJitterPercent() == 0, "flux jitter defaults to 0");

        // ±12 % peak-shift jitter on every edge: the separator's phase
        // feedback re-centres each window and the sector still verifies.
        std::vector<uint8_t> img(SonyDrive::kSize1440K, 0);
        for (int i = 0; i < 512; i++)
            img[size_t(i)] = uint8_t(0x3C ^ (i & 0x3F));
        SonyDrive drive;
        drive.setSuperDrive(true);
        drive.setSpinClockHz(15667200);
        drive.insertImage(img);
        drive.setFluxJitterPercent(12);

        Swim2 swim;
        swim.reset();
        swim.attachDrive(&drive, nullptr);
        drive.commandSwim(0x2);
        swim.write(5, 0x00);
        swim.write(7, 0x8A);
        auto stream = drainSwim(swim, drive, 1200, kMfmByte, 1200 * kMfmByte * 4);
        std::vector<uint8_t> sector;
        bool aCrc = false, dCrc = false;
        check(findMfmSector(stream, 1, sector, &aCrc, &dCrc),
              "12% jittered MFM: sector frames through the separator");
        bool payloadOk = !sector.empty();
        for (int i = 0; i < 512 && payloadOk; i++)
            if (sector[size_t(i)] != uint8_t(0x3C ^ (i & 0x3F))) payloadOk = false;
        check(payloadOk && aCrc && dCrc,
              "12% jittered MFM: payload and both CRCs verify");

        // Same jitter on GCR (31-clock cells, adjacent transitions).
        SonyDrive gcr;
        gcr.setSuperDrive(true);
        gcr.setSpinClockHz(15667200);
        gcr.insertImage(std::vector<uint8_t>(SonyDrive::kSize800K, 0x69));
        gcr.setFluxJitterPercent(12);
        Swim2 sg;
        sg.reset();
        sg.attachDrive(&gcr, nullptr);
        gcr.commandSwim(0x2);
        sg.write(5, 0x04);
        sg.write(7, 0x8A);
        auto gs = drainSwim(sg, gcr, 2000, kGcrByte, 2000 * kGcrByte * 4);
        int prologues = 0;
        for (size_t i = 0; i + 3 < gs.size(); i++)
            if ((gs[i] & 0xFF) == 0xD5 && (gs[i + 1] & 0xFF) == 0xAA &&
                (gs[i + 2] & 0xFF) == 0x96)
                prologues++;
        check(prologues >= 2, "12% jittered GCR: address prologues frame");
    }

    {
        // The case that BITES (TODO § 4 test-first note): a track written
        // on an off-rate spindle. ±8 % is inside the PLL's ±25 % period
        // pull and far outside a fixed window, which slips within 32 cells
        // (flux_pll_test) — so these two checks fail on any regression to
        // fixed-window reading while every ideal-edge block above passes.
        for (int permille : { 1080, 920 }) {
            std::vector<uint8_t> img(SonyDrive::kSize1440K, 0);
            for (int i = 0; i < 512; i++)
                img[size_t(i)] = uint8_t(0x81 + (i & 0x3F));
            SonyDrive drive;
            drive.setSuperDrive(true);
            drive.setSpinClockHz(15667200);
            drive.insertImage(std::move(img));
            drive.debugStretchFluxPermille(permille);

            Swim2 swim;
            swim.reset();
            swim.attachDrive(&drive, nullptr);
            drive.commandSwim(0x2);
            swim.write(5, 0x00);
            swim.write(7, 0x8A);
            auto stream = drainSwim(swim, drive, 1200, kMfmByte,
                                    1200 * kMfmByte * 5);
            std::vector<uint8_t> sector;
            bool aCrc = false, dCrc = false;
            const bool found = findMfmSector(stream, 1, sector, &aCrc, &dCrc);
            char what[80];
            std::snprintf(what, sizeof what,
                          "off-rate track (%d permille): PLL pulls and decodes",
                          permille);
            bool payloadOk = found && !sector.empty();
            for (int i = 0; i < 512 && payloadOk; i++)
                if (sector[size_t(i)] != uint8_t(0x81 + (i & 0x3F)))
                    payloadOk = false;
            check(payloadOk && aCrc && dCrc, what);
        }
    }

    {
        // The separator is live machine state: snapshot MID-SECTOR, restore
        // into fresh objects, and the remaining byte stream must match the
        // uninterrupted run — with jitter on, a nominal-loop restore would
        // shift every following window.
        auto makeImg = [] {
            std::vector<uint8_t> img(SonyDrive::kSize1440K, 0);
            for (int i = 0; i < 512; i++)
                img[size_t(i)] = uint8_t(0xE7 - (i & 0x1F));
            return img;
        };
        SonyDrive drive;
        drive.setSuperDrive(true);
        drive.setSpinClockHz(15667200);
        drive.insertImage(makeImg());
        drive.setFluxJitterPercent(10);
        Swim2 swim;
        swim.reset();
        swim.attachDrive(&drive, nullptr);
        drive.commandSwim(0x2);
        swim.write(5, 0x00);
        swim.write(7, 0x8A);

        auto head = drainSwim(swim, drive, 40, kMfmByte, 40 * kMfmByte * 4);
        check(head.size() == 40, "mid-sector point reached before snapshot");

        std::vector<sav::u8> buf;
        { sav::Writer w(buf); w(swim); w(drive); }

        // Reference: the uninterrupted continuation.
        auto ref = drainSwim(swim, drive, 200, kMfmByte, 200 * kMfmByte * 4);

        SonyDrive drive2;
        Swim2 swim2;
        {
            sav::Reader r(buf.data(), buf.size());
            r(swim2); r(drive2);
        }
        drive2.setFluxJitterPercent(10);         // config, not state
        swim2.attachDrive(&drive2, nullptr);     // pointers re-bound, not saved
        auto got = drainSwim(swim2, drive2, 200, kMfmByte, 200 * kMfmByte * 4);
        check(got == ref, "restored separator resumes bit-identically");
    }

    // ── Drive detect / eject ──────────────────────────────────────────
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
