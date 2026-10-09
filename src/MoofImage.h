// POM68K — Applesauce MOOF 1.x bitstream/flux import, not sector reconstruction.
// Specification: https://applesaucefdc.com/moof-reference/
#pragma once
#include "FloppyTrackMedium.h"
#include <cstring>
#include <span>
#include <ostream>

namespace moof {
inline constexpr size_t kMaxBytes = 32u << 20;
inline uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
inline uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | uint16_t(p[1]) << 8); }
inline uint32_t crc(std::span<const uint8_t> bytes) {
    uint32_t value = 0xffffffff;
    for (uint8_t byte : bytes) {
        value ^= byte;
        for (int bit = 0; bit < 8; ++bit) value = (value >> 1) ^ (0xedb88320u & (0u - (value & 1)));
    }
    return ~value;
}
inline bool candidate(std::span<const uint8_t> bytes) {
    return bytes.size() >= 4 && !std::memcmp(bytes.data(), "MOOF", 4);
}
// 125 ns in C15M/1024 flux ticks; keep the fractional part until each
// absolute position is converted, never accumulate rounded intervals.
inline int64_t ticks(uint64_t units) { return int64_t(units * 20054016 / 10000); }
struct Image {
    floppy::TrackMedium medium;
    uint8_t type = 0;
    bool protectedDisk = false;
};
inline bool unpack(std::span<const uint8_t> bytes, Image& result) {
    if (bytes.size() < 80 || bytes.size() > kMaxBytes ||
        std::memcmp(bytes.data(), "MOOF\xff\x0a\x0d\x0a", 8)) return false;
    const uint32_t checksum = le32(bytes.data() + 8);
    if (checksum && checksum != crc(bytes.subspan(12))) return false;
    std::span<const uint8_t> info, map, descriptors, flux;
    size_t dataStart = 0, dataEnd = 0, fluxOffset = 0;
    for (size_t offset = 12; offset < bytes.size();) {
        if (bytes.size() - offset < 8) return false;
        const uint8_t* header = bytes.data() + offset;
        const size_t length = le32(header + 4);
        if (length > bytes.size() - offset - 8) return false;
        auto chunk = bytes.subspan(offset + 8, length);
        auto assign = [&](std::span<const uint8_t>& target, size_t minimum) {
            if (!target.empty() || length < minimum) return false;
            target = chunk; return true;
        };
        if (!std::memcmp(header, "INFO", 4)) {
            if (offset != 12 || length != 60 || !assign(info, 60)) return false;
        } else if (!std::memcmp(header, "TMAP", 4)) {
            if (length != 160 || !assign(map, 160)) return false;
        } else if (!std::memcmp(header, "TRKS", 4)) {
            if (!assign(descriptors, 1280)) return false;
            dataStart = offset + 8 + 1280; dataEnd = offset + 8 + length;
        } else if (!std::memcmp(header, "FLUX", 4)) {
            if (length != 160 || !assign(flux, 160)) return false;
            fluxOffset = offset;
        }
        offset += 8 + length;
    }
    if (info.empty() || map.empty() || descriptors.empty() || info[0] < 1 ||
        info[1] < 1 || info[1] > 3 || info[2] > 1 || info[3] > 1 || !info[4]) return false;
    const bool hasFlux = le16(info.data() + 40) && le16(info.data() + 42);
    if (hasFlux && (flux.empty() || size_t(le16(info.data() + 40)) * 512 != fluxOffset)) return false;
    if (!hasFlux && (!flux.empty() || le16(info.data() + 40) || le16(info.data() + 42))) return false;
    Image decoded;
    decoded.type = info[1]; decoded.protectedDisk = info[2]; decoded.medium.native = true;
    size_t totalEdges = 0;
    for (size_t physical = 0; physical < 160; ++physical) {
        const bool isFlux = hasFlux && flux[physical] != 255;
        const uint8_t entry = isFlux ? flux[physical] : map[physical];
        if (info[1] == 1 && (physical & 1) && entry != 255) return false;
        if (entry == 255) continue;
        if (entry >= 160) return false;
        const auto* descriptor = descriptors.data() + size_t(entry) * 8;
        const size_t begin = size_t(le16(descriptor)) * 512;
        const size_t size = size_t(le16(descriptor + 2)) * 512;
        const uint32_t count = le32(descriptor + 4);
        const uint64_t required = isFlux ? count : (uint64_t(count) + 7) / 8;
        if (!count || !size || required > size || begin < dataStart ||
            begin > dataEnd || size > dataEnd - begin ||
            count > (isFlux ? 500000u : 8000000u)) return false;
        auto& track = decoded.medium.tracks[physical];
        track.present = true; track.decodeClock = ticks(info[4]);
        if (isFlux) {
            uint64_t elapsed = 0, interval = 0;
            for (size_t i = 0; i < count; ++i) {
                const uint8_t delta = bytes[begin + i];
                interval += delta;
                if (delta == 255) continue;
                if (!interval) return false;
                elapsed += interval; interval = 0;
                if (elapsed > 8000000) return false; // at most one second per turn
                track.edges.push_back(ticks(elapsed));
            }
            // An incomplete continuation is not a final transition.
            if (interval || !elapsed || track.edges.empty()) return false;
            track.revolution = ticks(elapsed);
            track.edges.back() = 0; // final transition is the next index origin
            std::rotate(track.edges.begin(), track.edges.end() - 1, track.edges.end());
        } else {
            if (uint64_t(count) * info[4] > 8000000) return false;
            track.revolution = ticks(uint64_t(count) * info[4]);
            for (uint32_t bit = 0; bit < count; ++bit)
                if (bytes[begin + bit / 8] & (0x80 >> (bit & 7)))
                    track.edges.push_back(ticks(uint64_t(bit) * info[4]));
        }
        totalEdges += track.edges.size();
        if (totalEdges > 20000000) return false;
    }
    result = std::move(decoded);
    return true;
}
inline void put32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    for (int i = 0; i < 4; ++i) bytes[offset + size_t(i)] = uint8_t(value >> (8 * i));
}
inline void put16(std::vector<uint8_t>& bytes, size_t offset, uint16_t value) {
    bytes[offset] = uint8_t(value); bytes[offset + 1] = uint8_t(value >> 8);
}
// Encode transitions on MOOF's finest legal 125 ns bit grid. This also
// retains flux-only tracks without inventing an index transition or losing
// cross-track phase. Guest writes round to that format's resolution; original
// MOOF transitions already lie on it. Sector headers are never regenerated.
inline bool write(std::ostream& out, const floppy::TrackMedium& medium,
                  const std::vector<uint8_t>& original, bool protectedDisk) {
    if (!medium.native || original.size() < 80) return false;
    std::vector<uint8_t> bytes(original.begin(), original.begin() + 80);
    bytes[22] = uint8_t(protectedDisk); bytes[24] = 1;
    std::fill(bytes.begin() + 25, bytes.begin() + 57, ' ');
    constexpr char creator[] = "POM68K native tracks";
    std::memcpy(bytes.data() + 25, creator, sizeof(creator) - 1);
    put16(bytes, 60, 0); put16(bytes, 62, 0);
    auto chunk = [&](const char* id, uint32_t size) {
        const size_t begin = bytes.size(); bytes.resize(begin + 8 + size, 0);
        std::memcpy(bytes.data() + begin, id, 4); put32(bytes, begin + 4, size);
        return begin + 8;
    };
    const size_t map = chunk("TMAP", 160);
    std::fill(bytes.begin() + map, bytes.end(), 255);
    const size_t trks = chunk("TRKS", 1280);
    uint16_t largest = 0;
    for (size_t i = 0; i < 160; ++i) {
        const auto& track = medium.tracks[i];
        if (!track.present) continue;
        auto units = [](int64_t value) { return uint64_t((value * 10000 + 10027008) / 20054016); };
        if (track.revolution <= 0 || track.revolution > ticks(8000000)) return false;
        const uint64_t count = units(track.revolution);
        const size_t blocks = size_t((count + 4095) / 4096);
        if (!count || bytes.size() + blocks * 512 > kMaxBytes) return false;
        const size_t start = bytes.size(); bytes.resize(start + blocks * 512, 0);
        bytes[map + i] = uint8_t(i);
        put16(bytes, trks + i * 8, uint16_t(start / 512));
        put16(bytes, trks + i * 8 + 2, uint16_t(blocks));
        put32(bytes, trks + i * 8 + 4, uint32_t(count));
        largest = std::max(largest, uint16_t(blocks));
        for (int64_t edge : track.edges) {
            const uint64_t bit = units(edge) % count;
            bytes[start + size_t(bit / 8)] |= uint8_t(0x80 >> (bit & 7));
        }
    }
    put32(bytes, trks - 4, uint32_t(bytes.size() - trks)); put16(bytes, 58, largest);
    // Preserve META and any unknown optional chunks verbatim, in file order.
    for (size_t offset = 80; offset < original.size();) {
        if (original.size() - offset < 8) return false;
        const size_t size = le32(original.data() + offset + 4) + size_t(8);
        if (size > original.size() - offset) return false;
        const auto* id = original.data() + offset;
        if (std::memcmp(id, "TMAP", 4) && std::memcmp(id, "TRKS", 4) && std::memcmp(id, "FLUX", 4)) {
            if (bytes.size() + size > kMaxBytes) return false;
            bytes.insert(bytes.end(), original.begin() + offset, original.begin() + offset + size);
        }
        offset += size;
    }
    put32(bytes, 8, crc(std::span<const uint8_t>(bytes).subspan(12)));
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    return bool(out);
}
} // namespace moof
