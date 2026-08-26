// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Beyond-boot gates on the Quadra 605 (68040 + MEMCjr/DAFB + Cuda +
// TurboSCSI, Mac OS 8.1) — the second machine to get them, on the LC II
// template (`lcii_beyond_etalon.cpp`). The boot etalons prove the Finder
// appears; these prove the machine is USABLE. POM68K_BEYOND selects the
// scenario (one CTest entry each):
//   soak    — after the Finder, idle ~3 emulated minutes: the low-memory
//             Time global ($20C) must advance in step, no halt, and the
//             Finder signature must still hold at the end.
//   persist — Cmd-N in the Finder creates a folder ("untitled folder" on
//             the 8.1 image): the SCSI image bytes must change, the new
//             catalog name must appear, and after a hard reset the machine
//             must boot back to the Finder off the modified volume.
//             This is the only gate that drives the 53C96 WRITE path end
//             to end from a real guest (everything else boots read-only).
// Keys are HELD 150 frames (~2.5 s): accepted by a Slow-Keys guest and a
// normal one alike, so the gate stays green whether or not the image's
// Easy Access state regresses (see q605_cudalle_key_etalon's header).
// POM68K_DUMP=1 writes q605_beyond_<mode>.ppm for eyeballing/calibration.
// Soft-skips without the FF7439EE ROM + hdv/MacOS-8.1-boot.vhd.

#include "AssetFingerprint.h"
#include "FolderProbe.h"
#include "Cpu040.h"
#include "Q605Memory.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {
std::string findAsset(std::initializer_list<const char*> names) {
    return testasset::findAny(names);
}

Q605Memory* gMem;
Cpu040* gCpu;
constexpr int kFrameCycles = 416667;          // 25 MHz / ~60 Hz

void runFrames(long n) {
    for (long f = 0; f < n && !gCpu->isHalted(); f++)
        gCpu->runCycles(kFrameCycles);
}

uint32_t peek32(uint32_t addr) {
    return uint32_t(gMem->peek8(addr)) << 24 |
           uint32_t(gMem->peek8(addr + 1)) << 16 |
           uint32_t(gMem->peek8(addr + 2)) << 8 |
           gMem->peek8(addr + 3);
}

// Low-memory Time global ($20C, seconds since 1904) — proves the one-
// second chain (VIA → Cuda RTC → Time Manager) stays alive.
uint32_t macTime() { return peek32(0x20C); }

struct Screen {
    int width = 0, height = 0, depth = 0;
    uint32_t stride = 0, offset = 0;
    std::vector<uint32_t> pixels;
};

// Same decode as q605_boot_etalon: the main GDevice's PixMap through the
// DAFB CLUT — the guest's own idea of the screen, not a hardcoded mode.
Screen decodeScreen(const Q605Memory& mem) {
    Screen s;
    uint32_t scrnBase = peek32(0x0824);
    uint32_t mainDevH = peek32(0x08A4);
    uint32_t mainDev = mainDevH ? peek32(mainDevH) : 0;
    uint32_t pmapH = mainDev ? peek32(mainDev + 0x16) : 0;
    uint32_t pmap = pmapH ? peek32(pmapH) : 0;
    if (!pmap) return s;

    uint32_t pmBase = peek32(pmap);
    uint32_t boundsA = peek32(pmap + 0x06);
    uint32_t boundsB = peek32(pmap + 0x0A);
    int top = int(boundsA >> 16), left = int(boundsA & 0xFFFF);
    int bottom = int(boundsB >> 16), right = int(boundsB & 0xFFFF);
    s.width = right - left;
    s.height = bottom - top;
    s.depth = mem.dafbDepth();
    s.stride = mem.dafbStride();
    s.offset = (pmBase ? pmBase : scrnBase) & (Q605Memory::kVramSize - 1);
    if (s.width <= 0 || s.width > 1600 || s.height <= 0 || s.height > 1200 ||
        (s.depth != 1 && s.depth != 2 && s.depth != 4 && s.depth != 8) ||
        s.stride < uint32_t((s.width * s.depth + 7) / 8) ||
        uint64_t(s.offset) + uint64_t(s.height) * s.stride > Q605Memory::kVramSize) {
        s = {};
        return s;
    }

    const uint8_t* vram = mem.vram();
    const uint8_t (*clut)[3] = mem.clut();
    s.pixels.resize(size_t(s.width) * s.height);
    for (int y = 0; y < s.height; y++) {
        uint32_t row = s.offset + uint32_t(y) * s.stride;
        for (int x = 0; x < s.width; x++) {
            uint8_t packed = vram[row + uint32_t(x * s.depth / 8)];
            uint8_t pen;
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
Stats luminanceStats(const Screen& s, int x0, int x1, int y0, int y1) {
    if (x1 > s.width) x1 = s.width;
    if (y1 > s.height) y1 = s.height;
    if (x0 >= x1 || y0 >= y1) return {};
    double sum = 0, sum2 = 0;
    long count = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            uint32_t p = s.pixels[size_t(y) * s.width + x];
            double lum = ((p >> 16) * 54 + ((p >> 8) & 0xFF) * 183 +
                          (p & 0xFF) * 19) / 256.0;
            sum += lum; sum2 += lum * lum; count++;
        }
    Stats r;
    if (count) {
        r.mean = sum / count;
        r.deviation = std::sqrt(sum2 / count - r.mean * r.mean);
    }
    return r;
}

// The full q605_boot_etalon Finder signature (menu bar AND desktop): the
// idle-Finder check used at boot and after the persist reboot.
bool finderUp(const Screen& s) {
    if (s.width != 640 || s.height != 480 || s.depth != 8) return false;
    Stats m = luminanceStats(s, 0, s.width, 2, 16);
    Stats d = luminanceStats(s, 520, 630, 40, 430);
    return m.mean > 170 && m.mean < 235 &&
           m.deviation > 40 && m.deviation < 100 &&
           d.mean > 100 && d.mean < 190 &&
           d.deviation > 30 && d.deviation < 90 &&
           m.mean - d.mean > 35;
}

// The menu bar stays visible whatever windows are open — the liveness
// check once the guest has been made to open/create something (a new
// folder's window or icon changes the desktop sample).
bool menuBarUp(const Screen& s) {
    if (s.width != 640 || s.height != 480) return false;
    Stats m = luminanceStats(s, 0, s.width, 2, 16);
    return m.mean > 170 && m.mean < 235 && m.deviation > 40;
}

void dump(const char* name, const Screen& s) {
    if (!getenv("POM68K_DUMP") || s.pixels.empty()) return;
    FILE* fp = fopen(name, "wb");
    std::fprintf(fp, "P6\n%d %d\n255\n", s.width, s.height);
    for (uint32_t p : s.pixels) {
        uint8_t rgb[3] = { uint8_t(p >> 16), uint8_t(p >> 8), uint8_t(p) };
        fwrite(rgb, 1, 3, fp);
    }
    fclose(fp);
}

long countNeedle(const std::vector<uint8_t>& hay, const char* needle) {
    size_t n = std::strlen(needle);
    long c = 0;
    for (size_t i = 0; i + n <= hay.size(); i++)
        if (std::memcmp(&hay[i], needle, n) == 0) c++;
    return c;
}

// Boot (or reboot) to a settled Finder: the boot etalon's sparse-sampling
// loop, stopping only on the full signature. The persist reboot mounts a
// volume the hard reset left dirty (drVolAtrb bit 8 clear), and 8.1's
// consistency pass makes that boot slower than a clean one — hence the
// caller-chosen frame budget. On failure the screen is described, because
// "menu ~230/dev ~8" is the known dirty-volume-stall signature and worth
// telling apart from a black screen or a halted CPU.
bool bootToFinder(int maxFrames) {
    while (gMem->cpuHeld()) gMem->tick(1000);
    long scsi0 = gMem->scsi().commands;      // counters survive mem.reset()
    for (int frame = 0; frame < maxFrames && !gCpu->isHalted(); frame++) {
        gCpu->runCycles(kFrameCycles);
        if (frame >= 3600 && !(frame % 60) &&
            gMem->scsi().commands - scsi0 > 4000) {
            Screen s = decodeScreen(*gMem);
            if (finderUp(s)) {
                runFrames(300);              // ~5 s to settle after the paint
                return !gCpu->isHalted();
            }
        }
    }
    Screen s = decodeScreen(*gMem);
    if (s.pixels.empty()) {
        std::fprintf(stderr, "[boot] no decodable screen after %d frames "
                     "(halted=%d SCSI +%ld)\n", maxFrames, gCpu->isHalted(),
                     gMem->scsi().commands - scsi0);
    } else {
        Stats m = luminanceStats(s, 0, s.width, 2, 16);
        Stats d = luminanceStats(s, 520, 630, 40, 430);
        std::fprintf(stderr, "[boot] no Finder after %d frames: %dx%d@%d "
                     "menu %.1f/%.1f desktop %.1f/%.1f (halted=%d SCSI +%ld)\n",
                     maxFrames, s.width, s.height, s.depth, m.mean, m.deviation,
                     d.mean, d.deviation, gCpu->isHalted(),
                     gMem->scsi().commands - scsi0);
        dump("q605_beyond_bootfail.ppm", s);
    }
    return false;
}
} // namespace

int main() {
    const std::string mode = getenv("POM68K_BEYOND") ? getenv("POM68K_BEYOND")
                                                     : "soak";
    std::string romPath = findAsset({
        "roms/1MB ROMs/1993-10 - FF7439EE - LC475,575,Quadra 605,Performa 475,476,575,577,578.ROM",
        "roms/mame/macqd605/ff7439ee.bin",
        "roms/quadra605.rom", "roms/q605.rom"
    });
    std::string diskPath = findAsset({
        "hdv/MacOS-8.1-boot.vhd", "hdv/q605-boot.vhd"
    });
    if (romPath.empty() || diskPath.empty()) {
        std::printf("SKIP: needs FF7439EE ROM + hdv/MacOS-8.1-boot.vhd\n");
        return 0;
    }
    testasset::report({ romPath, diskPath });

    std::ifstream in(romPath, std::ios::binary);
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
    if (rom.size() != Q605Memory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM is %zu bytes, want 1 MB\n", rom.size());
        return 1;
    }

    Q605Memory mem(pom68k::defaultCoreConfig(), 32u << 20);
    if (!mem.loadRom(rom) || !mem.attachScsi(diskPath)) {
        std::fprintf(stderr, "FAIL: could not load ROM/disk\n");
        return 1;
    }
    Cpu040 cpu(mem, jit::defaultResolvedConfig(),
               pom68k::defaultCoreConfig().cpu,
               pom68k::defaultCoreConfig().diagnostics);
    mem.setCpu(&cpu);
    cpu.hardReset();
    gMem = &mem; gCpu = &cpu;

    if (!bootToFinder(12000)) {
        std::fprintf(stderr, "FAIL: no Finder after boot (halted=%d SCSI=%ld)\n",
                     cpu.isHalted(), mem.scsi().commands);
        return 1;
    }
    std::printf("Finder up (SCSI %ld, Cuda %s)\n", mem.scsi().commands,
                mem.cudaLleActive() ? "LLE" : "HLE");

    // ADB code, press + release; the 150-frame hold outlasts a Slow Keys
    // acceptance delay, and a normal keyboard accepts it just the same.
    auto keyHold = [&](uint8_t code, int frames) {
        mem.keyEvent(code, true);
        runFrames(frames);
        mem.keyEvent(code, false);
        runFrames(6);
    };

    bool ok = false;

    if (mode == "soak") {
        // ~3 emulated minutes idle. Time must track (±25% — the 60 Hz tick
        // vs frames), the CPU must not halt, the Finder must still be there.
        const long kSoak = 10800;            // 180 s of 60 Hz frames
        uint32_t t0 = macTime();
        int64_t mcu0 = mem.cudaLleActive() ? mem.cudaLle().mcu().cycleCount() : 0;
        runFrames(kSoak);
        uint32_t t1 = macTime();
        long dt = long(t1 - t0);
        if (mem.cudaLleActive()) {
            // The MCU-overclock class of bug (CudaLle mcuDebt_, CHANGELOG
            // 2026-07-29) is invisible to boot gates and shows here as a
            // Mac-clock rate error — the assert on dt is what catches it.
            int64_t dm = mem.cudaLle().mcu().cycleCount() - mcu0;
            std::fprintf(stderr, "[soak] mcu cycles %lld over 180 s\n",
                         (long long)dm);
        }
        Screen s = decodeScreen(mem);
        dump("q605_beyond_soak.ppm", s);
        std::printf("soak: %ld s elapsed on the Mac clock (want 135-225), "
                    "halted=%d, Finder %s\n", dt, cpu.isHalted(),
                    finderUp(s) ? "up" : "GONE");
        ok = !cpu.isHalted() && dt >= 135 && dt <= 225 && finderUp(s);
    } else if (mode == "persist") {
        std::vector<uint8_t>& disk = mem.scsiDisk().image();
        // Mac OS 8.1 (US image) names a new folder "untitled folder"; the
        // localized names are accepted so the gate follows the volume, not
        // this comment. Full phrases only — the bare "untitled" substring
        // occurs hundreds of times in a stock volume and drowns the signal.
        long before[folderprobe::kCount];
        folderprobe::sample(disk, before, "before");
        std::vector<uint8_t> snap = disk;
        long dma0 = mem.scsi().dmaBytes;
        // Cmd-N (New Folder): the folder hits the catalog on creation; the
        // Return commits the still-editable default name.
        mem.keyEvent(0x37, true);            // Cmd down
        runFrames(6);
        keyHold(0x2D, 150);                  // 'n', held
        mem.keyEvent(0x37, false);
        runFrames(120);                      // let the rename field appear
        keyHold(0x24, 150);                  // Return — commit the name
        runFrames(900);                      // ~15 s: create + flush catalog
        long after[folderprobe::kCount];
        folderprobe::sample(disk, after, "after");
        const size_t grew = folderprobe::grew(before, after);
        bool wrote = disk != snap;
        std::printf("persist: %s, image %s, 53C96 DMA +%ld B\n",
                    grew < folderprobe::kCount
                        ? (std::string("'") + folderprobe::kNames[grew] + "' " +
                           std::to_string(before[grew]) + " -> " +
                           std::to_string(after[grew])).c_str()
                        : "NO candidate folder name appeared",
                    wrote ? "modified" : "UNCHANGED",
                    mem.scsi().dmaBytes - dma0);
        Screen s = decodeScreen(mem);
        dump("q605_beyond_persist.ppm", s);
        if (!menuBarUp(s))
            std::fprintf(stderr, "[persist] menu bar GONE after the gesture\n");
        // Reboot on the modified volume: it must still reach the Finder.
        // The volume is deliberately dirty (no clean unmount before the
        // reset) — surviving THAT is part of what "persist" claims.
        cpu.hardReset();
        bool rebooted = bootToFinder(24000);
        long survived[folderprobe::kCount];
        folderprobe::sample(disk, survived, "reboot");
        const bool kept = grew < folderprobe::kCount &&
                          survived[grew] > before[grew];
        std::printf("persist: reboot %s, folder %s\n",
                    rebooted ? "reached the Finder" : "FAILED",
                    kept ? "survived" : "did NOT survive");
        ok = wrote && grew < folderprobe::kCount && rebooted && kept;
    } else {
        std::fprintf(stderr, "FAIL: unknown POM68K_BEYOND=%s\n", mode.c_str());
        return 1;
    }

    std::printf("%s — Quadra 605 beyond-boot %s\n", ok ? "PASSED" : "FAILED",
                mode.c_str());
    return ok ? 0 : 1;
}
