// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Quadra 630 / LC 580 gate: boot the 06684214 ROM and a Mac OS disk to the
// 640x480x8 Finder desktop. Soft-skips when the user-provided assets are
// absent. This exercises the 68040/MMU/FPU on the F108 bus with PrimeTime II,
// the Valkyrie fixed-mode framebuffer, Cuda 341S0060 LLE/ADB, the IOSB ASC,
// pseudo-VIA2 and TurboSCSI. POM68K_Q630_ID=A55A225A selects the LC 580.

#include "AssetFingerprint.h"
#include "Q630Cpu.h"
#include "Q630Memory.h"

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

uint32_t peek32(const Q630Memory& mem, uint32_t addr) {
    return uint32_t(mem.peek8(addr)) << 24 |
           uint32_t(mem.peek8(addr + 1)) << 16 |
           uint32_t(mem.peek8(addr + 2)) << 8 |
           mem.peek8(addr + 3);
}

struct Screen {
    int width = 0, height = 0, depth = 0;
    uint32_t stride = 0, offset = 0;
    std::vector<uint32_t> pixels;
};

Screen decodeScreen(const Q630Memory& mem) {
    Screen s;
    uint32_t scrnBase = peek32(mem, 0x0824);
    uint32_t mainDevH = peek32(mem, 0x08A4);
    uint32_t mainDev = mainDevH ? peek32(mem, mainDevH) : 0;
    uint32_t pmapH = mainDev ? peek32(mem, mainDev + 0x16) : 0;
    uint32_t pmap = pmapH ? peek32(mem, pmapH) : 0;
    if (!pmap) return s;

    uint32_t pmBase = peek32(mem, pmap);
    uint32_t boundsA = peek32(mem, pmap + 0x06);
    uint32_t boundsB = peek32(mem, pmap + 0x0A);
    int top = int(boundsA >> 16), left = int(boundsA & 0xFFFF);
    int bottom = int(boundsB >> 16), right = int(boundsB & 0xFFFF);
    s.width = right - left;
    s.height = bottom - top;
    s.depth = mem.videoDepth();
    s.stride = mem.videoStride();
    s.offset = (pmBase ? pmBase : scrnBase) & (Q630Memory::kVramSize - 1);
    if (s.width <= 0 || s.width > 1600 || s.height <= 0 || s.height > 1200 ||
        (s.depth != 1 && s.depth != 2 && s.depth != 4 && s.depth != 8) ||
        s.stride < uint32_t((s.width * s.depth + 7) / 8) ||
        uint64_t(s.offset) + uint64_t(s.height) * s.stride > Q630Memory::kVramSize) {
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
} // namespace

int main() {
    std::string romPath = findAsset({
        "roms/1MB ROMs/1994-07 - 06684214 - LC,Quadra,Performa 630.ROM",
        "roms/mame/macqd630/06684214.bin", "roms/quadra630.rom"
    });
    // POM68K_Q630_ROM=lc580 runs the later 32F2 ROM instead (the LC 580's
    // own dump; MAME notes it cannot boot a SCSI CD-ROM, hard disks are fine).
    if (getenv("POM68K_Q630_ROM")) {
        std::string alt = findAsset({
            "roms/1MB ROMs/1995-04 - 064DC91D - LC, Performa 580 & Performa 588.ROM",
            "roms/mame/maclc580/064dc91d.bin"
        });
        if (!alt.empty()) romPath = alt;
    }
    std::string diskPath = findAsset({
        "hdv/MacOS-8.1-boot.vhd", "hdv/q605-boot.vhd"
    });
    if (romPath.empty() || diskPath.empty()) {
        std::printf("SKIP: needs the 06684214 ROM + hdv/MacOS-8.1-boot.vhd\n");
        return 0;
    }
    testasset::report({ romPath, diskPath });
    std::fflush(stdout);

    std::ifstream in(romPath, std::ios::binary);
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
    if (rom.size() != Q630Memory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM is %zu bytes, want 1 MB\n", rom.size());
        return 1;
    }

    Q630Memory mem(32u << 20);
    if (!mem.loadRom(rom) || !mem.attachScsi(diskPath)) {
        std::fprintf(stderr, "FAIL: could not load ROM/disk\n");
        return 1;
    }
    Q630Cpu cpu(mem);
    mem.setCpu(&cpu);
    cpu.hardReset();
    while (mem.cpuHeld()) mem.tick(1000);

    constexpr int kFrameCycles = 550000;      // 33 MHz / ~60 Hz
    // 30 000 frames ≈ 16 G cycles: the 630's ROM spends far longer in its
    // pre-boot device scan than the Quadra 605 does, and the LC 580's later
    // ROM longer still. POM68K_FRAMES overrides.
    const int kMaxFrames = getenv("POM68K_FRAMES") ? atoi(getenv("POM68K_FRAMES")) : 30000;         // 5 G cycles: AppleTalk-active
                                              // boots (disk prefs) add LAP
                                              // no-peer timeouts before Finder
    Screen screen;
    for (int frame = 0; frame < kMaxFrames && !cpu.isHalted(); frame++) {
        cpu.runCycles(kFrameCycles);
        // Sample sparsely after 1.5 G cycles and stop only when the FULL
        // Finder signature holds — depth 8 alone appears well before the
        // desktop is drawn, and breaking on it sampled a half-built screen
        // once AppleTalk delays slowed the boot.
        if (frame >= 3600 && !(frame % 60) && mem.scsi().commands > 4000) {
            screen = decodeScreen(mem);
            if (screen.width == 640 && screen.height == 480 && screen.depth == 8) {
                Stats m = luminanceStats(screen, 0, screen.width, 2, 16);
                Stats d = luminanceStats(screen, 520, 630, 40, 430);
                if (m.mean > 170 && m.mean < 235 &&
                    m.deviation > 40 && m.deviation < 100 &&
                    d.mean > 100 && d.mean < 190 &&
                    d.deviation > 30 && d.deviation < 90 &&
                    m.mean - d.mean > 35)
                    break;
            }
        }
    }
    if (cpu.isHalted()) {
        std::fprintf(stderr, "FAIL: CPU halted (double fault)\n");
        return 1;
    }
    if (screen.pixels.empty()) screen = decodeScreen(mem);
    if (screen.pixels.empty()) {
        std::fprintf(stderr,
            "FAIL: no valid main GDevice PixMap (pc=$%08X clk=%lld SCSI=%ld "
            "MainDevice=$%08X ScrnBase=$%08X Valkyrie base=$%05X depth=%u stride=%u "
            "SWIM mode=$%02X setup=$%02X fifo=%d)\n",
            cpu.getPC(), (long long)cpu.getClock(), mem.scsi().commands,
            peek32(mem, 0x08A4), peek32(mem, 0x0824), mem.videoBase(),
            mem.videoDepth(), mem.videoStride(), mem.swim().mode(),
            mem.swim().setup(), mem.swim().fifoCount());
        return 1;
    }

    Stats menu = luminanceStats(screen, 0, screen.width, 2, 16);
    Stats desktop = luminanceStats(screen, 520, 630, 40, 430);
    std::printf("%dx%d@%dbpp base=$%05X stride=%u depth=%u; "
                "menu mean/dev %.1f/%.1f desktop %.1f/%.1f; SCSI=%ld\n",
                screen.width, screen.height, screen.depth, screen.offset,
                screen.stride, mem.videoDepth(), menu.mean, menu.deviation,
                desktop.mean, desktop.deviation, mem.scsi().commands);

    bool geometry = screen.width == 640 && screen.height == 480 &&
                    screen.depth == 8 && mem.videoDepth() == 8;
    bool finder = menu.mean > 170 && menu.mean < 235 &&
                  menu.deviation > 40 && menu.deviation < 100 &&
                  desktop.mean > 100 && desktop.mean < 190 &&
                  desktop.deviation > 30 && desktop.deviation < 90 &&
                  menu.mean - desktop.mean > 35;
    bool ok = geometry && finder && mem.scsi().commands > 4000;
    std::printf("%s\n", ok ? "PASSED — Quadra 630 Finder in 256 colors" : "FAILED");
    return ok ? 0 : 1;
}
