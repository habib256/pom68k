// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Gate asset preamble — what a gate actually opened, printed before it boots.
//
// The 2026-08-06 IIfx incident cost two wrong "code regression" diagnoses
// because nothing in the gate's own output said WHICH image it opened or what
// state that image was in. hdv/MacOS-7.6-boot.vhd had been corrupted by a
// bring-up session (the GUI attaches with writeBack=true; gates never do), and
// its drVolAtrb had bit 8 clear. Both facts existed, and both were reachable
// only by hand, afterwards. Printing them up front makes a red gate say for
// itself whether the fixture moved.
//
// This is deliberately the cheap half of the fix. The assets stay
// user-provided and gitignored; the digest printed here is exactly what an
// assets.lock would pin, and it is emitted whether the gate passes or fails —
// a green run records the fingerprint that made it green.
//
// Reading the output:
//   ASSET disk "hdv/MacOS-8.1-boot.vhd" 314621952 B sha256 ea4f…  HFS
//     "Mac-8.1-US" drVolAtrb $0100 clean
// drVolAtrb bit 8 set = the volume was unmounted cleanly. Clear is a FLAG, not
// a verdict: hdv/HD20SC.vhd reads $0000 and four green gates boot from it. It
// tells you where to look first when a boot gate goes red — it does not tell
// you the image is broken.

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace testasset {

// ── SHA-256 (FIPS 180-4) ────────────────────────────────────────────────
// Self-contained on purpose: the gates link pom68k_core and nothing else,
// and a hash dependency is not worth a link-line edit on 130 targets.
namespace detail {

struct Sha256 {
    uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    uint64_t bits = 0;
    uint8_t buf[64] = {};
    size_t held = 0;

    static uint32_t ror(uint32_t x, int r) { return (x >> r) | (x << (32 - r)); }

    void compress(const uint8_t* p) {
        static const uint32_t K[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b,
            0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
            0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7,
            0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
            0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152,
            0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
            0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
            0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
            0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
            0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
            0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
            0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };
        uint32_t w[64];
        for (int i = 0; i < 16; i++)
            w[i] = uint32_t(p[i * 4]) << 24 | uint32_t(p[i * 4 + 1]) << 16 |
                   uint32_t(p[i * 4 + 2]) << 8 | uint32_t(p[i * 4 + 3]);
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t S1 = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            uint32_t S0 = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    void update(const uint8_t* p, size_t n) {
        bits += uint64_t(n) * 8;
        if (held) {
            size_t take = 64 - held < n ? 64 - held : n;
            std::memcpy(buf + held, p, take);
            held += take; p += take; n -= take;
            if (held < 64) return;
            compress(buf);
            held = 0;
        }
        while (n >= 64) { compress(p); p += 64; n -= 64; }
        if (n) { std::memcpy(buf, p, n); held = n; }
    }

    std::string hex() {
        uint64_t total = bits;
        uint8_t pad = 0x80;
        update(&pad, 1);
        bits = total;                    // padding is not message length
        uint8_t zero = 0;
        while (held != 56) { update(&zero, 1); bits = total; }
        uint8_t len[8];
        for (int i = 0; i < 8; i++) len[i] = uint8_t(total >> (56 - 8 * i));
        update(len, 8);
        char out[65];
        for (int i = 0; i < 8; i++) std::snprintf(out + i * 8, 9, "%08x", h[i]);
        return std::string(out, 64);
    }
};

}  // namespace detail

// ── Locating a user-provided asset ──────────────────────────────────────
// Same two-base search every gate already open-codes: the CTest working
// directory, then one level up (a gate run from build/ finds the repo root).
inline std::string find(const char* rel) {
    for (const std::string& base : { std::string(), std::string("../") }) {
        std::string p = base + rel;
        if (std::ifstream(p, std::ios::binary)) return p;
    }
    return {};
}

inline std::string findAny(std::initializer_list<const char*> names) {
    for (const char* n : names) {
        std::string p = find(n);
        if (!p.empty()) return p;
    }
    return {};
}

// ── HFS master directory block ──────────────────────────────────────────
struct HfsInfo {
    bool found = false;
    std::string name;                    // drVN, the volume name
    uint16_t atrb = 0;                   // drAtrb; bit 8 = cleanly unmounted
    uint64_t mdbOffset = 0;
    bool cleanlyUnmounted() const { return (atrb & 0x0100) != 0; }
};

namespace detail {

inline uint16_t be16(const uint8_t* p) { return uint16_t(p[0]) << 8 | p[1]; }
inline uint32_t be32(const uint8_t* p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 |
           uint32_t(p[2]) << 8 | p[3];
}

inline bool readMdb(std::ifstream& f, uint64_t off, HfsInfo& out) {
    uint8_t mdb[512];
    f.clear();
    f.seekg(std::streamoff(off), std::ios::beg);
    if (!f.read(reinterpret_cast<char*>(mdb), 512)) return false;
    if (be16(mdb) != 0x4244) return false;               // 'BD'
    out.found = true;
    out.atrb = be16(mdb + 10);
    out.mdbOffset = off;
    size_t n = mdb[36];                                  // drVN, Str27
    if (n > 27) n = 27;
    out.name.assign(reinterpret_cast<const char*>(mdb) + 37, n);
    return true;
}

}  // namespace detail

// Locate the boot volume's MDB: through the Apple Partition Map when the
// image carries one ('ER' at block 0 — every .vhd here), else at the flat
// offset 1024 (bare HFS, which is what the .dsk floppies are).
inline HfsInfo probeHfs(const std::string& path) {
    HfsInfo info;
    std::ifstream f(path, std::ios::binary);
    if (!f) return info;

    uint8_t blk[512];
    if (f.read(reinterpret_cast<char*>(blk), 512) && detail::be16(blk) == 0x4552) {
        uint32_t bs = detail::be16(blk + 2);              // sbBlkSize
        if (bs != 512 && bs != 1024 && bs != 2048) bs = 512;
        for (int i = 1; i <= 64; i++) {
            f.clear();
            f.seekg(std::streamoff(i) * 512, std::ios::beg);
            if (!f.read(reinterpret_cast<char*>(blk), 512)) break;
            if (detail::be16(blk) != 0x504D) break;        // 'PM'
            char type[33] = {};
            std::memcpy(type, blk + 48, 32);
            if (std::strcmp(type, "Apple_HFS") != 0) continue;
            uint64_t start = uint64_t(detail::be32(blk + 8)) * bs;
            if (detail::readMdb(f, start + 1024, info)) return info;
        }
    }
    detail::readMdb(f, 1024, info);
    return info;
}

// ── The preamble itself ─────────────────────────────────────────────────
inline uint64_t fileSize(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    return f ? uint64_t(f.tellg()) : 0;
}

inline std::string sha256File(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    detail::Sha256 s;
    std::vector<char> chunk(1 << 20);
    while (f.read(chunk.data(), std::streamsize(chunk.size())) || f.gcount())
        s.update(reinterpret_cast<const uint8_t*>(chunk.data()), size_t(f.gcount()));
    return s.hex();
}

// What kind of asset a path is, from the path alone. Gates then need a single
// uniform call — `testasset::report({ rom, img })` — instead of each one
// inventing its own labels for the same two things.
inline const char* roleOf(const std::string& path) {
    auto has = [&](const char* s) { return path.find(s) != std::string::npos; };
    auto endsWith = [&](const char* s) {
        size_t n = std::strlen(s);
        return path.size() >= n && path.compare(path.size() - n, n, s) == 0;
    };
    if (has("roms/")) return "rom";
    if (endsWith(".iso") || endsWith(".toast") || endsWith(".cue")) return "cd";
    if (has("disks35/")) return "floppy";
    return "disk";
}

// One line per asset, on stdout so CTest captures it with the rest of the
// gate's output. `role` is free-form and short: "rom", "disk", "floppy",
// "cd", "pram".
inline void report(const char* role, const std::string& path) {
    if (path.empty()) {
        std::printf("ASSET %-6s (absent)\n", role);
        std::fflush(stdout);
        return;
    }
    uint64_t size = fileSize(path);
    std::string digest = sha256File(path);
    std::printf("ASSET %-6s \"%s\" %llu B sha256 %s", role, path.c_str(),
                static_cast<unsigned long long>(size), digest.c_str());
    HfsInfo hfs = probeHfs(path);
    if (hfs.found)
        std::printf("  HFS \"%s\" drVolAtrb $%04X %s", hfs.name.c_str(), hfs.atrb,
                    hfs.cleanlyUnmounted() ? "clean"
                                           : "bit 8 clear: not cleanly unmounted");
    std::printf("\n");
    std::fflush(stdout);
}

inline void report(std::initializer_list<std::pair<const char*, std::string>> assets) {
    for (const auto& a : assets) report(a.first, a.second);
}

// The form the gates use: every asset the gate is about to open, labelled by
// roleOf(). Assets the gate resolved but did not find are skipped silently —
// the gate's own SKIP message already covers those.
inline void report(std::initializer_list<std::string> paths) {
    for (const std::string& p : paths)
        if (!p.empty()) report(roleOf(p), p);
}

}  // namespace testasset
