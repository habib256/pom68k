// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "RomFetch.h"

#include "HostProcess.h"
#include "ZipReader.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <map>

namespace fs = std::filesystem;

namespace pom68k::roms {
namespace {

constexpr int kCurlDeadlineMs = 330 * 1000;   // curl's own --max-time is 300 s

bool stopRequested(const FetchOptions& o) { return o.cancelled && o.cancelled(); }

bool curlDownload(const std::string& url, const fs::path& dest, std::string& err,
                  const std::function<bool()>& cancelled) {
    std::error_code ec;
    fs::create_directories(dest.parent_path(), ec);
    fs::remove(dest, ec);
    // Our own ceiling too: before curl 8.4 `--max-filesize` is ignored when
    // the server announces no length, and this poll is where it is enforced.
    bool tooBig = false;
    const auto stop = [&] {
        std::error_code sec;
        const auto n = fs::file_size(dest, sec);
        if (!sec && n > kMaxDownloadBytes) { tooBig = true; return true; }
        return cancelled && cancelled();
    };
    const host::ProcessOutcome r =
        host::runHostProcess("curl", curlArguments(url, dest.string()), kCurlDeadlineMs, stop);
    if (r.succeeded()) return true;
    fs::remove(dest, ec);
    if (!r.started) err = r.error + " — install curl to download ROMs";
    else if (tooBig) err = "the file is larger than any ROM archive: " + url;
    else if (r.stopped) err = "cancelled";
    else if (r.timedOut) err = "the download timed out: " + url;
    else err = curlFailureReason(r.exitCode) + " (curl exit " + std::to_string(r.exitCode) +
               "): " + url;
    return false;
}

fs::path scratchDir(const fs::path& romsDir) {
    static std::atomic<unsigned> counter{0};
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    return romsDir / (".fetch-" + std::to_string(tick) + "-" + std::to_string(++counter));
}

// Leftovers of a crashed run, by age: a live fetch is minutes old.
void sweepStaleScratch(const fs::path& romsDir) {
    std::error_code ec;
    const auto cutoff = fs::file_time_type::clock::now() - std::chrono::hours(24);
    for (fs::directory_iterator it(romsDir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.rfind(".fetch-", 0) != 0) continue;
        std::error_code sec;
        if (!it->is_directory(sec) || fs::last_write_time(it->path(), sec) > cutoff || sec)
            continue;
        fs::remove_all(it->path(), sec);
    }
}

std::string percentEncodePath(const std::string& p) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : p) {
        const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                           (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
                           c == '~' || c == '/';
        if (plain) out += char(c);
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}

} // namespace

std::string retroBiosUrl(const RomDump& d) {
    if (!d.retroBios) return {};
    return std::string("https://raw.githubusercontent.com/Abdess/retrobios/") +
           kRetroBiosCommit + "/" + percentEncodePath(d.retroBios);
}

std::string romArchiveMember(const RomDump& d) { return d.path; }

std::vector<std::string> curlArguments(const std::string& url, const std::string& outPath) {
    // -f: an HTTP error is a failure, not a saved error page. --proto pins
    // the first request to HTTPS and --proto-redir every hop after it (a
    // redirect to http:// would otherwise be followed silently). -g: the URL
    // is text, not a curl glob.
    return {"-fsSLg",
            "--proto", "=https",
            "--proto-redir", "=https",
            "--max-filesize", std::to_string(kMaxDownloadBytes),
            "--connect-timeout", "15",
            "--retry", "2",
            "--max-time", "300",
            "-o", outPath,
            url};
}

std::string curlFailureReason(int code) {
    switch (code) {
    case 6: return "the host name did not resolve";
    case 7: return "the server could not be reached";
    case 22:
    case 56: return "the server refused the download (an HTTP error such as 403 or 404) or broke it off";
    case 28: return "the download timed out";
    case 35:
    case 60: return "the secure connection failed";
    case 63: return "the file is larger than any ROM archive";
    default: return "curl failed";
    }
}

bool romFetchAvailable(std::string* why) {
    if (host::hostProcessesAvailable()) return true;
    if (why) *why = "ROM download is not available in the browser build";
    return false;
}

std::vector<const RomDump*> romsToFetch(const RomSearch& search,
                                        const std::vector<std::string>& only) {
    std::vector<const RomDump*> out;
    for (const RomStatus& st : scanRoms(search)) {
        if (!only.empty() &&
            std::find(only.begin(), only.end(), std::string(st.dump->id)) == only.end())
            continue;
        if (st.state != RomState::Verified) out.push_back(st.dump);
    }
    return out;
}

FetchResult fetchRoms(const RomSearch& search, const FetchOptions& options) {
    FetchResult r;
    std::string why;
    if (!options.download && !romFetchAvailable(&why)) {
        r.error = r.summary = why;
        return r;
    }
    const RomDestination dest = writableRomsDir(search);
    r.romsDir = dest.romsDir.string();
    if (!dest.ok()) {
        r.error = r.summary = dest.error;
        return r;
    }
    return fetchRomsInto(dest.romsDir, romsToFetch(search, options.only), search, options);
}

FetchResult fetchRomsInto(const fs::path& romsDir, const std::vector<const RomDump*>& wanted,
                          const RomSearch& search, const FetchOptions& options) {
    FetchResult r;
    r.romsDir = romsDir.string();
    r.skipped = int(kRomDumpCount) - int(wanted.size());
    if (wanted.empty()) {
        r.summary = "Every catalogued ROM is already present and verified.";
        return r;
    }
    std::string why;
    if (!options.download && !romFetchAvailable(&why)) {
        r.error = r.summary = why;
        return r;
    }
    std::error_code ec;
    fs::create_directories(romsDir, ec);
    if (!fs::is_directory(romsDir, ec)) {
        r.error = r.summary = "cannot create " + romsDir.string();
        return r;
    }
    sweepStaleScratch(romsDir);
    const fs::path scratch = scratchDir(romsDir);
    fs::create_directories(scratch, ec);

    auto fetchUrl = [&](const std::string& url, const fs::path& to, std::string& err) {
        if (options.download) {
            std::error_code dec;
            fs::create_directories(to.parent_path(), dec);
            return options.download(url, to, err);
        }
        return curlDownload(url, to, err, options.cancelled);
    };

    const int total = int(wanted.size());
    int done = 0;
    auto tick = [&](const std::string& label) {
        if (options.progress) options.progress(FetchProgress{done, total, label});
    };

    // Zips, fetched once per URL per run; a failure is remembered so its
    // other members do not each wait out the same timeout again.
    std::map<std::string, std::vector<std::uint8_t>> zips;
    std::map<std::string, std::string> zipFailed;
    auto fetchZip = [&](const std::string& url, std::string& err)
        -> const std::vector<std::uint8_t>* {
        if (auto it = zips.find(url); it != zips.end()) return &it->second;
        if (auto bad = zipFailed.find(url); bad != zipFailed.end()) { err = bad->second; return nullptr; }
        const fs::path file = scratch / ("zip-" + std::to_string(zips.size() + zipFailed.size()));
        std::vector<std::uint8_t> bytes;
        std::vector<zip::Entry> entries;
        bool ok = fetchUrl(url, file, err) &&
                  readFileCapped(file, kMaxDownloadBytes, bytes, err);
        if (ok && !zip::listEntries(bytes, entries, err)) {
            // An HTML page served with status 200 lands here.
            err = "the download is not a zip archive (" + err + ")";
            ok = false;
        }
        if (ok && zip::totalUncompressed(entries) > kMaxUnpackedBytes) {
            err = "the archive would unpack to " +
                  std::to_string(zip::totalUncompressed(entries)) +
                  " bytes — refusing it";
            ok = false;
        }
        std::error_code rm;
        fs::remove(file, rm);
        if (!ok) {
            if (!stopRequested(options)) zipFailed.emplace(url, err);
            return nullptr;
        }
        return &zips.emplace(url, std::move(bytes)).first->second;
    };

    const std::vector<std::uint8_t>* archive = nullptr;
    if (options.archive) {
        tick("POM68K ROM archive (archive.org)");
        std::string err;
        archive = fetchZip(kRomArchiveUrl, err);
        if (!archive)
            r.archiveError = "POM68K's ROM archive on archive.org is unavailable (" + err + ")";
    } else {
        r.archiveError = "POM68K's ROM archive was not asked";
    }

    for (const RomDump* d : wanted) {
        if (stopRequested(options)) { r.cancelled = true; break; }
        tick(d->label);
        FetchItem item;
        item.dump = d;
        std::string err;
        InstallOutcome inst;

        if (archive) {
            std::vector<std::uint8_t> bytes;
            if (zip::readMember(*archive, romArchiveMember(*d), bytes, err, d->size)) {
                inst = installRom(romsDir, *d, bytes);
                if (inst.ok) item.source = "archive";
                else err = inst.error;
            }
            if (!inst.ok) err = "archive: " + err;
        } else {
            err = r.archiveError;
        }
        if (!inst.ok && options.retroBios && d->retroBios && !stopRequested(options)) {
            std::string rbErr;
            const std::string url = retroBiosUrl(*d);
            std::vector<std::uint8_t> bytes;
            bool got = false;
            if (d->retroBiosMember) {
                if (const auto* z = fetchZip(url, rbErr))
                    got = zip::readMember(*z, d->retroBiosMember, bytes, rbErr, d->size);
            } else {
                const fs::path file = scratch / ("file-" + std::string(d->id));
                got = fetchUrl(url, file, rbErr) &&
                      readFileCapped(file, kMaxRomFileBytes, bytes, rbErr);
                std::error_code rm;
                fs::remove(file, rm);
            }
            if (got) {
                inst = installRom(romsDir, *d, bytes);
                if (inst.ok) item.source = "RetroBIOS";
                else rbErr = inst.error;
            }
            if (!inst.ok) err += (err.empty() ? "" : "; ") + std::string("RetroBIOS: ") + rbErr;
        }

        if (inst.ok) {
            item.installedAt = inst.installedAt;
            item.backup = inst.backup;
            // Saved is not the same as used: a different file the runtime
            // finds first, or a destination it does not search, is said so.
            const RomStatus st = checkRom(*d, search);
            if (st.state != RomState::Verified) {
                inst.ok = false;
                err = st.state == RomState::Shadowed
                          ? "saved " + inst.installedAt + ", but " + st.foundAt +
                                " is found first and is not the expected dump — remove or replace it"
                          : "saved " + inst.installedAt + ", but POM68K does not look there";
            }
        }
        item.ok = inst.ok;
        if (item.ok) {
            ++r.saved;
            if (item.source == "archive") ++r.fromArchive; else ++r.fromRetroBios;
            if (!item.backup.empty()) ++r.replaced;
        } else {
            item.error = stopRequested(options) ? "cancelled" : err;
            if (stopRequested(options)) r.cancelled = true;
            ++r.failed;
            if (r.error.empty()) r.error = std::string(d->label) + ": " + item.error;
        }
        r.items.push_back(std::move(item));
        ++done;
        tick(d->label);
    }
    fs::remove_all(scratch, ec);

    if (r.cancelled) {
        r.summary = "Cancelled: saved " + std::to_string(r.saved) + " ROM" +
                    (r.saved == 1 ? "" : "s") + " before stopping.";
    } else if (r.failed == 0) {
        r.summary = "Saved " + std::to_string(r.saved) + " ROM" + (r.saved == 1 ? "" : "s") +
                    " to " + r.romsDir + " (" + std::to_string(r.fromArchive) +
                    " from the POM68K archive, " + std::to_string(r.fromRetroBios) +
                    " from RetroBIOS).";
    } else {
        r.summary = "Saved " + std::to_string(r.saved) + ", failed " + std::to_string(r.failed) +
                    " — " + r.error;
    }
    if (r.replaced > 0)
        r.summary += " " + std::to_string(r.replaced) + " replaced a different file, kept in " +
                     replacedRomsDir(romsDir).string() + ".";
    if (!r.archiveError.empty() && options.archive &&
        r.summary.find(r.archiveError) == std::string::npos)
        r.summary += " " + r.archiveError + (options.retroBios ? "; RetroBIOS was asked instead." : ".");
    return r;
}

} // namespace pom68k::roms
