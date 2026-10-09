// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// MacSymbols — see MacSymbols.h.

#include "MacSymbols.h"

#include <algorithm>
#include <cstdio>
#include <iterator>

namespace pom68k::mac {

namespace {
#include "MacSymbolTables.inc"

const char* exactTrap(std::uint16_t word) {
    const auto* end = std::end(kMacTraps);
    const auto* it = std::lower_bound(
        std::begin(kMacTraps), end, word,
        [](const MacTrapName& t, std::uint16_t w) { return t.word < w; });
    return it != end && it->word == word ? it->name : nullptr;
}
} // namespace

const char* trapName(std::uint16_t word) {
    if ((word & 0xF000) != 0xA000) return nullptr;
    if (const char* n = exactTrap(word)) return n;
    const std::uint16_t mask = (word & 0x0800) ? 0xFBFF : 0xF8FF;
    return exactTrap(std::uint16_t(word & mask));
}

std::string lowMemLabel(std::uint32_t addr) {
    const auto* begin = std::begin(kMacLowMem);
    const auto* end = std::end(kMacLowMem);
    const auto* it = std::upper_bound(
        begin, end, addr,
        [](std::uint32_t a, const MacLowMemName& g) { return a < g.addr; });
    if (it == begin) return {};
    const MacLowMemName& g = *std::prev(it);
    const std::uint32_t next = it != end ? it->addr : g.addr + 4;
    const std::uint32_t span = std::min(next - g.addr, kLowMemSpan);
    const std::uint32_t off = addr - g.addr;
    if (off >= span) return {};
    if (!off) return g.name;
    char buf[64];
    std::snprintf(buf, sizeof buf, "%s+$%X", g.name, off);
    return buf;
}

} // namespace pom68k::mac
