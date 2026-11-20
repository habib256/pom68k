// Strict product-mode firmware manifest.  Dumps remain user-provided; these
// identities merely prevent a wrong image from qualifying as factory LLE.
// Every row is also a row of the ROM catalogue (src/RomCatalog.h) with the
// same path, size and digest — `rom_catalog_test` holds the two together.

#pragma once

#include "Sha256.h"

#include <cstdint>
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
    // The Eclipse towers' Egret (macquadra700.cpp:887) — the same part the
    // LC III and the IIvx carry.
    {"Egret 341s0851", "roms/egret/341s0851.bin", 4352,
     "77f5fb2baaca55a5921423b041e60257a864d79d734872991fa607fa33784d88"},
};


inline bool verify(const char* label, const std::string& firmwareRoot,
                   std::string& error) {
    const Entry* wanted = nullptr;
    for (const Entry& e : kManifest)
        if (std::strcmp(e.label, label) == 0) { wanted = &e; break; }
    if (!wanted) { error = std::string("firmware not in manifest: ") + label; return false; }

    std::string found;
    std::vector<std::string> bases;
    if (!firmwareRoot.empty())
        bases.emplace_back(firmwareRoot + "/");
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
        error = found + ": size " + std::to_string(n) + ", expected " +
                std::to_string(wanted->size);
        return false;
    }
    in.seekg(0);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(n));
    if (!in.read(reinterpret_cast<char*>(data.data()), std::streamsize(data.size()))) {
        error = found + ": unreadable"; return false;
    }
    const std::string got = sha256Hex(data);
    if (got != wanted->sha256) {
        error = found + ": SHA-256 " + got + ", expected " + wanted->sha256;
        return false;
    }
    return true;
}

inline bool verify(const char* label, std::string& error) {
    return verify(label, {}, error);
}

} // namespace pom68k::firmware
