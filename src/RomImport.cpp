// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "RomImport.h"

#include "ZipReader.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace fs = std::filesystem;

namespace pom68k::roms {
namespace {

// Pairs are tried among at most this many same-size members: n² hashes of
// a megabyte each stays well under a second.
constexpr std::size_t kMaxPairGroup = 24;

bool isZipName(const fs::path& p) {
    std::string ext = p.extension().string();
    for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".zip";
}

bool catalogueSize(std::span<const RomDump> catalogue, std::uint64_t n) {
    for (const RomDump& d : catalogue)
        if (d.size == n) return true;
    return false;
}

struct Found {
    const RomDump* dump;
    std::string source;
    std::vector<std::uint8_t> bytes;
};

class Scanner {
public:
    explicit Scanner(const ImportOptions& o) : opts_(o) {}

    bool stop() const { return opts_.cancelled && opts_.cancelled(); }

    void input(const fs::path& p, ImportResult& r) {
        std::error_code ec;
        if (fs::is_directory(p, ec)) { folder(p, r); return; }
        if (!fs::is_regular_file(p, ec)) {
            r.unrecognised.push_back(p.string() + ": not a file or folder");
            return;
        }
        if (isZipName(p)) zipFile(p, r);
        else looseFile(p, r, true);
    }

    std::vector<Found> found;

private:
    void add(const std::vector<const RomDump*>& dumps, const std::string& source,
             const std::vector<std::uint8_t>& bytes) {
        if (!dumps.empty()) matched_ = true;
        for (const RomDump* d : dumps) {
            if (!seen_.insert(d).second) continue;   // the same dump twice: once
            found.push_back({d, source, bytes});
        }
    }

    void folder(const fs::path& dir, ImportResult& r) {
        std::error_code ec;
        std::vector<fs::path> files;
        for (auto it = fs::recursive_directory_iterator(dir, ec);
             !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            std::error_code fe;
            if (it->is_regular_file(fe)) files.push_back(it->path());
            if (files.size() > kMaxImportFiles) {
                r.unrecognised.push_back(dir.string() + ": more than " +
                                         std::to_string(kMaxImportFiles) +
                                         " files — only the first were read");
                break;
            }
        }
        std::sort(files.begin(), files.end());
        for (const fs::path& f : files) {
            if (stop()) return;
            if (isZipName(f)) zipFile(f, r);
            else looseFile(f, r, false);
        }
    }

    void looseFile(const fs::path& p, ImportResult& r, bool explicitInput) {
        std::error_code ec;
        const auto n = fs::file_size(p, ec);
        if (ec || !catalogueSize(opts_.catalogue, n)) {
            if (explicitInput)
                r.unrecognised.push_back(p.string() + ": not a catalogued dump (size " +
                                         (ec ? std::string("unknown") : std::to_string(n)) + ")");
            return;
        }
        std::vector<std::uint8_t> bytes;
        std::string err;
        if (!readFileCapped(p, kMaxImportFileBytes, bytes, err)) {
            if (explicitInput) r.unrecognised.push_back(err);
            return;
        }
        const auto dumps = identifyRom(bytes, opts_.catalogue);
        if (dumps.empty()) {
            if (explicitInput)
                r.unrecognised.push_back(p.string() + ": not a catalogued dump");
            return;
        }
        add(dumps, p.string(), bytes);
    }

    void zipFile(const fs::path& p, ImportResult& r) {
        std::vector<std::uint8_t> z;
        std::string err;
        if (!readFileCapped(p, kMaxImportZipBytes, z, err)) {
            r.unrecognised.push_back(err);
            return;
        }
        std::vector<zip::Entry> entries;
        if (!zip::listEntries(z, entries, err)) {
            r.unrecognised.push_back(p.string() + ": " + err);
            return;
        }
        matched_ = false;
        // Members of one size, for the pair pass.
        std::map<std::uint64_t, std::vector<std::pair<std::string, std::vector<std::uint8_t>>>> bySize;
        for (const zip::Entry& e : entries) {
            if (stop()) return;
            if (e.directory()) continue;
            const bool whole = catalogueSize(opts_.catalogue, e.size);
            const bool half = catalogueSize(opts_.catalogue, e.size * 2);
            if (!whole && !half) continue;
            std::vector<std::uint8_t> bytes;
            std::string why;
            if (!zip::readEntry(z, e, bytes, why, std::size_t(e.size))) continue;
            if (whole) add(identifyRom(bytes, opts_.catalogue), p.string() + ":" + e.name, bytes);
            if (half) bySize[e.size].emplace_back(e.name, std::move(bytes));
        }
        for (const auto& [size, members] : bySize) {
            if (members.size() < 2 || members.size() > kMaxPairGroup) continue;
            for (const auto& [hiName, hi] : members) {
                for (const auto& [loName, lo] : members) {
                    if (stop()) return;
                    if (&hiName == &loName) continue;
                    std::vector<std::uint8_t> joined(std::size_t(size) * 2);
                    for (std::size_t i = 0; i < hi.size(); ++i) {
                        joined[2 * i] = hi[i];
                        joined[2 * i + 1] = lo[i];
                    }
                    const auto dumps = identifyRom(joined, opts_.catalogue);
                    if (!dumps.empty())
                        add(dumps, p.string() + ":" + hiName + "+" + loName, joined);
                }
            }
        }
        if (!matched_)
            r.unrecognised.push_back(p.string() + ": no catalogued dump inside");
    }

    const ImportOptions& opts_;
    std::set<const RomDump*> seen_;
    bool matched_ = false;
};

} // namespace

bool romImportCandidate(const fs::path& p) {
    std::error_code ec;
    const fs::file_status st = fs::status(p, ec);
    if (ec) return false;
    if (fs::is_directory(st)) return true;
    if (!fs::is_regular_file(st)) return false;
    if (isZipName(p)) return true;
    const auto n = fs::file_size(p, ec);
    return !ec && n <= kMaxImportFileBytes;
}

ImportResult importRoms(const std::vector<fs::path>& inputs, const RomSearch& search,
                        const ImportOptions& options) {
    const RomDestination dest = writableRomsDir(search);
    if (!dest.ok()) {
        ImportResult r;
        r.romsDir = dest.romsDir.string();
        r.summary = dest.error;
        return r;
    }
    return importRomsInto(inputs, dest.romsDir, search, options);
}

ImportResult importRomsInto(const std::vector<fs::path>& inputs, const fs::path& romsDir,
                            const RomSearch& search, const ImportOptions& options) {
    ImportResult r;
    r.romsDir = romsDir.string();
    Scanner scan(options);
    for (const fs::path& in : inputs) {
        if (scan.stop()) break;
        scan.input(in, r);
    }
    r.cancelled = scan.stop();
    std::sort(scan.found.begin(), scan.found.end(),
              [](const Found& a, const Found& b) { return a.dump < b.dump; });
    for (const Found& f : scan.found) {
        if (scan.stop()) { r.cancelled = true; break; }
        ImportItem item;
        item.dump = f.dump;
        item.source = f.source;
        if (checkRom(*f.dump, search).state == RomState::Verified) {
            item.alreadyPresent = true;
            ++r.alreadyPresent;
            r.items.push_back(std::move(item));
            continue;
        }
        const InstallOutcome inst = installRom(romsDir, *f.dump, f.bytes);
        if (inst.ok) {
            item.installedAt = inst.installedAt;
            item.backup = inst.backup;
            if (inst.unchanged) { item.alreadyPresent = true; ++r.alreadyPresent; }
            else ++r.installed;
        } else {
            item.error = inst.error;
            ++r.failed;
        }
        r.items.push_back(std::move(item));
    }
    r.summary = "Installed " + std::to_string(r.installed) + " ROM" +
                (r.installed == 1 ? "" : "s") + " into " + r.romsDir + ", " +
                std::to_string(r.alreadyPresent) + " already present";
    if (r.failed) r.summary += ", " + std::to_string(r.failed) + " refused";
    if (!r.unrecognised.empty())
        r.summary += ", " + std::to_string(r.unrecognised.size()) + " input(s) not recognised";
    r.summary += r.cancelled ? " (cancelled)." : ".";
    return r;
}

} // namespace pom68k::roms
