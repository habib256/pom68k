// POM68K — host-side insertion into an HFS catalog (src/HfsInject.h)
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// On a blank volume from HfsBlankVolume.h, exercised through a byte
// vector: folders are created and blessed, a MacBinary is decoded and
// installed as a Startup Item, read back fork for fork; sixty more files
// force leaf splits, an index level and a root split, and every one is
// found afterwards; the leaf chain stays strictly ordered and the header
// and MDB counts follow. The installer is idempotent and refuses a volume
// with no blessed folder. No ROM, no asset — the File Manager's own
// verdict on the same surgery is `scsi_agent_autostart_etalon`.

#include "HfsBlankVolume.h"
#include "HfsInject.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int gFails = 0;
static void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) gFails++;
}

namespace {

constexpr uint32_t kNow = 3870288000u;                 // 2026-09-09 00:00 UTC, Mac seconds

// A MacBinary II file with both forks, as Retro68 or a Mac would write it.
std::vector<uint8_t> macBinary(const std::string& name, const std::string& type,
                               const std::string& creator, const std::vector<uint8_t>& data,
                               const std::vector<uint8_t>& rsrc, uint16_t flags) {
    std::vector<uint8_t> f(128, 0);
    f[1] = uint8_t(name.size());
    std::memcpy(&f[2], name.data(), name.size());
    std::memcpy(&f[65], type.data(), 4);
    std::memcpy(&f[69], creator.data(), 4);
    f[73] = uint8_t(flags >> 8);
    f[101] = uint8_t(flags);
    auto be32 = [&](size_t at, uint32_t v) {
        f[at] = uint8_t(v >> 24); f[at + 1] = uint8_t(v >> 16); f[at + 2] = uint8_t(v >> 8); f[at + 3] = uint8_t(v);
    };
    be32(83, uint32_t(data.size()));
    be32(87, uint32_t(rsrc.size()));
    be32(91, kNow - 86400);
    be32(95, kNow - 3600);
    f.insert(f.end(), data.begin(), data.end());
    f.resize(128 + ((data.size() + 127) & ~size_t(127)), 0);
    f.insert(f.end(), rsrc.begin(), rsrc.end());
    return f;
}

std::vector<uint8_t> pattern(size_t n, uint8_t seed) {
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; i++) v[i] = uint8_t(seed + i * 7 + (i >> 8));
    return v;
}

} // namespace

int main() {
    using namespace hfsinject;
    std::string err;

    // ── MacBinary decoding ───────────────────────────────────────────────
    const std::vector<uint8_t> data = pattern(700, 1), rsrc = pattern(65298, 9);
    const std::vector<uint8_t> raw = macBinary("POM68KDisques", "APPL", "POMd", data, rsrc, 0x0100);
    MacBinary app;
    check(decodeMacBinary(raw, app, err), "MacBinary: decodes (" + err + ")");
    check(app.name == "POM68KDisques" && app.type == "APPL" && app.creator == "POMd",
          "MacBinary: name, type and creator");
    check(app.data == data && app.rsrc == rsrc, "MacBinary: both forks, the data fork padded to 128");
    check(app.finderFlags == 0x0100 && app.crDate == kNow - 86400, "MacBinary: Finder flags and dates");
    std::vector<uint8_t> bad(raw.begin(), raw.begin() + 200);
    check(!decodeMacBinary(bad, app, err), "MacBinary: a truncated file is refused");
    check(decodeMacBinary(raw, app, err), "MacBinary: decodes again");

    // ── The folder names the installer tries ─────────────────────────────
    // The French one is what a real System 7.5.5 names the folder
    // (GISTPERSO, gate lc520_agent_autostart_etalon), MacRoman é = $8E.
    check(startupItemsNames().size() == 2 && startupItemsNames()[0] == "Startup Items" &&
          startupItemsNames()[1] == "Ouverture au d\x8Emarrage",
          "installer: looks for Startup Items, then « Ouverture au démarrage »");
    check(compareNames(reinterpret_cast<const uint8_t*>("OUVERTURE AU D\x8EMARRAGE"), 22,
                       reinterpret_cast<const uint8_t*>("Ouverture au d\x8Emarrage"), 22) == 0,
          "collation: the French name matches case-insensitively, accent included");

    // ── A blank volume: no blessed folder, so nothing is installed ───────
    std::vector<uint8_t> img = hfsblank::build(8ull << 20, "Injecte");
    MemoryIo io(img);
    Outcome o = installStartupItem(io, app, kNow);
    check(o.kind == Outcome::NoFolder, "installer: a volume without a blessed folder is refused — " + o.message);

    // ── System Folder / Startup Items, blessed ───────────────────────────
    uint32_t start, length;
    check(findHfsVolume(io, start, length, err) && start == 0 && length == img.size() / 512,
          "volume: a bare HFS volume is found at block 0");
    uint32_t sysFolder = 0, items = 0;
    {
        Volume v(io, start, length);
        check(v.open(err), "volume: opens (" + err + ")");
        check(v.name() == "Injecte" && v.blessedFolder() == 0, "volume: name read, nothing blessed");
        const uint32_t cnid0 = v.nextCnid(), free0 = v.freeBlocks();
        check(v.createFolder(2, "System Folder", kNow, sysFolder, err), "folders: System Folder (" + err + ")");
        check(v.createFolder(sysFolder, "Startup Items", kNow, items, err), "folders: Startup Items (" + err + ")");
        check(sysFolder == cnid0 && items == cnid0 + 1, "folders: CNIDs handed out in order");
        check(!v.createFolder(2, "system folder", kNow, sysFolder, err),
              "folders: a second « system folder » is refused case-insensitively");
        v.bless(sysFolder);
        check(v.checkCatalog(err), "catalog: consistent after the folders (" + err + ")");
        check(v.freeBlocks() == free0, "folders: no allocation blocks used");
        check(v.commit(err), "volume: commit (" + err + ")");
    }
    {
        Volume v(io, start, length);
        check(v.open(err) && v.blessedFolder() == sysFolder, "volume: the blessing persisted");
        check(v.findFolder(2, "SYSTEM FOLDER") == sysFolder && v.findFolder(sysFolder, "startup items") == items,
              "folders: found back by name, case-insensitively");
        check(v.findFolder(2, "Startup Items") == 0, "folders: not found under the wrong parent");
    }

    // ── The installer, twice ─────────────────────────────────────────────
    o = installStartupItem(io, app, kNow);
    check(o.kind == Outcome::Installed, "installer: installed — " + o.message);
    o = installStartupItem(io, app, kNow);
    check(o.kind == Outcome::AlreadyPresent, "installer: idempotent — " + o.message);
    {
        Volume v(io, start, length);
        check(v.open(err), "volume: reopens");
        MacBinary back;
        check(v.readFile(items, "pom68kdisques", back, err), "file: read back by name (" + err + ")");
        check(back.data == data && back.rsrc == rsrc, "file: both forks intact through the extents");
        check(back.type == "APPL" && back.creator == "POMd" && back.finderFlags == 0x0100,
              "file: Finder info intact");
        check(back.crDate == kNow - 86400 && back.mdDate == kNow - 3600, "file: dates from the MacBinary");
        check(v.fileExists(items, "POM68KDisques") && !v.fileExists(sysFolder, "POM68KDisques"),
              "file: in Startup Items, not in the System Folder");
        check(v.nextCnid() == items + 2, "MDB: next CNID advanced once");
        check(v.checkCatalog(err), "catalog: consistent after the file (" + err + ")");
        const uint8_t* mdb = img.data() + 1024;
        check(mdb[84] == 0 && mdb[85] == 0 && mdb[86] == 0 && mdb[87] == 1, "MDB: one file counted");
        check(mdb[88] == 0 && mdb[89] == 0 && mdb[90] == 0 && mdb[91] == 2, "MDB: two folders counted");
        check(mdb[92] == 0 && mdb[93] == 0 && mdb[94] == 0 && mdb[95] == uint8_t(sysFolder),
              "MDB: the blessed folder");
        // The alternate MDB mirrors the primary.
        check(std::memcmp(mdb, img.data() + img.size() - 1024, 512) == 0, "MDB: the alternate is a copy");
    }

    // ── Sixty more files: splits, an index level, a root split ───────────
    {
        Volume v(io, start, length);
        check(v.open(err), "volume: reopens for the bulk");
        bool ok = true;
        for (int i = 0; i < 60 && ok; i++) {
            MacBinary f;
            char name[32];
            std::snprintf(name, sizeof name, "Item %02d de test", (i * 37) % 60);
            f.name = name; f.type = "TEXT"; f.creator = "ttxt";
            f.data = pattern(size_t(100 + i * 33), uint8_t(i));
            f.rsrc = (i % 3) ? pattern(size_t(50 + i), uint8_t(200 - i)) : std::vector<uint8_t>{};
            uint32_t cnid;
            ok = v.addFile(items, f, kNow + uint32_t(i), cnid, err);
        }
        check(ok, "bulk: sixty files added (" + err + ")");
        check(v.checkCatalog(err), "bulk: leaf chain ordered and counted (" + err + ")");
        check(v.commit(err), "bulk: commit (" + err + ")");
    }
    {
        Volume v(io, start, length);
        check(v.open(err), "volume: reopens after the bulk");
        bool all = true, forks = true;
        for (int i = 0; i < 60 && all; i++) {
            char name[32];
            std::snprintf(name, sizeof name, "item %02d DE TEST", i);
            MacBinary back;
            all = v.readFile(items, name, back, err);
            // The file written under this name had index j with (j*37)%60 == i.
            int j = 0;
            while ((j * 37) % 60 != i) j++;
            forks = forks && back.data == pattern(size_t(100 + j * 33), uint8_t(j)) &&
                    back.rsrc == ((j % 3) ? pattern(size_t(50 + j), uint8_t(200 - j)) : std::vector<uint8_t>{});
        }
        check(all, "bulk: every file found back through the split tree (" + err + ")");
        check(forks, "bulk: every fork intact");
        MacBinary back;
        check(v.readFile(items, "POM68KDisques", back, err) && back.rsrc == rsrc,
              "bulk: the Startup Item survived sixty insertions around it");
        check(v.checkCatalog(err), "bulk: still consistent (" + err + ")");
        const uint8_t* hdr = img.data() + 512 * (3 + 1) + 0;   // not used: the header lives in the catalog file
        (void)hdr;
        check(v.nextCnid() == items + 62, "MDB: CNIDs for all sixty-one files");
    }
    if (gFails) { std::printf("FAILED (%d)\n", gFails); return 1; }
    std::printf("PASS: folders, a Startup Item and sixty files inserted into a live catalog, all read back\n");
    return 0;
}
