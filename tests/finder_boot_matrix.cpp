// POM68K — matrix Finder boot harness (not a CTest gate).
// Usage: finder_boot_matrix <plus|macii|lcii|q605> <rom> <disk>
// Exit 0 = Finder signature, 1 = fail, 2 = bad usage.

#include "Cpu68k.h"
#include "MacMemory.h"
#include "MacVideo.h"
#include "MacFrame.h"
#include "MacIIMemory.h"
#include "TobyVideo.h"
#include "Cpu020.h"
#include "V8Memory.h"
#include "V8Video.h"
#include "Cpu030.h"
#include "Q605Memory.h"
#include "Cpu040.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

static std::vector<uint8_t> loadFile(const char* path) {
    std::ifstream in(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(in), {} };
}

static void ensureBootDriverType6A(std::vector<uint8_t>& img) {
    if (img.size() < 512 || img[0] != 'E' || img[1] != 'R') return;
    int count = (img[0x10] << 8) | img[0x11];
    for (int i = 0; i < count && 0x12 + i * 8 + 8 <= 512; i++) {
        int e = 0x12 + i * 8;
        if (((img[e + 6] << 8) | img[e + 7]) == 0x6A) return;
    }
    if (count >= 1 && 0x12 + count * 8 + 8 <= 512) {
        int src = 0x12, dst = 0x12 + count * 8;
        for (int k = 0; k < 8; k++) img[dst + k] = img[src + k];
        img[dst + 6] = 0x00;
        img[dst + 7] = 0x6A;
        img[0x10] = uint8_t((count + 1) >> 8);
        img[0x11] = uint8_t(count + 1);
    }
}

static double blackRatio(const uint32_t* fb, int W, int x0, int x1, int y0, int y1) {
    long black = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            if ((fb[y * W + x] & 0xFF) < 0x80) black++;
    return double(black) / double(x1 - x0) / double(y1 - y0);
}

static uint32_t peek32(const Q605Memory& mem, uint32_t addr) {
    return uint32_t(mem.peek8(addr)) << 24 |
           uint32_t(mem.peek8(addr + 1)) << 16 |
           uint32_t(mem.peek8(addr + 2)) << 8 |
           mem.peek8(addr + 3);
}

struct QScreen {
    int width = 0, height = 0, depth = 0;
    uint32_t stride = 0, offset = 0;
    std::vector<uint32_t> pixels;
};

static QScreen decodeQ605(const Q605Memory& mem) {
    QScreen s;
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
        return {};
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
static Stats luminance(const QScreen& s, int x0, int x1, int y0, int y1) {
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

static int bootPlus(const std::vector<uint8_t>& rom, const char* disk) {
    MacMemory mem;
    if (!mem.loadRom(rom)) return 1;
    Cpu68k cpu(mem);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(disk)) return 1;
    MacFrameClock fc;
    fc.resync(cpu);
    for (long f = 0; f < 5400 && !cpu.isHalted(); f++) fc.runFrame(cpu, mem);
    if (cpu.isHalted()) { std::fprintf(stderr, "FAIL: halted\n"); return 1; }
    MacVideo video;
    const uint32_t* fb = video.render(mem);
    double menu = blackRatio(fb, 512, 0, 512, 2, 16);
    double desk = blackRatio(fb, 512, 0, 512, 120, 240);
    std::printf("plus menu=%.2f desk=%.2f SCSI=%ld\n", menu, desk, mem.scsi().commands);
    bool ok = menu < 0.30 && desk > 0.40 && desk < 0.60 && mem.scsi().commands > 50;
    std::printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

static int bootMacII(const std::vector<uint8_t>& rom, const char* disk, long frames = 20000) {
    MacIIMemory mem;
    if (!mem.loadRom(rom)) return 1;
    mem.installTobyVideo();
    Cpu020 cpu(mem, true);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(disk)) return 1;
    const int64_t kFrame = 800 * 525;
    long lastScsi = 0;
    for (long f = 0; f < frames && !cpu.isHalted(); f++) {
        cpu.runCycles(kFrame);
        if (frames > 20000 && (f % 10000 == 0 || f == frames - 1)) {
            std::printf("  progress f=%ld PC=$%08X SCSI=%ld (+%ld)\n",
                        f, cpu.getPC(), mem.scsi().commands,
                        mem.scsi().commands - lastScsi);
            lastScsi = mem.scsi().commands;
            std::fflush(stdout);
        }
    }
    if (cpu.isHalted()) { std::fprintf(stderr, "FAIL: halted\n"); return 1; }
    TobyVideo* tv = mem.toby();
    std::vector<uint32_t> fb;
    tv->decode(fb);
    int W = tv->hres(), H = tv->vres();
    double menu = blackRatio(fb.data(), W, 0, W, 2, 20);
    double desk = blackRatio(fb.data(), W, W / 2, W, 40, H - 40);
    std::printf("macii menu=%.2f desk=%.2f SCSI=%ld %dx%d PC=$%08X frames=%ld\n",
                menu, desk, mem.scsi().commands, W, H, cpu.getPC(), frames);
    bool ok = menu < 0.35 && desk > 0.20 && desk < 0.70 && mem.scsi().commands > 500;
    std::printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

static int bootLcII(const std::vector<uint8_t>& rom, const char* disk, long frames = 16000) {
    V8Memory mem;
    if (!mem.loadRom(rom)) return 1;
    Cpu030 cpu(mem, true);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(disk)) return 1;
    ensureBootDriverType6A(mem.scsiDisk().image());
    while (mem.cpuHeld()) mem.tick(1000);
    const int64_t kFrame = 640 * 407;
    for (long f = 0; f < frames && !cpu.isHalted(); f++) cpu.runCycles(kFrame);
    if (cpu.isHalted()) { std::fprintf(stderr, "FAIL: halted\n"); return 1; }
    V8Video video(mem);
    std::vector<uint32_t> fb;
    video.decode(fb);
    const int W = 512;
    double menu = blackRatio(fb.data(), W, 0, W, 2, 16);
    double desk = blackRatio(fb.data(), W, 400, W, 40, 340);
    std::printf("lcii menu=%.2f desk=%.2f SCSI=%ld PC=$%08X frames=%ld\n",
                menu, desk, mem.scsi().commands, cpu.getPC(), frames);
    bool ok = menu < 0.30 && desk > 0.35 && desk < 0.65 && mem.scsi().commands > 50;
    std::printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

static int bootQ605(const std::vector<uint8_t>& rom, const char* disk) {
    Q605Memory mem(32u << 20);
    if (!mem.loadRom(rom) || !mem.attachScsi(disk)) return 1;
    Cpu040 cpu(mem);
    mem.setCpu(&cpu);
    cpu.hardReset();
    while (mem.cpuHeld()) mem.tick(1000);
    constexpr int kFrameCycles = 416667;
    constexpr int kMaxFrames = 6000;
    QScreen screen;
    for (int frame = 0; frame < kMaxFrames && !cpu.isHalted(); frame++) {
        cpu.runCycles(kFrameCycles);
        if (frame >= 3600 && !(frame % 60) && mem.scsi().commands > 5000) {
            screen = decodeQ605(mem);
            if (screen.width == 640 && screen.height == 480 && screen.depth == 8)
                break;
        }
    }
    if (cpu.isHalted()) { std::fprintf(stderr, "FAIL: halted\n"); return 1; }
    if (screen.pixels.empty()) screen = decodeQ605(mem);
    if (screen.pixels.empty()) {
        std::fprintf(stderr, "FAIL: no PixMap SCSI=%ld mode=%u\n",
                     mem.scsi().commands, mem.dafbMode());
        return 1;
    }
    Stats menu = luminance(screen, 0, screen.width, 2, 16);
    Stats desk = luminance(screen, 520, 630, 40, 430);
    std::printf("q605 %dx%d@%d menu=%.1f/%.1f desk=%.1f/%.1f SCSI=%ld mode=%u\n",
                screen.width, screen.height, screen.depth,
                menu.mean, menu.deviation, desk.mean, desk.deviation,
                mem.scsi().commands, mem.dafbMode());
    // Mac OS 8.1 defaults to 8bpp (etalon). System 7.x often stays at 1bpp
    // until Monitors; accept either a real Finder desktop.
    bool geo8 = screen.width == 640 && screen.height == 480 &&
                screen.depth == 8 && mem.dafbMode() == 3;
    bool geo1 = screen.width == 640 && screen.height == 480 && screen.depth == 1;
    bool finder8 = menu.mean > 170 && menu.mean < 235 &&
                   menu.deviation > 40 && menu.deviation < 100 &&
                   desk.mean > 100 && desk.mean < 190 &&
                   desk.deviation > 30 && desk.deviation < 90 &&
                   menu.mean - desk.mean > 35;
    // 1bpp Finder: light menu bar, ~50% gray desktop (black-ratio via lum).
    bool finder1 = menu.mean > 180 && desk.mean > 90 && desk.mean < 170 &&
                   menu.mean - desk.mean > 40;
    bool ok = ((geo8 && finder8) || (geo1 && finder1)) && mem.scsi().commands > 1000;
    std::printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr,
                     "usage: %s <plus|macii|lcii|q605> <rom> <disk> [frames]\n",
                     argv[0]);
        return 2;
    }
    auto rom = loadFile(argv[2]);
    if (rom.empty()) {
        std::fprintf(stderr, "FAIL: empty ROM %s\n", argv[2]);
        return 1;
    }
    long frames = (argc > 4) ? std::atol(argv[4]) : 0;
    std::printf("=== %s ROM=%s (%zu KB) disk=%s ===\n",
                argv[1], argv[2], rom.size() / 1024, argv[3]);
    std::fflush(stdout);
    if (!std::strcmp(argv[1], "plus")) return bootPlus(rom, argv[3]);
    if (!std::strcmp(argv[1], "macii"))
        return bootMacII(rom, argv[3], frames > 0 ? frames : 20000);
    if (!std::strcmp(argv[1], "lcii"))
        return bootLcII(rom, argv[3], frames > 0 ? frames : 16000);
    if (!std::strcmp(argv[1], "q605")) return bootQ605(rom, argv[3]);
    std::fprintf(stderr, "FAIL: unknown machine %s\n", argv[1]);
    return 2;
}
