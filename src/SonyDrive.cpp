// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Sense/command tables and GCR encoder ported from MAME (floppy.cpp
// mac_floppy_device, flopimg.cpp build_mac_track_gcr, ap_dsk35.cpp),
// MFM HD track layout after IBM System 34 / Apple SuperDrive (mfd75w,
// floppy.cpp:3452-3477). Cross-checked against pce gcr-mac.c — see DEV.md.

#include "SonyDrive.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace {

// 6&2 GCR translation table (MAME flopimg.cpp gcr6fw_tb, verbatim)
const uint8_t kGcr6[0x40] = {
    0x96, 0x97, 0x9a, 0x9b, 0x9d, 0x9e, 0x9f, 0xa6,
    0xa7, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb2, 0xb3,
    0xb4, 0xb5, 0xb6, 0xb7, 0xb9, 0xba, 0xbb, 0xbc,
    0xbd, 0xbe, 0xbf, 0xcb, 0xcd, 0xce, 0xcf, 0xd3,
    0xd6, 0xd7, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde,
    0xdf, 0xe5, 0xe6, 0xe7, 0xe9, 0xea, 0xeb, 0xec,
    0xed, 0xee, 0xef, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6,
    0xf7, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,
};

constexpr uint16_t kMark = 0x100;
// GCR self-sync tag: the byte is a 10-bit sync group (FF + two 0 cells)
// on the wire. Invisible to the IWM path (uint8_t cast strips it).
constexpr uint16_t kSync = 0x200;

// IBM MFM CRC-CCITT (poly 0x1021, init 0xFFFF) covering mark+payload
uint16_t crcCcitt(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= uint16_t(data[i]) << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? uint16_t((crc << 1) ^ 0x1021)
                                 : uint16_t(crc << 1);
    }
    return crc;
}

// Inverse 6&2 table: encoded nibble → 6-bit value, -1 if not a data nibble
int gcr6Inv(uint8_t v) {
    static int inv[256];
    static bool init = false;
    if (!init) {
        for (int& e : inv) e = -1;
        for (int i = 0; i < 0x40; i++) inv[kGcr6[i]] = i;
        init = true;
    }
    return inv[v];
}

inline void gcr6Encode(std::vector<uint16_t>& out, uint8_t va, uint8_t vb,
                       uint8_t vc, bool lastGroup) {
    out.push_back(kGcr6[((va >> 2) & 0x30) | ((vb >> 4) & 0x0c) | ((vc >> 6) & 0x03)]);
    out.push_back(kGcr6[va & 0x3f]);
    out.push_back(kGcr6[vb & 0x3f]);
    if (!lastGroup) out.push_back(kGcr6[vc & 0x3f]);
}

void pushGap(std::vector<uint16_t>& out, int n, uint8_t fill) {
    for (int i = 0; i < n; i++) out.push_back(fill);
}

void pushMarkA1(std::vector<uint16_t>& out) {
    out.push_back(kMark | 0xA1);
}

}  // namespace

int SonyDrive::sectorsInTrack(int track) { return 12 - (track >> 4); }

int SonyDrive::sectorsOnCurrentTrack() const {
    return hd_ ? sectorsInTrackHd() : sectorsInTrack(track_);
}

void SonyDrive::setMfmMode(bool on) {
    if (!superDrive_ && on) return;
    if (hd_ && !on) return;                      // HD media is always MFM
    if (mfmMode_ == on) return;
    mfmMode_ = on;
    encodeTrack();
}

void SonyDrive::reset() {
    track_ = 0;
    streamPos_ = 0;
    cellPos_ = 0;
    motorOn_ = false;
    dirToZero_ = false;
    switched_ = false;
    wrState_ = wrSync_ = wrAddrPos_ = wrDataPos_ = 0;
    if (hasDisk()) encodeTrack();
}

void SonyDrive::eject() {
    if (sound_ && hasDisk()) sound_->click();
    image_.clear();
    stream_.clear();
    cells_.clear();
    gcrWrBuf_.clear();
    streamPos_ = 0;
    cellPos_ = 0;
    hd_ = false;
    mfmMode_ = false;
    switched_ = true;
    wrState_ = 0;
}

bool SonyDrive::insert(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::vector<uint8_t> raw((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
    // DiskCopy 4.2: magic 0x01 0x00 at 0x52, big-endian dataSize at 0x40
    if (raw.size() > 0x54 && raw[0x52] == 0x01 && raw[0x53] == 0x00) {
        uint32_t dataSize = (uint32_t(raw[0x40]) << 24) | (uint32_t(raw[0x41]) << 16)
                          | (uint32_t(raw[0x42]) << 8) | uint32_t(raw[0x43]);
        if (raw.size() < 0x54 + dataSize) return false;
        raw.assign(raw.begin() + 0x54, raw.begin() + 0x54 + dataSize);
    }
    return insertImage(std::move(raw));
}

bool SonyDrive::insertImage(std::vector<uint8_t> data) {
    if (data.size() != kSize800K && data.size() != kSize400K &&
        data.size() != kSize1440K)
        return false;
    hd_ = (data.size() == kSize1440K);
    doubleSided_ = (data.size() != kSize400K);
    // HD media forces MFM; 800K/400K stay GCR (MAME mfd75w track_changed)
    mfmMode_ = hd_;
    if (hd_) superDrive_ = true;
    image_ = std::move(data);
    track_ = 0;
    side1_ = false;
    switched_ = true;
    wrState_ = 0;
    gcrWrBuf_.clear();
    encodeTrack();
    if (sound_) sound_->click();
    return true;
}

// Raw image ordering: cylinder-major, then head, then sector (MAME
// apple_gcr_format::load; HD is linear 80×2×18×512).
size_t SonyDrive::imageOffset(int track, int side, int sector) const {
    if (hd_) {
        return size_t(track) * 2 * 18 * 512 + size_t(side) * 18 * 512
             + size_t(sector) * 512;
    }
    size_t off = 0;
    for (int t = 0; t < track; t++)
        off += size_t(sectorsInTrack(t)) * (doubleSided_ ? 2 : 1) * 512;
    if (side) off += size_t(sectorsInTrack(track)) * 512;
    return off + size_t(sector) * 512;
}

void SonyDrive::selectSide(bool side1) {
    if (side1 == side1_) return;
    side1_ = side1;
    encodeTrack();
}

void SonyDrive::encodeTrack() {
    stream_.clear();
    if (!hasDisk()) { cells_.clear(); return; }
    if (mfmMode_ && hd_) encodeTrackMfm();
    else encodeTrackGcr();
    buildCells();
    if (streamPos_ >= stream_.size()) streamPos_ = 0;
    if (cellPos_ >= cells_.size()) cellPos_ = 0;
}

int SonyDrive::rpmNow() const {
    if (mfmMode_ && hd_) return 300;             // SuperDrive HD (mfd75w)
    static const int kRpm[5] = { 394, 429, 472, 525, 590 };
    return kRpm[track_ >> 4];
}

// Nominal cells per revolution: C15M / cell-divider vs the spindle RPM.
// The encoded track content is slightly shorter; the remainder is the
// physical gap4, padded with empty cells.
int64_t SonyDrive::nominalCells() const {
    const int cellCyc = (mfmMode_ && hd_) ? 16 : 31;   // swim2.cpp:329
    return (15667200LL * 60) / (int64_t(rpmNow()) * cellCyc);
}

int64_t SonyDrive::spinCyclesPerRev() const {
    return spinClockHz_ * 60 / rpmNow();
}

// Expand the byte stream into raw cells. MFM: clock+data half-cells per
// bit (clock = neither neighbour set), marks are the raw $4489 pattern
// with the missing clock. GCR: 8 cells per nibble, 10 for sync groups.
void SonyDrive::buildCells() {
    cells_.clear();
    if (mfmMode_ && hd_) {
        cells_.reserve(stream_.size() * 16);
        bool last = false;
        for (uint16_t v : stream_) {
            if (v & kMark) {
                for (int i = 15; i >= 0; i--)
                    cells_.push_back(uint8_t((0x4489 >> i) & 1));
                last = true;
                continue;
            }
            const uint8_t b = uint8_t(v);
            for (int i = 7; i >= 0; i--) {
                const uint8_t bit = (b >> i) & 1;
                cells_.push_back(uint8_t(!last && !bit));
                cells_.push_back(bit);
                last = bit;
            }
        }
    } else {
        cells_.reserve(stream_.size() * 10);
        for (uint16_t v : stream_) {
            const uint8_t b = uint8_t(v);
            for (int i = 7; i >= 0; i--) cells_.push_back((b >> i) & 1);
            if (v & kSync) {
                cells_.push_back(0);
                cells_.push_back(0);
            }
        }
    }
    const int64_t nominal = nominalCells();
    if (nominal > int64_t(cells_.size()))
        cells_.resize(size_t(nominal), 0);
}

// GCR-encode the current (track, side) as a byte-level nibble stream.
// Self-sync groups are emitted as plain 0xFF bytes: the IWM model delivers
// whole nibbles, so 10-bit sync framing is not needed (Plus Too approach).
void SonyDrive::encodeTrackGcr() {
    int ns = sectorsInTrack(track_);

    // Physical 2:1 interleave (MAME dc42 loader)
    int phys[12];
    int si = 0;
    for (int i = 0; i < ns; i++) {
        phys[si] = i;
        si = (si + 2) % ns;
        if (si == 0) si++;
    }

    uint8_t sideByte = uint8_t((side1_ ? 0x20 : 0x00) | ((track_ & 0x40) ? 1 : 0));
    uint8_t format = doubleSided_ ? 0x22 : 0x02;

    for (int s = 0; s < ns; s++) {
        int sector = phys[s];

        // Address field
        for (int i = 0; i < 38; i++) stream_.push_back(kSync | 0xFF);
        stream_.push_back(0xD5); stream_.push_back(0xAA); stream_.push_back(0x96);
        stream_.push_back(kGcr6[track_ & 0x3F]);
        stream_.push_back(kGcr6[sector & 0x3F]);
        stream_.push_back(kGcr6[sideByte & 0x3F]);
        stream_.push_back(kGcr6[format & 0x3F]);
        stream_.push_back(kGcr6[(track_ ^ sector ^ sideByte ^ format) & 0x3F]);
        stream_.push_back(0xDE); stream_.push_back(0xAA);
        stream_.push_back(kSync | 0xFF);

        // Data field: 12 tag bytes (zero) + 512 data bytes, 175 groups of 3
        for (int i = 0; i < 6; i++) stream_.push_back(kSync | 0xFF);
        stream_.push_back(0xD5); stream_.push_back(0xAA); stream_.push_back(0xAD);
        stream_.push_back(kGcr6[sector & 0x3F]);

        uint8_t buf[525] = {};                       // 12 tags + 512 data + pad
        const uint8_t* sec = &image_[imageOffset(track_, side1_ ? 1 : 0, sector)];
        for (int i = 0; i < 512; i++) buf[12 + i] = sec[i];

        // Rolling 3-way checksum, MAME build_mac_track_gcr verbatim:
        // va ^= OLD cc; vb ^= NEW ca; vc ^= NEW cb; 8-bit adds with the
        // carry chained a->b->c, seeded by the bit rotated out of cc.
        uint8_t ca = 0, cb = 0, cc = 0;
        for (int i = 0; i < 175; i++) {
            uint8_t va = buf[3 * i];
            uint8_t vb = buf[3 * i + 1];
            uint8_t vc = (i != 174) ? buf[3 * i + 2] : 0;

            cc = uint8_t((cc << 1) | (cc >> 7));
            uint16_t suma = uint16_t(ca + va + (cc & 1));
            ca = uint8_t(suma);
            va = va ^ cc;
            uint16_t sumb = uint16_t(cb + vb + (suma >> 8));
            cb = uint8_t(sumb);
            vb = vb ^ ca;
            if (i != 174) cc = uint8_t(cc + vc + (sumb >> 8));
            vc = vc ^ cb;

            gcr6Encode(stream_, va, vb, vc, i == 174);
        }
        gcr6Encode(stream_, ca, cb, cc, false);         // 4 checksum nibbles
        stream_.push_back(0xDE); stream_.push_back(0xAA);
        stream_.push_back(kSync | 0xFF); stream_.push_back(kSync | 0xFF);
    }
}

// IBM System 34 MFM track for Apple SuperDrive HD (80×2×18×512 @ 300 RPM).
// Decoded byte stream with MARK on the three A1 syncs — SWIM2's MFM path
// (MAME swim2.cpp:498-546) delivers the same shape into the FIFO.
void SonyDrive::encodeTrackMfm() {
    constexpr int kSectors = 18;
    // Index gap + per-sector gaps sized for a comfortable ~6250-byte track
    pushGap(stream_, 80, 0x4E);

    for (int sector = 1; sector <= kSectors; sector++) {
        pushGap(stream_, 12, 0x00);
        pushMarkA1(stream_);
        pushMarkA1(stream_);
        pushMarkA1(stream_);

        uint8_t addr[8] = {
            0xA1, 0xA1, 0xA1, 0xFE,
            uint8_t(track_), uint8_t(side1_ ? 1 : 0), uint8_t(sector), 0x02
        };
        stream_.push_back(0xFE);
        stream_.push_back(addr[4]);
        stream_.push_back(addr[5]);
        stream_.push_back(addr[6]);
        stream_.push_back(addr[7]);
        uint16_t acrc = crcCcitt(addr, 8);
        stream_.push_back(uint8_t(acrc >> 8));
        stream_.push_back(uint8_t(acrc));

        pushGap(stream_, 22, 0x4E);
        pushGap(stream_, 12, 0x00);
        pushMarkA1(stream_);
        pushMarkA1(stream_);
        pushMarkA1(stream_);
        stream_.push_back(0xFB);

        const uint8_t* sec =
            &image_[imageOffset(track_, side1_ ? 1 : 0, sector - 1)];
        uint8_t dataBlk[4 + 512];
        dataBlk[0] = dataBlk[1] = dataBlk[2] = 0xA1;
        dataBlk[3] = 0xFB;
        std::memcpy(dataBlk + 4, sec, 512);
        for (int i = 0; i < 512; i++) stream_.push_back(sec[i]);
        uint16_t dcrc = crcCcitt(dataBlk, sizeof dataBlk);
        stream_.push_back(uint8_t(dcrc >> 8));
        stream_.push_back(uint8_t(dcrc));

        pushGap(stream_, 84, 0x4E);
    }
    pushGap(stream_, 200, 0x4E);                      // track pad / index gap
}

uint8_t SonyDrive::nextNibble(bool side1) {
    return uint8_t(nextByte(side1));
}

uint16_t SonyDrive::nextByte(bool side1) {
    nibblesRead++;
    if (!hasDisk() || stream_.empty()) return 0xFF;
    selectSide(side1);
    uint16_t v = stream_[streamPos_];
    streamPos_ = (streamPos_ + 1) % stream_.size();
    return v;
}

int SonyDrive::nextCell(bool side1) {
    if (!hasDisk()) return 0;
    selectSide(side1);
    if (cells_.empty()) return 0;
    const int c = cells_[cellPos_];
    cellPos_ = (cellPos_ + 1) % cells_.size();
    return c;
}

void SonyDrive::syncCellsToRotation(bool side1) {
    if (!hasDisk()) return;
    selectSide(side1);
    if (cells_.empty()) return;
    const int64_t rev = spinCyclesPerRev();
    cellPos_ = size_t((spin_ % rev) * int64_t(cells_.size()) / rev);
}

int64_t SonyDrive::startWriteCells(bool side1) {
    syncCellsToRotation(side1);
    return int64_t(cellPos_);
}

// SWIM2 write-back: splice the written cells into the track, then decode
// the whole track and commit every sector whose address AND data CRCs
// verify. writeSector() re-encodes the track afterwards, so the raw cells
// are canonicalized (exotic layouts are not preserved — accepted
// simplification vs MAME's flux-level track store).
void SonyDrive::commitCells(int64_t startCell, int64_t totalCells,
                            const std::vector<int64_t>& transitions,
                            bool mfm) {
    if (!hasDisk() || writeProtected_ || cells_.empty()) return;
    const bool mediaMfm = mfmMode_ && hd_;
    if (mfm != mediaMfm) {
        // Encoding/media mismatch writes flux the media's decoder can't
        // read; nothing can commit — log the drop (LLE_VS_HLE rule b).
        std::fprintf(stderr,
                     "[sony] %s cell write on %s media dropped (%zu transitions)\n",
                     mfm ? "MFM" : "GCR", mediaMfm ? "MFM" : "GCR",
                     transitions.size());
        return;
    }
    const int64_t n = int64_t(cells_.size());
    for (int64_t i = 0; i < std::min(totalCells, n); i++)
        cells_[size_t((startCell + i) % n)] = 0;
    for (int64_t t : transitions) {
        if (t < 0 || t >= totalCells) continue;
        cells_[size_t((startCell + t) % n)] = 1;
    }
    if (mediaMfm) decodeMfmCells();
    else          decodeGcrCells();
}

// Offline replica of the SWIM2 MFM read engine (swim2.cpp:499-546) over
// the raw cell track: recover mark/CRC-tagged bytes, then walk the IBM
// System 34 fields. Two passes cover a write span wrapping the index.
void SonyDrive::decodeMfmCells() {
    struct TByte { uint8_t val; bool mark; bool crcOk; };
    std::vector<TByte> bytes;
    bytes.reserve(cells_.size() / 16);
    uint16_t sr = 0, crc = 0xCDB4;
    uint8_t tss = 0;
    int sync = 0;
    auto crcUp = [&](int bit) {
        if ((crc ^ (bit ? 0x8000 : 0x0000)) & 0x8000)
            crc = uint16_t((crc << 1) ^ 0x1021);
        else
            crc = uint16_t(crc << 1);
    };
    const size_t n = cells_.size();
    for (size_t k = 0; k < 2 * n; k++) {
        const int bit = cells_[k % n];
        if (sync < 64) {
            if (bit != (sync & 1)) sync++;
            else sync = 0;
            continue;
        }
        if (sync == 64 && bit) { sync--; continue; }
        if (sync == 65 || sync == 81) { tss = 0xFF; sr = 0; }
        if (sync & 1) {
            sr |= uint16_t(bit << (((96 - sync) >> 1) & 7));
            crcUp(bit);
        }
        tss = uint8_t((tss << 1) | bit);
        if ((tss & 0xF) == 1 && !(sync & 1)) sr |= kMark;
        sync++;
        if (sync == 80) {
            if (!(sr & kMark)) sync = 0;
            else {
                crc = 0xCDB4;
                bytes.push_back({uint8_t(sr), true, false});
            }
        } else if (sync == 96) {
            sync -= 16;
            bool crcOk = false;
            if (sr & kMark) crc = 0xCDB4;
            else if (!crc) crcOk = true;
            bytes.push_back({uint8_t(sr), (sr & kMark) != 0, crcOk});
        }
    }

    struct Pending { int t, h, s; uint8_t data[512]; };
    std::vector<Pending> pending;
    const size_t m = bytes.size();
    auto markRun = [&](size_t i) {
        return bytes[i].mark && bytes[i].val == 0xA1 &&
               bytes[i + 1].mark && bytes[i + 1].val == 0xA1 &&
               bytes[i + 2].mark && bytes[i + 2].val == 0xA1;
    };
    for (size_t i = 0; i + 10 < m; i++) {
        if (!(markRun(i) && !bytes[i + 3].mark && bytes[i + 3].val == 0xFE))
            continue;
        if (!bytes[i + 9].crcOk) continue;               // address CRC
        const int c = bytes[i + 4].val;
        const int h = bytes[i + 5].val;
        const int s = bytes[i + 6].val;
        // Data field inside the gap2 window (22×4E + 12×00 + marks)
        for (size_t j = i + 10; j + 517 < m && j < i + 80; j++) {
            if (!(markRun(j) && !bytes[j + 3].mark && bytes[j + 3].val == 0xFB))
                continue;
            if (!bytes[j + 517].crcOk) break;            // data CRC (2nd byte)
            Pending p;
            p.t = c;
            p.h = h & 1;
            p.s = s - 1;
            for (int b = 0; b < 512; b++) p.data[b] = bytes[j + 4 + size_t(b)].val;
            bool dup = false;
            for (const Pending& q : pending)
                if (q.t == p.t && q.h == p.h && q.s == p.s) { dup = true; break; }
            if (!dup) pending.push_back(p);
            break;
        }
    }
    if (debug)
        std::fprintf(stderr, "[sony] cell write-back: %zu CRC-valid sector(s)\n",
                     pending.size());
    for (const Pending& p : pending) writeSector(p.t, p.h, p.s, p.data);
    // Canonicalize the raw cells even when nothing verified (a bad-CRC
    // write must not leave garbage the next read would see as media).
    if (pending.empty()) encodeTrack();
}

// Offline replica of the SWIM2 GCR read framer (swim2.cpp:486-497) over
// the raw cell track: MSB-set bytes self-frame, leading zero cells (sync
// group tails) are absorbed by the empty shifter. Two passes cover a
// write span wrapping the index.
void SonyDrive::decodeGcrCells() {
    std::vector<uint8_t> bytes;
    bytes.reserve(cells_.size() / 8);
    uint8_t sr = 0;
    const size_t n = cells_.size();
    for (size_t k = 0; k < 2 * n; k++) {
        sr = uint8_t((sr << 1) | cells_[k % n]);
        if (sr & 0x80) {
            bytes.push_back(sr);
            sr = 0;
        }
    }
    const int committed = decodeGcrBytes(bytes.data(), bytes.size(), side1_);
    if (debug)
        std::fprintf(stderr, "[sony] GCR cell write-back: %d valid sector(s)\n",
                     committed);
    // Canonicalize even when nothing verified (a bad write must not leave
    // garbage the next read would see as media).
    if (!committed) encodeTrack();
}

// Scan a decoded nibble sequence for D5 AA AD data fields and commit every
// sector whose rolling 3-way checksum verifies (inverse of encodeTrackGcr;
// same loop as MAME extract_sectors_from_track_mac_gcr6, pinned by
// gcr_test). The physical head position names the track/side — a write can
// only land under the head; the field's own sector nibble names the slot.
// The 12 recovered tag bytes are dropped (flat images carry no tag space).
int SonyDrive::decodeGcrBytes(const uint8_t* nib, size_t n, bool side1) {
    int committed = 0;
    const int ns = sectorsInTrack(track_);
    std::vector<int> done;                       // dedup across wrap passes
    for (size_t p = 0; p + 707 <= n; p++) {
        if (!(nib[p] == 0xD5 && nib[p + 1] == 0xAA && nib[p + 2] == 0xAD))
            continue;
        const int sector = gcr6Inv(nib[p + 3]);
        if (sector < 0 || sector >= ns) continue;
        uint8_t sdata[525] = {};
        uint8_t ca = 0, cb = 0, cc = 0;
        size_t r = p + 4;
        auto pull = [&]() { return gcr6Inv(nib[r++]); };
        bool bad = false;
        for (int i = 0; i < 175 && !bad; i++) {
            const int h0 = pull(), n1 = pull(), n2 = pull();
            const int n3 = (i != 174) ? pull() : 0;
            if (h0 < 0 || n1 < 0 || n2 < 0 || n3 < 0) { bad = true; break; }
            uint8_t va = uint8_t((n1 & 0x3F) | ((h0 << 2) & 0xC0));
            uint8_t vb = uint8_t((n2 & 0x3F) | ((h0 << 4) & 0xC0));
            uint8_t vc = (i != 174)
                ? uint8_t((n3 & 0x3F) | ((h0 << 6) & 0xC0)) : 0;
            cc = uint8_t((cc << 1) | (cc >> 7));
            va = va ^ cc;
            const uint16_t suma = uint16_t(ca + va + (cc & 1));
            ca = uint8_t(suma);
            vb = vb ^ ca;
            const uint16_t sumb = uint16_t(cb + vb + (suma >> 8));
            cb = uint8_t(sumb);
            vc = vc ^ cb;
            sdata[3 * i] = va;
            sdata[3 * i + 1] = vb;
            if (i != 174) {
                cc = uint8_t(cc + vc + (sumb >> 8));
                sdata[3 * i + 2] = vc;
            }
        }
        if (bad) continue;
        const int h0 = pull(), w1 = pull(), w2 = pull(), w3 = pull();
        if (h0 < 0 || w1 < 0 || w2 < 0 || w3 < 0) continue;
        if (ca != uint8_t((w1 & 0x3F) | ((h0 << 2) & 0xC0)) ||
            cb != uint8_t((w2 & 0x3F) | ((h0 << 4) & 0xC0)) ||
            cc != uint8_t((w3 & 0x3F) | ((h0 << 6) & 0xC0)))
            continue;                            // checksum reject
        bool dup = false;
        for (int s : done)
            if (s == sector) dup = true;
        if (!dup) {
            done.push_back(sector);
            if (writeSector(track_, side1 ? 1 : 0, sector, sdata + 12))
                committed++;
        }
        p = r - 1;                               // resume past the field
    }
    return committed;
}

// IWM write path: nibbles accumulate while the IWM shifter runs; the field
// decode happens at flushWrite (write-mode exit or underrun), mirroring the
// cell path. The byte stream never touches stream_/cells_, so nothing needs
// canonicalizing when no field verifies.
void SonyDrive::writeNibble(uint8_t nibble) {
    if (!hasDisk() || writeProtected_) return;
    if (gcrWrBuf_.size() >= 65536) return;       // runaway-writer guard
    gcrWrBuf_.push_back(nibble);
}

void SonyDrive::flushWrite(bool side1) {
    if (gcrWrBuf_.empty()) return;
    selectSide(side1);                           // re-encode targets this side
    const int committed =
        decodeGcrBytes(gcrWrBuf_.data(), gcrWrBuf_.size(), side1);
    if (debug)
        std::fprintf(stderr,
                     "[sony] IWM write-back: %d valid sector(s) from %zu nibbles\n",
                     committed, gcrWrBuf_.size());
    gcrWrBuf_.clear();
}

bool SonyDrive::writeSector(int track, int side, int sector,
                            const uint8_t data[512]) {
    if (!hasDisk() || writeProtected_) return false;
    int ns = hd_ ? sectorsInTrackHd() : sectorsInTrack(track);
    if (track < 0 || track > 79 || side < 0 || side > (doubleSided_ ? 1 : 0) ||
        sector < 0 || sector >= ns)
        return false;
    size_t off = imageOffset(track, side, sector);
    if (off + 512 > image_.size()) return false;
    std::memcpy(&image_[off], data, 512);
    if (track == track_ && (side != 0) == side1_) encodeTrack();
    return true;
}

bool SonyDrive::readSector(int track, int side, int sector,
                           uint8_t data[512]) const {
    if (!hasDisk()) return false;
    int ns = hd_ ? sectorsInTrackHd() : sectorsInTrack(track);
    if (track < 0 || track > 79 || sector < 0 || sector >= ns) return false;
    size_t off = imageOffset(track, side, sector);
    if (off + 512 > image_.size()) return false;
    std::memcpy(data, &image_[off], 512);
    return true;
}

// Consume SWIM2 write-FIFO bytes: assemble IBM MFM address/data fields and
// commit 512-byte payloads via writeSector (legacy byte path; the cell
// engine commits through commitCells, GCR included).
void SonyDrive::writeByte(uint16_t value) {
    if (!hasDisk() || writeProtected_ || !motorOn_) return;
    const bool mark = (value & kMark) != 0;
    const uint8_t b = uint8_t(value);

    if (mark && b == 0xA1) {
        if (wrState_ == 0 || wrState_ == 1) {
            wrState_ = 1;
            wrSync_++;
        } else {
            wrState_ = 1;
            wrSync_ = 1;
        }
        return;
    }

    if (wrState_ == 1 && wrSync_ >= 3) {
        if (b == 0xFE) {
            wrState_ = 2;
            wrAddrPos_ = 0;
            wrSync_ = 0;
            return;
        }
        if (b == 0xFB) {
            wrState_ = 3;
            wrDataPos_ = 0;
            wrSync_ = 0;
            return;
        }
        wrState_ = 0;
        wrSync_ = 0;
        return;
    }

    if (wrState_ == 2) {
        switch (wrAddrPos_++) {
            case 0: wrTrack_ = b; break;
            case 1: wrHead_ = b; break;
            case 2: wrSector_ = b; break;
            case 3: break;                       // size code
            default:
                // CRC bytes — ignore; commit happens on data field
                if (wrAddrPos_ >= 6) { wrState_ = 0; wrAddrPos_ = 0; }
                break;
        }
        return;
    }

    if (wrState_ == 3) {
        if (wrDataPos_ < 512) {
            wrData_[wrDataPos_++] = b;
            return;
        }
        // Two CRC bytes follow; commit after the first payload completes
        writeSector(wrTrack_, wrHead_ & 1, wrSector_ - 1, wrData_);
        wrState_ = 0;
        wrDataPos_ = 0;
        return;
    }

    wrState_ = 0;
    wrSync_ = 0;
}

// Sense table for an 800K MFD-51W / SuperDrive MFD-75W (DEV.md § Sony;
// MAME mac_floppy_device::wpt_r — IWM CA2..CA0+SEL encoding).
// addr = (CA2<<3)|(CA1<<2)|(CA0<<1)|SEL, returned on IWM status bit 7.
bool SonyDrive::sense(int addr) const {
    switch (addr) {
        case 0x0: return dirToZero_;                 // DIRTN (1 = toward 0)
        case 0x1: return !hasDisk();                 // CSTIN (0 = inserted)
        case 0x2: return true;                       // STEP complete
        case 0x3: return !writeProtected_;           // WRTPRT (0 = protected)
        case 0x4: return !motorOn_;                  // MOTORON (0 = on)
        case 0x5: return track_ != 0;                // TKO (0 = track 0)
        case 0x6: return switched_;                  // SWITCHED / disk changed
        case 0x7: {                                  // TACH: 120 edges/rev
            int rpm;
            if (mfmMode_ && hd_) rpm = 300;          // SuperDrive HD (mfd75w)
            else {
                static const int kRpm[5] = { 394, 429, 472, 525, 590 };
                rpm = kRpm[track_ >> 4];
            }
            // Plus IWM tach is measured at 7.8336 MHz; SWIM2 hosts use the
            // same spin_ counter in CPU cycles of the attached machine —
            // relative edge rate is what the driver compares.
            int64_t cyclesPerRev = 7833600LL * 60 / rpm;
            int64_t phase = (spin_ % cyclesPerRev) * 120 / cyclesPerRev;
            return phase & 1;
        }
        case 0x8: return false;                      // RDDATA0
        case 0x9: return false;                      // RDDATA1
        case 0xA: return superDrive_;                // SUPERDRIVE (1 = HD-capable)
        case 0xB: return mfmMode_;                   // MFM mode
        case 0xC: return doubleSided_;               // SIDES (1 = DS)
        case 0xD: return !motorOn_;                  // READY (0 = ready)
        case 0xE: return false;                      // INSTALLED (0 = present)
        case 0xF: return hd_ ? false : true;         // REVISED / !2M (HD hole)
    }
    return true;
}

// MAME mac_floppy_device::wpt_r (floppy.cpp:3217-3285). SWIM2 presents the
// phase register directly; retain sense()/command() above for the IWM wiring.
bool SonyDrive::senseSwim(int reg) const {
    switch (reg & 0x0F) {
    case 0x0: return dirToZero_;                 // direction
    case 0x1: return true;                       // step complete
    case 0x2: return !motorOn_;                  // motor is active low
    case 0x3: return !hasDisk();                 // !m_dskchg (unload = high)
    case 0x4: case 0xC:                          // index/read-data
        // MAME reports high for a SuperDrive while stopped/no media, which
        // forms the documented initial capability signature x011 (2M,
        // ready, MFM, RD1). Once spinning, approximate the narrow index pulse.
        if (!superDrive_) return false;
        if (!hasDisk() || !motorOn_) return true;
        return (spin_ % (7833600LL / 5)) > 2000; // 300 RPM, active-low index
    case 0x5: return superDrive_;                // MFD-75W capability
    case 0x6: return doubleSided_;
    case 0x7: return false;                      // drive exists
    case 0x8: return !hasDisk();
    case 0x9: return !writeProtected_;
    case 0xA: return track_ != 0;
    case 0xB: {                                  // 120 tach inversions/rev
        if (!hasDisk() || !motorOn_) return false;
        const int rpm = hd_ ? 300 : 394;
        const int64_t cyclesPerRev = 7833600LL * 60 / rpm;
        return ((spin_ % cyclesPerRev) * 120 / cyclesPerRev) & 1;
    }
    case 0xD: return mfmMode_;
    case 0xE: return !hasDisk() || !motorOn_;     // NoReady (active high)
    case 0xF: return superDrive_ && hasDisk() && !hd_; // MAME mfd75w::is_2m
    }
    return false;
}

// MAME mac_floppy_device::seek_phase_w (floppy.cpp:3292-3361).
void SonyDrive::commandSwim(int reg) {
    switch (reg & 0x0F) {
    case 0x0: dirToZero_ = false; break;          // next cylinder
    case 0x1:                                      // STEP pulse
        track_ += dirToZero_ ? -1 : 1;
        if (track_ < 0) track_ = 0;
        if (track_ > 79) track_ = 79;
        encodeTrack();
        if (sound_) sound_->step(track_, soundMicros());
        break;
    case 0x2: setMotorState(true); break;
    case 0x4: dirToZero_ = true; break;           // previous cylinder
    case 0x6: setMotorState(false); break;
    case 0x7: eject(); break;
    case 0x9:
        if (superDrive_) setMfmMode(true);
        break;
    case 0xC: break;                             // clear dskchg (inserted stays low)
    case 0xD: setMfmMode(false); break;
    default: break;
    }
}

// LSTRB rising-edge commands: CA2 is the value, (CA1,CA0,SEL) the address.
void SonyDrive::command(int addr) {
    bool ca2 = (addr & 8) != 0;
    if (debug)
        std::fprintf(stderr, "[sony] cmd addr=%X (track=%d, nibbles=%ld)\n",
                     addr, track_, nibblesRead);
    switch (addr & 7) {                              // CA1 CA0 SEL
        case 0b000: dirToZero_ = ca2; break;         // DIRTN
        case 0b010:                                  // STEP (instant)
            if (!ca2) {
                track_ += dirToZero_ ? -1 : 1;
                if (track_ < 0) track_ = 0;
                if (track_ > 79) track_ = 79;
                encodeTrack();
                if (sound_) sound_->step(track_, soundMicros());
            }
            break;
        case 0b100: setMotorState(!ca2); break;      // MOTOR ON / OFF
        case 0b110:                                  // EJECT (ca2=1)
            if (ca2) eject();
            break;
        case 0b001:                                  // CLRSWITCHED / MFM on
            // 800K: CA2=1 clears disk-changed (DEV.md). SuperDrive also
            // uses this strobe as MFM-mode-on (MAME seek_phase_w 0x9).
            if (ca2) {
                if (superDrive_) {
                    mfmMode_ = true;
                    encodeTrack();
                }
                switched_ = false;
            }
            break;
        case 0b101:                                  // GCR mode on (SuperDrive)
            // MAME mac_floppy seek_phase_w case 0xd
            if (ca2 && superDrive_ && !hd_) {
                mfmMode_ = false;
                encodeTrack();
            }
            break;
        default: break;
    }
}

void SonyDrive::tick(int cpuCycles) {
    cycles_ += cpuCycles;                            // free-running (sound stamps)
    if (motorOn_) spin_ += cpuCycles;                // spin-up itself is instant
}

// Emulated-time stamp for the sound sink, in microseconds — integer
// split so precision holds over hours of runtime.
uint64_t SonyDrive::soundMicros() const {
    const int64_t hz = spinClockHz_ > 0 ? spinClockHz_ : 7833600;
    return uint64_t(cycles_ / hz) * 1000000ull +
           uint64_t((cycles_ % hz) * 1000000 / hz);
}

void SonyDrive::setMotorState(bool on) {
    if (on == motorOn_) return;
    motorOn_ = on;
    if (sound_) sound_->motor(on, hasDisk());
}
