// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The guest's desk scrap, read from the host: the TEXT the guest last cut
// or copied, without running a byte of guest code. Inside Macintosh:
// More Macintosh Toolbox, "Scrap Manager", and its low-memory globals:
//
//   ScrapSize   $0960  long   bytes in the scrap
//   ScrapHandle $0964  Handle the scrap in memory (0: none)
//   ScrapCount  $0968  word   bumped by every ZeroScrap
//   ScrapState  $096A  word   > 0 in memory, 0 on disk, < 0 uninitialized
//   MMU32Bit    $0CB2  byte   0: 24-bit Memory Manager (master pointers
//                             carry flags in their high byte)
//
// The scrap is a sequence of entries — type (4), length (4), data, padded
// to an even length. The reader follows the handle as it is NOW (a moved
// block is found through its master pointer; a purged one has a nil master
// pointer and is refused), bounds every length against ScrapSize and a
// fixed ceiling, and converts the TEXT entry from MacRoman to UTF-8 (CR to
// LF). A scrap on disk, uninitialized, purged, unreadable through the MMU
// or malformed is refused by name, never guessed at.
//
// `read(addr, out, n)` is a side-effect-free LOGICAL read that fails when
// any byte is untranslated or not memory: MachineHost passes the
// debugger's (DebugCpuTarget::readMemory), the gate a flat image.
// Gate: guest_scrap_test.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace pom68k {

inline constexpr std::uint32_t kMaxGuestScrapBytes = 16u << 20;
inline constexpr std::size_t kMaxGuestScrapText = 64u << 10;

struct GuestScrapText {
    enum class Status : std::uint8_t {
        Ok, NoScrap, OnDisk, Uninitialized, Purged, Unreadable, Malformed, NoText,
    };
    Status status = Status::NoScrap;
    std::string utf8;          // the TEXT entry, converted
    std::uint32_t macBytes = 0; // its length in the scrap
    bool truncated = false;    // longer than the ceiling
    std::uint16_t count = 0;   // ScrapCount when read
    std::string reason;        // for every status but Ok
    bool ok() const { return status == Status::Ok; }
};

namespace scrap_detail {

// MacRoman $80-$FF (Apple's ROMAN.TXT, with $DB as ¤: the euro took that
// slot only in Mac OS 8.5, after every System a 68k guest here runs).
inline constexpr char16_t kMacRomanHigh[128] = {
    0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1,
    0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5, 0x00E7, 0x00E9, 0x00E8,
    0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3,
    0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC,
    0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6, 0x00DF,
    0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8,
    0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211,
    0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8,
    0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB,
    0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153,
    0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA,
    0x00FF, 0x0178, 0x2044, 0x00A4, 0x2039, 0x203A, 0xFB01, 0xFB02,
    0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1,
    0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4,
    0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC,
    0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7,
};

inline void appendUtf8(std::string& out, char32_t cp) {
    if (cp < 0x80) { out += char(cp); return; }
    if (cp < 0x800) {
        out += char(0xC0 | cp >> 6);
    } else {
        out += char(0xE0 | cp >> 12);
        out += char(0x80 | (cp >> 6 & 0x3F));
    }
    out += char(0x80 | (cp & 0x3F));
}

} // namespace scrap_detail

inline std::string macRomanToUtf8(const std::uint8_t* text, std::size_t n) {
    std::string out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint8_t c = text[i];
        if (c == '\r') out += '\n';
        else if (c < 0x80) out += char(c);
        else scrap_detail::appendUtf8(out, scrap_detail::kMacRomanHigh[c - 0x80]);
    }
    return out;
}

template <class Read>
GuestScrapText readGuestScrapText(Read&& read, std::size_t maxText = kMaxGuestScrapText) {
    using Status = GuestScrapText::Status;
    GuestScrapText result;
    auto refuse = [&](Status status, const char* reason) {
        result.status = status;
        result.reason = reason;
        return result;
    };
    std::uint8_t globals[12] = {}, mmu32 = 1;
    if (!read(0x0960u, globals, sizeof globals) || !read(0x0CB2u, &mmu32, 1))
        return refuse(Status::Unreadable, "les variables du Scrap Manager sont illisibles");
    auto be32 = [](const std::uint8_t* p) {
        return std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 |
               std::uint32_t(p[2]) << 8 | p[3];
    };
    const std::uint32_t size = be32(globals);
    const std::uint32_t handle = be32(globals + 4);
    result.count = std::uint16_t(globals[8] << 8 | globals[9]);
    const auto state = std::int16_t(std::uint16_t(globals[10] << 8 | globals[11]));
    if (state < 0) return refuse(Status::Uninitialized, "presse-papiers jamais initialisé");
    if (state == 0)
        return refuse(Status::OnDisk, "presse-papiers sur disque (fichier Presse-papiers), "
                                      "pas en mémoire");
    if (!handle) return refuse(Status::NoScrap, "aucun presse-papiers en mémoire");
    if (handle & 1) return refuse(Status::Malformed, "handle impair");
    std::uint8_t mp[4];
    if (!read(handle, mp, 4)) return refuse(Status::Unreadable, "handle illisible");
    std::uint32_t block = be32(mp);
    if (!mmu32) block &= 0x00FFFFFFu;            // 24-bit: flags in the high byte
    if (!block) return refuse(Status::Purged, "bloc du presse-papiers purgé");
    if ((block & 1) || size > kMaxGuestScrapBytes || block + size < block)
        return refuse(Status::Malformed, "taille ou adresse du presse-papiers incohérente");

    std::uint32_t offset = 0;
    while (offset + 8 <= size) {
        std::uint8_t head[8];
        if (!read(block + offset, head, 8))
            return refuse(Status::Unreadable, "entrée du presse-papiers illisible");
        const std::uint32_t length = be32(head + 4);
        if (length > size - offset - 8)
            return refuse(Status::Malformed, "entrée plus longue que le presse-papiers");
        if (be32(head) == 0x54455854u) {            // 'TEXT'
            const std::size_t take = length > maxText ? maxText : length;
            std::string raw(take, '\0');
            if (take && !read(block + offset + 8,
                              reinterpret_cast<std::uint8_t*>(raw.data()), take))
                return refuse(Status::Unreadable, "texte du presse-papiers illisible");
            result.utf8 = macRomanToUtf8(
                reinterpret_cast<const std::uint8_t*>(raw.data()), take);
            result.macBytes = length;
            result.truncated = take < length;
            result.status = Status::Ok;
            return result;
        }
        offset += 8 + length + (length & 1);
    }
    return refuse(Status::NoText, "pas de texte (TEXT) dans le presse-papiers");
}

} // namespace pom68k
