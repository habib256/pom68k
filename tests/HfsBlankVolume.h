// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// A freshly formatted, EMPTY classic HFS volume, written by the host: the
// blank target disk an installation gate hands to the guest. Nothing here is
// a file-system implementation — it is the fixed image a Mac's own
// initializer leaves behind, laid out from Inside Macintosh: Files, chapter
// 2 ("Data Organization on Volumes"): two zero boot blocks, the master
// directory block, the volume bitmap, then the allocation blocks holding the
// extents overflow B-tree (a header node alone — an empty tree) and the
// catalog B-tree (header node + one leaf carrying the root directory record
// and its thread), the alternate MDB in the second-to-last sector and one
// spare sector. Boot blocks stay zero: the volume is not bootable until a
// System is installed on it, which is precisely what the gate proves.
//
// The layout is what `machfs` (the Python HFS library the media tools use)
// reads back as an empty volume of the given name; ScsiDisk's bare-HFS
// façade wraps it with a partition map and an Apple driver at attach time,
// so the ROM sees an ordinary SCSI disk.

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace hfsblank {

namespace detail {
inline void be16(uint8_t* p, uint32_t v) { p[0] = uint8_t(v >> 8); p[1] = uint8_t(v); }
inline void be32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v);
}
// Str27/Str31: length byte then MacRoman bytes, zero-padded to the field.
inline void pstr(uint8_t* p, const std::string& s, size_t field) {
    std::memset(p, 0, field);
    const size_t n = s.size() < field - 1 ? s.size() : field - 1;
    p[0] = uint8_t(n);
    std::memcpy(p + 1, s.data(), n);
}
// B-tree node: 512 bytes, descriptor at 0, record offsets growing down from
// the end. `records` are appended in order; offsets[n] is the free-space
// start.
struct Node {
    uint8_t data[512] = {};
    int count = 0;
    int free = 14;
    void descriptor(uint32_t fLink, uint32_t bLink, uint8_t type, uint8_t height) {
        be32(data, fLink); be32(data + 4, bLink);
        data[8] = type; data[9] = height;
        be16(data + 12, 0);
    }
    void add(const uint8_t* rec, int len) {
        std::memcpy(data + free, rec, size_t(len));
        be16(data + 512 - 2 * (count + 1), uint32_t(free));
        free += len;
        count++;
        be16(data + 10, uint32_t(count));
        be16(data + 512 - 2 * (count + 1), uint32_t(free));      // free-space offset
    }
};
// Mac epoch seconds for a fixed, documented moment (2026-09-08 00:00 UTC):
// deterministic images hash identically across runs.
constexpr uint32_t kCreateDate = 3870201600u;
}  // namespace detail

struct Layout {
    uint32_t sectors = 0, allocBlockSize = 0, allocBlocks = 0, bitmapSectors = 0;
    uint32_t allocStart = 0, extentsBlocks = 0, catalogBlocks = 0;
};

// Builds the volume in memory. `bytes` must be a multiple of 512 and at
// least a few MB; the allocation block size is the smallest power of two
// keeping the block count under HFS's 65535 ceiling.
inline std::vector<uint8_t> build(uint64_t bytes, const std::string& name, Layout* out = nullptr) {
    using namespace detail;
    std::vector<uint8_t> img(bytes, 0);
    Layout L;
    L.sectors = uint32_t(bytes / 512);
    L.allocBlockSize = 512;
    while ((bytes - 8 * 512) / L.allocBlockSize > 65535) L.allocBlockSize *= 2;
    // Bitmap: one bit per allocation block, whole sectors.
    uint32_t blocks = uint32_t((bytes - 5 * 512) / L.allocBlockSize);
    L.bitmapSectors = (blocks + 4095) / 4096;
    L.allocStart = 3 + L.bitmapSectors;
    while (uint64_t(L.allocStart) * 512 + uint64_t(blocks) * L.allocBlockSize + 2 * 512 > bytes)
        blocks--;
    L.allocBlocks = blocks;
    // B-tree files: enough nodes for a fresh install's catalog to grow into
    // before the System extends them (it can — the extents tree is there).
    const uint32_t xBytes = 64 * 512, cBytes = 512 * 512;          // 32 KB, 256 KB
    L.extentsBlocks = (xBytes + L.allocBlockSize - 1) / L.allocBlockSize;
    L.catalogBlocks = (cBytes + L.allocBlockSize - 1) / L.allocBlockSize;
    const uint32_t xSize = L.extentsBlocks * L.allocBlockSize;
    const uint32_t cSize = L.catalogBlocks * L.allocBlockSize;
    const uint32_t used = L.extentsBlocks + L.catalogBlocks;

    // ── MDB ──────────────────────────────────────────────────────────────
    uint8_t mdb[512] = {};
    be16(mdb + 0, 0x4244);                      // drSigWord 'BD'
    be32(mdb + 2, kCreateDate);                 // drCrDate
    be32(mdb + 6, kCreateDate);                 // drLsMod
    be16(mdb + 10, 0x0100);                     // drAtrb: unmounted cleanly
    be16(mdb + 12, 0);                          // drNmFls
    be16(mdb + 14, 3);                          // drVBMSt
    be16(mdb + 16, used);                       // drAllocPtr
    be16(mdb + 18, blocks);                     // drNmAlBlks
    be32(mdb + 20, L.allocBlockSize);           // drAlBlkSiz
    be32(mdb + 24, L.allocBlockSize * 4);       // drClpSiz
    be16(mdb + 28, L.allocStart);               // drAlBlSt
    be32(mdb + 30, 16);                         // drNxtCNID
    be16(mdb + 34, blocks - used);              // drFreeBks
    pstr(mdb + 36, name, 28);                   // drVN
    be32(mdb + 64, 0);                          // drVolBkUp
    be16(mdb + 68, 0);                          // drVSeqNum
    be32(mdb + 70, 0);                          // drWrCnt
    be32(mdb + 74, xSize);                      // drXTClpSiz
    be32(mdb + 78, cSize);                      // drCTClpSiz
    be16(mdb + 82, 0);                          // drNmRtDirs
    be32(mdb + 84, 0);                          // drFilCnt
    be32(mdb + 88, 0);                          // drDirCnt
    // drFndrInfo[8] at 92: zero (no blessed folder)
    be16(mdb + 124, 0); be16(mdb + 126, 0); be16(mdb + 128, 0);   // cache sizes
    be32(mdb + 130, xSize);                     // drXTFlSize
    be16(mdb + 134, 0); be16(mdb + 136, L.extentsBlocks);         // drXTExtRec[0]
    be32(mdb + 146, cSize);                     // drCTFlSize
    be16(mdb + 150, L.extentsBlocks); be16(mdb + 152, L.catalogBlocks);  // drCTExtRec[0]
    std::memcpy(img.data() + 2 * 512, mdb, 512);
    std::memcpy(img.data() + (uint64_t(L.sectors) - 2) * 512, mdb, 512);   // alternate MDB

    // ── volume bitmap: the first `used` blocks are taken ─────────────────
    for (uint32_t b = 0; b < used; b++)
        img[3 * 512 + b / 8] |= uint8_t(0x80 >> (b % 8));

    // ── extents overflow tree: header node only (an empty tree) ──────────
    auto header = [&](uint32_t depth, uint32_t root, uint32_t nrecs, uint32_t first,
                      uint32_t last, uint32_t keyLen, uint32_t nodes, uint32_t usedNodes) {
        Node n;
        n.descriptor(0, 0, 1, 0);
        uint8_t hdr[106] = {};
        be16(hdr + 0, depth); be32(hdr + 2, root); be32(hdr + 6, nrecs);
        be32(hdr + 10, first); be32(hdr + 14, last); be16(hdr + 18, 512);
        be16(hdr + 20, keyLen); be32(hdr + 22, nodes); be32(hdr + 26, nodes - usedNodes);
        n.add(hdr, 106);
        uint8_t reserved[128] = {};
        n.add(reserved, 128);
        uint8_t map[256] = {};
        for (uint32_t i = 0; i < usedNodes; i++) map[i / 8] |= uint8_t(0x80 >> (i % 8));
        n.add(map, 256);
        return n;
    };
    const uint64_t xOff = uint64_t(L.allocStart) * 512;
    const Node xHdr = header(0, 0, 0, 0, 0, 7, xSize / 512, 1);
    std::memcpy(img.data() + xOff, xHdr.data, 512);

    // ── catalog tree: header + one leaf (root folder + its thread) ───────
    const uint64_t cOff = xOff + uint64_t(L.extentsBlocks) * L.allocBlockSize;
    const Node cHdr = header(1, 1, 2, 1, 1, 37, cSize / 512, 2);
    std::memcpy(img.data() + cOff, cHdr.data, 512);
    Node leaf;
    leaf.descriptor(0, 0, 0xFF, 1);
    {
        // Key: len, reserved, parent 1, name; padded to even. Then the
        // directory record for the root (CNID 2).
        uint8_t rec[128] = {};
        const uint8_t nameLen = uint8_t(name.size() < 31 ? name.size() : 31);
        rec[0] = uint8_t(6 + nameLen);          // ckrKeyLen
        be32(rec + 2, 1);                       // ckrParID
        rec[6] = nameLen;
        std::memcpy(rec + 7, name.data(), nameLen);
        int k = 1 + rec[0];
        if (k & 1) k++;
        uint8_t* d = rec + k;
        d[0] = 1;                               // cdrType: directory
        be16(d + 2, 0);                         // dirFlags
        be16(d + 4, 0);                         // dirVal
        be32(d + 6, 2);                         // dirDirID
        be32(d + 10, kCreateDate); be32(d + 14, kCreateDate); be32(d + 18, 0);
        leaf.add(rec, k + 70);
    }
    {
        // Thread: key (parent 2, empty name) → parent 1 + the root's name.
        uint8_t rec[128] = {};
        rec[0] = 6;                             // 1 + 4 + 1 (empty name)
        be32(rec + 2, 2);
        rec[6] = 0;
        int k = 1 + rec[0];
        if (k & 1) k++;                         // 7 → 8
        uint8_t* d = rec + k;
        d[0] = 3;                               // cdrType: directory thread
        be32(d + 10, 1);                        // thdParID
        pstr(d + 14, name, 32);                 // thdCName (Str31)
        leaf.add(rec, k + 46);
    }
    std::memcpy(img.data() + cOff + 512, leaf.data, 512);

    if (out) *out = L;
    return img;
}

inline bool writeFile(const std::string& path, const std::vector<uint8_t>& img) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(img.data(), 1, img.size(), f) == img.size();
    return std::fclose(f) == 0 && ok;
}

}  // namespace hfsblank
