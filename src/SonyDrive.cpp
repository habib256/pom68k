// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Sense/command tables and GCR encoder ported from MAME (floppy.cpp
// mac_floppy_device, flopimg.cpp build_mac_track_gcr, ap_dsk35.cpp),
// cross-checked against pce gcr-mac.c and Snow drive.rs — see DEV.md § Sony.

#include "SonyDrive.h"
#include <cstdio>
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

// First nibble packs the three high 2-bit fragments (MAME gcr6_encode)
inline void gcr6Encode(std::vector<uint8_t>& out, uint8_t va, uint8_t vb,
                       uint8_t vc, bool lastGroup) {
    out.push_back(kGcr6[((va >> 2) & 0x30) | ((vb >> 4) & 0x0c) | ((vc >> 6) & 0x03)]);
    out.push_back(kGcr6[va & 0x3f]);
    out.push_back(kGcr6[vb & 0x3f]);
    if (!lastGroup) out.push_back(kGcr6[vc & 0x3f]);
}

}  // namespace

int SonyDrive::sectorsInTrack(int track) { return 12 - (track >> 4); }

void SonyDrive::reset() {
    track_ = 0;
    gcrPos_ = 0;
    motorOn_ = false;
    dirToZero_ = false;
    switched_ = false;
    if (hasDisk()) encodeTrack();
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
    if (data.size() != 819200 && data.size() != 409600) return false;
    doubleSided_ = (data.size() == 819200);
    image_ = std::move(data);
    track_ = 0;
    encodeTrack();
    return true;
}

// Raw image ordering: cylinder-major, then head, then sector (MAME
// apple_gcr_format::load).
size_t SonyDrive::imageOffset(int track, int side, int sector) const {
    size_t off = 0;
    for (int t = 0; t < track; t++)
        off += size_t(sectorsInTrack(t)) * (doubleSided_ ? 2 : 1) * 512;
    if (side) off += size_t(sectorsInTrack(track)) * 512;
    return off + size_t(sector) * 512;
}

// GCR-encode the current (track, side) as a byte-level nibble stream.
// Self-sync groups are emitted as plain 0xFF bytes: the IWM model delivers
// whole nibbles, so 10-bit sync framing is not needed (Plus Too approach).
void SonyDrive::encodeTrack() {
    gcr_.clear();
    if (!hasDisk()) return;
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
        for (int i = 0; i < 38; i++) gcr_.push_back(0xFF);
        gcr_.push_back(0xD5); gcr_.push_back(0xAA); gcr_.push_back(0x96);
        gcr_.push_back(kGcr6[track_ & 0x3F]);
        gcr_.push_back(kGcr6[sector & 0x3F]);
        gcr_.push_back(kGcr6[sideByte & 0x3F]);
        gcr_.push_back(kGcr6[format & 0x3F]);
        gcr_.push_back(kGcr6[(track_ ^ sector ^ sideByte ^ format) & 0x3F]);
        gcr_.push_back(0xDE); gcr_.push_back(0xAA); gcr_.push_back(0xFF);

        // Data field: 12 tag bytes (zero) + 512 data bytes, 175 groups of 3
        for (int i = 0; i < 6; i++) gcr_.push_back(0xFF);
        gcr_.push_back(0xD5); gcr_.push_back(0xAA); gcr_.push_back(0xAD);
        gcr_.push_back(kGcr6[sector & 0x3F]);

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

            gcr6Encode(gcr_, va, vb, vc, i == 174);
        }
        gcr6Encode(gcr_, ca, cb, cc, false);         // 4 checksum nibbles
        gcr_.push_back(0xDE); gcr_.push_back(0xAA);
        gcr_.push_back(0xFF); gcr_.push_back(0xFF);
    }
    if (gcrPos_ >= gcr_.size()) gcrPos_ = 0;
}

uint8_t SonyDrive::nextNibble(bool side1) {
    nibblesRead++;
    if (!hasDisk()) return 0xFF;
    if (side1 != side1_) {                           // head select via SEL
        side1_ = side1;
        encodeTrack();
    }
    uint8_t v = gcr_[gcrPos_];
    gcrPos_ = (gcrPos_ + 1) % gcr_.size();
    return v;
}

// Sense table for an 800K MFD-51W (DEV.md § Sony; MAME/pce/Snow-verified).
// addr = (CA2<<3)|(CA1<<2)|(CA0<<1)|SEL, returned on IWM status bit 7.
bool SonyDrive::sense(int addr) const {
    switch (addr) {
        case 0x0: return dirToZero_;                 // DIRTN (1 = toward 0)
        case 0x1: return !hasDisk();                 // CSTIN (0 = inserted)
        case 0x2: return true;                       // STEP complete
        case 0x3: return false;                      // WRTPRT (0 = protected)
        case 0x4: return !motorOn_;                  // MOTORON (0 = on)
        case 0x5: return track_ != 0;                // TKO (0 = track 0)
        case 0x6: return switched_;                  // SWITCHED / disk changed
        case 0x7: {                                  // TACH: 120 edges/rev,
            // time-based — the ROM measures the spindle speed against the
            // VIA timer even while the IWM is not streaming data
            static const int kRpm[5] = { 394, 429, 472, 525, 590 };
            int64_t cyclesPerRev = 7833600LL * 60 / kRpm[track_ >> 4];
            int64_t phase = (spin_ % cyclesPerRev) * 120 / cyclesPerRev;
            return phase & 1;
        }
        case 0x8: return false;                      // RDDATA0
        case 0x9: return false;                      // RDDATA1
        case 0xA: return false;                      // SUPERDRIVE (0 = GCR)
        case 0xB: return false;                      // MFM mode
        case 0xC: return doubleSided_;               // SIDES (1 = DS)
        case 0xD: return !motorOn_;                  // READY (0 = ready)
        case 0xE: return false;                      // INSTALLED (0 = present)
        case 0xF: return true;                       // REVISED (800K = 1)
    }
    return true;
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
            }
            break;
        case 0b100: motorOn_ = !ca2; break;          // MOTOR ON / OFF
        case 0b110:                                  // EJECT (ca2=1)
            if (ca2) { image_.clear(); gcr_.clear(); switched_ = true; }
            break;
        case 0b001:                                  // CLRSWITCHED (ca2=1)
            if (ca2) switched_ = false;
            break;
        default: break;
    }
}

void SonyDrive::tick(int cpuCycles) {
    if (motorOn_) spin_ += cpuCycles;                // spin-up itself is instant
}
