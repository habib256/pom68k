// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
#pragma once
#include "DartLzh.h"
#include <algorithm>
#include <array>
#include <ostream>
#include <vector>

// Apple DART data fork. Reference: CiderPress2 Disk/DART.cs and DART-notes.md;
// Snow floppy/src/loaders/dart.rs. Lisa/Apple II/DOS types are not Mac media.
namespace dart {
inline uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
inline bool candidate(const std::vector<uint8_t>& raw) {
    if (raw.size() < 4 || raw[0] > 2) return false;
    const auto size = be16(raw.data() + 2);
    return size == 400 || size == 800 || size == 1440;
}
inline bool unpack(std::vector<uint8_t>& raw, std::vector<uint8_t>& tags) {
    if (!candidate(raw)) return false;
    const size_t kib = be16(raw.data() + 2), chunks = kib / 20;
    if (!((raw[1] == 1 && (kib == 400 || kib == 800)) ||
          (raw[1] == 16 && kib == 1440))) return false;
    const size_t entries = kib == 1440 ? 72 : 40, prefix = 4 + entries * 2;
    if (raw.size() < prefix) return false;
    std::vector<uint8_t> data, decodedTags;
    data.reserve(kib * 1024); decodedTags.reserve(chunks * 480);
    size_t offset = prefix;
    for (size_t i = 0; i < entries; ++i) {
        const size_t length = be16(raw.data() + 4 + i * 2);
        if (i >= chunks) { if (length) return false; continue; }
        if (!length) return false;
        const bool stored = length == 0xffff || (raw[0] == 2 && length == 20960);
        if (raw[0] == 2 && !stored) return false;
        const size_t bytes = stored ? 20960 : raw[0] == 0 ? length * 2 : length;
        if (bytes > raw.size() - offset) return false;
        std::array<uint8_t, 20960> block{};
        if (stored) std::copy_n(raw.data() + offset, block.size(), block.data());
        else if (raw[0] == 1) {
            if (!DartLzh({raw.data() + offset, bytes}).expand(block)) return false;
        } else {
            size_t read = offset, written = 0;
            while (read < offset + bytes) {
                if (offset + bytes - read < 2) return false;
                const uint16_t word = be16(raw.data() + read); read += 2;
                const int count = word < 0x8000 ? int(word) : int(word) - 65536;
                const size_t amount = size_t(count < 0 ? -count : count) * 2;
                if (!count || amount > block.size() - written) return false;
                if (count > 0) {
                    if (amount > offset + bytes - read) return false;
                    std::copy_n(raw.data() + read, amount, block.data() + written);
                    read += amount;
                } else {
                    if (offset + bytes - read < 2) return false;
                    for (size_t j = 0; j < amount; j += 2) {
                        block[written + j] = raw[read];
                        block[written + j + 1] = raw[read + 1];
                    }
                    read += 2;
                }
                written += amount;
            }
            if (written != block.size()) return false;
        }
        data.insert(data.end(), block.begin(), block.begin() + 20480);
        if (kib == 1440 && std::any_of(block.begin() + 20480, block.end(),
            [](uint8_t byte) { return byte != 0; })) return false;
        decodedTags.insert(decodedTags.end(), block.begin() + 20480, block.end());
        offset += bytes;
    }
    if (offset != raw.size()) return false;
    raw.swap(data);
    if (kib != 1440) tags.swap(decodedTags); // MFM has no physical GCR tags.
    else tags.clear();
    return true;
}
// Keep the archive type on write-back. Fast-mode raw chunks (ffff) are a
// historical DART representation, avoiding a lossy conversion to raw/DC42.
inline bool write(std::ostream& out, const std::vector<uint8_t>& data,
                  const std::vector<uint8_t>& tags) {
    const size_t kib = data.size() / 1024;
    if ((kib != 400 && kib != 800 && kib != 1440) || data.size() != kib * 1024 ||
        (kib != 1440 && tags.size() != data.size() / 512 * 12) ||
        (kib == 1440 && !tags.empty())) return false;
    const size_t chunks = kib / 20, entries = kib == 1440 ? 72 : 40;
    std::vector<uint8_t> header(4 + entries * 2, 0);
    header[1] = kib == 1440 ? 16 : 1;
    header[2] = uint8_t(kib >> 8); header[3] = uint8_t(kib);
    std::fill(header.begin() + 4, header.begin() + 4 + chunks * 2, 0xff);
    out.write(reinterpret_cast<const char*>(header.data()), header.size());
    const std::array<uint8_t, 480> zeroTags{};
    for (size_t i = 0; i < chunks; ++i) {
        out.write(reinterpret_cast<const char*>(data.data() + i * 20480), 20480);
        out.write(reinterpret_cast<const char*>(kib == 1440 ? zeroTags.data() :
                                               tags.data() + i * 480), 480);
    }
    return bool(out);
}
} // namespace dart
