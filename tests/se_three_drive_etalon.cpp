// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The SE's three floppy mechanisms, as its own ROM numbers them.
//
// The SE board puts two internal connectors behind the IWM's ENABLE1 and
// selects between them with VIA1 PA4; ENABLE2 reaches the external port
// (Iwm.h § PA4 internal-connector line, SE ROM DiskSelect at $35316).
// Before that line was wired, both internal slots of the ROM's Sony driver
// reached the same mechanism and the Finder mounted the boot disk twice.
//
// SE / SE FDHD: three distinct 800K disks, one per mechanism, must give
// three drive-queue entries on the .Sony driver (refNum -5) and three
// mounted volumes with three different names on three different drives.
// `--single` (the single-floppy SE with its hard disk) and the Classic
// (POM68K_COMPACT_MODEL=classic): their ROMs drive the same PA4 line, but
// nothing answers PA4 high — exactly two .Sony drives, the boot volume
// mounted once as drive 1, and the second-internal insert refused.
// Soft-skips unless the ROM and the three disks are present.

#include "AssetFingerprint.h"
#include "Cpu68k.h"
#include "JitTestConfig.h"
#include "MacFrame.h"
#include "MacMemory.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace {

struct Drive { int num = 0; int refNum = 0; };
struct Volume { std::string name; int drive = 0; };

uint16_t be16(MacMemory& m, uint32_t a) {
    return uint16_t(m.peek8(a) << 8 | m.peek8(a + 1));
}
uint32_t be32(MacMemory& m, uint32_t a) {
    return uint32_t(be16(m, a)) << 16 | be16(m, a + 2);
}

// Inside Macintosh: Files — DrvQHdr $308, VCBQHdr $356; QHdr.qHead at +2.
// DrvQEl: dQDrive +6, dQRefNum +8. VCB: vcbVN +44 (Str27), vcbDrvNum +72.
std::vector<Drive> driveQueue(MacMemory& m) {
    std::vector<Drive> out;
    for (uint32_t p = be32(m, 0x30A) & 0xFFFFFF; p && out.size() < 16;
         p = be32(m, p) & 0xFFFFFF)
        out.push_back({ int(be16(m, p + 6)), int(int16_t(be16(m, p + 8))) });
    return out;
}
std::vector<Volume> vcbQueue(MacMemory& m) {
    std::vector<Volume> out;
    for (uint32_t p = be32(m, 0x358) & 0xFFFFFF; p && out.size() < 16;
         p = be32(m, p) & 0xFFFFFF) {
        const int len = std::min<int>(m.peek8(p + 44), 27);
        std::string name;
        for (int i = 0; i < len; i++) name += char(m.peek8(p + 45 + uint32_t(i)));
        out.push_back({ name, int(int16_t(be16(m, p + 72))) });
    }
    return out;
}

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), {});
}

}  // namespace

int main(int argc, char** argv) {
    const bool single = argc > 1 && !std::strcmp(argv[1], "--single");
    const char* which = getenv("POM68K_COMPACT_MODEL");
    MacMemory::Model model = MacMemory::Model::SE;
    const char* romRel = "roms/256KB ROMs/1987-03 - B2E362A8 - Mac SE.ROM";
    const char* name = "Macintosh SE";
    if (which && !std::strcmp(which, "sefdhd")) {
        model = MacMemory::Model::SEFDHD;
        romRel = "roms/256KB ROMs/1989-08 - B306E171 - Mac SE FDHD.ROM";
        name = "Macintosh SE FDHD";
    } else if (which && !std::strcmp(which, "classic")) {
        model = MacMemory::Model::Classic;
        romRel = "roms/512KB ROMs/1990-10 - A49F9914 - Mac Classic.rom";
        name = "Macintosh Classic";
    }
    const bool three = !single && model != MacMemory::Model::Classic;

    const std::string rom = testasset::find(romRel);
    const std::string boot = testasset::find("disks35/Disk605.dsk");
    const std::string lower = testasset::find("disks35/Rogue.dsk");
    const std::string ext = testasset::find("disks35/BonjourPommeOne.dsk");
    if (rom.empty() || boot.empty() || lower.empty() || ext.empty()) {
        std::printf("SKIP: needs %s + disks35/{Disk605,Rogue,BonjourPommeOne}.dsk\n", romRel);
        return 0;
    }
    testasset::report({ rom, boot, lower, ext });

    pom68k::CoreConfig core = pom68k::defaultCoreConfig();
    core.storage.secondInternalFloppy = !single;   // ignored by the Classic
    MacMemory mem(core, model);
    if (!mem.loadRom(readFile(rom))) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    const jit::ResolvedConfig jitConfig = testjit::resolveFromEnvironment();
    Cpu68k cpu(mem, jitConfig);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.insertDisk(boot) || !mem.insertExternalDisk(ext)) {
        std::fprintf(stderr, "FAIL: bad disk\n");
        return 1;
    }
    if (mem.insertSecondInternalDisk(lower) != three) {
        std::fprintf(stderr, "FAIL: second internal insert %s on %s\n",
                     three ? "refused" : "accepted", name);
        return 1;
    }

    const long kFrames = getenv("POM68K_FRAMES") ? atol(getenv("POM68K_FRAMES")) : 6000;
    MacFrameClock fc;
    fc.resync(cpu);
    for (long f = 0; f < kFrames; f++) fc.runFrame(cpu, mem);

    // The ROM tries physical slot 1 — the PA4-high mechanism, drive 1 —
    // first. Rogue carries no System, so a real SE ejects it and starts
    // up from drive 2; the user then puts it back.
    if (three) {
        if (mem.secondInternalDrive().hasDisk()) {
            std::fprintf(stderr, "FAIL: the non-startup disk in drive 1 was not ejected\n");
            return 1;
        }
        if (mem.secondInternalDrive().nibblesRead == 0) {
            std::fprintf(stderr, "FAIL: the ROM never read the PA4-high mechanism\n");
            return 1;
        }
        std::printf("drive 1 (PA4 high) tried first and ejected its non-startup disk\n");
        if (!mem.insertSecondInternalDisk(lower)) {
            std::fprintf(stderr, "FAIL: reinsert refused\n");
            return 1;
        }
        for (long f = 0; f < 900; f++) fc.runFrame(cpu, mem);
    }

    const std::vector<Drive> drives = driveQueue(mem);
    const std::vector<Volume> vols = vcbQueue(mem);
    std::set<int> sony;
    for (const Drive& d : drives) {
        std::printf("drive %d refNum %d\n", d.num, d.refNum);
        if (d.refNum == -5) sony.insert(d.num);
    }
    std::set<std::string> names;
    std::set<int> volDrives;
    for (const Volume& v : vols) {
        std::printf("volume \"%s\" on drive %d\n", v.name.c_str(), v.drive);
        names.insert(v.name);
        volDrives.insert(v.drive);
    }
    std::printf("%s: %zu .Sony drives, %zu volumes; nibbles read low %ld high %ld ext %ld\n",
                name, sony.size(), vols.size(), mem.internalDrive().nibblesRead,
                mem.secondInternalDrive().nibblesRead, mem.externalDrive().nibblesRead);

    const size_t want = three ? 3 : 2;
    if (sony.size() != want) {
        std::fprintf(stderr, "FAIL: %zu .Sony drives, want %zu\n", sony.size(), want);
        return 1;
    }
    if (vols.size() != want || names.size() != want || volDrives.size() != want) {
        std::fprintf(stderr, "FAIL: want %zu distinct volumes on %zu drives\n", want, want);
        return 1;
    }
    if (!three && mem.secondInternalDrive().nibblesRead != 0) {
        std::fprintf(stderr, "FAIL: %s read an unfitted mechanism\n", name);
        return 1;
    }
    // The ROM's slot 1 is empty, so its drive translation makes the
    // PA4-low mechanism drive 1.
    if (!three) {
        bool bootOnOne = false;
        for (const Volume& v : vols)
            if (v.name == "System Tools" && v.drive == 1) bootOnOne = true;
        if (!bootOnOne) {
            std::fprintf(stderr, "FAIL: the boot volume is not drive 1\n");
            return 1;
        }
    }
    std::printf("PASS: %s mounts %zu distinct floppies\n", name, want);
    return 0;
}
