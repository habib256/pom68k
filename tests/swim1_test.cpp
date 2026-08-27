// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// SWIM1 gate (LC II O6.7): the two personalities behind one window.
// - IWM mode at reset: the embedded Iwm serves the GCR nibble stream.
// - The 1-0-1-1 bit-6 magic on the IWM mode register switches to ISM
//   (MAME swim1.cpp:555-579); a broken pattern does not.
// - ISM register file: 16-deep param RAM ring, mode set/clear pair,
//   FIFO handshake; mode-clear dropping bit 6 returns to IWM.
// - ISM MFM end-to-end on a 1.44 MB image: driver-style setup (params,
//   GCR/MFM select, ACTION) reads track 0 sector 1 with a verified CRC,
//   and the TSS write engine commits a sector back (param-timed cells).

#include "Swim1.h"
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

// IWM personality register lines (reg = line*2 + set)
constexpr int kQ6On = 13, kQ7On = 15;
// Named for the map's sake; the ISM switch below drives the other two.
[[maybe_unused]] constexpr int kQ7Off = 14, kEnableOn = 9;

// Perform the ISM switch exactly like the .Sony driver: four mode-register
// writes (q7-set odd address while idle) with bit 6 = 1,0,1,1. Two 0-bit
// writes first normalize the pattern counter from any prior state (a
// 0-bit resets it from states 0/2 and walks 1→2→reset).
void switchToIsm(Swim1& s) {
    s.read(kQ6On);                               // q6 on, enable off: mode reg
    s.write(kQ7On, 0x17);
    s.write(kQ7On, 0x17);
    s.write(kQ7On, 0x57);                        // bit6=1
    s.write(kQ7On, 0x17);                        // bit6=0
    s.write(kQ7On, 0x57);                        // bit6=1
    s.write(kQ7On, 0x57);                        // bit6=1 -> ISM
}

// Driver-style ISM param table. With the real CSM read engine the params
// are LOAD-BEARING, as on silicon: every classification threshold is
// parameter-RAM arithmetic (swim1.cpp:1006-1080). These MFM-at-fclk values
// are DERIVED from the engine's own threshold shapes, not dumped from a
// driver — chosen so the cumulative boundaries land halfway between the
// legal gap lengths (2/3/4 cells = 64/96/128 halves):
//   MINCT+6 = 48, +SSS+4 = 80, +SLS+4 = 112, +RPT+4 = 144,
// with the S and L hypotheses identical (symmetric channel, no marginal
// pairs) and P_MULT = 64 so a nominal calibration sums 32×64×32 = 0x10000
// per pair side → correction factor 0x100 = the neutral scale 256.
// Write side unchanged: an empty cell is 31 halves (P_TIME0+4), a flux
// swallows the next clock window = 63 halves (P_TIME1+4).
void loadParams(Swim1& s, uint8_t mult = 64, uint8_t time1 = 59) {
    const uint8_t kParams[16] = {
        42, mult, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 0, 27, 0, time1,
    };
    s.write(6, 0xBF);                            // clear all BUT bit 6 → idx 0
    for (uint8_t p : kParams) s.write(3, p);
}

// GCR-at-fclk table, same derivation for 31-clock cells (gaps of 1/2/3
// cells = 62/124/186 halves): MINCT+6 = 46, +SSS+4 = 93, +SLS+4 = 155,
// +RPT+4 = 217. GCR ACTION starts in CSM_SYNCHRONIZED (swim1.cpp:394), so
// P_MULT is inert there.
void loadParamsGcr(Swim1& s) {
    static const uint8_t kParams[16] = {
        40, 64, 43, 43, 58, 58, 58, 58, 43, 43, 58, 58, 0, 57, 0, 57,
    };
    s.write(6, 0xBF);
    for (uint8_t p : kParams) s.write(3, p);
}

constexpr int kMfmByte = 16 * 16;

std::vector<uint16_t> drainFifo(Swim1& s, SonyDrive& d, int need, int budget) {
    std::vector<uint16_t> out;
    const int step = kMfmByte / 2;
    while (int(out.size()) < need && budget > 0) {
        s.tick(step);
        d.tick(step);
        budget -= step;
        while (s.fifoCount() && int(out.size()) < need) {
            const uint8_t h = s.read(7);         // handshake first (flags)
            uint16_t v = s.read(1);              // mark register: any byte
            if (s.fifoCount() == 0) {
                if (h & 0x01) v |= 0x100;        // MARK
                if (!(h & 0x02)) v |= 0x200;     // CRC ok on this byte
            }
            out.push_back(v);
        }
    }
    return out;
}
} // namespace

int main() {
    std::printf("swim1_test — IWM/ISM personalities + ISM MFM media\n");

    // ── Switch pattern ────────────────────────────────────────────────
    {
        Swim1 s;
        s.reset();
        check(!s.ism(), "reset: IWM personality");
        s.read(kQ6On);
        s.write(kQ7On, 0x57);
        s.write(kQ7On, 0x57);                    // breaks the 1-0-1-1 pattern
        s.write(kQ7On, 0x17);
        s.write(kQ7On, 0x57);
        check(!s.ism(), "broken magic pattern stays IWM");
        switchToIsm(s);
        check(s.ism(), "1-0-1-1 bit-6 magic enters ISM");
        check((s.read(6) & 0x40) != 0, "ISM mode register bit 6 set");
        s.write(6, 0x40);                        // mode clear: drop bit 6
        check(!s.ism(), "mode-clear bit 6 returns to IWM");
    }

    // ── Param RAM ring ────────────────────────────────────────────────
    {
        Swim1 s;
        s.reset();
        switchToIsm(s);
        s.write(6, 0x3F);                        // clear (keeps bit 6) → idx 0
        for (int i = 0; i < 16; i++) s.write(3, uint8_t(0xA0 + i));
        s.write(6, 0x00);
        s.write(6, 0x20);                        // any clear resets the index
        bool ring = true;
        for (int i = 0; i < 16 && ring; i++)
            if (s.read(3) != uint8_t(0xA0 + i)) ring = false;
        check(ring, "16-deep param RAM ring reads back");
    }

    // ── ISM MFM read: 1.44 MB sector through the cell engine ──────────
    {
        std::vector<uint8_t> img(SonyDrive::kSize1440K, 0);
        for (int i = 0; i < 512; i++) img[size_t(i)] = uint8_t(0x30 + (i & 0x3F));

        SonyDrive drive;
        drive.setSpinClockHz(15667200);
        Swim1 swim;
        swim.reset();
        swim.attachDrive(&drive, nullptr);       // marks it a SuperDrive
        check(drive.insertImage(img), "insert 1.44MB image");
        check(drive.isHd() && drive.mfmMode(), "HD media: MFM mode");

        switchToIsm(swim);
        loadParams(swim);
        drive.commandSwim(0x2);                  // spindle on
        swim.write(5, 0x00);                     // MFM, fclk
        swim.write(7, 0x8A);                     // motor + drive A + ACTION rd

        auto stream = drainFifo(swim, drive, 1200, 1200 * kMfmByte * 4);
        bool found = false, crcOk = false;
        std::vector<uint8_t> sector;
        auto marks = [&](size_t i) { return (stream[i] & 0x1FF) == 0x1A1; };
        for (size_t i = 0; i + 10 < stream.size() && !found; i++) {
            if (!(marks(i) && marks(i + 1) && marks(i + 2) &&
                  (stream[i + 3] & 0xFF) == 0xFE && int(stream[i + 6] & 0xFF) == 1))
                continue;
            for (size_t j = i + 10; j + 517 < stream.size() && j < i + 80; j++) {
                if (!(marks(j) && marks(j + 1) && marks(j + 2) &&
                      (stream[j + 3] & 0xFF) == 0xFB))
                    continue;
                sector.clear();
                for (int k = 0; k < 512; k++)
                    sector.push_back(uint8_t(stream[j + 4 + size_t(k)]));
                crcOk = (stream[j + 517] & 0x200) != 0;
                found = true;
                break;
            }
        }
        check(found, "ISM read finds track0 sector1 marks");
        bool payload = found;
        for (int i = 0; i < 512 && payload; i++)
            if (sector[size_t(i)] != uint8_t(0x30 + (i & 0x3F))) payload = false;
        check(payload, "ISM payload matches the image");
        check(crcOk, "data CRC verifies through the ISM FIFO flags");
    }

    // ── ISM read through the data separator (§ 1.3 flux plan, step 4a):
    // 12 % peak-shift jitter on every edge and the sector still verifies —
    // the property the ideal-cell fixed window never had to earn. ─────────
    {
        std::vector<uint8_t> img(SonyDrive::kSize1440K, 0);
        for (int i = 0; i < 512; i++) img[size_t(i)] = uint8_t(0x30 + (i & 0x3F));

        SonyDrive drive;
        drive.setSpinClockHz(15667200);
        Swim1 swim;
        swim.reset();
        swim.attachDrive(&drive, nullptr);
        drive.insertImage(std::move(img));
        drive.setFluxJitterPercent(12);

        switchToIsm(swim);
        loadParams(swim);
        drive.commandSwim(0x2);
        swim.write(5, 0x00);
        swim.write(7, 0x8A);

        auto stream = drainFifo(swim, drive, 1200, 1200 * kMfmByte * 4);
        bool found = false, crcOk = false;
        std::vector<uint8_t> sector;
        auto marks = [&](size_t i) { return (stream[i] & 0x1FF) == 0x1A1; };
        for (size_t i = 0; i + 10 < stream.size() && !found; i++) {
            if (!(marks(i) && marks(i + 1) && marks(i + 2) &&
                  (stream[i + 3] & 0xFF) == 0xFE && int(stream[i + 6] & 0xFF) == 1))
                continue;
            for (size_t j = i + 10; j + 517 < stream.size() && j < i + 80; j++) {
                if (!(marks(j) && marks(j + 1) && marks(j + 2) &&
                      (stream[j + 3] & 0xFF) == 0xFB))
                    continue;
                sector.clear();
                for (int k = 0; k < 512; k++)
                    sector.push_back(uint8_t(stream[j + 4 + size_t(k)]));
                crcOk = (stream[j + 517] & 0x200) != 0;
                found = true;
                break;
            }
        }
        bool payload = found;
        for (int i = 0; i < 512 && payload; i++)
            if (sector[size_t(i)] != uint8_t(0x30 + (i & 0x3F))) payload = false;
        check(found && payload && crcOk,
              "12% jittered ISM read: sector + CRC through the separator");
    }

    // ── The Correction State Machine is LIVE, gated through the register
    // file: a +20 % off-rate track misclassifies its 2-cell gaps at the
    // neutral scale (115 > the 112-half boundary), so the read only
    // succeeds if the 64-min-cell calibration rescales the thresholds.
    // P_MULT = 0 starves the calibration — same track, error $08, no
    // sector. That pair is the port's bite test, live in the suite. ──────
    {
        auto tryRead = [](uint8_t mult, uint8_t* errOut) {
            std::vector<uint8_t> img(SonyDrive::kSize1440K, 0);
            for (int i = 0; i < 512; i++)
                img[size_t(i)] = uint8_t(0x52 ^ (i & 0x7F));
            SonyDrive drive;
            drive.setSpinClockHz(15667200);
            Swim1 swim;
            swim.reset();
            swim.attachDrive(&drive, nullptr);
            drive.insertImage(std::move(img));
            drive.debugStretchFluxPermille(1200);

            switchToIsm(swim);
            loadParams(swim, mult);
            drive.commandSwim(0x2);
            swim.write(5, 0x00);
            swim.write(7, 0x8A);
            auto stream = drainFifo(swim, drive, 1200, 1200 * kMfmByte * 5);
            if (errOut) *errOut = swim.read(2);
            auto marks = [&](size_t i) { return (stream[i] & 0x1FF) == 0x1A1; };
            for (size_t i = 0; i + 10 < stream.size(); i++) {
                if (!(marks(i) && marks(i + 1) && marks(i + 2) &&
                      (stream[i + 3] & 0xFF) == 0xFE &&
                      int(stream[i + 6] & 0xFF) == 1))
                    continue;
                for (size_t j = i + 10; j + 517 < stream.size() && j < i + 80; j++) {
                    if (!(marks(j) && marks(j + 1) && marks(j + 2) &&
                          (stream[j + 3] & 0xFF) == 0xFB))
                        continue;
                    if (!(stream[j + 517] & 0x200)) return false;   // CRC bad
                    for (int k = 0; k < 512; k++)
                        if (uint8_t(stream[j + 4 + size_t(k)]) !=
                            uint8_t(0x52 ^ (k & 0x7F)))
                            return false;
                    return true;
                }
                return false;
            }
            return false;
        };
        uint8_t err = 0;
        check(tryRead(64, nullptr),
              "+20% off-rate track: the CSM recalibrates and the sector reads");
        check(!tryRead(0, &err),
              "P_MULT=0 starves the calibration and the same track fails");
        check((err & 0x08) != 0,
              "starved calibration raises error $08 (out of range)");
    }

    // ── GCR through the ISM: CSM_SYNCHRONIZED from ACTION (swim1.cpp:394),
    // gap→bits TSM, high-bit framing — the address prologues come out. ───
    {
        std::vector<uint8_t> img(SonyDrive::kSize800K, 0x37);
        SonyDrive drive;
        drive.setSpinClockHz(15667200);
        Swim1 swim;
        swim.reset();
        swim.attachDrive(&drive, nullptr);
        check(drive.insertImage(std::move(img)), "insert 800K for GCR ISM");
        check(!drive.mfmMode(), "800K media stays GCR on the SuperDrive");

        switchToIsm(swim);
        loadParamsGcr(swim);
        drive.commandSwim(0x2);
        swim.write(5, 0x04);                     // GCR read clocking
        swim.write(7, 0x8A);

        constexpr int kGcrByte = 8 * 31;
        std::vector<uint16_t> stream;
        {
            int budget = 2200 * kGcrByte * 4;
            const int step = kGcrByte / 2;
            while (int(stream.size()) < 2200 && budget > 0) {
                swim.tick(step);
                drive.tick(step);
                budget -= step;
                while (swim.fifoCount() && int(stream.size()) < 2200)
                    stream.push_back(swim.read(1));
            }
        }
        int prologues = 0;
        for (size_t i = 0; i + 3 < stream.size(); i++)
            if ((stream[i] & 0xFF) == 0xD5 && (stream[i + 1] & 0xFF) == 0xAA &&
                (stream[i + 2] & 0xFF) == 0x96)
                prologues++;
        check(prologues >= 2, "GCR ISM read frames address prologues");
    }

    // ── ISM MFM write: param-timed TSS commits a sector ───────────────
    {
        std::vector<uint8_t> img(SonyDrive::kSize1440K, 0);
        SonyDrive drive;
        drive.setSpinClockHz(15667200);
        Swim1 swim;
        swim.reset();
        swim.attachDrive(&drive, nullptr);
        drive.insertImage(std::move(img));

        switchToIsm(swim);
        loadParams(swim);
        drive.commandSwim(0x2);
        swim.write(5, 0x00);                     // MFM write, fclk

        uint8_t wr[512];
        for (int i = 0; i < 512; i++) wr[i] = uint8_t(0x90 ^ (i & 0xFF));

        auto feed = [&](int reg, uint8_t v) {
            int guard = 64;                      // bail if ACTION died (underrun)
            while (swim.fifoCount() >= 2 && guard--) { swim.tick(64); drive.tick(64); }
            swim.write(reg, v);
            swim.tick(kMfmByte / 2);
            drive.tick(kMfmByte / 2);
        };
        swim.write(7, 0x9A);                     // motor + A + write + ACTION
        for (int i = 0; i < 12; i++) feed(0, 0x00);
        for (int i = 0; i < 3; i++) feed(1, 0xA1);
        feed(0, 0xFE);
        feed(0, 0x00); feed(0, 0x00); feed(0, 0x02); feed(0, 0x02);
        feed(2, 0);                              // CRC token
        for (int i = 0; i < 22; i++) feed(0, 0x4E);
        for (int i = 0; i < 12; i++) feed(0, 0x00);
        for (int i = 0; i < 3; i++) feed(1, 0xA1);
        feed(0, 0xFB);
        for (int i = 0; i < 512; i++) feed(0, wr[i]);
        feed(2, 0);
        for (int i = 0; i < 4; i++) feed(0, 0x4E);
        int drainGuard = 64;
        while (swim.fifoCount() && drainGuard--) {
            swim.tick(kMfmByte);
            drive.tick(kMfmByte);
        }
        swim.tick(kMfmByte * 2); drive.tick(kMfmByte * 2);
        swim.write(6, 0x18);                     // exit write -> commit

        uint8_t back[512];
        check(drive.readSector(0, 0, 1, back), "read back ISM-written sector");
        check(std::memcmp(back, wr, 512) == 0,
              "param-timed TSS write commits through the cell decoder");
    }

    // ── The flux STORE, ISM side (§ 1.3 flux plan, step 5) ────────────
    // The write above is already off the medium's rate: P_TIME1+4 = 63
    // halves between fluxes where the HD MFM cell is 32, i.e. -1.6 %. The
    // store is what lets that reach the disk — before it, finishWrite
    // reconstructed cell indices and the medium came back on its own grid,
    // so the parameter RAM had no effect a read could ever observe. Here
    // the param RAM chooses the spacing and the spacing is what stays.
    {
        constexpr int64_t kHalfTick = FluxPll::kSubCell / 2;
        constexpr int64_t kMfmCellTicks = 16 * FluxPll::kSubCell;
        auto countGap = [](const SonyDrive& d, int64_t gap) {
            const std::vector<int64_t>& f = d.debugFlux();
            int n = 0;
            for (size_t i = 1; i < f.size(); i++)
                if (f[i] - f[i - 1] == gap) n++;
            return n;
        };

        // Write one MFM data field with P_TIME1 = t1, return the drive.
        auto ismWrite = [&](SonyDrive& drive, Swim1& swim, uint8_t t1,
                            const uint8_t* wr) {
            drive.setSpinClockHz(15667200);
            swim.reset();
            swim.attachDrive(&drive, nullptr);
            drive.insertImage(std::vector<uint8_t>(SonyDrive::kSize1440K, 0));
            switchToIsm(swim);
            loadParams(swim, 64, t1);
            drive.commandSwim(0x2);
            swim.write(5, 0x00);                 // MFM write, fclk
            auto feed = [&](int reg, uint8_t v) {
                int guard = 64;
                while (swim.fifoCount() >= 2 && guard--) {
                    swim.tick(64); drive.tick(64);
                }
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
            for (int i = 0; i < 512; i++) feed(0, wr[i]);
            feed(2, 0);
            for (int i = 0; i < 4; i++) feed(0, 0x4E);
            int drainGuard = 64;
            while (swim.fifoCount() && drainGuard--) {
                swim.tick(kMfmByte); drive.tick(kMfmByte);
            }
            swim.tick(kMfmByte * 2); drive.tick(kMfmByte * 2);
            swim.write(6, 0x18);                 // exit write -> commit
        };

        uint8_t wr[512];
        for (int i = 0; i < 512; i++) wr[i] = uint8_t(0x90 ^ (i & 0xFF));

        // Control: a canonical HD track sits on the 16-clock media grid.
        {
            SonyDrive fresh;
            fresh.insertImage(std::vector<uint8_t>(SonyDrive::kSize1440K, 0));
            int off = 0;
            for (int64_t t : fresh.debugFlux())
                if ((t - kMfmCellTicks / 2) % kMfmCellTicks != 0) off++;
            check(!fresh.debugFlux().empty() && off == 0,
                  "canonical HD track sits on the 16-clock media grid");
            check(countGap(fresh, 63 * kHalfTick) == 0,
                  "canonical HD track has no 63-half gap");
        }

        // Default params: 63 halves between fluxes, and that is what lands.
        {
            SonyDrive drive; Swim1 swim;
            ismWrite(drive, swim, 59, wr);
            uint8_t back[512];
            check(drive.readSector(0, 0, 1, back) &&
                  std::memcmp(back, wr, 512) == 0,
                  "ISM flux-store write still commits its sector");
            check(countGap(drive, 63 * kHalfTick) > 100,
                  "the medium keeps P_TIME1's 63-half spacing");
        }

        // The bite: move P_TIME1 and the DISK moves with it. A guest that
        // reprograms its write timings is writing a different disk, which
        // is only true because the store holds times.
        {
            SonyDrive drive; Swim1 swim;
            ismWrite(drive, swim, 67, wr);       // P_TIME1+4 = 71 halves
            uint8_t back[512];
            check(drive.readSector(0, 0, 1, back) &&
                  std::memcmp(back, wr, 512) == 0,
                  "a +11% param-timed write still verifies on read-back");
            check(countGap(drive, 71 * kHalfTick) > 100,
                  "the medium carries the reprogrammed 71-half spacing");
            check(countGap(drive, 63 * kHalfTick) == 0,
                  "and none of the default spacing");
        }
    }

    std::printf("%s\n", gFails ? "FAILED" : "PASSED");
    return gFails ? 1 : 0;
}
