// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// RomStore — where the catalogued dumps are on this host, and the one gate
// through which POM68K ever writes one.
//
// Status. `checkRom`/`scanRoms` look where the runtime looks, in its order:
//   * machine ROMs — each profile's `romPath`, the catalogue path and its
//     aliases, under the working directory, the executable's directory and
//     its parent (MachineFactory::findPath); then, as
//     `findRomBySignature` does, any file below a `roms/` whose NAME carries
//     the ROM's header checksum;
//   * firmware and declaration ROMs — `<firmware root>/<path minus roms/>`
//     for the PMU and colour-video dumps when a root is configured, then the
//     catalogue path and aliases under the working directory and its parent
//     (the devices' `roms/…` / `../roms/…` candidate lists).
// Every candidate is hashed: a file is "the dump" only when its SHA-256 is
// the catalogue's.
//
// Writes. `installRom` is the only writer, for the downloader and the
// importer alike: size and SHA-256 gate first; bytes already in place are
// left alone; a different file in the way is COPIED to `roms-replaced/`
// beside the roms folder before it is replaced, and if that copy fails
// nothing is replaced; the new file is written beside its destination and
// renamed over it, so a crash leaves the old file or the new one, never a
// torn one. A symbolic link at the destination is replaced by a regular
// file; what it pointed to is never written.
//
// Every function here touches the file system (and hashes up to a few
// megabytes per call): run them on a worker thread, not the ImGui frame.
// They keep no state between calls and may run concurrently.

#pragma once

#include "RomCatalog.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace pom68k::roms {

// Largest file the store reads as a candidate dump (the biggest catalogued
// dump is 1 MiB).
inline constexpr std::size_t kMaxRomFileBytes = 4u << 20;

// Where to look. `current()` is what the running application uses.
struct RomSearch {
    std::filesystem::path workingDir;     // where relative `roms/…` resolve
    std::filesystem::path executableDir;  // empty: not probed
    std::filesystem::path firmwareRoot;   // RuntimeConfig::core().firmware.root
                                          // when set; empty: none
    // The process's working directory and executable directory. Reads no
    // environment variable; pass `firmwareRoot` from the RuntimeConfig.
    static RomSearch current();
};

enum class RomState : std::uint8_t {
    Verified,  // the first file the runtime finds is the catalogued dump
    Shadowed,  // the dump is present, but another file is found first
    Mismatch,  // files exist where it is looked for, none is the dump
    Missing,   // nothing where it is looked for
};

const char* romStateName(RomState s);

struct RomStatus {
    const RomDump* dump = nullptr;
    RomState state = RomState::Missing;
    std::string foundAt;     // first existing candidate (what loads first)
    std::string verifiedAt;  // a candidate holding exactly the dump, if any
    std::string detail;      // why `foundAt` is not the dump, when it is not
};

// Candidates for `d`, in runtime order, existing or not (signature hits
// excluded — those need a directory walk; see checkRom).
std::vector<std::filesystem::path> romCandidates(const RomDump& d, const RomSearch& s);

RomStatus checkRom(const RomDump& d, const RomSearch& s);
// Every catalogue entry, in catalogue order. One directory walk for the
// signature scan, shared by all machine ROMs.
std::vector<RomStatus> scanRoms(const RomSearch& s);

// True when `bytes` are `d` (size, then SHA-256).
bool romBytesMatch(const RomDump& d, const std::vector<std::uint8_t>& bytes);
// Every entry of `catalogue` these bytes are (normally zero or one).
std::vector<const RomDump*> identifyRom(const std::vector<std::uint8_t>& bytes,
                                        std::span<const RomDump> catalogue = kRomDumps);

// Reads at most `cap` bytes; false with `err` for a larger, unreadable or
// unseekable file.
bool readFileCapped(const std::filesystem::path& p, std::size_t cap,
                    std::vector<std::uint8_t>& out, std::string& err);

// ── Destination ──────────────────────────────────────────────────────────

struct RomDestination {
    std::filesystem::path romsDir;  // absolute; may not exist yet
    std::string error;              // set when POM68K may not write anywhere
    bool ok() const { return error.empty(); }
};

// Where downloads and imports go: the `roms/` the runtime searches first
// that already exists — `<working dir>/roms`, else `<working dir>/../roms`
// (a run from `build/`) — or a new `<working dir>/roms` when neither
// exists. Packaged builds start in their data directory (AppImage AppRun,
// the macOS launcher, the Windows zip's folder), so that is where this
// lands. Refused, with the reason, when that folder is read-only or lies
// inside a macOS `.app` (writing there breaks the bundle's signature and an
// update discards it). Creates nothing.
RomDestination writableRomsDir(const RomSearch& s);

// True when POM68K may add files to `romsDir` (or create it): writable, and
// not inside a `.app` bundle.
bool romsDirTakesWrites(const std::filesystem::path& romsDir);

// `<romsDir>/../roms-replaced`: OUTSIDE roms/, so a kept file can never be
// found by the runtime's recursive signature scan.
std::filesystem::path replacedRomsDir(const std::filesystem::path& romsDir);

// `<romsDir>/<path minus "roms/">`.
std::filesystem::path romInstallPath(const std::filesystem::path& romsDir, const RomDump& d);

struct InstallOutcome {
    bool ok = false;
    bool unchanged = false;     // the destination already held these bytes
    std::string installedAt;
    std::string backup;         // where a replaced file was kept
    std::string error;
};

InstallOutcome installRom(const std::filesystem::path& romsDir, const RomDump& d,
                          const std::vector<std::uint8_t>& bytes);

} // namespace pom68k::roms
