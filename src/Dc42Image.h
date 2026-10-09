// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
#pragma once
#include <algorithm>
#include <cstdint>
#include <ostream>
#include <vector>

// Disk Copy 4.2 stores the physical GCR sector's 12-byte tags separately
// from its 512-byte data. Sector order is track, head, logical sector.
// Snow floppy/src/loaders/diskcopy42.rs retains these for its GCR encoder.
// Format/checksum reference: MAME src/lib/formats/ap_dsk35.cpp (dc42_format).
namespace dc42 {
inline uint32_t be32(const uint8_t* p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
inline void put32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16);
    p[2] = uint8_t(v >> 8); p[3] = uint8_t(v);
}
inline uint32_t checksum(const uint8_t* data, size_t size) {
    uint32_t sum = 0;
    for (size_t i = 0; i + 1 < size; i += 2) {
        sum += uint32_t(data[i]) << 8 | data[i + 1];
        sum = sum >> 1 | sum << 31;
    }
    return sum;
}
inline bool nonzeroTags(const std::vector<uint8_t>& tags) {
    return std::any_of(tags.begin(), tags.end(), [](uint8_t b) { return b != 0; });
}
// Raw sector media pass through. DC42 lengths are checked before any
// iterator arithmetic; truncated or geometrically impossible tags fail.
inline bool unpack(std::vector<uint8_t>& raw, std::vector<uint8_t>& header,
                   std::vector<uint8_t>& tags) {
    header.clear(); tags.clear();
    if (raw.size() < 0x54 || raw[0x52] != 1 || raw[0x53] != 0) return true;
    const auto dataSize = be32(raw.data() + 0x40);
    const auto tagSize = be32(raw.data() + 0x44);
    if (dataSize > 1474560 || uint64_t(0x54) + dataSize + tagSize > raw.size() ||
        (tagSize && ((dataSize != 409600 && dataSize != 819200) ||
                     tagSize != dataSize / 512 * 12))) return false;
    header.assign(raw.begin(), raw.begin() + 0x54);
    tags.assign(raw.begin() + 0x54 + dataSize, raw.begin() + 0x54 + dataSize + tagSize);
    std::vector<uint8_t> data(raw.begin() + 0x54, raw.begin() + 0x54 + dataSize);
    raw.swap(data);
    return true;
}
inline bool write(std::ostream& out, std::vector<uint8_t>& header,
                  const std::vector<uint8_t>& data, const std::vector<uint8_t>& tags) {
    if (header.size() != 0x54) return false;
    const size_t tagSize = be32(header.data() + 0x44) || nonzeroTags(tags) ? tags.size() : 0;
    put32(header.data() + 0x40, uint32_t(data.size()));
    put32(header.data() + 0x44, uint32_t(tagSize));
    put32(header.data() + 0x48, checksum(data.data(), data.size()));
    // Disk Copy excludes the first sector's twelve tag bytes, including
    // when they are nonzero. MAME's writer preserves this compatibility.
    put32(header.data() + 0x4c, tagSize > 12 ? checksum(tags.data() + 12, tagSize - 12) : 0);
    out.write(reinterpret_cast<const char*>(header.data()), header.size());
    out.write(reinterpret_cast<const char*>(data.data()), data.size());
    if (tagSize) out.write(reinterpret_cast<const char*>(tags.data()), tagSize);
    return bool(out);
}
} // namespace dc42
