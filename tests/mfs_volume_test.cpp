// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Gate: the MFS reader (src/MfsVolume.h) on a volume built here byte by
// byte — no Apple asset — then, when the pinned System 1.1 floppy is on
// hand, on the real thing.
//
// The synthetic volume exercises what a stock floppy would not: a data
// fork whose chain spans three allocation blocks out of order, a second
// directory sector, an entry that would straddle a sector, a chain that
// loops, and a chain that leaves the volume.

#include "MfsVolume.h"
#include "AssetFingerprint.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {
int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-66s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}

void put16(std::vector<uint8_t>& v, size_t off, uint16_t x) {
    v[off] = uint8_t(x >> 8); v[off + 1] = uint8_t(x);
}
void put32(std::vector<uint8_t>& v, size_t off, uint32_t x) {
    put16(v, off, uint16_t(x >> 16)); put16(v, off + 2, uint16_t(x));
}
// 12-bit block map entry for allocation block `block` (2-based).
void setMap(std::vector<uint8_t>& v, uint32_t block, uint16_t next) {
    const size_t i = block - 2;
    const size_t base = 2 * 512 + 64 + (i / 2) * 3;
    if (i % 2 == 0) {
        v[base] = uint8_t(next >> 4);
        v[base + 1] = uint8_t((v[base + 1] & 0x0F) | ((next & 0x0F) << 4));
    } else {
        v[base + 1] = uint8_t((v[base + 1] & 0xF0) | (next >> 8));
        v[base + 2] = uint8_t(next);
    }
}
// One directory entry at `off`; returns the offset after it (even-padded).
size_t putEntry(std::vector<uint8_t>& v, size_t off, const char* name,
                uint32_t fileNumber, uint16_t dataStart, uint32_t dataLen,
                uint16_t rsrcStart, uint32_t rsrcLen, bool locked = false) {
    const size_t n = std::strlen(name);
    v[off] = uint8_t(0x80 | (locked ? 1 : 0));
    std::memcpy(&v[off + 2], "TEXTttxt", 8);
    put32(v, off + 18, fileNumber);
    put16(v, off + 22, dataStart);
    put32(v, off + 24, dataLen);
    put32(v, off + 28, ((dataLen + 1023) / 1024) * 1024);
    put16(v, off + 32, rsrcStart);
    put32(v, off + 34, rsrcLen);
    put32(v, off + 38, ((rsrcLen + 1023) / 1024) * 1024);
    put32(v, off + 42, 0x9A3F1234);
    put32(v, off + 46, 0x9A3F5678);
    v[off + 50] = uint8_t(n);
    std::memcpy(&v[off + 51], name, n);
    const size_t entry = 51 + n;
    return off + entry + (entry & 1);
}

// A 400 K volume: directory at sectors 4-5, allocation blocks (1 KB) from
// sector 16, 391 of them.
std::vector<uint8_t> makeVolume() {
    std::vector<uint8_t> v(409600, 0);
    const size_t vib = 2 * 512;
    put16(v, vib + 0, 0xD2D7);
    put32(v, vib + 2, 0x9A000000);    // drCrDate
    put16(v, vib + 12, 2);            // drNmFls
    put16(v, vib + 14, 4);            // drDirSt
    put16(v, vib + 16, 2);            // drBlLen
    put16(v, vib + 18, 391);          // drNmAlBlks
    put32(v, vib + 20, 1024);         // drAlBlkSiz
    put32(v, vib + 24, 8192);         // drClpSiz
    put16(v, vib + 28, 16);           // drAlBlSt
    put32(v, vib + 30, 3);            // drNxtFNum
    put16(v, vib + 34, 385);          // drFreeBks
    v[vib + 36] = 4; std::memcpy(&v[vib + 37], "Test", 4);
    // File 1: data fork 2500 bytes over blocks 5 -> 3 -> 9 (out of order).
    setMap(v, 5, 3); setMap(v, 3, 9); setMap(v, 9, 1);
    auto block = [&](uint32_t b) { return 16 * 512 + (b - 2) * 1024; };
    for (size_t i = 0; i < 1024; i++) v[block(5) + i] = uint8_t(i);
    for (size_t i = 0; i < 1024; i++) v[block(3) + i] = uint8_t(0x40 + i);
    for (size_t i = 0; i < 1024; i++) v[block(9) + i] = uint8_t(0x80 + i);
    // File 2: resource fork of one block (block 12), no data fork.
    setMap(v, 12, 1);
    std::memcpy(&v[block(12)], "RSRC", 4);
    size_t off = putEntry(v, 4 * 512, "Chained", 1, 5, 2500, 0, 0);
    (void)off;
    // Second directory sector carries the second file.
    putEntry(v, 5 * 512, "Second sector", 2, 0, 0, 12, 4, true);
    return v;
}
} // namespace

int main() {
    std::printf("mfs_volume_test — Macintosh File System reader\n");
    using namespace pom68k::mfs;

    {
        const std::vector<uint8_t> img = makeVolume();
        const Volume v = parse(img);
        check(v.valid && v.error.empty(), "synthetic volume parses without a note");
        check(v.name == "Test" && v.allocationBlocks == 391 &&
                  v.allocationBlockSize == 1024 && v.allocationStart == 16 &&
                  v.freeBlocks == 385 && v.nextFileNumber == 3,
              "volume information fields read back");
        check(v.files.size() == 2 && v.fileCount == 2,
              "both directory sectors are walked");
        const File* a = v.find("Chained");
        const File* b = v.find("Second sector");
        check(a && b, "files are found by name");
        if (a && b) {
            check(a->type == 0x54455854 && a->creator == 0x74747874,
                  "type/creator decode from the Finder info");
            check(!a->locked() && b->locked(), "flFlags lock bit");
            const std::vector<uint8_t> data = dataFork(img, v, *a);
            bool chain = data.size() == 2500;
            for (size_t i = 0; chain && i < 1024; i++) chain = data[i] == uint8_t(i);
            for (size_t i = 0; chain && i < 1024; i++) chain = data[1024 + i] == uint8_t(0x40 + i);
            for (size_t i = 0; chain && i < 452; i++) chain = data[2048 + i] == uint8_t(0x80 + i);
            check(chain, "a three-block out-of-order chain reads in order, cut at flLgLen");
            const std::vector<uint8_t> rsrc = resourceFork(img, v, *b);
            check(rsrc.size() == 4 && std::memcmp(rsrc.data(), "RSRC", 4) == 0,
                  "the resource fork follows its own chain");
            check(dataFork(img, v, *b).empty(), "an empty fork yields no bytes");
        }
        check(mapEntry(img.data(), img.size(), v, 5) == 3 &&
                  mapEntry(img.data(), img.size(), v, 9) == 1 &&
                  mapEntry(img.data(), img.size(), v, 400) == 0,
              "12-bit block map entries, bounded by drNmAlBlks");
    }

    {   // ── a chain that loops, a chain that leaves the volume, a bad sig ──
        // flLgLen 5000 on a 3-block chain: the reader must keep following
        // the map past the third block, which is where each fault sits.
        // A loop with a length it can never satisfy: only the step guard
        // ends the walk (a loop that still covers flLgLen reads as bytes,
        // which is what the guest would read too).
        std::vector<uint8_t> img = makeVolume();
        put32(img, 4 * 512 + 24, 500000);
        setMap(img, 9, 5);                             // 5 -> 3 -> 9 -> 5
        Volume v = parse(img);
        check(v.valid && dataFork(img, v, *v.find("Chained")).empty(),
              "a looping chain yields no bytes, and no hang");
        img = makeVolume();
        put32(img, 4 * 512 + 24, 5000);
        setMap(img, 9, 0xFFE);                         // past drNmAlBlks
        v = parse(img);
        check(v.valid && dataFork(img, v, *v.find("Chained")).empty(),
              "a chain leaving the volume yields no bytes");
        img = makeVolume();
        put32(img, 4 * 512 + 24, 4000);                // longer than the chain
        v = parse(img);
        check(v.valid && dataFork(img, v, *v.find("Chained")).empty(),
              "a chain that ends short of flLgLen yields no bytes");
        img = makeVolume();
        img[2 * 512] = 0x42; img[2 * 512 + 1] = 0x44;  // 'BD' — HFS
        check(!parse(img).valid, "an HFS signature is refused");
        img = makeVolume();
        img[4 * 512 + 50] = 250;                       // first entry: 301 bytes
        img[4 * 512 + 302] = 0x80;                     // second one runs past 512
        img[4 * 512 + 302 + 50] = 250;
        check(!parse(img).valid, "an entry straddling a sector is refused");
        img = makeVolume();
        put16(img, 2 * 512 + 12, 5);                   // drNmFls lies
        v = parse(img);
        check(v.valid && !v.error.empty(), "a wrong drNmFls is a note, not a refusal");
        check(!parse(nullptr, 0).valid, "an empty image is refused");
    }

    {   // ── the pinned System 1.1 floppy, when present ──
        const std::string path = testasset::find("disks35/System 1.1.dsk");
        if (path.empty()) {
            std::printf("note: disks35/ref/System 1.1.dsk absent — the real floppy is not read here\n");
        } else {
            testasset::report({ path });
            std::ifstream in(path, std::ios::binary);
            const std::vector<uint8_t> img((std::istreambuf_iterator<char>(in)), {});
            const Volume v = parse(img);
            check(v.valid && v.error.empty(), "System 1.1 floppy parses cleanly");
            check(v.name == "System Disk" && v.files.size() == 13 &&
                      v.allocationBlocks == 391 && v.allocationStart == 16,
                  "System Disk: 13 files, 391 x 1 KB blocks from sector 16");
            const File* sys = v.find("System");
            const File* welcome = v.find("Welcome!");
            check(sys && sys->rsrcLength == 127488 &&
                      resourceFork(img, v, *sys).size() == 127488,
                  "the System file's 124 KB resource fork reads whole");
            check(welcome && welcome->dataLength == 8906 &&
                      dataFork(img, v, *welcome).size() == 8906 &&
                      std::memcmp(dataFork(img, v, *welcome).data(), "Placeholder", 11) == 0,
                  "Welcome!'s data fork begins with its TeachText text");
            for (const File& f : v.files)
                std::printf("    %-20s data %7u rsrc %7u\n", f.name.c_str(),
                            unsigned(f.dataLength), unsigned(f.rsrcLength));
        }
    }

    std::printf("%s\n", gFails ? "FAILED" : "PASSED");
    return gFails ? 1 : 0;
}
