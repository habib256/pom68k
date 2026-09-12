// Independent host oracle for the real Finder's two-fork copy. No AFP calls.
#pragma once
#include <algorithm>

namespace afplive {
inline std::vector<uint8_t> pattern(size_t size, uint32_t seed) {
    std::vector<uint8_t> bytes(size);
    for (auto& byte : bytes) {
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        byte = uint8_t(seed);
    }
    return bytes;
}
inline const auto data = pattern(32791, 0x41504631);
inline const auto resource = pattern(8317, 0x52455332);
inline void put32(std::vector<uint8_t>& bytes, uint32_t v) {
    for (int shift = 24; shift >= 0; shift -= 8) bytes.push_back(uint8_t(v >> shift));
}
inline uint32_t get32(const uint8_t* p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
inline bool write(const fs::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    out.close();
    return bool(out);
}
inline std::vector<uint8_t> read(const fs::path& path) {
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (ec || size > 1024 * 1024) return {};
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
inline bool seed(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir / ".AppleDouble", ec);
    std::vector<uint8_t> sidecar;
    put32(sidecar, 0x00051607); put32(sidecar, 0x00020000);
    sidecar.resize(24, 0); sidecar.push_back(0); sidecar.push_back(2);
    put32(sidecar, 9); put32(sidecar, 50); put32(sidecar, 32);
    put32(sidecar, 2); put32(sidecar, 82); put32(sidecar, uint32_t(resource.size()));
    // TEXT/ttxt, no custom icon or bundle flags; Finder copies raw fork bytes.
    sidecar.insert(sidecar.end(), {'T', 'E', 'X', 'T', 't', 't', 'x', 't'});
    sidecar.resize(82, 0);
    sidecar.insert(sidecar.end(), resource.begin(), resource.end());
    if (ec || !write(dir / "BONJOUR.txt", data) ||
        !write(dir / ".AppleDouble" / "BONJOUR.txt", sidecar)) return false;
    // The server reports HOST mtimes to the guest (AfpServer.cpp:473 stats the
    // file, :500-501 send it as creation/modification date), so a freshly
    // seeded share hands the guest different date bytes on every run. A gate
    // that asserts a deterministic trajectory must not consume a
    // nondeterministic input; pin both forks to a fixed instant.
    // Directories carry host mtimes too: the stat in AfpServer.cpp sits in the
    // shared node helper, above the isDir branch, so FPGetFileDirParms reports
    // a folder's date exactly as it reports a file's.
    const auto epoch = fs::file_time_type{};
    fs::last_write_time(dir / "BONJOUR.txt", epoch, ec);
    fs::last_write_time(dir / ".AppleDouble" / "BONJOUR.txt", epoch, ec);
    fs::last_write_time(dir / ".AppleDouble", epoch, ec);
    fs::last_write_time(dir, epoch, ec);
    return true;
}
inline bool exactCopy(const fs::path& path) {
    if (read(path) != data) return false;
    const auto bytes = read(path.parent_path() / ".AppleDouble" / path.filename());
    if (bytes.size() < 26 || get32(bytes.data()) != 0x00051607 ||
        get32(bytes.data() + 4) != 0x00020000) return false;
    const size_t count = size_t(bytes[24]) * 256 + bytes[25];
    bool fork = false, finder = false;
    for (size_t i = 0; i < count; ++i) {
        const size_t entry = 26 + i * 12;
        if (entry + 12 > bytes.size()) return false;
        const auto id = get32(bytes.data() + entry);
        const auto offset = get32(bytes.data() + entry + 4);
        const auto length = get32(bytes.data() + entry + 8);
        if (offset > bytes.size() || length > bytes.size() - offset) return false;
        if (id == 2) fork = length == resource.size() &&
            std::equal(resource.begin(), resource.end(), bytes.begin() + offset);
        if (id == 9) finder = length >= 8 &&
            std::equal(bytes.begin() + offset, bytes.begin() + offset + 8, "TEXTttxt");
    }
    return fork && finder;
}
}
