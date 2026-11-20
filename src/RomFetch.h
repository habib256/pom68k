// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// RomFetch — download the catalogued dumps this host lacks.
//
// Two sources, in order:
//   1. PRIMARY — POM68K's own archive.org item, one zip
//      (`kRomArchiveUrl`) holding every catalogued dump as a member named
//      by its catalogue path (`roms/…`). Downloaded at most once per run.
//   2. SECONDARY — RetroBIOS (github.com/Abdess/retrobios) at ONE pinned
//      commit (`kRetroBiosCommit`), for the entries it really serves
//      (`RomDump::retroBios`; 20 of the catalogue's 52, each mapping checked
//      byte for byte). A commit is immutable: what it served the day the
//      mapping was verified it serves for good. Moving the pin is a
//      deliberate act — re-verify every mapped entry's SHA-256 first.
// An entry the archive cannot supply (archive unreachable, 403/404, an
// HTML page in place of the zip, member absent or not the dump) is asked of
// RetroBIOS; an entry RetroBIOS does not carry then fails with the
// archive's reason.
//
// Neither source is trusted, only checked: every byte goes through
// `installRom` (RomStore.h) — size and SHA-256 before anything is written,
// a replaced file kept in `roms-replaced/`, an atomic rename. A zip is
// refused before unpacking when its central directory declares more than
// `kMaxUnpackedBytes` (the zip-bomb gate), and a member is never inflated
// past its catalogue size.
//
// Transport: the system `curl` (HostProcess.h) with the argv
// `curlArguments` returns — HTTPS only, on the first request and on every
// redirect; a size ceiling; a connect and a total timeout; URL globbing
// off. POM68K links no TLS library. The browser build has no helper
// processes: `romFetchAvailable` says so and `fetchRoms` returns that
// error without trying. Tests replace the transport with
// `FetchOptions::download` and play the servers.
//
// Threads: `fetchRoms` blocks for as long as the downloads take — run it on
// a worker thread. `progress` is called on that thread; `cancelled` is
// polled from it (every 30 ms while curl runs, and between entries), so the
// GUI can set an atomic flag and join. Nothing here touches ImGui or the
// emulator.

#pragma once

#include "RomCatalog.h"
#include "RomStore.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace pom68k::roms {

// The item does not exist yet: the code must survive its 404/403/HTML.
inline constexpr const char* kRomArchivePage = "https://archive.org/details/pom68k_firm";
inline constexpr const char* kRomArchiveUrl =
    "https://archive.org/download/pom68k_firm/pom68k_firm.zip";

// RetroBIOS `main` on 2026-11-20.
inline constexpr const char* kRetroBiosCommit = "c25426f14cb079e63227e2022cb7fca1623e4141";
inline constexpr const char* kRetroBiosHome = "https://github.com/Abdess/retrobios";

// Largest download, and largest total a zip may declare, that a fetch
// accepts (the whole catalogue is ~33 MiB unpacked).
inline constexpr std::size_t kMaxDownloadBytes = 96u << 20;
inline constexpr std::size_t kMaxUnpackedBytes = 96u << 20;

// raw.githubusercontent.com URL of `d`'s RetroBIOS file at the pinned
// commit, percent-encoded; empty when RetroBIOS does not carry it.
std::string retroBiosUrl(const RomDump& d);
// Member of the POM68K archive holding `d`: its catalogue path.
std::string romArchiveMember(const RomDump& d);

// The exact argv (after argv[0]) handed to curl. Pinned by rom_fetch_test.
std::vector<std::string> curlArguments(const std::string& url, const std::string& outPath);
// A sentence for a curl exit status ("the server refused the download…").
std::string curlFailureReason(int exitCode);

// False on the browser build (`why` says so).
bool romFetchAvailable(std::string* why = nullptr);

// Writes `url`'s body to `dest`, or returns false with `err` set.
using DownloadFn = std::function<bool(const std::string& url,
                                      const std::filesystem::path& dest,
                                      std::string& err)>;

struct FetchProgress {
    int done = 0;            // entries finished (saved or failed)
    int total = 0;           // entries this run will attempt
    std::string label;       // what is being fetched now
};

struct FetchOptions {
    bool archive = true;                  // ask POM68K's archive
    bool retroBios = true;                // ask RetroBIOS
    std::vector<std::string> only;        // catalogue ids; empty = every entry not verified
    DownloadFn download;                  // empty = system curl
    std::function<void(const FetchProgress&)> progress;  // on the worker thread
    std::function<bool()> cancelled;      // polled on the worker thread
};

struct FetchItem {
    const RomDump* dump = nullptr;
    bool ok = false;
    std::string source;       // "archive" or "RetroBIOS" when ok
    std::string installedAt;
    std::string backup;       // replaced file kept here, if any
    std::string error;
};

struct FetchResult {
    std::vector<FetchItem> items;   // one per attempted entry, in catalogue order
    int saved = 0, failed = 0, skipped = 0, replaced = 0;
    int fromArchive = 0, fromRetroBios = 0;
    bool cancelled = false;
    std::string romsDir;
    std::string archiveError;  // why the primary source was not usable, if it was not
    std::string error;         // the run could not start, or the first failure
    std::string summary;       // one line for the panel, always set
};

// Catalogue entries `fetchRoms` would try: not Verified under `search`,
// restricted to `only` when non-empty.
std::vector<const RomDump*> romsToFetch(const RomSearch& search,
                                        const std::vector<std::string>& only = {});

// Fetch into writableRomsDir(search).
FetchResult fetchRoms(const RomSearch& search, const FetchOptions& options = {});

// Fetch `wanted` into `romsDir`; `search` is used afterwards to check that
// the runtime now finds each saved dump (a different file found first is a
// failure, said in words).
FetchResult fetchRomsInto(const std::filesystem::path& romsDir,
                          const std::vector<const RomDump*>& wanted,
                          const RomSearch& search, const FetchOptions& options);

} // namespace pom68k::roms
