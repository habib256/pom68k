// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Sense/command tables and GCR encoder ported from MAME (floppy.cpp
// mac_floppy_device, flopimg.cpp build_mac_track_gcr, ap_dsk35.cpp),
// MFM HD track layout after IBM System 34 / Apple SuperDrive (mfd75w,
// floppy.cpp:3452-3477). Cross-checked against pce gcr-mac.c — see DEV.md.

#include "SonyDrive.h"
#include "AtomicReplace.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
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

// Setup-register reflection path (SWIM1/2 setup.2 → drive mode, an HLE
// shortcut: in MAME a controller setup write never touches the drive's
// m_mfm, which only moves on the seek_phase strobes). The guards keep the
// reflection conservative — a non-SuperDrive can't enter MFM, HD media
// stays MFM — which is the closest byte-level stand-in for MAME's "setup
// writes change nothing on the drive". The strobes go through
// commandMfmMode() below, which is the MAME-exact table.
void SonyDrive::setMfmMode(bool on) {
    if (!superDrive_ && on) return;
    if (hd_ && !on) return;                      // reflection-only guard
    if (mfmMode_ == on) return;
    mfmMode_ = on;
    encodeTrack();
}

// Strobe-path mode switch (MAME mac_floppy seek_phase_w): MFM-on is gated
// on the MECHANISM (m_has_mfm, floppy.cpp:3369-3375); GCR-on has NO guard
// at all (floppy.cpp:3382-3386) — HD media obeys too, and the GCR framer
// then sees the MFM cells as garbage exactly like hardware. The rpm swap
// of MAME's track_changed() is rpmNow(), already keyed on mfmMode_ && hd_.
void SonyDrive::commandMfmMode(bool on) {
    if (on && !superDrive_) return;
    if (mfmMode_ == on) return;
    mfmMode_ = on;
    encodeTrack();
}

void SonyDrive::reset() {
    track_ = 0;
    streamPos_ = 0;
    motorOn_ = false;
    dirToZero_ = false;
    // Disk-change latch across reset: MAME's device_reset leaves m_dskchg
    // alone and device_start seeds it from exists() (floppy.cpp:560) —
    // "changed" reads high only while the drive sits empty.
    switched_ = !hasDisk();
    wrState_ = wrSync_ = wrAddrPos_ = wrDataPos_ = 0;
    // A machine reset mid-write must not leave stale nibbles for the next
    // flushWrite to commit as a half-sector (insert/eject already clear it).
    gcrWrBuf_.clear();
    // MAME mac_floppy device_reset: m_mfm = m_has_mfm — a SuperDrive powers
    // up in MFM mode (capability signature x011); the driver strobes GCR-on
    // before touching GCR media. HD media stays MFM regardless.
    mfmMode_ = superDrive_ || hd_;
    if (hasDisk()) encodeTrack();
}

void SonyDrive::eject() {
    if (sound_ && hasDisk()) sound_->click();
    flushToFile();                 // Mac OS has flushed its caches by now
    path_.clear();
    dc42Header_.clear();
    dirty_ = false;
    image_.clear();
    stream_.clear();
    cells_.clear();
    flux_.clear();
    fluxRev_ = 0;
    gcrWrBuf_.clear();
    streamPos_ = 0;
    hd_ = false;
    mfmMode_ = false;
    switched_ = true;
    wrState_ = 0;
}

bool SonyDrive::insert(const std::string& path) {
    // Insert-over-insert must honour the same write-back contract as eject():
    // insertImage() drops path_/dirty_ wholesale, so without this the outgoing
    // media's committed sectors are lost when the user picks a second image
    // straight from the Disques menu (no eject in between).
    if (hasDisk()) flushToFile();
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::vector<uint8_t> raw((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
    std::vector<uint8_t> header;
    // DiskCopy 4.2: magic 0x01 0x00 at 0x52, big-endian dataSize at 0x40
    if (raw.size() > 0x54 && raw[0x52] == 0x01 && raw[0x53] == 0x00) {
        uint32_t dataSize = (uint32_t(raw[0x40]) << 24) | (uint32_t(raw[0x41]) << 16)
                          | (uint32_t(raw[0x42]) << 8) | uint32_t(raw[0x43]);
        // 64-bit throughout: "0x54 + dataSize" as int+uint32_t wraps, but the
        // iterator arithmetic below widens dataSize to ptrdiff_t and does NOT,
        // so a dataSize near 2^32 slipped past the guard and copied gigabytes
        // off the heap. Bound by the largest medium we accept as well.
        if (dataSize > kSize1440K || raw.size() - 0x54 < size_t(dataSize)) return false;
        header.assign(raw.begin(), raw.begin() + 0x54);
        raw.assign(raw.begin() + 0x54, raw.begin() + 0x54 + size_t(dataSize));
    }
    if (!insertImage(std::move(raw))) return false;
    path_ = path;                  // remember the source for flushToFile
    dc42Header_ = std::move(header);
    dirty_ = false;
    return true;
}

bool SonyDrive::insertImage(std::vector<uint8_t> data) {
    if (data.size() != kSize800K && data.size() != kSize400K &&
        data.size() != kSize1440K)
        return false;
    path_.clear();                 // in-memory media has no backing file
    dc42Header_.clear();
    dirty_ = false;
    hd_ = (data.size() == kSize1440K);
    doubleSided_ = (data.size() != kSize400K);
    // HD media forces MFM; 800K/400K stay GCR (MAME mfd75w track_changed).
    // The mechanism is NOT promoted to SuperDrive by the media: an HD image
    // in a plain 800K drive is unreadable, exactly like the real thing —
    // SWIM platforms all setSuperDrive(true) at attach.
    mfmMode_ = hd_;
    image_ = std::move(data);
    track_ = 0;
    side1_ = false;
    // MAME floppy.cpp:672-673 (init_floppy_load): inserting media SETS
    // m_dskchg on a Mac drive (m_dskchg_writable), i.e. CLEARS the change
    // latch — only eject raises it (call_unload, floppy.cpp:723).
    switched_ = false;
    wrState_ = 0;
    gcrWrBuf_.clear();
    if (motorOn_) readyCounter_ = 2;             // MAME call_load :666-669
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
    // A single-sided mechanism has no side-1 head: the IWM drives SEL from VIA
    // PA5 unconditionally, and imageOffset() would then index a whole side past
    // a 400K image (track 79 lands exactly at image_.size()).
    side1 = side1 && doubleSided_;
    if (side1 == side1_) return;
    side1_ = side1;
    encodeTrack();
}

// Lay the canonical track: the byte stream, the cells it implies, and the
// flux those cells put on the medium. This is what a track looks like
// before any guest has written to it — a seek, a side change or a sector
// commit through one of the byte paths all land here, and all of them
// legitimately discard whatever flux the medium held: the content comes
// from `image_`, which is the authority for everything a decoder verified.
void SonyDrive::encodeTrack() {
    refreshStream();
    if (!hasDisk()) { cells_.clear(); flux_.clear(); fluxRev_ = 0; return; }
    buildCells();
    fluxSeedFromCells();
}

// The byte stream alone. Used when the flux on the medium must NOT be
// re-laid — commitFlux()'s own sector commits, where the controller's
// transitions are the medium now and `stream_` is only the legacy nibble
// path's view of it (retired with the Iwm cell engine, § 1.3 step 6).
void SonyDrive::refreshStream() {
    stream_.clear();
    if (!hasDisk()) return;
    if (mfmMode_ && hd_) encodeTrackMfm();
    else encodeTrackGcr();
    if (streamPos_ >= stream_.size()) streamPos_ = 0;
}

// KNOWN MAME DIVERGENCE, deliberately kept (parity audit § 2.3, cosmetic):
// **2M mode is unreachable.** MAME's mac_floppy_device::track_changed picks
// `m_mfm ? (is_2m() ? 600 : 300) : <GCR zone>` (floppy.cpp:3399-3414), and
// mfd75w::is_2m() is true whenever the inserted medium is DD (floppy.cpp:
// 3494-3503) — so MFM clocking on an 800K/400K disk spins the spindle at
// 600 RPM and lays down the 1.6 MB "2M" format. We never return 600: the
// combination mfmMode_ && !hd_ falls through to the GCR zone table here, and
// encodeTrack() hands it to encodeTrackGcr(). The *sense* bit is modelled
// exactly (sense() case $F and senseSwim() case $F both implement
// mfd75w::is_2m), so a guest probing for the capability gets MAME's answer;
// only the spin rate and the format are missing.
// Not aligned. It is not a comment-sized change — it needs a whole DD-MFM
// track encoder (encodeTrackMfm is hard-wired to the HD 18×512 System-34
// layout) for a format no shipped Mac driver ever writes: the Sony 1.4 MB
// mechanism reads DD media in GCR at the five variable speeds below. And the
// one line that *is* small — the 600 in this function — feeds
// spinCyclesPerRev() and nominalCells(), i.e. index pulse, tach, rotational
// latency and track length: rotation timing, off limits after 2026-08-05.
// Reopen if a guest is ever seen strobing MFM-on (commandSwim $9) with DD
// media in the drive; commandMfmMode() is the place that would notice.
int SonyDrive::rpmNow() const {
    if (mfmMode_ && hd_) return 300;             // SuperDrive HD (mfd75w)
    static const int kRpm[5] = { 394, 429, 472, 525, 590 };
    return kRpm[track_ >> 4];
}

// Nominal cells per revolution: C15M / cell-divider vs the spindle RPM.
// The encoded track content is slightly shorter; the remainder is the
// physical gap4, padded with empty cells at the TAIL — MAME instead sizes a
// self-sync PREGAP at the head so the zone fills exactly. Deliberate, see
// the geometry note on encodeTrackGcr().
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
    cellsDirty_ = false;
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
    // Physical gap4. MAME sizes a self-sync PREGAP so the track fills its
    // speed zone exactly (flopimg.cpp:2037-2051); this encoder keeps the
    // fixed pregap its geometry note pins and puts the zone slack at the
    // TAIL — but the slack has to be WRITTEN, because a formatted track has
    // no unmagnetised arc. It used to be dead cells, which was invisible
    // while the Iwm walked a byte stream and is not since the cell engine
    // (§ 1.3 step 6): a transition-free arc gives the read shifter nothing
    // to re-centre its window on for ~2000 cells, once per revolution.
    // 10-cell self-sync groups, the same $FF + two 0 cells the sync runs
    // between fields use.
    if (!(mfmMode_ && hd_)) {
        while (int64_t(cells_.size()) + 10 <= nominal) {
            for (int i = 0; i < 8; i++) cells_.push_back(1);
            cells_.push_back(0);
            cells_.push_back(0);
        }
    }
    if (nominal > int64_t(cells_.size()))
        cells_.resize(size_t(nominal), 0);
}

// GCR-encode the current (track, side) as a byte-level nibble stream.
// Self-sync groups are emitted as plain 0xFF bytes: the IWM model delivers
// whole nibbles, so 10-bit sync framing is not needed (Plus Too approach).
//
// KNOWN MAME DIVERGENCE, deliberately kept (parity audit § 2.3, cosmetic).
// docs/LLE_VS_HLE.md § 1.3 inventories only the tag half of this ("committed
// tracks re-encode canonically… recovered tag bytes are dropped"); the filler
// lengths below are recorded here and nowhere else. Fields and checksum are
// MAME-exact (build_mac_track_gcr, flopimg.cpp:2019-2110); the *filler
// geometry* is not:
//   • pregap. MAME sizes it so the track fills its speed zone exactly —
//     `pregap = cells_per_speed_zone[zone] - 6208*sectors`, emitted at the
//     head of the track as alternating 24-cell $ff3fcf/$f3fcff sync groups
//     (flopimg.cpp:2037-2051). We emit a fixed 38 sync bytes ahead of each
//     address field — 40 counting the previous sector's two tail syncs, i.e.
//     400 cells against MAME's 8×48 = 384 (flopimg.cpp:2054-2057) — and push
//     the zone slack to the TAIL, as zero cells (see buildCells()) instead of
//     self-sync, so gap4 reads back as a run of no-transition cells. On the
//     *nibble* path (Iwm) there is no gap4 at all: stream_ wraps straight
//     from the last sector's tail into the next sector's pregap.
//   • the address→data gap is 7 sync bytes here vs MAME's 48 cells (~4.8).
//   • tag bytes are zero-filled: MAME's DC42 loader carries the real 12-byte
//     tag per sector (ap_dsk35.cpp:225-227, 290-296); flat .dsk images have
//     no tag space at all, and writeSector()/flushToFile() would have nowhere
//     to put a recovered one.
// Not aligned, on the standing rule that anything shaping the nibble stream
// is off limits after the 2026-08-05 GCR/denibble repair: these lengths ARE
// the spacing Apple's hand-timed read loop runs against, and every byte of
// slack moves where the guest's sync hunt lands. Zero benefit — the guest
// hunts for $D5 $AA $96, it never counts filler. Gated as-is by
// tests/gcr_test.cpp (tag bytes zero + the 40/7 sync runs). Reopen only
// together with step 2 of the § 1.3 flux plan, which replaces this encoder
// with a cell/flux track store where MAME's zone arithmetic applies directly.
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
        // readSector()/writeSector() both bound this; the encoder did not.
        const size_t off = imageOffset(track_, side1_ ? 1 : 0, sector);
        if (off + 512 <= image_.size())
            for (int i = 0; i < 512; i++) buf[12 + i] = image_[off + i];

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

        static const uint8_t kBlank[512] = {};
        const size_t soff = imageOffset(track_, side1_ ? 1 : 0, sector - 1);
        const uint8_t* sec = (soff + 512 <= image_.size())
                           ? &image_[soff] : kBlank;    // bound like readSector
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

int64_t SonyDrive::startWriteFlux(bool side1) {
    return fluxAngleTicks(side1);
}

// ── Flux store (LLE_VS_HLE § 1.3, steps 2 and 5) ────────────────────────
// One C15M clock = FluxPll::kSubCell ticks. A canonical track puts an edge
// at the CENTRE of each 1-cell — the same placement FluxPll::writeNextBit
// uses, so a write-then-read round trip lands edges where the reader's
// loop expects them. The revolution is fluxRev_ ticks; the tail gap4 is
// simply edge-free time.

void SonyDrive::setFluxJitterPercent(int pct) {
    fluxJitterPct_ = std::clamp(pct, 0, 45);
    fluxJitterTicks_ = int64_t(fluxJitterPct_) * fluxCellTicks() / 100;
}

void SonyDrive::debugStretchFluxPermille(int permille) {
    fluxStretchPermille_ = std::max(1, permille);
    // Re-lay the canonical track under the new spacing rather than scaling
    // what is there: the seam is idempotent that way, so a gate can set
    // 1100 twice and get +10 %, not +21 %.
    encodeTrack();
}

int64_t SonyDrive::fluxCellTicks() const {
    const int cellCyc = (mfmMode_ && hd_) ? 16 : 31;   // = nominalCells()
    return int64_t(cellCyc) * FluxPll::kSubCell;
}

int64_t SonyDrive::fluxRevTicks() const {
    return fluxRev_;
}

int64_t SonyDrive::fluxAngleTicks(bool side1) {
    if (!hasDisk()) return 0;
    selectSide(side1);
    if (fluxRev_ <= 0) return 0;
    const int64_t rev = spinCyclesPerRev();
    // The head's angular position as a time into the revolution, so the PLL
    // can land between two cell boundaries once media stop being ideal.
    return (spin_ % rev) * fluxRev_ / rev;
}

// Seed the store from the canonical cell ring — the medium as it comes off
// the encoder, before any guest has written to it.
void SonyDrive::fluxSeedFromCells() {
    flux_.clear();
    decodeCellTicks_ = 0;                        // canonical track: nominal
    fluxJitterTicks_ = int64_t(fluxJitterPct_) * fluxCellTicks() / 100;
    const int64_t cellT = fluxCellTicks();
    fluxRev_ = int64_t(cells_.size()) * cellT;
    if (cells_.empty()) return;
    for (size_t i = 0; i < cells_.size(); i++) {
        if (!cells_[i]) continue;
        int64_t t = int64_t(i) * cellT + cellT / 2;
        if (fluxStretchPermille_ != 1000)        // test seam: off-rate track
            t = t * fluxStretchPermille_ / 1000;
        if (t < fluxRev_) flux_.push_back(t);    // past the index: overwritten
    }
    // The cells just built ARE the separator's reading of this flux when the
    // spacing is nominal; under the stretch seam they are not, so make the
    // decoders re-derive rather than trust them.
    if (fluxStretchPermille_ != 1000) cellsDirty_ = true;
}

// The store, read back through a real separator — the offline write-back
// decoders' view of the medium. Quantizing on the nominal grid instead
// would defeat the store's whole purpose: a track the guest wrote at its
// own rate would decode to garbage here and commit nothing, where a real
// controller's PLL reads it back without noticing.
void SonyDrive::rebuildCellsFromFlux() {
    cellsDirty_ = false;
    cells_.clear();
    if (flux_.empty() || fluxRev_ <= 0) return;
    const int64_t cellT = decodeCellTicks_ > 0 ? decodeCellTicks_
                                               : fluxCellTicks();
    FluxPll pll;
    pll.setClock(cellT);
    pll.readReset(0);
    size_t idx = 0;
    cells_.reserve(size_t(fluxRev_ / cellT) + 8);
    while (pll.ctime() < fluxRev_) {
        while (idx < flux_.size() && flux_[idx] < pll.ctime()) idx++;
        const int64_t edge = idx < flux_.size() ? flux_[idx] : FluxPll::kNever;
        const int cell = pll.feedReadData(edge, fluxRev_);
        if (cell < 0) break;                     // window crosses the index
        cells_.push_back(uint8_t(cell));
        if (cell) idx++;                         // that edge is consumed
    }
}

const std::vector<uint8_t>& SonyDrive::cellsView() {
    if (cellsDirty_) rebuildCellsFromFlux();
    return cells_;
}

// Deterministic per-(track, side, transition, revolution) displacement in
// [-fluxJitterTicks_, +fluxJitterTicks_] — splitmix64 over the identity, so
// a re-read of the same revolution sees the same edges (replays and save
// states stay bit-identical) while successive revolutions differ, which is
// what real peak-shift noise looks like to a separator.
int64_t SonyDrive::fluxJitter(size_t idx, int64_t revNo) const {
    if (!fluxJitterTicks_) return 0;
    uint64_t x = (uint64_t(idx) << 1) ^ (uint64_t(revNo) << 24)
               ^ (uint64_t(uint32_t(track_)) << 48) ^ (side1_ ? 1ull : 0ull);
    x += 0x9E3779B97F4A7C15ull;
    x ^= x >> 30; x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27; x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    const uint64_t span = uint64_t(2 * fluxJitterTicks_ + 1);
    return int64_t(x % span) - fluxJitterTicks_;
}

int64_t SonyDrive::nextFluxAfter(int64_t tick, bool side1) {
    if (!hasDisk()) return FluxPll::kNever;
    selectSide(side1);
    if (flux_.empty() || fluxRev_ <= 0) return FluxPll::kNever;
    const int64_t rev = fluxRev_;
    if (tick < 0) tick = 0;
    int64_t revNo = tick / rev;
    // Jitter keeps edges within ± <half a min gap>, so the jittered
    // sequence stays sorted and a search on the ideal positions (widened by
    // the amplitude) followed by a short forward walk is exact. Two passes:
    // the wrap into the next revolution always finds flux_[0].
    for (int hop = 0; hop < 2; hop++, revNo++) {
        const int64_t pos = tick - revNo * rev;
        auto it = std::lower_bound(flux_.begin(), flux_.end(),
                                   pos - fluxJitterTicks_);
        for (; it != flux_.end(); ++it) {
            const size_t idx = size_t(it - flux_.begin());
            const int64_t t = *it + fluxJitter(idx, revNo);
            if (t >= pos) return revNo * rev + t;
        }
    }
    return FluxPll::kNever;                      // unreachable: flux_ nonempty
}

// Controller write-back (§ 1.3 step 5): erase the arc the head was over,
// lay the transitions the write serializer produced AT THE TIMES it
// produced them, then decode what is now on the medium and commit every
// sector whose CRCs verify. The flux stays as written — no canonical
// re-lay — which is what makes a track written at the controller's own
// rate readable afterwards instead of silently replaced.
void SonyDrive::commitFlux(int64_t startTick, int64_t totalTicks,
                           const std::vector<int64_t>& atTicks, bool mfm,
                           int64_t cellTicks) {
    if (!hasDisk() || writeProtected_ || fluxRev_ <= 0) return;
    const bool mediaMfm = mfmMode_ && hd_;
    if (mfm != mediaMfm) {
        // Encoding/media mismatch writes flux the media's decoder can't
        // read; nothing can commit — log the drop (LLE_VS_HLE rule b).
        std::fprintf(stderr,
                     "[sony] %s flux write on %s media dropped (%zu transitions)\n",
                     mfm ? "MFM" : "GCR", mediaMfm ? "MFM" : "GCR",
                     atTicks.size());
        return;
    }
    const int64_t rev = fluxRev_;
    // A write longer than a revolution overwrites the whole track; anything
    // it laid down before the last pass is gone under the later one.
    const int64_t span = std::min(totalTicks, rev);
    auto wrap = [rev](int64_t t) {
        t %= rev;
        return t < 0 ? t + rev : t;
    };
    const int64_t from = wrap(startTick);
    const int64_t to = from + span;              // may run past rev: wraps
    auto erased = [&](int64_t t) {
        return (t >= from && t < to) || (to > rev && t < to - rev);
    };
    std::vector<int64_t> next;
    next.reserve(flux_.size() + atTicks.size());
    for (int64_t t : flux_)
        if (!erased(t)) next.push_back(t);
    for (int64_t t : atTicks) {
        if (t < 0 || t >= span) continue;        // outside the opened arc
        next.push_back(wrap(from + t));
    }
    std::sort(next.begin(), next.end());
    flux_.swap(next);
    cellsDirty_ = true;
    decodeCellTicks_ = cellTicks > 0 ? cellTicks : 0;
    inFluxCommit_ = true;
    if (mediaMfm) decodeMfmCells();
    else          decodeGcrCells();
    inFluxCommit_ = false;
}

// Offline replica of the SWIM2 MFM read engine (swim2.cpp:499-546) over
// the raw cell track: recover mark/CRC-tagged bytes, then walk the IBM
// System 34 fields. Two passes cover a write span wrapping the index.
void SonyDrive::decodeMfmCells() {
    struct TByte { uint8_t val; bool mark; bool crcOk; };
    const std::vector<uint8_t>& cv = cellsView();
    if (cv.empty()) return;
    std::vector<TByte> bytes;
    bytes.reserve(cv.size() / 16);
    uint16_t sr = 0, crc = 0xCDB4;
    uint8_t tss = 0;
    int sync = 0;
    auto crcUp = [&](int bit) {
        if ((crc ^ (bit ? 0x8000 : 0x0000)) & 0x8000)
            crc = uint16_t((crc << 1) ^ 0x1021);
        else
            crc = uint16_t(crc << 1);
    };
    const size_t n = cv.size();
    for (size_t k = 0; k < 2 * n; k++) {
        const int bit = cv[k % n];
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
    // Nothing verified. Before the flux store this re-laid the canonical
    // track, because the cell ring was the only medium there was and
    // leaving garbage in it would have been read back as media forever.
    // The store makes that unnecessary AND wrong on the flux path: a write
    // whose CRC does not verify left real garbage on a real disk, and the
    // next read is supposed to find it. The byte paths, which have no flux
    // of their own, still canonicalize.
    if (pending.empty() && !inFluxCommit_) encodeTrack();
}

// Offline replica of the SWIM2 GCR read framer (swim2.cpp:486-497) over
// the raw cell track: MSB-set bytes self-frame, leading zero cells (sync
// group tails) are absorbed by the empty shifter. Two passes cover a
// write span wrapping the index.
void SonyDrive::decodeGcrCells() {
    const std::vector<uint8_t>& cv = cellsView();
    if (cv.empty()) return;
    std::vector<uint8_t> bytes;
    bytes.reserve(cv.size() / 8);
    uint8_t sr = 0;
    const size_t n = cv.size();
    for (size_t k = 0; k < 2 * n; k++) {
        sr = uint8_t((sr << 1) | cv[k % n]);
        if (sr & 0x80) {
            bytes.push_back(sr);
            sr = 0;
        }
    }
    const int committed = decodeGcrBytes(bytes.data(), bytes.size(), side1_);
    if (debug)
        std::fprintf(stderr, "[sony] GCR cell write-back: %d valid sector(s)\n",
                     committed);
    // See decodeMfmCells: canonicalizing a failed write belongs to the byte
    // paths, which carry no flux of their own. On the flux path the garbage
    // IS the medium now.
    if (!committed && !inFluxCommit_) encodeTrack();
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
        // First occurrence wins — deliberately. A splice that landed at the
        // live rotation angle can leave the sector's OLD field intact
        // elsewhere on the track; the first field under the head from the
        // scan origin is the one a read would return (pinned by
        // swim2_media_test "inverse of the read path").
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
    dirty_ = true;
    if (track == track_ && (side != 0) == side1_) {
        // Re-lay the live track so the medium shows what was just written —
        // except under commitFlux(), where the controller's own transitions
        // ARE the medium and re-laying them canonically is exactly the loss
        // the flux store exists to stop. Only the legacy nibble stream is
        // resynced there (it retires with the Iwm cell engine, step 6).
        if (inFluxCommit_) refreshStream();
        else               encodeTrack();
    }
    return true;
}

// DiskCopy 4.2 rolling checksum over big-endian 16-bit words: add the
// word, then rotate the 32-bit sum right by one (DC42 spec; Mini vMac /
// MAME dc42 writers).
static uint32_t dc42Checksum(const uint8_t* d, size_t n) {
    uint32_t sum = 0;
    for (size_t i = 0; i + 1 < n; i += 2) {
        sum += uint32_t(d[i] << 8 | d[i + 1]);
        sum = (sum >> 1) | (sum << 31);
    }
    return sum;
}

bool SonyDrive::flushToFile() {
    if (!writeBack_ || !dirty_ || path_.empty() || image_.empty()) return false;
    const std::string tmp = path_ + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        if (!dc42Header_.empty()) {
            // Regenerate the data checksum at header +$48. insert() strips the
            // tag block, so tagSize (+$44) and tagChecksum (+$4C) must be
            // zeroed too — leaving the originals declares N tag bytes that are
            // not in the file, which Disk Copy / MAME / Mini vMac read past EOF.
            uint32_t ck = dc42Checksum(image_.data(), image_.size());
            dc42Header_[0x48] = uint8_t(ck >> 24);
            dc42Header_[0x49] = uint8_t(ck >> 16);
            dc42Header_[0x4A] = uint8_t(ck >> 8);
            dc42Header_[0x4B] = uint8_t(ck);
            for (int i = 0x44; i < 0x48; i++) dc42Header_[i] = 0;   // tagSize
            for (int i = 0x4C; i < 0x50; i++) dc42Header_[i] = 0;   // tagChecksum
            out.write(reinterpret_cast<const char*>(dc42Header_.data()),
                      std::streamsize(dc42Header_.size()));
        }
        out.write(reinterpret_cast<const char*>(image_.data()),
                  std::streamsize(image_.size()));
        if (!out) { std::remove(tmp.c_str()); return false; }
    }
    if (!atomicReplaceFile(tmp, path_)) {
        std::remove(tmp.c_str());
        return false;
    }
    dirty_ = false;
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
// engine commits through commitFlux, GCR included).
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
        case 0x6: return switched_;                  // DSKCHG latch (MAME reg 3
                                                     // !m_dskchg: eject raises,
                                                     // insert/strobe clear)
        case 0x7: {                                  // TACH: 120 edges/rev
            // MAME floppy.cpp:3293-3301 (wpt_r 0xb): the tach only runs
            // with media in and the spindle on; otherwise the line is low
            // (same gate senseSwim reg 0xB already had).
            if (!hasDisk() || !motorOn_) return false;
            // Same machine-clock rule as senseSwim reg B (2026-08-05):
            // spin_ counts whatever cycles the platform ticks, and
            // spinCyclesPerRev() knows both the drive clock and the
            // per-cylinder GCR speed group. The old hardcoded 7833600
            // read 2x fast on every C15M host of the IWM personality.
            const int64_t cyclesPerRev = spinCyclesPerRev();
            const int64_t phase = (spin_ % cyclesPerRev) * 120 / cyclesPerRev;
            return phase & 1;
        }
        case 0x8:                                    // RDDATA0 (MAME reg 0x4)
        case 0x9: {                                  // RDDATA1 (MAME reg 0xC)
            // MAME floppy.cpp:3269-3271 (wpt_r case 0x4/0xc):
            //   !m_has_mfm ? false : (!m_image || m_mon) ? true : !m_idx.
            // The idle-high level on a SuperDrive is the rd1=1 of the
            // documented capability signature x011 (floppy.cpp:3229-3235);
            // pinning it to 0 made an empty MFD-75W sign f..c = 1110,
            // i.e. an HD-20. Spinning: one ~2 ms active-low index pulse
            // per revolution, same law as senseSwim reg 0x4/0xC.
            if (!superDrive_) return false;
            if (!hasDisk() || !motorOn_) return true;
            const int64_t rev = spinCyclesPerRev();
            return (spin_ % rev) > spinClockHz_ / 500;
        }
        case 0xA: return superDrive_;                // SUPERDRIVE (1 = HD-capable)
        case 0xB: return mfmMode_;                   // MFM mode
        case 0xC: return true;                       // SIDES: the MECHANISM,
            // not the media — MAME wpt_r case 6 returns m_sides == 2, a
            // drive constant (floppy.cpp:3278-3279); MFD-51W and MFD-75W
            // are both two-head drives (:3461-3491). 400K media still
            // signs itself via the address-field format byte $02.
        case 0xD: return !driveReady();              // READY (0 = ready)
        case 0xE: return false;                      // INSTALLED (0 = present)
        case 0xF:                                    // 2M (MAME reg 0xF, is_2m)
            // SuperDrive: mfd75w::is_2m (floppy.cpp:3494-3503) — 1 only
            // with DD media inserted; empty drive reads 0 (the x of the
            // x011 signature). 800K drive: mfd51w::is_2m — always 1.
            return superDrive_ ? (hasDisk() && !hd_) : true;
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
    case 0x3: return switched_;                  // !m_dskchg: the change
        // LATCH, not presence — raised by eject (floppy.cpp:723), cleared
        // by insert (:672-673) and by the DskchgClear strobe (:3377-3379,
        // unconditionally, media present or not).
    case 0x4: case 0xC: {                        // index/read-data
        // MAME reports high for a SuperDrive while stopped/no media, which
        // forms the documented initial capability signature x011 (2M,
        // ready, MFM, RD1). Once spinning, one narrow active-low index
        // pulse per revolution — on the DRIVE'S clock and speed. The old
        // `spin_ % (7833600/5)` hardcoded the Plus clock and 300 RPM while
        // spin_ counts MACHINE cycles, so every speed a guest measured off
        // this line on a 15.7/25/33 MHz host was off by that clock ratio.
        // (A flux-rate variant was tried here on 2026-08-05 and REVERTED:
        // the symptom that motivated it — 7.5.5 refusing a hot-inserted
        // floppy — turned out to be a false observable in the probe, not
        // emulator behaviour. MAME's per-rev pulse stands.)
        if (!superDrive_) return false;
        if (!hasDisk() || !motorOn_) return true;
        const int64_t rev = spinCyclesPerRev();
        return (spin_ % rev) > spinClockHz_ / 500;   // ~2 ms low pulse
    }
    case 0x5: return superDrive_;                // MFD-75W capability
    case 0x6: return true;                       // DoubleSide: mechanism
        // constant (MAME m_sides == 2, floppy.cpp:3278-3279) — see the
        // classic table's case 0xC.
    case 0x7: return false;                      // drive exists
    case 0x8: return !hasDisk();
    case 0x9: return !writeProtected_;
    case 0xA: return track_ != 0;
    case 0xB: {                                  // 120 tach inversions/rev
        // Same wrong-clock bug as the index pulse above: 7833600 was the
        // Plus clock, spin_ counts machine cycles. spinCyclesPerRev() also
        // tracks the GCR speed group of the current cylinder (rpmNow).
        if (!hasDisk() || !motorOn_) return false;
        const int64_t cyclesPerRev = spinCyclesPerRev();
        return ((spin_ % cyclesPerRev) * 120 / cyclesPerRev) & 1;
    }
    case 0xD: return mfmMode_;
    case 0xE: return !hasDisk() || !driveReady(); // NoReady (active high)
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
    case 0x9: commandMfmMode(true); break;       // MFM on (floppy.cpp:3369-3375)
    case 0xC: switched_ = false; break;          // DskchgClear (floppy.cpp:
                                                 // 3377-3379, unconditional)
    case 0xD: commandMfmMode(false); break;      // GCR on, NO media guard
                                                 // (floppy.cpp:3382-3386)
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
        case 0b001:                                  // (CA1,CA0,SEL) = 001
            // CA2=1 → MAME reg 0xC "DskchgClear" (seek_phase_w,
            // floppy.cpp:3379-3382); CA2=0 → reg 0x8, unassigned.
            // Clearing disk-changed must NOT touch the MFM mode: the
            // old fused decode flipped a SuperDrive into MFM on every
            // disk-change ack.
            if (ca2) switched_ = false;
            break;
        case 0b011:                                  // (CA1,CA0,SEL) = 011
            // CA2=0 → MAME reg 0x9 "MFMModeOn" (floppy.cpp:3369-3375,
            // gated on m_has_mfm); CA2=1 → reg 0xD "GCRModeOn"
            // (floppy.cpp:3382-3386, NO guard — HD media obeys too). The
            // old code had GCR-on parked on 0b101 (MAME reg 0xA/0xE, both
            // unassigned). commandMfmMode() is the MAME-exact strobe table
            // and carries the re-encode.
            commandMfmMode(!ca2);
            break;
        default: break;
    }
}

// Spin-up is NOT instant on the READY line. MAME reports the drive ready
// only after two index pulses (floppy.cpp:888-891, armed at :825), so a
// Sony at 394 RPM answers ~0.25 s after the motor command — POM68K used to
// answer the same cycle. The counter decrements on index CROSSINGS rather
// than on elapsed time, which is what makes the first one land less than a
// revolution in: the head is wherever the last access left it. Index pulses
// do not need media (MAME's m_image test at :873 only refines the hole
// position for hard-sectored disks), so an empty spinning drive becomes
// ready too, exactly as before this counter existed.
//
// spinCyclesPerRev() moves with the GCR speed zone, so `spin_ / rev` is not
// stable across a seek — the same approximation the rotation angle beside
// it already makes, and a seek during the 2 revolutions of spin-up only
// shifts which cycle READY arrives on.
void SonyDrive::tick(int cpuCycles) {
    cycles_ += cpuCycles;                            // free-running (sound stamps)
    if (!motorOn_) return;
    const int64_t rev = spinCyclesPerRev();
    const int64_t before = spin_;
    spin_ += cpuCycles;
    if (readyCounter_ > 0 && rev > 0) {
        const int64_t crossed = spin_ / rev - before / rev;
        if (crossed > 0)
            readyCounter_ = int(std::max<int64_t>(0, readyCounter_ - crossed));
    }
}

// The READY line itself: spindle turning AND the spin-up delay elapsed.
bool SonyDrive::driveReady() const {
    return motorOn_ && readyCounter_ == 0;
}

// Emulated-time stamp for the sound sink, in microseconds — integer
// split so precision holds over hours of runtime.
uint64_t SonyDrive::soundMicros() const {
    const int64_t hz = spinClockHz_ > 0 ? spinClockHz_ : 7833600;
    return uint64_t(cycles_ / hz) * 1000000ull +
           uint64_t((cycles_ % hz) * 1000000 / hz);
}

// MAME floppy_image_device::mon_w (floppy.cpp:818-827): starting the
// spindle arms a two-index READY delay; stopping it drops ready at once.
void SonyDrive::setMotorState(bool on) {
    if (on == motorOn_) return;
    motorOn_ = on;
    readyCounter_ = 2;
    if (sound_) sound_->motor(on, hasDisk());
}
