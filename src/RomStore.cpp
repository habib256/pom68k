// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "RomStore.h"

#include "Sha256.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <ctime>
#include <fstream>
#include <map>
#include <set>
#include <string_view>

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace pom68k::roms {
namespace {

std::string upper(std::string s) {
    for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> splitAliases(const char* list) {
    std::vector<std::string> out;
    std::string_view v = list ? list : "";
    while (!v.empty()) {
        const std::size_t cut = v.find('|');
        if (cut) out.emplace_back(v.substr(0, cut));
        if (cut == std::string_view::npos) break;
        v.remove_prefix(cut + 1);
    }
    return out;
}

std::vector<fs::path> uniqueBases(std::initializer_list<fs::path> in) {
    std::vector<fs::path> out;
    for (const fs::path& b : in) {
        if (b.empty()) continue;
        const fs::path n = b.lexically_normal();
        if (std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
    }
    return out;
}

fs::path workDir(const RomSearch& s) {
    if (!s.workingDir.empty()) return s.workingDir;
    std::error_code ec;
    return fs::current_path(ec);
}

// The relative paths the runtime asks for, in its order. Machine ROMs start
// with the `romPath` of every profile that dispatches on this ROM.
std::vector<std::string> relativeCandidates(const RomDump& d) {
    std::vector<std::string> rel;
    auto add = [&](const std::string& p) {
        if (std::find(rel.begin(), rel.end(), p) == rel.end()) rel.push_back(p);
    };
    if (d.kind == RomKind::MachineRom) {
        for (const MachineProfile& p : kMachineProfiles) {
            const bool byChecksum = p.romCrc32 && d.checksum &&
                                    std::string_view(p.romCrc32) == d.checksum;
            const bool byPath = !p.romCrc32 && std::string_view(p.romPath) == d.path;
            if (byChecksum || byPath) add(p.romPath);
        }
    }
    add(d.path);
    for (const std::string& a : splitAliases(d.alsoAt)) add(a);
    return rel;
}

// Files below each `roms/` whose upper-cased name contains a header
// checksum, keyed by that checksum — MachineFactory::findRomBySignature's
// walk, done once for every machine ROM.
std::map<std::string, std::vector<fs::path>> signatureIndex(const RomSearch& s,
                                                            const std::set<std::string>& sigs) {
    std::map<std::string, std::vector<fs::path>> hits;
    if (sigs.empty()) return hits;
    const fs::path wd = workDir(s);
    std::vector<fs::path> roots = uniqueBases({wd / "roms",
        s.executableDir.empty() ? fs::path() : s.executableDir / "roms",
        s.executableDir.empty() ? fs::path() : s.executableDir / ".." / "roms"});
    std::size_t visited = 0;
    for (const fs::path& root : roots) {
        std::error_code ec;
        if (!fs::is_directory(root, ec)) continue;
        for (auto it = fs::recursive_directory_iterator(root, ec);
             !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (++visited > 100000) break;   // a pathological tree, not a ROM folder
            std::error_code fe;
            if (!it->is_regular_file(fe)) continue;
            const std::string name = upper(it->path().filename().string());
            for (const std::string& sig : sigs)
                if (name.find(sig) != std::string::npos) hits[sig].push_back(it->path());
        }
    }
    for (auto& [sig, paths] : hits) std::sort(paths.begin(), paths.end());
    return hits;
}

RomStatus evaluate(const RomDump& d, std::vector<fs::path> candidates) {
    RomStatus st;
    st.dump = &d;
    std::set<fs::path> seen;
    for (const fs::path& p : candidates) {
        std::error_code ec;
        const fs::path canon = fs::weakly_canonical(p, ec);
        if (!seen.insert(ec ? p : canon).second) continue;
        if (!fs::is_regular_file(p, ec)) continue;
        std::vector<std::uint8_t> bytes;
        std::string err;
        const bool read = readFileCapped(p, kMaxRomFileBytes, bytes, err);
        const bool match = read && romBytesMatch(d, bytes);
        if (st.foundAt.empty()) {
            st.foundAt = p.string();
            if (!read) st.detail = err;
            else if (bytes.size() != d.size)
                st.detail = std::to_string(bytes.size()) + " bytes, expected " +
                            std::to_string(d.size);
            else if (!match)
                st.detail = "SHA-256 " + sha256Hex(bytes) + ", expected " + d.sha256;
        }
        if (match) { st.verifiedAt = p.string(); break; }
    }
    if (st.foundAt.empty()) st.state = RomState::Missing;
    else if (st.verifiedAt == st.foundAt) st.state = RomState::Verified;
    else if (!st.verifiedAt.empty()) st.state = RomState::Shadowed;
    else st.state = RomState::Mismatch;
    return st;
}

RomStatus checkWithIndex(const RomDump& d, const RomSearch& s,
                         const std::map<std::string, std::vector<fs::path>>& index) {
    std::vector<fs::path> c = romCandidates(d, s);
    if (d.checksum) {
        if (auto it = index.find(d.checksum); it != index.end())
            c.insert(c.end(), it->second.begin(), it->second.end());
    }
    return evaluate(d, std::move(c));
}

bool dirWritable(const fs::path& dir) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return false;
#if defined(_WIN32)
    static std::atomic<unsigned> n{0};
    const fs::path probe = dir / (".pom68k-write-probe-" + std::to_string(++n));
    {
        std::ofstream f(probe, std::ios::binary | std::ios::trunc);
        if (!f) return false;
    }
    fs::remove(probe, ec);
    return true;
#else
    return ::access(dir.string().c_str(), W_OK) == 0;
#endif
}

std::string timestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y%m%d-%H%M%S", &tmv);
    return buf;
}

// Copy `dest` (following a link) into roms-replaced/, keeping its path
// below roms/ so two kept files of the same name cannot collide.
bool keepOld(const fs::path& romsDir, const RomDump& d, const fs::path& dest,
             std::string& backup, std::string& err) {
    std::error_code ec;
    const std::string rel = std::string(d.path).substr(5);   // minus "roms/"
    const fs::path base = replacedRomsDir(romsDir) / fs::path(rel);
    fs::create_directories(base.parent_path(), ec);
    if (ec) {
        err = "cannot keep the old " + dest.filename().string() + " (" +
              base.parent_path().string() + ": " + ec.message() + ") — not replaced";
        return false;
    }
    const std::string stamp = timestamp();
    for (int n = 0; n < 100; ++n) {
        const fs::path target = base.string() + "." + stamp +
                                (n ? "-" + std::to_string(n) : std::string());
        ec.clear();
        if (fs::exists(target, ec)) continue;   // never overwrite a kept file
        if (fs::copy_file(dest, target, fs::copy_options::none, ec)) {
            backup = target.string();
            return true;
        }
        break;
    }
    err = "cannot keep the old " + dest.filename().string() +
          (ec ? ": " + ec.message() : std::string()) + " — not replaced";
    return false;
}

bool writeAtomically(const fs::path& dest, const std::vector<std::uint8_t>& bytes,
                     std::string& err) {
    static std::atomic<unsigned> counter{0};
    std::error_code ec;
    fs::create_directories(dest.parent_path(), ec);
    if (ec) { err = "cannot create " + dest.parent_path().string() + ": " + ec.message(); return false; }
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path tmp = dest.string() + ".part-" + std::to_string(tick) + "-" +
                         std::to_string(++counter);
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) { err = "cannot write " + tmp.string(); return false; }
        f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        f.flush();
        if (!f) { f.close(); fs::remove(tmp, ec); err = "short write to " + tmp.string(); return false; }
    }
    // A symlink at `dest` is replaced by the rename itself; its target is
    // never opened for writing.
    fs::rename(tmp, dest, ec);
    if (ec) {
        err = "cannot replace " + dest.string() + ": " + ec.message();
        std::error_code rm;
        fs::remove(tmp, rm);
        return false;
    }
    return true;
}

} // namespace

const char* romStateName(RomState s) {
    switch (s) {
    case RomState::Verified: return "verified";
    case RomState::Shadowed: return "shadowed";
    case RomState::Mismatch: return "not the expected dump";
    case RomState::Missing: return "missing";
    }
    return "?";
}


bool readFileCapped(const fs::path& p, std::size_t cap, std::vector<std::uint8_t>& out,
                    std::string& err) {
    out.clear();
    std::ifstream f(p, std::ios::binary);
    if (!f) { err = "cannot read " + p.string(); return false; }
    f.seekg(0, std::ios::end);
    const std::streamoff end = f.tellg();
    if (end < 0) { err = "cannot measure " + p.string(); return false; }
    if (std::uintmax_t(end) > cap) {
        err = p.string() + " is " + std::to_string(end) + " bytes, over the " +
              std::to_string(cap) + "-byte limit";
        return false;
    }
    f.seekg(0, std::ios::beg);
    out.resize(std::size_t(end));
    if (end && !f.read(reinterpret_cast<char*>(out.data()), std::streamsize(end))) {
        out.clear();
        err = "short read of " + p.string();
        return false;
    }
    return true;
}

bool romBytesMatch(const RomDump& d, const std::vector<std::uint8_t>& bytes) {
    return bytes.size() == d.size && sha256Hex(bytes) == d.sha256;
}

std::vector<const RomDump*> identifyRom(const std::vector<std::uint8_t>& bytes,
                                        std::span<const RomDump> catalogue) {
    std::vector<const RomDump*> out;
    std::string digest;
    for (const RomDump& d : catalogue) {
        if (d.size != bytes.size()) continue;
        if (digest.empty()) digest = sha256Hex(bytes);
        if (digest == d.sha256) out.push_back(&d);
    }
    return out;
}

std::vector<fs::path> romCandidates(const RomDump& d, const RomSearch& s) {
    const fs::path wd = workDir(s);
    std::vector<fs::path> out;
    const std::vector<std::string> rel = relativeCandidates(d);
    if (d.kind == RomKind::MachineRom) {
        const std::vector<fs::path> bases = uniqueBases({wd, s.executableDir,
            s.executableDir.empty() ? fs::path() : s.executableDir / ".."});
        for (const std::string& r : rel)
            for (const fs::path& b : bases) out.push_back(b / fs::path(r));
    } else {
        if (d.rootRelative && !s.firmwareRoot.empty())
            out.push_back(s.firmwareRoot / fs::path(std::string(d.path).substr(5)));
        for (const fs::path& b : uniqueBases({wd, wd / ".."}))
            for (const std::string& r : rel) out.push_back(b / fs::path(r));
    }
    return out;
}

RomStatus checkRom(const RomDump& d, const RomSearch& s) {
    std::set<std::string> sigs;
    if (d.checksum) sigs.insert(d.checksum);
    return checkWithIndex(d, s, signatureIndex(s, sigs));
}

std::vector<RomStatus> scanRoms(const RomSearch& s) {
    std::set<std::string> sigs;
    for (const RomDump& d : kRomDumps)
        if (d.checksum) sigs.insert(d.checksum);
    const auto index = signatureIndex(s, sigs);
    std::vector<RomStatus> out;
    out.reserve(kRomDumpCount);
    for (const RomDump& d : kRomDumps) out.push_back(checkWithIndex(d, s, index));
    return out;
}

bool romsDirTakesWrites(const fs::path& romsDir) {
    std::error_code ec;
    const fs::path abs = fs::absolute(romsDir, ec).lexically_normal();
    for (const fs::path& part : abs) {
        const std::string s = part.string();
        if (s.size() > 4 && s.compare(s.size() - 4, 4, ".app") == 0) return false;
    }
    if (fs::exists(abs, ec)) return dirWritable(abs);
    return dirWritable(abs.parent_path());
}

RomDestination writableRomsDir(const RomSearch& s) {
    RomDestination out;
    std::error_code ec;
    const fs::path wd = fs::absolute(workDir(s), ec).lexically_normal();
    fs::path chosen;
    for (const fs::path& c : {wd / "roms", (wd / ".." / "roms").lexically_normal()}) {
        if (fs::is_directory(c, ec)) { chosen = c; break; }
    }
    if (chosen.empty()) chosen = wd / "roms";
    out.romsDir = chosen;
    if (!romsDirTakesWrites(chosen))
        out.error = chosen.string() +
                    " cannot take new files (read-only, or inside an application bundle)";
    return out;
}

fs::path replacedRomsDir(const fs::path& romsDir) {
    std::error_code ec;
    const fs::path abs = fs::absolute(romsDir, ec).lexically_normal();
    return (abs.has_filename() ? abs : abs.parent_path()).parent_path() / "roms-replaced";
}

fs::path romInstallPath(const fs::path& romsDir, const RomDump& d) {
    return romsDir / fs::path(std::string(d.path).substr(5));
}

InstallOutcome installRom(const fs::path& romsDir, const RomDump& d,
                          const std::vector<std::uint8_t>& bytes) {
    InstallOutcome r;
    const fs::path dest = romInstallPath(romsDir, d);
    r.installedAt = dest.string();
    if (bytes.size() != d.size) {
        r.error = std::string(d.label) + ": " + std::to_string(bytes.size()) +
                  " bytes, expected " + std::to_string(d.size);
        return r;
    }
    const std::string got = sha256Hex(bytes);
    if (got != d.sha256) {
        r.error = std::string(d.label) + ": SHA-256 " + got + ", expected " + d.sha256 +
                  " — not the catalogued dump";
        return r;
    }
    std::error_code ec;
    if (fs::is_directory(dest, ec)) {
        r.error = dest.string() + " is a directory";
        return r;
    }
    if (fs::exists(dest, ec)) {
        std::vector<std::uint8_t> have;
        std::string ignore;
        if (readFileCapped(dest, kMaxRomFileBytes, have, ignore) && have == bytes) {
            r.ok = r.unchanged = true;
            return r;
        }
        if (!keepOld(romsDir, d, dest, r.backup, r.error)) return r;
    }
    // (A dangling link has nothing to keep; the rename replaces the link.)
    if (!writeAtomically(dest, bytes, r.error)) return r;
    r.ok = true;
    return r;
}

} // namespace pom68k::roms
