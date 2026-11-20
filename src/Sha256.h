// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// SHA-256, the identity every user-provided dump is checked against
// (assets.lock, firmware::kManifest, the ROM catalogue). [B] FIPS 180-4
// § 6.2; `asset_fingerprint_test` and `rom_catalog_test` hold it to the
// standard's vectors, padding boundaries included.
//
// Header-only and dependency-free: the strict firmware manifest, the ROM
// store and the gates all hash with this one implementation.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace pom68k {

class Sha256 {
public:
    void add(const std::uint8_t* p, std::size_t n) {
        bits_ += std::uint64_t(n) * 8;
        if (held_) {
            while (n && held_ < 64) { buf_[held_++] = *p++; --n; }
            if (held_ < 64) return;
            block(buf_);
            held_ = 0;
        }
        for (; n >= 64; n -= 64, p += 64) block(p);
        while (n--) buf_[held_++] = *p++;
    }
    void add(const std::vector<std::uint8_t>& bytes) {
        add(bytes.data(), bytes.size());
    }

    // Lowercase hexadecimal digest. The object is spent afterwards.
    std::string finish() {
        const std::uint64_t total = bits_;
        std::uint8_t pad[72] = {0x80};
        const std::size_t padLen = (held_ < 56 ? 56 : 120) - held_;
        for (int i = 0; i < 8; ++i)
            pad[padLen + std::size_t(i)] = std::uint8_t(total >> (56 - 8 * i));
        add(pad, padLen + 8);
        char out[65];
        for (int i = 0; i < 8; ++i)
            std::snprintf(out + 8 * i, 9, "%08x", unsigned(h_[i]));
        return {out, 64};
    }

private:
    static std::uint32_t ror(std::uint32_t x, int r) {
        return (x >> r) | (x << (32 - r));
    }
    void block(const std::uint8_t* p) {
        static constexpr std::uint32_t k[64] = {
          0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
          0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
          0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
          0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
          0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
          0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
          0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
          0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        std::uint32_t w[64];
        for (int i = 0; i < 16; i++)
            w[i] = std::uint32_t(p[4*i]) << 24 | std::uint32_t(p[4*i+1]) << 16 |
                   std::uint32_t(p[4*i+2]) << 8 | p[4*i+3];
        for (int i = 16; i < 64; i++) {
            const auto s0 = ror(w[i-15], 7) ^ ror(w[i-15], 18) ^ (w[i-15] >> 3);
            const auto s1 = ror(w[i-2], 17) ^ ror(w[i-2], 19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        std::uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3];
        std::uint32_t e = h_[4], f = h_[5], g = h_[6], z = h_[7];
        for (int i = 0; i < 64; i++) {
            const auto s1 = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25);
            const auto ch = (e & f) ^ (~e & g);
            const auto t1 = z + s1 + ch + k[i] + w[i];
            const auto s0 = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22);
            const auto maj = (a & b) ^ (a & c) ^ (b & c);
            const auto t2 = s0 + maj;
            z = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d;
        h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += z;
    }

    std::uint32_t h_[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                           0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::uint64_t bits_ = 0;
    std::uint8_t buf_[64]{};
    std::size_t held_ = 0;
};

inline std::string sha256Hex(const std::uint8_t* p, std::size_t n) {
    Sha256 s;
    s.add(p, n);
    return s.finish();
}

inline std::string sha256Hex(const std::vector<std::uint8_t>& bytes) {
    return sha256Hex(bytes.data(), bytes.size());
}

} // namespace pom68k
