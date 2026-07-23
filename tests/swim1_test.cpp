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
constexpr int kQ6On = 13, kQ7On = 15, kQ7Off = 14, kEnableOn = 9;

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

// Driver-style ISM param table: only TIME0/TIME1 matter to our engine.
// MFM at fclk: an empty cell is 31 halves (P_TIME0+4), a flux transition
// swallows the following clock window = 63 halves (P_TIME1+4) — the same
// 31/63 spacing the SWIM2 engine hardwires (swim2.cpp:432-436).
void loadParams(Swim1& s) {
    static const uint8_t kParams[16] = {
        8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 0, 27, 0, 59,
    };
    s.write(6, 0xBF);                            // clear all BUT bit 6 → idx 0
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

    std::printf("%s\n", gFails ? "FAILED" : "PASSED");
    return gFails ? 1 : 0;
}
