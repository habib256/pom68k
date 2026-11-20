// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// CRC-32 (IEEE 802.3, reflected, polynomial 0xEDB88320) — the checksum a zip
// member carries and the one MAME prints beside every dump. [B] PKWARE
// APPNOTE.TXT 6.3.10 § 4.4.7. It detects damage; it does not identify a
// file (anyone can collide 32 bits on purpose), which is why the ROM store
// gates every install on SHA-256 and shows the CRC for reading only.
//
// NOT the "checksum" of a Macintosh ROM: that is the big-endian long in the
// image's first four bytes (MachineCatalog `romCrc32`, despite the name).

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace pom68k {

namespace detail {
inline constexpr std::array<std::uint32_t, 256> kCrc32Table = [] {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k)
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        t[i] = c;
    }
    return t;
}();
} // namespace detail

// Running form: start at 0, feed chunks, the return value is the CRC so far.
inline std::uint32_t crc32Update(std::uint32_t crc, const std::uint8_t* p,
                                 std::size_t n) {
    std::uint32_t c = crc ^ 0xFFFFFFFFu;
    while (n--) c = detail::kCrc32Table[(c ^ *p++) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

inline std::uint32_t crc32(const std::uint8_t* p, std::size_t n) {
    return crc32Update(0, p, n);
}

inline std::uint32_t crc32(const std::vector<std::uint8_t>& bytes) {
    return crc32(bytes.data(), bytes.size());
}

} // namespace pom68k
