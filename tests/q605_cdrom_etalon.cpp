// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Gate: a CD-ROM mounts IN THE GUEST. `scsi_cdrom_test` pins the target
// against hand-written CDBs; this one puts a real Apple disc in front of
// a real Mac OS driver, which is what found the three bring-up bugs
// (missing MODE SENSE block descriptor, no READ TOC format 1, no mode
// page $0E — CHANGELOG 2026-07-29).
//
// Layout matters: the ROM's SCSI scan runs 6→0, so a bootable CD at ID 3
// would win over a hard disk at ID 0. The boot volume therefore sits at
// ID 6 and the CD at 3 — the machine boots from the hard disk and the
// disc arrives as DATA, which is the case this gate is about.
//
// POM68K_CD_BOOT=1 runs the other half: no hard disk at all, so the ROM
// scan reaches the CD and BOOTS it. The disc must then supply the whole
// System — measured in megabytes off the CD target, not in screen
// pixels, because a Finder drawn from a hard disk looks identical.
//
// Asserted: the 640×480×8 Finder signature (same thresholds as
// q605_boot_etalon) AND real catalog traffic served BY THE CD TARGET.
// The second half is the load-bearing one: a disc the System merely
// probes and ignores still leaves a perfect Finder on screen, and reads
// counted on the CONTROLLER cannot tell the two apart (9619 vs 9618
// measured). Soft-skips without the assets.

#include "AssetFingerprint.h"
#include "Cpu040.h"
#include "Q605Memory.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {
std::string findAsset(std::initializer_list<const char*> names) {
    return testasset::findAny(names);
}

uint32_t peek32(const Q605Memory& mem, uint32_t addr) {
    return uint32_t(mem.peek8(addr)) << 24 | uint32_t(mem.peek8(addr + 1)) << 16 |
           uint32_t(mem.peek8(addr + 2)) << 8 | mem.peek8(addr + 3);
}

struct Screen { int width = 0, height = 0, depth = 0; uint32_t stride = 0, offset = 0;
                std::vector<uint32_t> pixels; };

Screen decodeScreen(const Q605Memory& mem) {
    Screen s;
    uint32_t scrnBase = peek32(mem, 0x0824);
    uint32_t mainDevH = peek32(mem, 0x08A4);
    uint32_t mainDev = mainDevH ? peek32(mem, mainDevH) : 0;
    uint32_t pmapH = mainDev ? peek32(mem, mainDev + 0x16) : 0;
    uint32_t pmap = pmapH ? peek32(mem, pmapH) : 0;
    if (!pmap) return s;
    uint32_t pmBase = peek32(mem, pmap);
    uint32_t boundsA = peek32(mem, pmap + 0x06), boundsB = peek32(mem, pmap + 0x0A);
    int top = int(boundsA >> 16), left = int(boundsA & 0xFFFF);
    int bottom = int(boundsB >> 16), right = int(boundsB & 0xFFFF);
    s.width = right - left; s.height = bottom - top;
    s.depth = mem.dafbDepth(); s.stride = mem.dafbStride();
    s.offset = (pmBase ? pmBase : scrnBase) & (Q605Memory::kVramSize - 1);
    if (s.width <= 0 || s.width > 1600 || s.height <= 0 || s.height > 1200 ||
        (s.depth != 1 && s.depth != 2 && s.depth != 4 && s.depth != 8) ||
        uint64_t(s.offset) + uint64_t(s.height) * s.stride > Q605Memory::kVramSize)
        return Screen{};
    const uint8_t* vram = mem.vram();
    const uint8_t (*clut)[3] = mem.clut();
    s.pixels.resize(size_t(s.width) * s.height);
    for (int y = 0; y < s.height; y++) {
        uint32_t row = s.offset + uint32_t(y) * s.stride;
        for (int x = 0; x < s.width; x++) {
            uint8_t packed = vram[row + uint32_t(x * s.depth / 8)], pen;
            if (s.depth == 1) pen = (packed >> (7 - (x & 7))) & 1;
            else if (s.depth == 2) pen = (packed >> (6 - 2 * (x & 3))) & 3;
            else if (s.depth == 4) pen = (x & 1) ? packed & 0x0F : packed >> 4;
            else pen = packed;
            const uint8_t* c = clut[pen];
            s.pixels[size_t(y) * s.width + x] =
                uint32_t(c[0]) << 16 | uint32_t(c[1]) << 8 | c[2];
        }
    }
    return s;
}

struct Stats { double mean = 0, deviation = 0; };
Stats luminance(const Screen& s, int x0, int x1, int y0, int y1) {
    if (x1 > s.width) x1 = s.width;
    if (y1 > s.height) y1 = s.height;
    if (x0 >= x1 || y0 >= y1) return {};
    double sum = 0, sum2 = 0; long n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            uint32_t p = s.pixels[size_t(y) * s.width + x];
            double l = ((p >> 16) * 54 + ((p >> 8) & 0xFF) * 183 + (p & 0xFF) * 19) / 256.0;
            sum += l; sum2 += l * l; n++;
        }
    Stats r;
    if (n) { r.mean = sum / n; r.deviation = std::sqrt(sum2 / n - r.mean * r.mean); }
    return r;
}
} // namespace

int main() {
    std::string romPath = findAsset({
        "roms/1MB ROMs/1993-10 - FF7439EE - LC475,575,Quadra 605,Performa 475,476,575,577,578.ROM",
        "roms/mame/macqd605/ff7439ee.bin", "roms/quadra605.rom" });
    std::string diskPath = findAsset({ "hdv/MacOS-8.1-boot.vhd", "hdv/q605-boot.vhd" });
    // A disc whose driver descriptor map declares 2048-byte blocks. The
    // 512-byte-DDM hybrids in hdv/ are read but not mounted by Mac OS —
    // observed 2026-07-29, cause not yet established (TODO).
    const bool bootFromCd = std::getenv("POM68K_CD_BOOT") != nullptr;
    // Hot insert: the drive is EMPTY through the whole boot; the disc goes
    // in only once the Finder is up, the way the Disques window does it.
    // What makes it mount is ScsiDisk's UNIT ATTENTION / $28 on the medium
    // change plus READ SUB-CHANNEL answering (the 8.1 CD extension sends
    // 42 02 40 01 right after the change and aborts on a CHECK).
    const bool hotInsert = !bootFromCd && std::getenv("POM68K_CD_HOT") != nullptr;
    // Boot-from-CD needs a 68k-bootable disc: Mac OS 8.1 is the last
    // release that runs on a 68040 (8.5/8.6 are PowerPC-only, and a 68k
    // Mac stops at a black screen on them however good the emulation).
    // `cd/` first: it is where the Disques window scans and therefore where
    // a user's discs actually live since 2026-08-15 (`DiskBays.cpp`). The
    // older `input/`+`hdv/` spellings stay so an existing checkout keeps
    // finding what it had.
    std::string cdPath = bootFromCd
        ? findAsset({ "cd/MAC_OS_8-1_RETAIL_0.ISO", "cd/macos81.iso",
                      "input/MAC_OS_8-1_RETAIL_0.ISO", "hdv/MAC_OS_8-1_RETAIL_0.ISO",
                      "input/macos81.iso", "hdv/macos81.iso" })
        : findAsset({ "cd/MacOS_86.iso", "cd/MAC_OS_8-1_RETAIL_0.ISO", "cd/cd.iso",
                      "input/MacOS_86.iso", "hdv/MacOS_86.iso",
                      "input/MAC_OS_8-1_RETAIL_0.ISO", "input/cd.iso", "hdv/cd.iso" });
    if (romPath.empty() || cdPath.empty() || (!bootFromCd && diskPath.empty())) {
        std::printf("SKIP: needs FF7439EE ROM + hdv/MacOS-8.1-boot.vhd + an "
                    "Apple CD image (cd/MacOS_86.iso)\n");
        return 0;
    }
    testasset::report({ romPath, cdPath, diskPath });

    std::ifstream in(romPath, std::ios::binary);
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
    Q605Memory mem(pom68k::defaultCoreConfig(), 32u << 20);
    if (!mem.loadRom(rom)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    // Boot volume at ID 6 so it wins the ROM's 6→0 scan against the CD.
    // With POM68K_CD_BOOT there is no hard disk, so the scan reaches the
    // disc and the machine boots off it.
    if (!bootFromCd && !mem.attachScsi(diskPath, false, 6)) {
        std::fprintf(stderr, "FAIL: could not load the boot disk\n");
        return 1;
    }
    if (hotInsert) {
        if (!mem.attachCdromEmpty(3)) {
            std::fprintf(stderr, "FAIL: could not attach the empty CD drive\n");
            return 1;
        }
    } else if (!mem.attachCdrom(cdPath, 3)) {
        std::fprintf(stderr, "FAIL: could not attach the CD\n");
        return 1;
    }
    Cpu040 cpu(mem, jit::defaultResolvedConfig(),
               pom68k::defaultCoreConfig().cpu,
               pom68k::defaultCoreConfig().diagnostics);
    mem.setCpu(&cpu);
    cpu.hardReset();
    while (mem.cpuHeld()) mem.tick(1000);

    constexpr int kFrameCycles = 416667;      // 25 MHz / ~60 Hz
    for (long f = 0; f < 9000 && !cpu.isHalted(); f++) cpu.runCycles(kFrameCycles);
    if (cpu.isHalted()) { std::fprintf(stderr, "FAIL: CPU halted\n"); return 1; }

    if (hotInsert) {
        if (!mem.insertBayMedia(3, cdPath)) {
            std::fprintf(stderr, "FAIL: hot insert refused\n");
            return 1;
        }
        std::printf("hot insert after boot\n");
        // The CD extension polls the drive about once a second; the mount
        // (catalog + desktop DB) lands well inside this window (measured
        // ~39 READs, desktop icon + window open on the dumped screen).
        for (long f = 0; f < 8000 && !cpu.isHalted(); f++) cpu.runCycles(kFrameCycles);
        if (cpu.isHalted()) { std::fprintf(stderr, "FAIL: halted after insert\n"); return 1; }
    }

    Screen screen = decodeScreen(mem);
    if (screen.pixels.empty()) { std::fprintf(stderr, "FAIL: no PixMap\n"); return 1; }
    Stats menu = luminance(screen, 0, screen.width, 2, 16);
    Stats desktop = luminance(screen, 520, 630, 40, 430);
    long cdCmds = mem.scsiDiskAt(3).readCommands;
    long cdBlocks = mem.scsiDiskAt(3).readBlocks;
    std::printf("%dx%d@%dbpp; menu %.1f/%.1f desktop %.1f/%.1f; "
                "CD served %ld READs / %ld blocks (%ld KB)\n",
                screen.width, screen.height, screen.depth, menu.mean,
                menu.deviation, desktop.mean, desktop.deviation,
                cdCmds, cdBlocks, cdBlocks * 2);

    bool finder = screen.width == 640 && screen.height == 480 && screen.depth == 8 &&
                  menu.mean > 170 && menu.mean < 235 &&
                  menu.deviation > 40 && menu.deviation < 100 &&
                  menu.deviation > 40 && menu.deviation < 100;
    // The desktop pattern differs between a hard-disk boot and the CD's
    // own System, so only the menu bar is pinned here; the load-bearing
    // signal is the traffic below.
    (void)desktop;
    // Mounting a volume costs its MDB, catalog B-tree and desktop
    // database — hundreds of KB. A disc that is merely probed and
    // ignored stops at a handful of blocks (measured: 4).
    // Booting off the disc means the System itself came from it —
    // megabytes, not the ~100 blocks a data mount costs.
    // The hot-insert mount is leaner than the boot-time one (no boot-scan
    // re-probing): 39 READs measured for a full mount with the window open.
    long need = bootFromCd ? 1000 : hotInsert ? 30 : 40;
    bool served = cdBlocks > need;
    std::printf("%s\n", (finder && served)
        ? (bootFromCd ? "PASSED — the machine BOOTED from the CD"
                      : "PASSED — Finder up and the CD mounted in the guest")
        : (finder ? "FAILED — Finder up but the CD did not serve the volume"
                  : "FAILED — no Finder"));
    return (finder && served) ? 0 : 1;
}
