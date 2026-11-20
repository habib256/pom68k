// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// rom_import_test — src/RomImport: identify user files by SHA-256 and
// install each recognised dump at its catalogue path. Asset-free: the
// catalogue is narrowed to synthetic rows (ImportOptions::catalogue).
//
//   loose file under any name   → installed
//   folder, recursively         → every dump inside, zips included
//   zip member                  → installed
//   MAME high/low chip pair     → interleaved and installed
//   the same dump twice         → installed once
//   a file matching nothing     → reported, left alone
//   a wrong file in the way     → kept in roms-replaced/ first
//   the dump already in place   → "already present", untouched
//   cancel                      → stops

#include "RomImport.h"
#include "ZipFixture.h"

#include "Crc32.h"
#include "Sha256.h"

#include <cstdio>
#include <string>

using namespace pom68k;
using namespace pom68k::roms;
namespace fs = std::filesystem;

static int gFails = 0;
static void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++gFails;
}

static std::vector<std::uint8_t> noise(std::size_t n, std::uint32_t seed) {
    std::vector<std::uint8_t> v(n);
    for (auto& b : v) { seed = seed * 22695477u + 1u; b = std::uint8_t(seed >> 16); }
    return v;
}

static bool holds(const fs::path& p, const std::vector<std::uint8_t>& want) {
    std::error_code ec;
    return fs::is_regular_file(p, ec) && testzip::readFile(p) == want;
}

int main() {
    // A: 4 KB firmware. P: a 64 KB "machine ROM" MAME keeps as two 32 KB
    // chips, even bytes in `hi`, odd bytes in `lo`.
    const auto a = noise(4096, 11);
    const auto p = noise(65536, 12);
    std::vector<std::uint8_t> hi(32768), lo(32768);
    for (std::size_t i = 0; i < hi.size(); ++i) { hi[i] = p[2 * i]; lo[i] = p[2 * i + 1]; }
    const std::string shaA = sha256Hex(a), shaP = sha256Hex(p);
    const RomDump cat[] = {
        {.id = "imp-a", .label = "Import A", .kind = RomKind::Firmware, .need = RomNeed::Substitute,
         .path = "roms/cuda/imp-a.bin", .alsoAt = "", .size = 4096, .sha256 = shaA.c_str(),
         .crc32 = crc32(a), .checksum = nullptr, .profiles = "q605", .absent = "",
         .rootRelative = false, .retroBios = nullptr, .retroBiosMember = nullptr},
        {.id = "imp-p", .label = "Import P", .kind = RomKind::Firmware, .need = RomNeed::Required,
         .path = "roms/64KB ROMs/imp-p.ROM", .alsoAt = "", .size = 65536, .sha256 = shaP.c_str(),
         .crc32 = crc32(p), .checksum = nullptr, .profiles = "plus", .absent = "",
         .rootRelative = false, .retroBios = nullptr, .retroBiosMember = nullptr},
    };
    ImportOptions opts;
    opts.catalogue = cat;

    {
        testzip::TempDir t("import-loose");
        testzip::writeFile(t.path / "in/Some Random Name.rom", a);
        testzip::writeFile(t.path / "in/notes.txt", {'h', 'i'});
        const fs::path roms = t.path / "roms";
        const ImportResult r = importRomsInto({t.path / "in/Some Random Name.rom",
                                               t.path / "in/notes.txt"},
                                              roms, {t.path, {}, {}}, opts);
        check(r.installed == 1 && holds(roms / "cuda/imp-a.bin", a),
              "loose file identified by content, installed at its catalogue path (" + r.summary + ")");
        check(r.unrecognised.size() == 1 && r.unrecognised[0].find("notes.txt") != std::string::npos,
              "a file matching nothing is reported");
        check(holds(t.path / "in/Some Random Name.rom", a), "the input is never moved or changed");
        const ImportResult again = importRomsInto({t.path / "in/Some Random Name.rom"}, roms,
                                                  {t.path, {}, {}}, opts);
        check(again.installed == 0 && again.alreadyPresent == 1 && again.items.size() == 1 &&
                  again.items[0].alreadyPresent,
              "importing it again: already present");
    }
    {
        testzip::TempDir t("import-zip");
        testzip::writeFile(t.path / "in/mamecuda.zip",
                           testzip::make({testzip::stored("readme.txt", {'x'}),
                                          testzip::stored("341s9999.bin", a)}));
        testzip::writeFile(t.path / "in/macplus.zip",
                           testzip::make({testzip::stored("342-0341-c.u6d", hi),
                                          testzip::stored("342-0342-b.u8d", lo)}));
        const fs::path roms = t.path / "roms";
        const ImportResult r = importRomsInto({t.path / "in/mamecuda.zip", t.path / "in/macplus.zip"},
                                              roms, {t.path, {}, {}}, opts);
        check(holds(roms / "cuda/imp-a.bin", a), "zip member identified and installed");
        check(holds(roms / "64KB ROMs/imp-p.ROM", p),
              "a high/low chip pair is interleaved into the whole ROM");
        bool pairSource = false;
        for (const ImportItem& i : r.items)
            pairSource |= i.source.find("342-0341-c.u6d+342-0342-b.u8d") != std::string::npos;
        check(pairSource, "the pair's source names both members, high first");
        check(r.installed == 2 && r.unrecognised.empty(), "zip import summary (" + r.summary + ")");
    }
    {
        testzip::TempDir t("import-folder");
        testzip::writeFile(t.path / "collection/deep/er/x.bin", a);
        testzip::writeFile(t.path / "collection/copy-of-x.bin", a);
        testzip::writeFile(t.path / "collection/sets/plus.zip",
                           testzip::make({testzip::stored("lo", lo), testzip::stored("hi", hi)}));
        testzip::writeFile(t.path / "collection/big.dsk", noise(300000, 3));
        const ImportResult r = importRomsInto({t.path / "collection"}, t.path / "roms",
                                              {t.path, {}, {}}, opts);
        check(r.installed == 2 && r.items.size() == 2,
              "folder walked recursively; the same dump twice installed once (" + r.summary + ")");
        check(holds(t.path / "roms/64KB ROMs/imp-p.ROM", p), "a zip inside the folder is opened");
    }
    {
        testzip::TempDir t("import-replace");
        const auto old = noise(4096, 99);
        testzip::writeFile(t.path / "roms/cuda/imp-a.bin", old);
        testzip::writeFile(t.path / "in/a.bin", a);
        const ImportResult r = importRomsInto({t.path / "in/a.bin"}, t.path / "roms",
                                              {t.path, {}, {}}, opts);
        check(r.installed == 1 && r.items.size() == 1 && !r.items[0].backup.empty() &&
                  holds(r.items[0].backup, old) && holds(t.path / "roms/cuda/imp-a.bin", a),
              "a wrong file in the way is kept in roms-replaced/ before being replaced");
    }
    {
        testzip::TempDir t("import-cancel");
        testzip::writeFile(t.path / "in/a.bin", a);
        ImportOptions stop = opts;
        stop.cancelled = [] { return true; };
        const ImportResult r = importRomsInto({t.path / "in/a.bin"}, t.path / "roms",
                                              {t.path, {}, {}}, stop);
        check(r.cancelled && r.installed == 0 && !fs::exists(t.path / "roms"),
              "cancelled before starting: nothing written");
    }
    {
        testzip::TempDir t("import-candidate");
        testzip::writeFile(t.path / "small.bin", a);
        testzip::writeFile(t.path / "huge.img", noise(kMaxImportFileBytes + 1, 1));
        testzip::writeFile(t.path / "set.ZIP", testzip::make({}));
        check(romImportCandidate(t.path / "small.bin") && romImportCandidate(t.path / "set.ZIP") &&
                  romImportCandidate(t.path) && !romImportCandidate(t.path / "huge.img") &&
                  !romImportCandidate(t.path / "absent"),
              "romImportCandidate: small files, zips and folders; not a disk image");
    }
    {
        // The real catalogue: a file that is no Apple dump stays unrecognised.
        testzip::TempDir t("import-real");
        testzip::writeFile(t.path / "x.bin", noise(4352, 5));
        const ImportResult r = importRomsInto({t.path / "x.bin"}, t.path / "roms", {t.path, {}, {}});
        check(r.installed == 0 && r.unrecognised.size() == 1,
              "real catalogue: a 4352-byte file that is no Cuda is not installed");
    }

    std::printf("%s rom_import_test (%d failure%s)\n", gFails ? "FAIL" : "PASS", gFails,
                gFails == 1 ? "" : "s");
    return gFails ? 1 : 0;
}
