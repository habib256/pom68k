// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── ROM symbols, tied to the ROM they describe ──
// A ROM's internal labels are only true of that ROM: a file is accepted
// only if its declared checksum is the running ROM's own (a Mac ROM's first
// longword, `romChecksum()` on every memory map), and only if it says where
// its names come from. Format, version 1:
//
//   # POM68K ROM symbols v1
//   # rom-checksum <8 hex digits>     required, must match the ROM
//   # source <free text>              required: provenance of the names
//   # size <hex>                      required: ROM size in bytes
//   # window <hex>                    one or more: logical bases where the
//                                     CPU executes this ROM (its mirrors)
//   <offset hex> <name>               ROM offset, one symbol per line
//
// An address inside a window is labelled with the nearest symbol at or
// below its ROM offset ("Name" or "Name+$off"). No symbols ship with
// POM68K; this is the contract a user's or a tool's file must meet.
//
// Gate: tests/debug_session_test.cpp (symbol checks).

#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace pom68k::dbg {

class RomSymbols {
public:
    // Replaces the table; on refusal (false + `why`) the old one is kept.
    bool load(const std::string& path, std::uint32_t romChecksum, std::string& why);
    void clear() { *this = RomSymbols{}; }

    std::string label(std::uint32_t addr) const;
    std::size_t size() const { return symbols_.size(); }
    const std::string& source() const { return source_; }
    std::uint32_t checksum() const { return checksum_; }

private:
    std::vector<std::pair<std::uint32_t, std::string>> symbols_;   // by offset
    std::vector<std::uint32_t> windows_;
    std::uint32_t romSize_ = 0;
    std::uint32_t checksum_ = 0;
    std::string source_;
};

} // namespace pom68k::dbg
