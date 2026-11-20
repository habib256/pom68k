// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ZipReader — read members out of a zip archive held in memory, in process.
//
// The ROM downloader unpacks POM68K's own archive and MAME romsets, and the
// importer opens whatever zip a user drops (a romset, a "macroms"
// collection). Neither may run a host `unzip`: it is absent from Windows and
// from many Linux installs, GNU tar does not read zip, and a host tool
// writes its output with no size cap while it runs. Stored (method 0) and
// deflated (method 8) members cover every archive POM68K fetches; anything
// else — encryption, Deflate64, bzip2, zip64 — is refused with a reason.
//
// Hostile input is the design case: every offset is bounds-checked by
// subtraction (no `offset + n` that can wrap on a 32-bit host), a member's
// declared size is checked against the caller's cap BEFORE a byte is
// inflated, the inflater stops at the cap whatever the header claimed, and
// the CRC-32 of every member is verified. `totalUncompressed` lets a caller
// refuse a zip bomb from its central directory alone.
//
// [B] PKWARE APPNOTE.TXT 6.3.10 (§ 4.3.7 local header, § 4.3.12 central
// directory, § 4.3.16 end record); RFC 1951 (DEFLATE). `zip_reader_test`
// covers stored, fixed- and dynamic-Huffman members, the CRC and size
// gates, truncation and the bomb cap.
//
// Pure functions over byte vectors; safe from any thread.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pom68k::zip {

struct Entry {
    std::string name;               // as stored ('/'-separated)
    std::uint16_t method = 0;       // 0 stored, 8 deflate, else unsupported
    std::uint16_t flags = 0;        // general-purpose bit flag
    std::uint32_t crc32 = 0;
    std::uint64_t compressedSize = 0;
    std::uint64_t size = 0;         // uncompressed, as the directory declares
    std::size_t localHeader = 0;    // offset of the local file header

    bool directory() const { return !name.empty() && name.back() == '/'; }
    bool encrypted() const { return (flags & 1u) != 0; }
};

// True when `bytes` ends in a well-formed end-of-central-directory record.
// A cheap "is this a zip at all" — an HTML error page is not.
bool looksLikeZip(const std::vector<std::uint8_t>& bytes);

// Every central-directory entry, in archive order. False with `err` set
// when the archive is malformed, truncated or zip64.
bool listEntries(const std::vector<std::uint8_t>& zip,
                 std::vector<Entry>& entries, std::string& err);

// Sum of the declared uncompressed sizes — the zip-bomb gate, to compare
// with a cap before anything is unpacked.
std::uint64_t totalUncompressed(const std::vector<Entry>& entries);

// One member's bytes. Refuses (false, `err` set) a member that is encrypted,
// uses another method, declares or inflates to more than `maxBytes`, is
// truncated, or fails its CRC-32.
bool readEntry(const std::vector<std::uint8_t>& zip, const Entry& entry,
               std::vector<std::uint8_t>& out, std::string& err,
               std::size_t maxBytes);

// readEntry() of the member named exactly `name`.
bool readMember(const std::vector<std::uint8_t>& zip, const std::string& name,
                std::vector<std::uint8_t>& out, std::string& err,
                std::size_t maxBytes);

// Raw DEFLATE stream (RFC 1951, no zlib/gzip wrapper). Stops with an error
// once the output would pass `maxBytes`.
bool inflate(const std::uint8_t* in, std::size_t n,
             std::vector<std::uint8_t>& out, std::size_t maxBytes,
             std::string& err);

} // namespace pom68k::zip
