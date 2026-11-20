// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// A minimal zip WRITER for the ROM gates: they build the archives a server
// would serve (and the broken ones it must survive) in memory, so no gate
// carries a binary fixture file. Members are stored, or carry a DEFLATE
// payload the caller already holds (tests/zip_reader_test.cpp embeds zlib's
// own output for that). [B] PKWARE APPNOTE.TXT 6.3.10 § 4.3.

#pragma once

#include "Crc32.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace testzip {

struct Member {
    std::string name;
    std::vector<std::uint8_t> data;        // what is stored in the archive
    std::uint16_t method = 0;              // 0 stored, 8 deflate (data = raw stream)
    std::uint32_t size = 0;                // uncompressed size; 0 → data.size()
    std::uint32_t crc = 0;                 // 0 → CRC-32 of data (stored members)
    bool overrideCrc = false;              // keep `crc` even when 0
};

inline void put16(std::vector<std::uint8_t>& v, std::uint32_t x) {
    v.push_back(std::uint8_t(x)); v.push_back(std::uint8_t(x >> 8));
}
inline void put32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    put16(v, x & 0xFFFF); put16(v, x >> 16);
}

inline std::vector<std::uint8_t> make(const std::vector<Member>& members) {
    std::vector<std::uint8_t> z, cd;
    for (const Member& m : members) {
        const std::uint32_t size = m.size ? m.size : std::uint32_t(m.data.size());
        const std::uint32_t crc = (m.crc || m.overrideCrc) ? m.crc : pom68k::crc32(m.data);
        const std::uint32_t offset = std::uint32_t(z.size());
        put32(z, 0x04034B50); put16(z, 20); put16(z, 0); put16(z, m.method);
        put16(z, 0); put16(z, 0); put32(z, crc);
        put32(z, std::uint32_t(m.data.size())); put32(z, size);
        put16(z, std::uint32_t(m.name.size())); put16(z, 0);
        z.insert(z.end(), m.name.begin(), m.name.end());
        z.insert(z.end(), m.data.begin(), m.data.end());

        put32(cd, 0x02014B50); put16(cd, 20); put16(cd, 20); put16(cd, 0);
        put16(cd, m.method); put16(cd, 0); put16(cd, 0); put32(cd, crc);
        put32(cd, std::uint32_t(m.data.size())); put32(cd, size);
        put16(cd, std::uint32_t(m.name.size())); put16(cd, 0); put16(cd, 0);
        put16(cd, 0); put16(cd, 0); put32(cd, 0); put32(cd, offset);
        cd.insert(cd.end(), m.name.begin(), m.name.end());
    }
    const std::uint32_t cdOffset = std::uint32_t(z.size());
    z.insert(z.end(), cd.begin(), cd.end());
    put32(z, 0x06054B50); put16(z, 0); put16(z, 0);
    put16(z, std::uint32_t(members.size())); put16(z, std::uint32_t(members.size()));
    put32(z, std::uint32_t(cd.size())); put32(z, cdOffset); put16(z, 0);
    return z;
}

inline Member stored(std::string name, std::vector<std::uint8_t> data) {
    Member m;
    m.name = std::move(name);
    m.data = std::move(data);
    return m;
}

inline void writeFile(const std::filesystem::path& p, const std::vector<std::uint8_t>& b) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary)
        .write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
}

inline std::vector<std::uint8_t> readFile(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

// A fresh, empty directory under the system temp dir, removed by the
// destructor.
struct TempDir {
    std::filesystem::path path;
    explicit TempDir(const std::string& tag) {
        namespace fs = std::filesystem;
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() / ("pom68k-" + tag + "-" + std::to_string(tick));
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::permissions(path, std::filesystem::perms::owner_all,
                                     std::filesystem::perm_options::add, ec);
        std::filesystem::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

} // namespace testzip
