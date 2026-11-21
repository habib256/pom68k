// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// rom_catalog_test — src/RomCatalog.h against the owners it must agree
// with. Asset-free: it reads the SOURCE tree (argv[1]), never a dump.
//
//   * assets.lock: its firmware / machine-rom / machine-rom-variant /
//     declaration-rom rows and the catalogue, row for row both ways —
//     label, size, SHA-256, profile set, kind/need, and a lock path the
//     catalogue probes;
//   * firmware::kManifest: every strict-LLE row is a catalogue row with the
//     same path, size and digest;
//   * kMachineProfiles: each profile has exactly one default machine ROM,
//     the required row under its menu's identity (also a static_assert);
//   * the runtime: every non-machine canonical path is spelled, literally,
//     in a source file that opens it, and every machine ROM's canonical
//     file name carries the checksum the signature scan looks for (or is
//     its profiles' romPath);
//   * the RetroBIOS mapping: 20 entries, URLs at the pinned commit,
//     percent-encoded;
//   * SHA-256 (FIPS 180-4 vectors and padding boundaries) and CRC-32.

#include "RomCatalog.h"
#include "RomFetch.h"
#include "FirmwareManifest.h"
#include "Sha256.h"
#include "Crc32.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace pom68k;
using namespace pom68k::roms;
namespace fs = std::filesystem;

static int gFails = 0;
static void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++gFails;
}

static std::string trim(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

static std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::stringstream in(s);
    for (std::string part; std::getline(in, part, sep);) out.push_back(trim(part));
    return out;
}

static std::set<std::string> asSet(const std::string& list, char sep) {
    std::set<std::string> out;
    for (const auto& s : split(list, sep)) if (!s.empty()) out.insert(s);
    return out;
}

static std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

struct LockRow { std::string role, label, sha, path, profiles; std::uint64_t size; int line; };

static const char* expectedRole(const RomDump& d) {
    switch (d.kind) {
    case RomKind::MachineRom: return d.need == RomNeed::Required ? "machine-rom" : "machine-rom-variant";
    case RomKind::Firmware: return "firmware";
    case RomKind::DeclarationRom: return "declaration-rom";
    }
    return "?";
}

static std::set<std::string> probedPaths(const RomDump& d) {
    std::set<std::string> out = asSet(d.alsoAt, '|');
    out.insert(d.path);
    if (d.kind == RomKind::MachineRom)
        for (const MachineProfile& p : kMachineProfiles)
            if (p.romCrc32 && d.checksum && std::string(p.romCrc32) == d.checksum)
                out.insert(p.romPath);
    return out;
}

static void hashes() {
    auto hex = [](const std::string& s) {
        return sha256Hex(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
    };
    check(hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "SHA-256 of the empty string (FIPS 180-4)");
    check(hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "SHA-256 of \"abc\"");
    check(hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
          "SHA-256 of the 448-bit message");
    const std::map<std::size_t, const char*> boundary = {
        {55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
        {56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
        {63, "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"},
        {64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
        {119, "31eba51c313a5c08226adf18d4a359cfdfd8d2e816b13f4af952f7ea6584dcfb"},
        {120, "2f3d335432c70b580af0e8e1b3674a7c020d683aa5f73aaaedfdc55af904c21c"}};
    for (const auto& [n, want] : boundary)
        check(hex(std::string(n, 'a')) == want, "SHA-256 padding boundary at " + std::to_string(n) + " bytes");
    // A million 'a', fed in odd-sized chunks: the buffered path.
    Sha256 s;
    const std::string chunk(997, 'a');
    std::size_t left = 1000000;
    while (left) {
        const std::size_t n = std::min(left, chunk.size());
        s.add(reinterpret_cast<const std::uint8_t*>(chunk.data()), n);
        left -= n;
    }
    check(s.finish() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
          "SHA-256 of one million 'a' in 997-byte chunks");
    const std::string nine = "123456789";
    check(crc32(reinterpret_cast<const std::uint8_t*>(nine.data()), nine.size()) == 0xCBF43926u,
          "CRC-32 check value");
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("FAIL usage: rom_catalog_test <source dir>\n");
        return 1;
    }
    const fs::path root = argv[1];
    hashes();

    // ── assets.lock ──────────────────────────────────────────────────────
    std::vector<LockRow> lock;
    {
        std::ifstream f(root / "assets.lock");
        check(bool(f), "assets.lock readable");
        int n = 0;
        for (std::string line; std::getline(f, line);) {
            ++n;
            const std::string t = trim(line);
            if (t.empty() || t[0] == '#') continue;
            const auto cells = split(t, '|');
            if (cells.size() != 6) continue;
            static const std::set<std::string> romRoles = {
                "firmware", "machine-rom", "machine-rom-variant", "declaration-rom"};
            if (!romRoles.count(cells[0])) continue;
            lock.push_back({cells[0], cells[1], cells[3], cells[4], cells[5],
                            std::stoull(cells[2]), n});
        }
    }
    check(lock.size() == kRomDumpCount,
          "assets.lock ROM rows (" + std::to_string(lock.size()) + ") == catalogue rows (" +
              std::to_string(kRomDumpCount) + ")");
    std::set<const RomDump*> matched;
    for (const LockRow& r : lock) {
        const RomDump* d = nullptr;
        for (const RomDump& c : kRomDumps)
            if (r.sha == c.sha256) d = &c;
        const std::string at = "assets.lock:" + std::to_string(r.line) + " " + r.label;
        if (!d) { check(false, at + ": its SHA-256 is in the catalogue"); continue; }
        matched.insert(d);
        check(r.label == d->label, at + ": label matches " + d->id);
        check(r.size == d->size, at + ": size matches");
        check(r.role == expectedRole(*d), at + ": role " + r.role + " matches kind/need");
        check(asSet(r.profiles, ',') == asSet(d->profiles, ','), at + ": profile set matches");
        check(probedPaths(*d).count(r.path) == 1, at + ": lock path is a path the catalogue probes");
    }
    for (const RomDump& d : kRomDumps)
        check(matched.count(&d) == 1, std::string("catalogue ") + d.id + " has an assets.lock row");

    // ── firmware::kManifest ──────────────────────────────────────────────
    for (const firmware::Entry& e : firmware::kManifest) {
        const RomDump* d = nullptr;
        for (const RomDump& c : kRomDumps)
            if (std::string(c.path) == e.path) d = &c;
        check(d && d->size == e.size && std::string(d->sha256) == e.sha256 &&
                  d->kind == RomKind::Firmware,
              std::string("kManifest ") + e.label + " is the catalogue's row");
    }

    // ── uniqueness (kept out of the static_assert's step budget) ─────────
    {
        std::set<std::string> ids, paths, digests;
        for (const RomDump& d : kRomDumps) {
            check(ids.insert(d.id).second, std::string("unique id ") + d.id);
            check(paths.insert(d.path).second, std::string("unique path ") + d.path);
            check(digests.insert(d.sha256).second, std::string("unique SHA-256 for ") + d.id);
        }
    }

    // ── kMachineProfiles ─────────────────────────────────────────────────
    static_assert(validRomCatalog());
    for (const MachineProfile& p : kMachineProfiles) {
        int defaults = 0;
        for (const RomDump* d : romDumpsForProfile(p.slug))
            defaults += romIsDefaultFor(*d, p);
        check(defaults == 1, std::string(p.slug) + ": exactly one default machine ROM");
    }

    // ── the runtime probes each canonical path ───────────────────────────
    std::string sources;
    for (const auto& e : fs::recursive_directory_iterator(root / "src")) {
        const fs::path& p = e.path();
        if (!e.is_regular_file() || p.filename() == "RomCatalog.h") continue;
        if (p.extension() == ".cpp" || p.extension() == ".h") sources += slurp(p);
    }
    for (const RomDump& d : kRomDumps) {
        if (d.kind == RomKind::MachineRom) {
            std::string name = fs::path(d.path).filename().string();
            for (char& c : name) c = char(std::toupper(static_cast<unsigned char>(c)));
            bool byRomPath = false;
            for (const MachineProfile& p : kMachineProfiles)
                byRomPath |= !p.romCrc32 && std::string(p.romPath) == d.path;
            check(byRomPath || name.find(d.checksum) != std::string::npos,
                  std::string(d.id) + ": canonical file name carries " + d.checksum +
                      " (found by the signature scan)");
        } else {
            check(sources.find(std::string("\"") + d.path + "\"") != std::string::npos,
                  std::string(d.id) + ": " + d.path + " is opened by a source file");
        }
    }

    // ── RetroBIOS mapping ────────────────────────────────────────────────
    int served = 0;
    for (const RomDump& d : kRomDumps) {
        const std::string url = retroBiosUrl(d);
        if (!d.retroBios) { check(url.empty(), std::string(d.id) + ": archive-only, no URL"); continue; }
        ++served;
        check(url.rfind(std::string("https://raw.githubusercontent.com/Abdess/retrobios/") +
                            kRetroBiosCommit + "/bios/", 0) == 0 &&
                  url.find(' ') == std::string::npos,
              std::string(d.id) + ": " + url);
    }
    check(served == 20, "RetroBIOS serves 20 catalogue entries (" + std::to_string(served) + ")");
    check(retroBiosUrl(*findRomDump("adb-342s0440b")) ==
              std::string("https://raw.githubusercontent.com/Abdess/retrobios/") + kRetroBiosCommit +
                  "/bios/Apple/Macintosh%20II/adbmodem.zip",
          "a space in a RetroBIOS path is percent-encoded");
    check(std::string(kRetroBiosCommit).size() == 40, "the RetroBIOS pin is a full commit hash");
    check(romArchiveMember(*findRomDump("pmu-pb160")) == "roms/pmu/pb160.bin",
          "archive member = catalogue path");

    std::printf("%s rom_catalog_test: %zu dumps (%d failure%s)\n", gFails ? "FAIL" : "PASS",
                kRomDumpCount, gFails, gFails == 1 ? "" : "s");
    return gFails ? 1 : 0;
}
