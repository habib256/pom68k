// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// M5 gate: GCR encode/decode roundtrip. The SonyDrive encoder is verified
// with an independent decoder implementing MAME's
// extract_sectors_from_track_mac_gcr6 loop: for several tracks/sides of a
// patterned 800K image, every sector must decode with matching address
// fields, data bytes and checksum.

#include "SonyDrive.h"
#include <cstdio>
#include <vector>

static const uint8_t kGcr6[0x40] = {
    0x96, 0x97, 0x9a, 0x9b, 0x9d, 0x9e, 0x9f, 0xa6,
    0xa7, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb2, 0xb3,
    0xb4, 0xb5, 0xb6, 0xb7, 0xb9, 0xba, 0xbb, 0xbc,
    0xbd, 0xbe, 0xbf, 0xcb, 0xcd, 0xce, 0xcf, 0xd3,
    0xd6, 0xd7, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde,
    0xdf, 0xe5, 0xe6, 0xe7, 0xe9, 0xea, 0xeb, 0xec,
    0xed, 0xee, 0xef, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6,
    0xf7, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,
};

static int gcrInv(uint8_t nib) {
    for (int i = 0; i < 64; i++) if (kGcr6[i] == nib) return i;
    return -1;
}

static uint8_t expectedByte(int track, int side, int sector, int i) {
    return uint8_t(track * 7 + side * 31 + sector * 13 + i);
}

#define CHECK(cond, ...) do { if (!(cond)) { \
    std::fprintf(stderr, "FAIL: " __VA_ARGS__); std::fprintf(stderr, "\n"); return 1; } } while (0)

int main() {
    // Patterned 800K image
    std::vector<uint8_t> img(819200);
    size_t off = 0;
    for (int t = 0; t < 80; t++)
        for (int h = 0; h < 2; h++)
            for (int s = 0; s < SonyDrive::sectorsInTrack(t); s++)
                for (int i = 0; i < 512; i++)
                    img[off++] = expectedByte(t, h, s, i);
    CHECK(off == 819200, "geometry: %zu", off);

    SonyDrive drive;
    drive.reset();
    CHECK(drive.insertImage(img), "insertImage");

    for (int t : { 0, 15, 16, 40, 64, 79 }) {
        // seek: DIRTN then STEPs (commands: value CA2 in bit 3)
        while (drive.currentTrack() < t) { drive.command(0b0000); drive.command(0b0010); }
        while (drive.currentTrack() > t) { drive.command(0b1000); drive.command(0b0010); }
        CHECK(drive.currentTrack() == t, "seek to %d", t);

        for (int h = 0; h < 2; h++) {
            int ns = SonyDrive::sectorsInTrack(t);
            std::vector<bool> seen(size_t(ns), false);
            // pull two tracks' worth of nibbles and decode every sector
            std::vector<uint8_t> nib;
            for (int i = 0; i < 2 * ns * 800; i++) nib.push_back(drive.nextNibble(h == 1));

            for (size_t p = 0; p + 760 < nib.size(); p++) {
                if (!(nib[p] == 0xD5 && nib[p+1] == 0xAA && nib[p+2] == 0x96)) continue;
                int atrk = gcrInv(nib[p+3]), asec = gcrInv(nib[p+4]);
                int aside = gcrInv(nib[p+5]), afmt = gcrInv(nib[p+6]), asum = gcrInv(nib[p+7]);
                CHECK(atrk == (t & 0x3F), "addr track %d vs %d", atrk, t);
                CHECK(aside == ((h ? 0x20 : 0) | ((t & 0x40) ? 1 : 0)), "side byte");
                CHECK(afmt == 0x22, "format byte");
                CHECK(asum == ((atrk ^ asec ^ aside ^ afmt) & 0x3F), "addr checksum");
                CHECK(nib[p+8] == 0xDE && nib[p+9] == 0xAA, "addr epilogue");

                // find data mark
                size_t q = p + 10;
                while (q + 710 < nib.size() &&
                       !(nib[q] == 0xD5 && nib[q+1] == 0xAA && nib[q+2] == 0xAD)) q++;
                CHECK(nib[q] == 0xD5, "data mark for sector %d", asec);
                CHECK(gcrInv(nib[q+3]) == asec, "data sector byte");

                // MAME decode loop (extract_sectors_from_track_mac_gcr6)
                uint8_t sdata[525] = {};
                uint8_t ca = 0, cb = 0, cc = 0;
                size_t r = q + 4;
                for (int i = 0; i < 175; i++) {
                    uint8_t h0 = uint8_t(gcrInv(nib[r++]));
                    uint8_t va = uint8_t((gcrInv(nib[r++]) & 0x3F) | ((h0 << 2) & 0xC0));
                    uint8_t vb = uint8_t((gcrInv(nib[r++]) & 0x3F) | ((h0 << 4) & 0xC0));
                    uint8_t vc = (i != 174)
                        ? uint8_t((gcrInv(nib[r++]) & 0x3F) | ((h0 << 6) & 0xC0)) : 0;

                    cc = uint8_t((cc << 1) | (cc >> 7));
                    va = va ^ cc;
                    uint16_t suma = uint16_t(ca + va + (cc & 1));
                    ca = uint8_t(suma);
                    vb = vb ^ ca;
                    uint16_t sumb = uint16_t(cb + vb + (suma >> 8));
                    cb = uint8_t(sumb);
                    vc = vc ^ cb;
                    sdata[3*i] = va; sdata[3*i+1] = vb;
                    if (i != 174) { cc = uint8_t(cc + vc + (sumb >> 8)); sdata[3*i+2] = vc; }
                }
                uint8_t h0 = uint8_t(gcrInv(nib[r]));
                uint8_t wa = uint8_t((gcrInv(nib[r+1]) & 0x3F) | ((h0 << 2) & 0xC0));
                uint8_t wb = uint8_t((gcrInv(nib[r+2]) & 0x3F) | ((h0 << 4) & 0xC0));
                uint8_t wc = uint8_t((gcrInv(nib[r+3]) & 0x3F) | ((h0 << 6) & 0xC0));
                CHECK(wa == ca && wb == cb && wc == cc,
                      "data checksum t%d h%d s%d", t, h, asec);
                CHECK(nib[r+4] == 0xDE && nib[r+5] == 0xAA, "data epilogue");

                for (int i = 0; i < 12; i++)
                    CHECK(sdata[i] == 0, "tag byte %d", i);
                for (int i = 0; i < 512; i++)
                    CHECK(sdata[12+i] == expectedByte(t, h, asec, i),
                          "data t%d h%d s%d byte %d: got %02X want %02X",
                          t, h, asec, i, sdata[12+i], expectedByte(t, h, asec, i));
                if (!seen[size_t(asec)]) seen[size_t(asec)] = true;
                p = r + 5;
            }
            for (int s = 0; s < ns; s++)
                CHECK(seen[size_t(s)], "sector %d missing on t%d h%d", s, t, h);
        }
    }
    std::printf("gcr_test: all tracks roundtrip OK\n");
    return 0;
}
