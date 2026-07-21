// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Q8.2 gate: TRUE bare no-FPU (POM68K_Q605_NOFPU=2, FPUModel::NONE, no
// soft 68882) boots Mac OS 8.1 to the Finder — the LC 475 identity. Guards
// the _FP68K binding chain: XPRAM $AE combo read served as DATA (not the
// Cuda command echo) -> InitResources validation falls back to
// UniversalInfo defaultRSRCs 4 -> integer PACK 4 bound (CHANGELOG
// 2026-07-21 "Bare no-FPU solved"). Soft-skips when assets are absent.

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
} // namespace

int main() {
    // Cpu040 and Q605Memory::loadRom both read this at construction/load.
    setenv("POM68K_Q605_NOFPU", "2", 1);

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
    std::printf("assets: ROM=%s disk=%s (bare NOFPU=2)\n", romPath.c_str(), diskPath.c_str());
    std::fflush(stdout);

    std::ifstream in(romPath, std::ios::binary);
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
    if (rom.size() != Q605Memory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM is %zu bytes, want 1 MB\n", rom.size());
        return 1;
    }

    // File image must still advertise FPU at UniversalInfo (unchanged ROM).
    constexpr uint32_t kLc475Hw = 0x0A8090;       // UniversalInfo $0A8080 + $10
    uint32_t fileHw = uint32_t(rom[kLc475Hw]) << 24 | uint32_t(rom[kLc475Hw + 1]) << 16 |
                      uint32_t(rom[kLc475Hw + 2]) << 8 | rom[kLc475Hw + 3];
    if (!(fileHw & 0x10000000u)) {
        std::fprintf(stderr, "FAIL: file UniversalInfo $0A8080 lacks FPU bit "
                     "($%08X) — unexpected ROM layout\n", fileHw);
        return 1;
    }

    Q605Memory mem(32u << 20);
    if (!mem.loadRom(rom) || !mem.attachScsi(diskPath)) {
        std::fprintf(stderr, "FAIL: could not load ROM/disk\n");
        return 1;
    }
    Cpu040 cpu(mem);
    mem.setCpu(&cpu);
    if (cpu.getModel() != moira::Model::M68LC040 ||
        cpu.getFPUModel() != moira::FPUModel::NONE) {
        std::fprintf(stderr, "FAIL: NOFPU=2 should select M68LC040 + bare NONE\n");
        return 1;
    }
    std::printf("NOFPU=2 CPU: M68LC040 + bare FPUModel::NONE\n");
    std::fflush(stdout);
    cpu.hardReset();
    while (mem.cpuHeld()) mem.tick(1000);

    // Continue to Finder — same budget as q605_boot_etalon (AppleTalk-active
    // disk prefs add LAP no-peer timeouts before the desktop settles).
    constexpr int kFrameCycles = 416667;
    constexpr int kMaxFrames = 12000;
    Screen screen;
    for (int frame = 0; frame < kMaxFrames && !cpu.isHalted(); frame++) {
        cpu.runCycles(kFrameCycles);
        if (frame >= 3600 && !(frame % 60) && mem.scsi().commands > 4000) {
            screen = decodeScreen(mem);
            if (screen.width == 640 && screen.height == 480 && screen.depth == 8) {
                Stats menu = luminanceStats(screen, 0, screen.width, 2, 16);
                Stats desktop = luminanceStats(screen, 520, 630, 40, 430);
                bool finder = menu.mean > 170 && menu.mean < 235 &&
                              menu.deviation > 40 && menu.deviation < 100 &&
                              desktop.mean > 100 && desktop.mean < 190 &&
                              desktop.deviation > 30 && desktop.deviation < 90 &&
                              menu.mean - desktop.mean > 35;
                if (finder) break;
            }
        }
    }
    if (cpu.isHalted()) {
        std::fprintf(stderr, "FAIL: CPU halted (double fault) clk=%lld\n",
                     (long long)cpu.getClock());
        return 1;
    }
    if (screen.pixels.empty()) screen = decodeScreen(mem);
    if (screen.pixels.empty()) {
        int stale = 0, clean = 0;
        for (uint32_t a = 0; a + 4 <= (32u << 20); a += 4) {
            uint32_t v = peek32(mem, a);
            if (v == 0xDC00530Du || v == 0xDD00530Du) stale++;
            if (v == 0xCC00530Du || v == 0xCD00530Du) clean++;
        }
        std::fprintf(stderr,
            "FAIL: no valid main GDevice PixMap (pc=$%08X clk=%lld SCSI=%ld "
            "staleHW=%d cleanHW=%d) — likely SysError before Finder under NOFPU\n",
            cpu.getPC(), (long long)cpu.getClock(), mem.scsi().commands,
            stale, clean);
        return 1;
    }

    Stats menu = luminanceStats(screen, 0, screen.width, 2, 16);
    Stats desktop = luminanceStats(screen, 520, 630, 40, 430);
    std::printf("%dx%d@%dbpp base=$%05X stride=%u DAFB-mode=%u; "
                "menu mean/dev %.1f/%.1f desktop %.1f/%.1f; SCSI=%ld clk=%lld\n",
                screen.width, screen.height, screen.depth, screen.offset,
                screen.stride, mem.dafbMode(), menu.mean, menu.deviation,
                desktop.mean, desktop.deviation, mem.scsi().commands,
                (long long)cpu.getClock());

    bool geometry = screen.width == 640 && screen.height == 480 &&
                    screen.depth == 8 && mem.dafbMode() == 3;
    bool finder = menu.mean > 170 && menu.mean < 235 &&
                  menu.deviation > 40 && menu.deviation < 100 &&
                  desktop.mean > 100 && desktop.mean < 190 &&
                  desktop.deviation > 30 && desktop.deviation < 90 &&
                  menu.mean - desktop.mean > 35;
    bool ok = geometry && finder && mem.scsi().commands > 4000;
    if (!ok) {
        int stale = 0, clean = 0;
        for (uint32_t a = 0; a + 4 <= (32u << 20); a += 4) {
            uint32_t v = peek32(mem, a);
            if (v == 0xDC00530Du || v == 0xDD00530Du) stale++;
            if (v == 0xCC00530Du || v == 0xCD00530Du) clean++;
        }
        uint32_t romHw = uint32_t(mem.peek8(0x408A8090)) << 24 |
                         uint32_t(mem.peek8(0x408A8091)) << 16 |
                         uint32_t(mem.peek8(0x408A8092)) << 8 |
                         mem.peek8(0x408A8093);
        std::printf("HWCfg RAM copies: stale=%d clean=%d romHW=$%08X\n",
                    stale, clean, romHw);
    }
    std::printf("%s\n", ok ? "PASSED — bare 68LC040 (no FPU) Finder in 256 colors"
                           : "FAILED");
    return ok ? 0 : 1;
}
