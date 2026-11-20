// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// RomImport — install a dump the user HAS, under the name POM68K probes.
//
// A dump found elsewhere almost never carries its catalogue name: MAME
// calls the Cuda `341s0788.bin` inside `cuda.zip`, a collection calls the
// Quadra 605 ROM `FF7439EE - LC475,575,Quadra 605….ROM`, and the Plus ROM
// comes as two 64 KB chips. So identify by CONTENT: every catalogue entry
// carries the SHA-256 of the dump, and byte-identical bytes are that dump
// whatever their name. Inputs may be
//   * a loose file (up to kMaxImportFileBytes — a disk image dropped by
//     mistake is not read at all),
//   * a folder, walked recursively (every file and every .zip inside),
//   * a .zip (up to kMaxImportZipBytes): only members whose DECLARED size
//     is a catalogue size are unpacked, and the inflater stops at that size
//     whatever the stream holds — a collection of thousands of dumps is
//     fine, a zip bomb unpacks nothing. Every such member is hashed, and
//     so is every PAIR of
//     same-size members byte-interleaved (even byte first, then odd) —
//     MAME keeps the 128K, 512K and Plus ROMs as a high/low chip pair.
// A match goes through installRom (RomStore.h): size and SHA-256 gate, the
// replaced file kept in roms-replaced/, atomic rename. A file matching
// nothing is reported and left alone: POM68K installs only what it can
// vouch for.
//
// Blocking file I/O and hashing: call from a worker thread. `cancelled` is
// polled between files and members.

#pragma once

#include "RomStore.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace pom68k::roms {

inline constexpr std::size_t kMaxImportFileBytes = 4u << 20;
inline constexpr std::size_t kMaxImportZipBytes = 256u << 20;
inline constexpr std::size_t kMaxImportFiles = 20000;   // per folder walk

struct ImportItem {
    const RomDump* dump = nullptr;
    std::string source;        // "file", "archive.zip:member" or "archive.zip:a+b"
    std::string installedAt;   // empty when skipped or refused
    std::string backup;        // where a replaced file was kept
    bool alreadyPresent = false;  // the runtime already finds this dump
    std::string error;         // set when the install was refused
};

struct ImportResult {
    std::vector<ImportItem> items;          // one per distinct dump found
    std::vector<std::string> unrecognised;  // inputs holding no catalogued dump (with reason)
    int installed = 0, alreadyPresent = 0, failed = 0;
    bool cancelled = false;
    std::string romsDir;
    std::string summary;                    // one line for the panel, always set
};

struct ImportOptions {
    std::function<bool()> cancelled;          // polled on the worker thread
    std::span<const RomDump> catalogue = kRomDumps;  // what is recognised (tests narrow it)
};

// Cheap pre-filter for a drop handler: a directory, a .zip, or a regular
// file no larger than kMaxImportFileBytes. One stat.
bool romImportCandidate(const std::filesystem::path& p);

// Identify every input and install each match into writableRomsDir(search).
ImportResult importRoms(const std::vector<std::filesystem::path>& inputs,
                        const RomSearch& search, const ImportOptions& options = {});

// Same, into an explicit folder (tests, tools).
ImportResult importRomsInto(const std::vector<std::filesystem::path>& inputs,
                            const std::filesystem::path& romsDir,
                            const RomSearch& search, const ImportOptions& options = {});

} // namespace pom68k::roms
