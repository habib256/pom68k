// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// See HfsInject.h. Offsets cited are Inside Macintosh: Files, chapter 2.

#include "HfsInject.h"

#include <algorithm>
#include <cstring>

namespace hfsinject {

namespace {

constexpr uint32_t kNodeSize = 512;
constexpr uint8_t kIndexKeyLen = 0x25;          // catalog index keys are padded to 37 bytes
constexpr int kNodeIndex = 0, kNodeHeader = 1, kNodeLeaf = 0xFF;

inline uint16_t rd16(const uint8_t* p) { return uint16_t((p[0] << 8) | p[1]); }
inline uint32_t rd32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
inline void wr16(uint8_t* p, uint32_t v) { p[0] = uint8_t(v >> 8); p[1] = uint8_t(v); }
inline void wr32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v);
}

// The File Manager's catalog collation (RelString order for MacRoman),
// verbatim from machfs `_catalog_rec_sort`: case-folded, accents ordered
// after their base letter, the ligatures and quotes interleaved where the
// Mac sorts them.
constexpr uint8_t kOrder[256] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
    0x20, 0x22, 0x23, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36,
    0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46,
    0x47, 0x48, 0x58, 0x5a, 0x5e, 0x60, 0x67, 0x69, 0x6b, 0x6d, 0x73, 0x75, 0x77, 0x79, 0x7b, 0x7f,
    0x8d, 0x8f, 0x91, 0x93, 0x96, 0x98, 0x9f, 0xa1, 0xa3, 0xa5, 0xa8, 0xaa, 0xab, 0xac, 0xad, 0xae,
    0x54, 0x48, 0x58, 0x5a, 0x5e, 0x60, 0x67, 0x69, 0x6b, 0x6d, 0x73, 0x75, 0x77, 0x79, 0x7b, 0x7f,
    0x8d, 0x8f, 0x91, 0x93, 0x96, 0x98, 0x9f, 0xa1, 0xa3, 0xa5, 0xa8, 0xaf, 0xb0, 0xb1, 0xb2, 0xb3,
    0x4c, 0x50, 0x5c, 0x62, 0x7d, 0x81, 0x9a, 0x55, 0x4a, 0x56, 0x4c, 0x4e, 0x50, 0x5c, 0x62, 0x64,
    0x65, 0x66, 0x6f, 0x70, 0x71, 0x72, 0x7d, 0x89, 0x8a, 0x8b, 0x81, 0x83, 0x9c, 0x9d, 0x9e, 0x9a,
    0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0x95, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf, 0xc0, 0x52, 0x85,
    0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xcb, 0x57, 0x8c, 0xcc, 0x52, 0x85,
    0xcd, 0xce, 0xcf, 0xd0, 0xd1, 0xd2, 0xd3, 0x26, 0x27, 0xd4, 0x20, 0x4a, 0x4e, 0x83, 0x87, 0x87,
    0xd5, 0xd6, 0x24, 0x25, 0x2d, 0x2e, 0xd7, 0xd8, 0xa7, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
    0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
    0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,
};

// A catalog key as stored: [keyLen][resrv][parID×4][nameLen][name…].
std::vector<uint8_t> makeKey(uint32_t parent, const std::string& name) {
    std::vector<uint8_t> k(7 + name.size());
    k[0] = uint8_t(6 + name.size());
    k[1] = 0;
    wr32(&k[2], parent);
    k[6] = uint8_t(name.size());
    std::memcpy(&k[7], name.data(), name.size());
    return k;
}
// ≤ 0 / 0 / > 0 between two stored keys (each starting at its length byte).
int compareKeys(const uint8_t* a, const uint8_t* b) {
    const uint32_t pa = rd32(a + 2), pb = rd32(b + 2);
    if (pa != pb) return pa < pb ? -1 : 1;
    return compareNames(a + 7, a[6], b + 7, b[6]);
}
// Where a record's data begins: after the key, on an even boundary.
inline size_t dataOffset(const uint8_t* rec) { return (size_t(rec[0]) + 2) & ~size_t(1); }

// A record as it sits in a leaf: key + data.
std::vector<uint8_t> makeRecord(const std::vector<uint8_t>& key, const uint8_t* data, size_t len) {
    std::vector<uint8_t> r(key);
    if (r.size() & 1) r.push_back(0);
    r.insert(r.end(), data, data + len);
    return r;
}
// An index record: the child's first key padded to the maximum, then the
// child's node number (machfs `_make_index_record`).
std::vector<uint8_t> makeIndexRecord(const std::vector<uint8_t>& firstRecord, uint32_t child) {
    std::vector<uint8_t> r(kIndexKeyLen + 1 + 4, 0);
    const size_t keyBytes = size_t(firstRecord[0]) + 1;
    std::memcpy(r.data(), firstRecord.data(), std::min(keyBytes, r.size() - 4));
    r[0] = kIndexKeyLen;
    wr32(&r[kIndexKeyLen + 1], child);
    return r;
}

} // namespace

int compareNames(const uint8_t* a, size_t alen, const uint8_t* b, size_t blen) {
    const size_t n = std::min(alen, blen);
    for (size_t i = 0; i < n; i++) {
        const uint8_t ca = kOrder[a[i]], cb = kOrder[b[i]];
        if (ca != cb) return ca < cb ? -1 : 1;
    }
    if (alen == blen) return 0;
    return alen < blen ? -1 : 1;
}

// The US name and the French one. The French name was first guessed as
// « Éléments de démarrage » (2026-09-14) and corrected the same day from
// a real System 7.5.5 volume (GISTPERSO), whose blessed « Dossier
// Système » holds « Ouverture au démarrage » — CNID 4769 there, next to
// « Ouverture à l'extinction » for Shutdown Items. A folder name is a
// fact about a shipped System, not a translation exercise.
const std::vector<std::string>& startupItemsNames() {
    static const std::vector<std::string> names = {
        "Startup Items",
        "Ouverture au d\x8Emarrage",                 // « Ouverture au démarrage »
    };
    return names;
}

// ── Block sources ───────────────────────────────────────────────────────

bool ScsiDiskIo::read(uint32_t lba, uint8_t* out) {
    const std::vector<uint8_t>& img = disk.image();
    const uint64_t off = uint64_t(lba) * 512;
    if (off + 512 > img.size()) return false;
    std::memcpy(out, img.data() + off, 512);
    return true;
}
bool ScsiDiskIo::write(uint32_t lba, const uint8_t* in) {
    if (uint64_t(lba) * 512 + 512 > disk.image().size()) return false;
    disk.hostWrite(lba, in, 1);
    return true;
}
bool MemoryIo::read(uint32_t lba, uint8_t* out) {
    const uint64_t off = uint64_t(lba) * 512;
    if (off + 512 > image.size()) return false;
    std::memcpy(out, image.data() + off, 512);
    return true;
}
bool MemoryIo::write(uint32_t lba, const uint8_t* in) {
    const uint64_t off = uint64_t(lba) * 512;
    if (off + 512 > image.size()) return false;
    std::memcpy(image.data() + off, in, 512);
    return true;
}

// ── MacBinary ───────────────────────────────────────────────────────────
// 128-byte header: version 0 at 0, Str63 name at 1, type/creator at 65/69,
// Finder flags high byte at 73, location at 75, folder at 79, fork
// lengths at 83/87, dates at 91/95, Finder flags low byte at 101 (II).
// Forks follow, each padded to 128.
bool decodeMacBinary(const std::vector<uint8_t>& raw, MacBinary& out, std::string& err) {
    if (raw.size() < 128) { err = "shorter than a MacBinary header"; return false; }
    const uint8_t* h = raw.data();
    const size_t nameLen = h[1];
    if (h[0] != 0 || nameLen < 1 || nameLen > 63 || h[74] != 0) {
        err = "not a MacBinary header";
        return false;
    }
    const uint32_t dlen = rd32(h + 83), rlen = rd32(h + 87);
    const uint64_t dpad = (uint64_t(dlen) + 127) & ~uint64_t(127);
    if (128 + dpad + rlen > raw.size() || dlen > 0x7FFFFF00u) {
        err = "MacBinary fork lengths exceed the file";
        return false;
    }
    out.name.assign(reinterpret_cast<const char*>(h + 2), nameLen);
    out.type.assign(reinterpret_cast<const char*>(h + 65), 4);
    out.creator.assign(reinterpret_cast<const char*>(h + 69), 4);
    out.finderFlags = uint16_t((h[73] << 8) | h[101]);
    out.x = int16_t(rd16(h + 77));
    out.y = int16_t(rd16(h + 75));
    out.crDate = rd32(h + 91);
    out.mdDate = rd32(h + 95);
    out.data.assign(raw.begin() + 128, raw.begin() + 128 + dlen);
    out.rsrc.assign(raw.begin() + std::ptrdiff_t(128 + dpad),
                    raw.begin() + std::ptrdiff_t(128 + dpad + rlen));
    return true;
}

// ── Finding the volume ──────────────────────────────────────────────────

bool findHfsVolume(BlockIo& io, uint32_t& startLba, uint32_t& lengthBlocks, std::string& err) {
    uint8_t b[512];
    if (!io.read(0, b)) { err = "cannot read block 0"; return false; }
    if (b[0] == 'E' && b[1] == 'R') {
        // Driver descriptor map, then the partition map from block 1: each
        // entry names its own count of entries (pmMapBlkCnt at +4).
        uint8_t pm[512];
        if (!io.read(1, pm)) { err = "cannot read the partition map"; return false; }
        if (pm[0] != 'P' || pm[1] != 'M') { err = "no partition map after the DDM"; return false; }
        const uint32_t entries = rd32(pm + 4);
        for (uint32_t i = 0; i < entries && i < 64; i++) {
            if (i && !io.read(1 + i, pm)) { err = "partition map truncated"; return false; }
            if (std::strncmp(reinterpret_cast<const char*>(pm + 48), "Apple_HFS", 9) == 0) {
                startLba = rd32(pm + 8);
                lengthBlocks = rd32(pm + 12);
                return true;
            }
        }
        err = "no Apple_HFS partition";
        return false;
    }
    if (!io.read(2, b)) { err = "cannot read block 2"; return false; }
    if (b[0] == 'B' && b[1] == 'D') {
        startLba = 0;
        lengthBlocks = io.blocks();
        return true;
    }
    err = "neither a partitioned disk nor a bare HFS volume";
    return false;
}

// ── Nodes ───────────────────────────────────────────────────────────────

struct Volume::Node {
    uint32_t fLink = 0, bLink = 0;
    uint8_t type = 0, height = 0;
    std::vector<std::vector<uint8_t>> recs;

    bool parse(const uint8_t* d) {
        fLink = rd32(d); bLink = rd32(d + 4); type = d[8]; height = d[9];
        const uint32_t n = rd16(d + 10);
        if (n > 200) return false;
        recs.clear();
        for (uint32_t i = 0; i < n; i++) {
            const uint32_t a = rd16(d + kNodeSize - 2 * (i + 1));
            const uint32_t b = rd16(d + kNodeSize - 2 * (i + 2));
            if (a < 14 || b < a || b > kNodeSize - 2 * (n + 1)) return false;
            recs.emplace_back(d + a, d + b);
        }
        return true;
    }
    size_t bytes() const {
        size_t t = 14 + 2 * (recs.size() + 1);
        for (const auto& r : recs) t += r.size();
        return t;
    }
    bool fits() const { return bytes() <= kNodeSize; }
    void serialize(uint8_t* d) const {
        std::memset(d, 0, kNodeSize);
        wr32(d, fLink); wr32(d + 4, bLink); d[8] = type; d[9] = height;
        wr16(d + 10, uint32_t(recs.size()));
        uint32_t off = 14;
        for (size_t i = 0; i < recs.size(); i++) {
            std::memcpy(d + off, recs[i].data(), recs[i].size());
            wr16(d + kNodeSize - 2 * (i + 1), off);
            off += uint32_t(recs[i].size());
        }
        wr16(d + kNodeSize - 2 * (recs.size() + 1), off);
    }
};

// The nodes visited from the root to a leaf, with the record index taken
// at each level; `pos` on the leaf is the insertion point.
struct Volume::Path {
    std::vector<uint32_t> nodes;       // root first
    std::vector<int> index;            // record followed at each index node
    uint32_t leaf = 0;
    int pos = 0;
};

// ── Volume ──────────────────────────────────────────────────────────────

Volume::Volume(BlockIo& io, uint32_t startLba, uint32_t lengthBlocks)
    : io_(io), start_(startLba), length_(lengthBlocks) {}

bool Volume::readBlock(uint32_t lba, uint8_t* out) {
    auto it = pending_.find(lba);
    if (it != pending_.end()) { std::memcpy(out, it->second.data(), 512); return true; }
    return io_.read(lba, out);
}
void Volume::stageBlock(uint32_t lba, const uint8_t* in) {
    pending_[lba].assign(in, in + 512);
}

bool Volume::open(std::string& err) {
    if (!readBlock(start_ + 2, mdb_)) { err = "cannot read the MDB"; return false; }
    if (mdb_[0] != 'B' || mdb_[1] != 'D') { err = "no HFS signature in the MDB"; return false; }
    vbmSt_ = rd16(mdb_ + 14);
    nmAlBlks_ = rd16(mdb_ + 18);
    alBlkSiz_ = rd32(mdb_ + 20);
    alBlSt_ = rd16(mdb_ + 28);
    if (!alBlkSiz_ || (alBlkSiz_ % 512) || !nmAlBlks_) { err = "MDB geometry is not sane"; return false; }
    for (int i = 0; i < 6; i++) ctExt_[i] = rd16(mdb_ + 150 + 2 * i);
    // The volume bitmap, one bit per allocation block, whole sectors.
    const uint32_t sectors = (nmAlBlks_ + 4095) / 4096;
    bitmap_.assign(size_t(sectors) * 512, 0);
    for (uint32_t s = 0; s < sectors; s++)
        if (!readBlock(start_ + vbmSt_ + s, &bitmap_[size_t(s) * 512])) {
            err = "cannot read the volume bitmap";
            return false;
        }
    bitmapDirty_ = false;
    return readHeader(err);
}

std::string Volume::name() const {
    return std::string(reinterpret_cast<const char*>(mdb_ + 37), mdb_[36] < 28 ? mdb_[36] : 27);
}
uint32_t Volume::blessedFolder() const { return rd32(mdb_ + 92); }
uint32_t Volume::nextCnid() const { return rd32(mdb_ + 30); }
uint32_t Volume::freeBlocks() const { return rd16(mdb_ + 34); }

uint32_t Volume::allocLba(uint32_t allocBlock) const {
    return start_ + alBlSt_ + allocBlock * (alBlkSiz_ / 512);
}

// Node n of the catalog file lives at file offset n×512, somewhere in the
// three extents the MDB holds. A fourth extent would be in the extents
// overflow tree, which this code does not read: refused, not guessed.
bool Volume::catalogNodeLba(uint32_t node, uint32_t& lba, std::string& err) {
    uint32_t fileBlock = node / (alBlkSiz_ / 512);
    const uint32_t within = node % (alBlkSiz_ / 512);
    for (int e = 0; e < 3; e++) {
        const uint32_t startAb = ctExt_[2 * e], count = ctExt_[2 * e + 1];
        if (!count) break;
        if (fileBlock < count) {
            lba = allocLba(startAb + fileBlock) + within;
            return true;
        }
        fileBlock -= count;
    }
    err = "catalog node " + std::to_string(node) + " lies beyond the MDB's three extents";
    return false;
}

bool Volume::readNode(uint32_t node, Node& out, std::string& err) {
    uint32_t lba;
    uint8_t d[512];
    if (!catalogNodeLba(node, lba, err)) return false;
    if (!readBlock(lba, d)) { err = "cannot read catalog node " + std::to_string(node); return false; }
    if (!out.parse(d)) { err = "catalog node " + std::to_string(node) + " is malformed"; return false; }
    return true;
}
bool Volume::writeNode(uint32_t node, const Node& n, std::string& err) {
    uint32_t lba;
    uint8_t d[512];
    if (!catalogNodeLba(node, lba, err)) return false;
    if (!n.fits()) { err = "catalog node " + std::to_string(node) + " overflows"; return false; }
    n.serialize(d);
    stageBlock(lba, d);
    return true;
}

bool Volume::readHeader(std::string& err) {
    Node h;
    if (!readNode(0, h, err)) return false;
    if (h.type != kNodeHeader || h.recs.size() != 3 || h.recs[0].size() < 30) {
        err = "catalog header node is malformed";
        return false;
    }
    const uint8_t* r = h.recs[0].data();
    bthDepth_ = rd16(r); bthRoot_ = rd32(r + 2); bthNRecs_ = rd32(r + 6);
    bthFNode_ = rd32(r + 10); bthLNode_ = rd32(r + 14);
    if (rd16(r + 18) != kNodeSize) { err = "catalog node size is not 512"; return false; }
    if (rd16(r + 20) != kIndexKeyLen) { err = "catalog key length is not 37"; return false; }
    bthNNodes_ = rd32(r + 22); bthFree_ = rd32(r + 26);
    return true;
}
bool Volume::writeHeader(std::string& err) {
    Node h;
    if (!readNode(0, h, err)) return false;
    uint8_t* r = h.recs[0].data();
    wr16(r, bthDepth_); wr32(r + 2, bthRoot_); wr32(r + 6, bthNRecs_);
    wr32(r + 10, bthFNode_); wr32(r + 14, bthLNode_);
    wr32(r + 22, bthNNodes_); wr32(r + 26, bthFree_);
    return writeNode(0, h, err);
}

// A free node from the map: the header node's 256-byte map record covers
// nodes 0–2047, map nodes chained from the header's forward link cover
// 3952 more each.
bool Volume::allocNode(uint32_t& node, std::string& err) {
    uint32_t mapNode = 0, base = 0;
    for (int hop = 0; hop < 64; hop++) {
        Node m;
        if (!readNode(mapNode, m, err)) return false;
        const int mapRec = mapNode == 0 ? 2 : 0;
        if (int(m.recs.size()) <= mapRec) { err = "catalog map record missing"; return false; }
        std::vector<uint8_t>& bits = m.recs[size_t(mapRec)];
        for (size_t i = 0; i < bits.size() * 8; i++) {
            const uint32_t n = base + uint32_t(i);
            if (n >= bthNNodes_) { err = "catalog file is full (no free node)"; return false; }
            if (bits[i / 8] & (0x80 >> (i % 8))) continue;
            bits[i / 8] |= uint8_t(0x80 >> (i % 8));
            if (!writeNode(mapNode, m, err)) return false;
            if (bthFree_) bthFree_--;
            node = n;
            return true;
        }
        base += uint32_t(bits.size() * 8);
        if (!m.fLink) break;
        mapNode = m.fLink;
    }
    err = "catalog file is full (no free node)";
    return false;
}

// Descend from the root: at an index node follow the last record whose
// key is ≤ the search key (the first when none is); at the leaf, `pos` is
// the first record with a greater key and `exact` says whether the one
// before it matches.
bool Volume::search(const std::vector<uint8_t>& key, Path& path, bool& exact, std::string& err) {
    path = Path{};
    exact = false;
    uint32_t n = bthRoot_;
    for (int depth = 0; depth < 16; depth++) {
        Node node;
        if (!readNode(n, node, err)) return false;
        if (node.type == kNodeLeaf) {
            path.leaf = n;
            int pos = 0;
            for (; pos < int(node.recs.size()); pos++) {
                const int c = compareKeys(node.recs[size_t(pos)].data(), key.data());
                if (c == 0) exact = true;
                if (c > 0) break;
            }
            path.pos = pos;
            return true;
        }
        if (node.type != kNodeIndex || node.recs.empty()) {
            err = "catalog node " + std::to_string(n) + " is neither index nor leaf";
            return false;
        }
        int take = 0;
        for (int i = 0; i < int(node.recs.size()); i++)
            if (compareKeys(node.recs[size_t(i)].data(), key.data()) <= 0) take = i;
        path.nodes.push_back(n);
        path.index.push_back(take);
        const std::vector<uint8_t>& r = node.recs[size_t(take)];
        n = rd32(r.data() + r[0] + 1);
    }
    err = "catalog deeper than 16 levels";
    return false;
}

// Insert `record` at the leaf `path` found; split upward as needed.
bool Volume::insert(Path& path, std::vector<uint8_t> record, std::string& err) {
    if (!insertInto(path.leaf, int(path.nodes.size()), path, std::move(record), err)) return false;
    bthNRecs_++;
    return writeHeader(err);
}

// `level` indexes path.nodes for the parent of `nodeNum` (level-1); the
// position in `nodeNum` is path.pos for the leaf and path.index[level]+1
// for an index node receiving the key of a new right sibling.
bool Volume::insertInto(uint32_t nodeNum, int level, Path& path, std::vector<uint8_t> record,
                        std::string& err) {
    Node node;
    if (!readNode(nodeNum, node, err)) return false;
    const int pos = (node.type == kNodeLeaf) ? path.pos : path.index[size_t(level)] + 1;
    if (pos < 0 || pos > int(node.recs.size())) { err = "insertion position out of range"; return false; }
    node.recs.insert(node.recs.begin() + pos, std::move(record));
    if (node.fits()) return writeNode(nodeNum, node, err);

    // Split: the upper half moves to a new right sibling.
    uint32_t newNum;
    if (!allocNode(newNum, err)) return false;
    Node right;
    right.type = node.type;
    right.height = node.height;
    const size_t half = node.recs.size() / 2;
    right.recs.assign(node.recs.begin() + std::ptrdiff_t(half), node.recs.end());
    node.recs.resize(half);
    right.fLink = node.fLink;
    right.bLink = nodeNum;
    node.fLink = newNum;
    if (right.fLink) {
        Node next;
        if (!readNode(right.fLink, next, err)) return false;
        next.bLink = newNum;
        if (!writeNode(right.fLink, next, err)) return false;
    }
    if (node.type == kNodeLeaf && bthLNode_ == nodeNum) bthLNode_ = newNum;
    if (!writeNode(nodeNum, node, err) || !writeNode(newNum, right, err)) return false;

    const std::vector<uint8_t> up = makeIndexRecord(right.recs[0], newNum);
    if (level == 0) {
        // The root split: a new root above both halves.
        uint32_t rootNum;
        if (!allocNode(rootNum, err)) return false;
        Node root;
        root.type = kNodeIndex;
        root.height = uint8_t(node.height + 1);
        root.recs.push_back(makeIndexRecord(node.recs[0], nodeNum));
        root.recs.push_back(up);
        bthRoot_ = rootNum;
        bthDepth_++;
        return writeNode(rootNum, root, err);
    }
    return insertInto(path.nodes[size_t(level - 1)], level - 1, path, up, err);
}

// First-fit run of `count` allocation blocks from drAllocPtr, wrapping.
bool Volume::allocBlocks(uint32_t count, uint32_t& first, std::string& err) {
    if (!count) { first = 0; return true; }
    auto used = [&](uint32_t b) { return (bitmap_[b / 8] & (0x80 >> (b % 8))) != 0; };
    const uint32_t from = rd16(mdb_ + 16) < nmAlBlks_ ? rd16(mdb_ + 16) : 0;
    for (int pass = 0; pass < 2; pass++) {
        const uint32_t lo = pass == 0 ? from : 0, hi = pass == 0 ? nmAlBlks_ : from;
        uint32_t run = 0;
        for (uint32_t b = lo; b < hi; b++) {
            run = used(b) ? 0 : run + 1;
            if (run == count) {
                first = b + 1 - count;
                for (uint32_t i = 0; i < count; i++) bitmap_[(first + i) / 8] |= uint8_t(0x80 >> ((first + i) % 8));
                bitmapDirty_ = true;
                wr16(mdb_ + 34, rd16(mdb_ + 34) - count);          // drFreeBks
                wr16(mdb_ + 16, first + count < nmAlBlks_ ? first + count : 0);   // drAllocPtr
                return true;
            }
        }
    }
    err = "no contiguous run of " + std::to_string(count) + " free allocation blocks";
    return false;
}

bool Volume::writeFork(const std::vector<uint8_t>& bytes, uint32_t& startBlock, uint32_t& physLen,
                       std::string& err) {
    if (bytes.empty()) { startBlock = 0; physLen = 0; return true; }
    const uint32_t count = uint32_t((uint64_t(bytes.size()) + alBlkSiz_ - 1) / alBlkSiz_);
    if (count > rd16(mdb_ + 34)) { err = "not enough free space for the fork"; return false; }
    if (!allocBlocks(count, startBlock, err)) return false;
    physLen = count * alBlkSiz_;
    const uint32_t lba0 = allocLba(startBlock);
    uint8_t sector[512];
    for (uint32_t s = 0; s < physLen / 512; s++) {
        const uint64_t off = uint64_t(s) * 512;
        std::memset(sector, 0, 512);
        if (off < bytes.size())
            std::memcpy(sector, bytes.data() + off, std::min<size_t>(512, bytes.size() - size_t(off)));
        stageBlock(lba0 + s, sector);
    }
    return true;
}

bool Volume::readFork(const uint8_t* extents, uint32_t logicalLen, std::vector<uint8_t>& out,
                      std::string& err) {
    out.clear();
    uint8_t sector[512];
    for (int e = 0; e < 3 && out.size() < logicalLen; e++) {
        const uint32_t startAb = rd16(extents + 4 * e), count = rd16(extents + 4 * e + 2);
        if (!count) break;
        for (uint32_t s = 0; s < count * (alBlkSiz_ / 512) && out.size() < logicalLen; s++) {
            if (!readBlock(allocLba(startAb) + s, sector)) { err = "cannot read a fork block"; return false; }
            const size_t take = std::min<size_t>(512, logicalLen - out.size());
            out.insert(out.end(), sector, sector + take);
        }
    }
    if (out.size() != logicalLen) { err = "fork extends beyond its three extents"; return false; }
    return true;
}

// The leaf record for `key`, exactly.
bool Volume::leafRecord(const std::vector<uint8_t>& key, std::vector<uint8_t>& record,
                        uint32_t& node, int& index, std::string& err) {
    Path p;
    bool exact;
    if (!search(key, p, exact, err)) return false;
    if (!exact) return false;
    Node leaf;
    if (!readNode(p.leaf, leaf, err)) return false;
    node = p.leaf;
    index = p.pos - 1;
    record = leaf.recs[size_t(index)];
    return true;
}

uint32_t Volume::findFolder(uint32_t parent, const std::string& name) {
    std::vector<uint8_t> rec;
    uint32_t node; int idx; std::string err;
    if (!leafRecord(makeKey(parent, name), rec, node, idx, err)) return 0;
    const uint8_t* d = rec.data() + dataOffset(rec.data());
    return d[0] == 1 ? rd32(d + 6) : 0;         // cdrType 1: dirDirID at +6
}

bool Volume::fileExists(uint32_t parent, const std::string& name) {
    std::vector<uint8_t> rec;
    uint32_t node; int idx; std::string err;
    if (!leafRecord(makeKey(parent, name), rec, node, idx, err)) return false;
    return rec[dataOffset(rec.data())] == 2;
}

bool Volume::readFile(uint32_t parent, const std::string& name, MacBinary& out, std::string& err) {
    std::vector<uint8_t> rec;
    uint32_t node; int idx;
    if (!leafRecord(makeKey(parent, name), rec, node, idx, err)) {
        if (err.empty()) err = "no such file";
        return false;
    }
    const uint8_t* d = rec.data() + dataOffset(rec.data());
    if (d[0] != 2) { err = "not a file record"; return false; }
    out.name = name;
    out.type.assign(reinterpret_cast<const char*>(d + 4), 4);
    out.creator.assign(reinterpret_cast<const char*>(d + 8), 4);
    out.finderFlags = rd16(d + 12);
    out.crDate = rd32(d + 44);
    out.mdDate = rd32(d + 48);
    return readFork(d + 74, rd32(d + 26), out.data, err) &&
           readFork(d + 86, rd32(d + 36), out.rsrc, err);
}

// Valence and modification date of a folder record, found through its
// thread (key: the folder's own CNID, empty name).
bool Volume::bumpFolder(uint32_t folder, uint32_t macNow, int delta, std::string& err) {
    std::vector<uint8_t> thread;
    uint32_t node; int idx;
    if (!leafRecord(makeKey(folder, ""), thread, node, idx, err)) {
        if (err.empty()) err = "folder " + std::to_string(folder) + " has no thread record";
        return false;
    }
    const uint8_t* t = thread.data() + dataOffset(thread.data());
    if (t[0] != 3) { err = "thread record of the wrong kind"; return false; }
    const uint32_t parent = rd32(t + 10);
    const std::string name(reinterpret_cast<const char*>(t + 15), t[14]);
    std::vector<uint8_t> rec;
    if (!leafRecord(makeKey(parent, name), rec, node, idx, err)) {
        if (err.empty()) err = "folder record not found for its thread";
        return false;
    }
    uint8_t* d = rec.data() + dataOffset(rec.data());
    if (d[0] != 1) { err = "folder record of the wrong kind"; return false; }
    wr16(d + 4, uint32_t(int(rd16(d + 4)) + delta));            // dirVal
    wr32(d + 14, macNow);                                        // dirMdDat
    Node leaf;
    if (!readNode(node, leaf, err)) return false;
    leaf.recs[size_t(idx)] = rec;
    return writeNode(node, leaf, err);
}

bool Volume::createFolder(uint32_t parent, const std::string& name, uint32_t macNow,
                          uint32_t& cnid, std::string& err) {
    if (name.empty() || name.size() > 31) { err = "folder name length"; return false; }
    Path p;
    bool exact;
    if (!search(makeKey(parent, name), p, exact, err)) return false;
    if (exact) { err = "a catalog entry of that name exists"; return false; }
    cnid = rd32(mdb_ + 30);
    // Folder record: type 1, flags, valence, dirID, three dates, two 16-byte
    // Finder blocks, 16 reserved — 70 bytes.
    uint8_t d[70] = {};
    d[0] = 1;
    wr32(d + 6, cnid);
    wr32(d + 10, macNow); wr32(d + 14, macNow);
    if (!insert(p, makeRecord(makeKey(parent, name), d, sizeof d), err)) return false;
    // Thread record: type 3, 8 reserved, parent, Str31 name — 46 bytes.
    uint8_t t[46] = {};
    t[0] = 3;
    wr32(t + 10, parent);
    t[14] = uint8_t(name.size());
    std::memcpy(t + 15, name.data(), name.size());
    if (!search(makeKey(cnid, ""), p, exact, err)) return false;
    if (!insert(p, makeRecord(makeKey(cnid, ""), t, sizeof t), err)) return false;
    wr32(mdb_ + 30, cnid + 1);                                   // drNxtCNID
    wr32(mdb_ + 88, rd32(mdb_ + 88) + 1);                        // drDirCnt
    if (parent == 2) wr16(mdb_ + 82, rd16(mdb_ + 82) + 1);       // drNmRtDirs
    wr32(mdb_ + 6, macNow);                                      // drLsMod
    return bumpFolder(parent, macNow, +1, err);
}

bool Volume::addFile(uint32_t parent, const MacBinary& file, uint32_t macNow,
                     uint32_t& cnid, std::string& err) {
    if (file.name.empty() || file.name.size() > 31) { err = "file name length"; return false; }
    Path p;
    bool exact;
    if (!search(makeKey(parent, file.name), p, exact, err)) return false;
    if (exact) { err = "a catalog entry of that name exists"; return false; }
    uint32_t dStart, dPhys, rStart, rPhys;
    if (!writeFork(file.data, dStart, dPhys, err)) return false;
    if (!writeFork(file.rsrc, rStart, rPhys, err)) return false;
    cnid = rd32(mdb_ + 30);
    // File record (102 bytes): type 2, flags, version, Finder info (type,
    // creator, flags, location, folder), CNID, data fork (start, logical,
    // physical), resource fork likewise, three dates, extended Finder
    // info, clump, both extent records, 4 reserved.
    uint8_t d[102] = {};
    d[0] = 2;
    std::memcpy(d + 4, file.type.data(), 4);
    std::memcpy(d + 8, file.creator.data(), 4);
    wr16(d + 12, file.finderFlags);
    wr16(d + 14, uint16_t(file.y)); wr16(d + 16, uint16_t(file.x));
    wr32(d + 20, cnid);
    wr16(d + 24, dStart); wr32(d + 26, uint32_t(file.data.size())); wr32(d + 30, dPhys);
    wr16(d + 34, rStart); wr32(d + 36, uint32_t(file.rsrc.size())); wr32(d + 40, rPhys);
    const uint32_t cr = file.crDate ? file.crDate : macNow;
    wr32(d + 44, cr); wr32(d + 48, file.mdDate ? file.mdDate : cr); wr32(d + 52, 0);
    if (!file.data.empty()) { wr16(d + 74, dStart); wr16(d + 76, dPhys / alBlkSiz_); }
    if (!file.rsrc.empty()) { wr16(d + 86, rStart); wr16(d + 88, rPhys / alBlkSiz_); }
    // The record's own view of the file starts at d+4 in the layout above
    // because the key/data boundary is even; the fields are at the offsets
    // Inside Macintosh gives from cdrType: filFlags +2, filTyp +3,
    // filUsrWds +4, filFlNum +20, filStBlk +24, filLgLen +26, filPyLen +30,
    // filRStBlk +34, filRLgLen +36, filRPyLen +40, filCrDat +44,
    // filMdDat +48, filBkDat +52, filFndrInfo +56, filClpSize +72,
    // filExtRec +74, filRExtRec +86.
    if (!insert(p, makeRecord(makeKey(parent, file.name), d, sizeof d), err)) return false;
    wr32(mdb_ + 30, cnid + 1);                                   // drNxtCNID
    wr32(mdb_ + 84, rd32(mdb_ + 84) + 1);                        // drFilCnt
    if (parent == 2) wr16(mdb_ + 12, rd16(mdb_ + 12) + 1);       // drNmFls
    wr32(mdb_ + 6, macNow);                                      // drLsMod
    wr32(mdb_ + 70, rd32(mdb_ + 70) + 1);                        // drWrCnt
    return bumpFolder(parent, macNow, +1, err);
}

void Volume::bless(uint32_t folder) { wr32(mdb_ + 92, folder); }

bool Volume::checkCatalog(std::string& err) {
    uint32_t n = bthFNode_, count = 0, last = 0;
    std::vector<uint8_t> prev;
    for (int hop = 0; hop < 1000000 && n; hop++) {
        Node leaf;
        if (!readNode(n, leaf, err)) return false;
        if (leaf.type != kNodeLeaf) { err = "leaf chain reaches a non-leaf"; return false; }
        for (const auto& r : leaf.recs) {
            if (!prev.empty() && compareKeys(prev.data(), r.data()) >= 0) {
                err = "leaf keys not strictly increasing at node " + std::to_string(n);
                return false;
            }
            prev = r;
            count++;
        }
        last = n;
        n = leaf.fLink;
    }
    if (count != bthNRecs_) { err = "leaf records " + std::to_string(count) + " != header " + std::to_string(bthNRecs_); return false; }
    if (last != bthLNode_) { err = "last leaf differs from the header's"; return false; }
    return true;
}

bool Volume::commit(std::string& err) {
    if (bitmapDirty_) {
        for (uint32_t s = 0; s < bitmap_.size() / 512; s++)
            stageBlock(start_ + vbmSt_ + s, &bitmap_[size_t(s) * 512]);
        bitmapDirty_ = false;
    }
    stageBlock(start_ + 2, mdb_);
    if (length_ >= 4) stageBlock(start_ + length_ - 2, mdb_);   // alternate MDB
    for (const auto& [lba, bytes] : pending_)
        if (!io_.write(lba, bytes.data())) { err = "write failed at block " + std::to_string(lba); return false; }
    pending_.clear();
    return true;
}

// ── The installer ───────────────────────────────────────────────────────

Outcome installStartupItem(BlockIo& io, const MacBinary& app, uint32_t macNow) {
    Outcome o;
    std::string err;
    uint32_t start, length;
    if (!findHfsVolume(io, start, length, err)) {
        o.kind = Outcome::NoVolume;
        o.message = "pas de volume HFS : " + err;
        return o;
    }
    Volume v(io, start, length);
    if (!v.open(err)) { o.kind = Outcome::NoVolume; o.message = "volume illisible : " + err; return o; }
    const uint32_t system = v.blessedFolder();
    if (!system) { o.kind = Outcome::NoFolder; o.message = "« " + v.name() + " » : aucun Dossier Système béni"; return o; }
    uint32_t items = 0;
    for (const std::string& n : startupItemsNames())
        if ((items = v.findFolder(system, n)) != 0) break;
    if (!items) {
        o.kind = Outcome::NoFolder;
        o.message = "« " + v.name() + " » : pas de dossier Startup Items dans le Dossier Système";
        return o;
    }
    if (v.fileExists(items, app.name)) {
        o.kind = Outcome::AlreadyPresent;
        o.message = "« " + v.name() + " » : " + app.name + " est déjà dans Startup Items";
        return o;
    }
    uint32_t cnid;
    if (!v.addFile(items, app, macNow, cnid, err) || !v.commit(err)) {
        o.kind = Outcome::Refused;
        o.message = "« " + v.name() + " » : " + app.name + " non installé — " + err;
        return o;
    }
    o.kind = Outcome::Installed;
    o.message = "« " + v.name() + " » : " + app.name + " installé dans Startup Items (CNID " +
                std::to_string(cnid) + ")";
    return o;
}

} // namespace hfsinject
