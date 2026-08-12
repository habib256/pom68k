// Strict product-mode firmware manifest.  Dumps remain user-provided; these
// identities merely prevent a wrong image from qualifying as factory LLE.

#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace pom68k::firmware {

struct Entry {
    const char* label;
    const char* path;
    std::uint64_t size;
    const char* sha256;
};

inline constexpr Entry kManifest[] = {
    {"Cuda 341s0788", "roms/cuda/341s0788.bin", 4352,
     "4bad3d4f18425f96bbbdde55eb0413603dba4910d172ffed7ed4d7c1a34358cc"},
    {"Cuda 341s0060", "roms/cuda/341s0060.bin", 4352,
     "607890e9ed816be6ca2210620f649ef63816e78d35b4e1e23858b9c8478a2c16"},
    {"ADB PIC1654S 342s0440-b", "roms/adbmodem/342s0440-b.bin", 1024,
     "22466ae8e4c11509dcc862e85c65239efcc780d23eda9fd4c69bb8041cb1318e"},
};

namespace detail {
struct Sha256 {
    std::uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                          0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::uint64_t bits = 0;
    std::uint8_t buf[64]{};
    std::size_t held = 0;
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
        for (int i=0;i<16;i++) w[i]=std::uint32_t(p[4*i])<<24|std::uint32_t(p[4*i+1])<<16|std::uint32_t(p[4*i+2])<<8|p[4*i+3];
        for (int i=16;i<64;i++) {
            auto s0=ror(w[i-15],7)^ror(w[i-15],18)^(w[i-15]>>3);
            auto s1=ror(w[i-2],17)^ror(w[i-2],19)^(w[i-2]>>10);
            w[i]=w[i-16]+s0+w[i-7]+s1;
        }
        std::uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],z=h[7];
        for(int i=0;i<64;i++) { auto s1=ror(e,6)^ror(e,11)^ror(e,25); auto ch=(e&f)^(~e&g); auto t1=z+s1+ch+k[i]+w[i]; auto s0=ror(a,2)^ror(a,13)^ror(a,22); auto maj=(a&b)^(a&c)^(b&c); auto t2=s0+maj; z=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2; }
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=z;
    }
    void add(const std::uint8_t* p, std::size_t n) {
        bits += std::uint64_t(n)*8;
        while(n--) { buf[held++]=*p++; if(held==64){block(buf);held=0;} }
    }
    std::string finish() {
        const auto total=bits; std::uint8_t x=0x80; add(&x,1); bits=total;
        x=0; while(held!=56){add(&x,1);bits=total;}
        std::uint8_t len[8]; for(int i=0;i<8;i++)len[i]=std::uint8_t(total>>(56-8*i)); add(len,8);
        char out[65]; for(int i=0;i<8;i++)std::snprintf(out+8*i,9,"%08x",h[i]);
        return {out,64};
    }
};
} // namespace detail

inline bool verify(const char* label, std::string& error) {
    const Entry* wanted = nullptr;
    for (const Entry& e : kManifest)
        if (std::strcmp(e.label, label) == 0) { wanted = &e; break; }
    if (!wanted) { error = std::string("firmware non manifesté: ") + label; return false; }

    std::string found;
    std::vector<std::string> bases;
    if (const char* root = std::getenv("POM68K_FIRMWARE_ROOT"))
        bases.emplace_back(std::string(root) + "/");
    else
        bases = {std::string(), std::string("../")};
    for (const std::string& base : bases) {
        const std::string p = base + wanted->path;
        if (std::ifstream(p, std::ios::binary)) { found = p; break; }
    }
    if (found.empty()) { error = std::string("absent: ") + wanted->path; return false; }
    std::ifstream in(found, std::ios::binary | std::ios::ate);
    const auto n = std::uint64_t(in.tellg());
    if (n != wanted->size) {
        error = found + ": taille " + std::to_string(n) + ", attendue " +
                std::to_string(wanted->size);
        return false;
    }
    in.seekg(0);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(n));
    if (!in.read(reinterpret_cast<char*>(data.data()), std::streamsize(data.size()))) {
        error = found + ": lecture impossible"; return false;
    }
    detail::Sha256 sha; sha.add(data.data(), data.size());
    const std::string got = sha.finish();
    if (got != wanted->sha256) {
        error = found + ": SHA-256 " + got + ", attendu " + wanted->sha256;
        return false;
    }
    return true;
}

} // namespace pom68k::firmware
