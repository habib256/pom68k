// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// DebugSymbols — see DebugSymbols.h for the format.

#include "DebugSymbols.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace pom68k::dbg {

namespace {
bool parseHex(const std::string& s, std::uint32_t& out) {
    if (s.empty() || s.size() > 8) return false;
    std::uint32_t v = 0;
    for (char c : s) {
        int d = c >= '0' && c <= '9' ? c - '0'
              : c >= 'a' && c <= 'f' ? c - 'a' + 10
              : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0) return false;
        v = v << 4 | std::uint32_t(d);
    }
    out = v;
    return true;
}
} // namespace

bool RomSymbols::load(const std::string& path, std::uint32_t romChecksum,
                      std::string& why) {
    std::ifstream in(path);
    if (!in) {
        why = "Symboles : fichier illisible " + path;
        return false;
    }
    RomSymbols t;
    bool magic = false, haveSum = false;
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
        ++n;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        std::istringstream ls(line);
        if (line[0] == '#') {
            std::string hash, key, value;
            ls >> hash >> key;
            std::getline(ls >> std::ws, value);
            if (line == "# POM68K ROM symbols v1") magic = true;
            else if (key == "rom-checksum") haveSum = parseHex(value, t.checksum_);
            else if (key == "source") t.source_ = value;
            else if (key == "size") parseHex(value, t.romSize_);
            else if (key == "window") {
                std::uint32_t w = 0;
                if (parseHex(value, w)) t.windows_.push_back(w);
            }
            continue;
        }
        std::string off, name;
        std::uint32_t o = 0;
        if (!(ls >> off >> name) || !parseHex(off, o)) {
            why = "Symboles : ligne " + std::to_string(n) + " illisible";
            return false;
        }
        t.symbols_.emplace_back(o, name);
    }
    char sum[16];
    std::snprintf(sum, sizeof sum, "%08X", romChecksum);
    std::string err;
    if (!magic) err = "Symboles : en-tête « # POM68K ROM symbols v1 » absent";
    else if (!haveSum) err = "Symboles : # rom-checksum absent";
    else if (t.checksum_ != romChecksum)
        err = std::string("Symboles d'une autre ROM (la ROM en cours est ") + sum + ")";
    else if (t.source_.empty()) err = "Symboles : # source (provenance) absente";
    else if (!t.romSize_ || t.windows_.empty())
        err = "Symboles : # size et au moins un # window requis";
    else if (t.symbols_.empty()) err = "Symboles : aucun symbole";
    if (!err.empty()) {
        why = err;
        return false;
    }
    std::sort(t.symbols_.begin(), t.symbols_.end());
    *this = std::move(t);
    return true;
}

std::string RomSymbols::label(std::uint32_t addr) const {
    for (std::uint32_t w : windows_) {
        if (addr < w || addr - w >= romSize_) continue;
        const std::uint32_t off = addr - w;
        auto it = std::upper_bound(
            symbols_.begin(), symbols_.end(), off,
            [](std::uint32_t o, const auto& s) { return o < s.first; });
        if (it == symbols_.begin()) return {};
        --it;
        if (it->first == off) return it->second;
        char buf[24];
        std::snprintf(buf, sizeof buf, "+$%X", off - it->first);
        return it->second + buf;
    }
    return {};
}

} // namespace pom68k::dbg
