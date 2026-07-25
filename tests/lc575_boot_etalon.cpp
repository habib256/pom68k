// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Phase C gate: the Macintosh LC 575 / Performa 575 "Optimus" boots Mac OS
// 8.1 to the 640x480x8 Finder on the SAME Q605 machine as the Quadra 605 /
// LC 475, but with the LC 575 identity — model longword $A55A222E and the
// 68LC040 CPU (M68LC040 + soft 68882, the Finder-usable no-FPU path). The
// FF7439EE universal ROM, MEMCjr/DAFB, Cuda firmware LLE, PrimeTime IOSB
// ASC, pseudo-VIA2 and TurboSCSI are all shared with q605_boot_etalon /
// lc475_boot_etalon (MAME macquadra605.cpp maclc575, $A55A222E @ 33 MHz).
//
// Two LC 575-specific notes vs the LC 475 gate:
//  * The all-in-one ROM path probes un-emulated PrimeTime I/O windows; a
//    /BERR there dropped the ROM into its serial debugger, so Q605Memory now
//    reads unmapped PrimeTime I/O back as 0 (MAME iosb.cpp:54-65 — no BERR).
//  * At 33 MHz the desktop is painted a few hundred frames later than on the
//    25 MHz LC 475, and a startup "not shut down properly" alert (dirty
//    boot volume) covers the menu bar shortly after. We therefore sample
//    finely and latch the first fully-drawn Finder frame (populated menu bar
//    + desktop pattern) before that alert appears.
//
// Soft-skips without assets.

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
    for (const char* name : names)
        for (const std::string& base : { std::string(), std::string("../") }) {
            std::string path = base + name;
            if (std::ifstream(path, std::ios::binary)) return path;
        }
    return {};
}

uint32_t peek32(const Q605Memory& mem, uint32_t addr) {
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

Screen decodeScreen(const Q605Memory& mem) {
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

// The fully-drawn Finder: a bright menu bar carrying titles/Apple logo
// (high deviation) over the 50%-gray desktop pattern on the right.
bool isFinder(const Screen& s) {
    if (s.width != 640 || s.height != 480 || s.depth != 8) return false;
    Stats m = luminanceStats(s, 0, s.width, 2, 16);
    Stats d = luminanceStats(s, 520, 630, 40, 430);
    return m.mean > 170 && m.mean < 235 && m.deviation > 40 && m.deviation < 100 &&
           d.mean > 100 && d.mean < 190 && d.deviation > 30 && d.deviation < 90 &&
           m.mean - d.mean > 35;
}
} // namespace

int main() {
    // Select the LC 575 identity BEFORE the machine/CPU are built: the
    // $A55A222E model longword and the 68LC040 (M68LC040 + soft 68882).
    setenv("POM68K_Q605_ID", "A55A222E", 1);
    setenv("POM68K_Q605_NOFPU", "1", 1);

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
    std::printf("assets: ROM=%s disk=%s\n", romPath.c_str(), diskPath.c_str());
    std::fflush(stdout);

    std::ifstream in(romPath, std::ios::binary);
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
    if (rom.size() != Q605Memory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM is %zu bytes, want 1 MB\n", rom.size());
        return 1;
    }

    Q605Memory mem(32u << 20);
    if (!mem.loadRom(rom) || !mem.attachScsi(diskPath)) {
        std::fprintf(stderr, "FAIL: could not load ROM/disk\n");
        return 1;
    }

    // Confirm the machine really identifies as the LC 575 ($A55A222E).
    uint32_t id = peek32(mem, 0x5FFFFFFC);
    std::printf("model ID $%08X (want $A55A222E)\n", id);
    if (id != 0xA55A222Eu) {
        std::fprintf(stderr, "FAIL: model longword is not the LC 575\n");
        return 1;
    }

    Cpu040 cpu(mem);
    mem.setCpu(&cpu);
    cpu.hardReset();
    while (mem.cpuHeld()) mem.tick(1000);

    constexpr int kFrameCycles = 555555;      // 33 MHz / ~60 Hz
    constexpr int kMaxFrames = 12000;
    Screen screen;
    bool booted = false;
    for (int frame = 0; frame < kMaxFrames && !cpu.isHalted(); frame++) {
        cpu.runCycles(kFrameCycles);
        // Sample finely once the System is loading: the fully-drawn desktop is
        // only briefly clear before the startup alert covers the menu bar, so
        // latch the first Finder frame instead of polling on a coarse cadence.
        if (frame >= 2400 && !(frame % 10) && mem.scsi().commands > 3000) {
            Screen s = decodeScreen(mem);
            if (isFinder(s)) { screen = s; booted = true; break; }
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
            "MainDevice=$%08X ScrnBase=$%08X DAFB base=$%05X mode=%u stride=%u)\n",
            cpu.getPC(), (long long)cpu.getClock(), mem.scsi().commands,
            peek32(mem, 0x08A4), peek32(mem, 0x0824), mem.dafbBase(),
            mem.dafbMode(), mem.dafbStride());
        return 1;
    }

    Stats menu = luminanceStats(screen, 0, screen.width, 2, 16);
    Stats desktop = luminanceStats(screen, 520, 630, 40, 430);
    std::printf("%dx%d@%dbpp base=$%05X stride=%u DAFB-mode=%u; "
                "menu mean/dev %.1f/%.1f desktop %.1f/%.1f; SCSI=%ld\n",
                screen.width, screen.height, screen.depth, screen.offset,
                screen.stride, mem.dafbMode(), menu.mean, menu.deviation,
                desktop.mean, desktop.deviation, mem.scsi().commands);

    bool geometry = screen.width == 640 && screen.height == 480 &&
                    screen.depth == 8 && mem.dafbMode() == 3;
    bool ok = geometry && booted && isFinder(screen) &&
              mem.scsi().commands > 4000;
    std::printf("%s\n", ok ? "PASSED — LC 575 Finder in 256 colors" : "FAILED");
    return ok ? 0 : 1;
}
