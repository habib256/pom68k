// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// A 68040 FMOVE to a byte, word or long word that meets a signaling NaN, a
// NaN, an infinity or an integer overflow takes a nonmaskable exception
// (MC68040UM §9.7.2.2, §9.7.3.2), and the M68040FPSP handler in the ROM
// completes the move from the state frame of its FSAVE (§9.8, Table 9-16).
// This gate boots a Quadra 605 with the integrated FPU to the Finder, runs
// such moves and checks what the ROM's own handler leaves in the
// destination: the value of the manual, whatever the stack below the
// handler held. With an idle frame instead of Table 9-16's, the SNAN handler
// stored $4088D6BA (a ROM address) when that memory was zero
// (CHANGELOG 2026-11-26). Soft-skips when assets are absent.

#include "AssetFingerprint.h"
#include "Cpu040.h"
#include "BoardIdentity.h"
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
    pom68k::CoreConfig core = pom68k::profileCoreConfig(pom68k::SnapMachine::Q605);
    core.cpu.q605Fpu = pom68k::Q605FpuMode::Integrated;
    std::string romPath = findAsset({
        "roms/1MB ROMs/1993-10 - FF7439EE - LC475,575,Quadra 605,Performa 475,476,575,577,578.ROM",
        "roms/mame/macqd605/ff7439ee.bin", "roms/quadra605.rom", "roms/q605.rom" });
    std::string diskPath = findAsset({ "hdv/MacOS-8.1-boot.vhd" });
    if (romPath.empty() || diskPath.empty()) {
        std::printf("SKIP: needs FF7439EE ROM + hdv/MacOS-8.1-boot.vhd\n");
        return 0;
    }
    testasset::report({ romPath, diskPath });
    std::ifstream in(romPath, std::ios::binary);
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    Q605Memory mem(core, 32u << 20);
    if (!mem.loadRom(rom) || !mem.attachScsi(diskPath)) return 1;
    Cpu040 cpu(mem, jit::defaultResolvedConfig(), core.cpu, core.diagnostics);
    mem.setCpu(&cpu);
    if (cpu.getModel() != moira::CPUModel::M68040) {
        std::fprintf(stderr, "FAIL: the integrated FPU needs a 68040\n");
        return 1;
    }
    cpu.hardReset();
    while (mem.cpuHeld()) mem.tick(1000);
    constexpr int kFrameCycles = 416667;
    bool finder = false;
    for (int frame = 0; frame < 30000 && !cpu.isHalted() && !finder; frame++) {
        cpu.runCycles(kFrameCycles);
        if (frame >= 3600 && !(frame % 60) && mem.scsi().commands > 4000) {
            Screen screen = decodeScreen(mem);
            if (screen.width == 640 && screen.height == 480 && screen.depth == 8) {
                Stats menu = luminanceStats(screen, 0, screen.width, 2, 16);
                Stats desktop = luminanceStats(screen, 520, 630, 40, 430);
                finder = menu.mean > 170 && menu.mean < 235 && menu.deviation > 40 &&
                         menu.deviation < 100 && desktop.mean > 100 && desktop.mean < 190 &&
                         desktop.deviation > 30 && desktop.deviation < 90 &&
                         menu.mean - desktop.mean > 35;
            }
        }
    }
    if (!finder) {
        std::fprintf(stderr, "FAIL: no Finder (halted=%d clk=%lld)\n", cpu.isHalted(),
                     (long long)cpu.getClock());
        return 1;
    }
    for (int i = 0; i < 120; i++) cpu.runCycles(kFrameCycles);

    const uint32_t a7 = cpu.getSP();
    const uint32_t X = (a7 - 0x4000) & ~3u;         // scratch code and data, below the stack
    std::printf("vectors 52/54 = %08X/%08X, scratch at %08X\n",
                peek32(mem, 52 * 4), peek32(mem, 54 * 4), X);

    struct Op { const char* name; uint16_t op, ext; bool mem; uint32_t mask; };
    const Op ops[] = {
        { "FMOVE.L FP0,D0", 0xF200, 0x6000, false, 0xFFFFFFFF },
        { "FMOVE.W FP0,D0", 0xF200, 0x7000, false, 0x0000FFFF },
        { "FMOVE.B FP0,D0", 0xF200, 0x7800, false, 0x000000FF },
        { "FMOVE.L FP0,(A0)", 0xF210, 0x6000, true, 0xFFFFFFFF },
    };
    // want: the long-word result; a word or byte takes its low bits
    // (largest integers and NaN mantissa bits scale with the size).
    struct Val { const char* name; uint32_t w[3]; uint32_t l, w16, b; bool trap; };
    const Val vals[] = {
        { "+inf", { 0x7fff0000, 0, 0 },                   0x7FFFFFFF, 0x7FFF, 0x7F, true },
        { "-inf", { 0xffff0000, 0, 0 },                   0x80000000, 0x8000, 0x80, true },
        { "1e20", { 0x40410000, 0xad78ebc5, 0xac620000 }, 0x7FFFFFFF, 0x7FFF, 0x7F, true },
        { "NaN",  { 0x7fff0000, 0xc0000000, 0x00001234 }, 0xC0000000, 0xC000, 0xC0, true },
        { "SNaN", { 0x7fff0000, 0xa0000000, 0x00001234 }, 0xE0000000, 0xE000, 0xE0, true },
        { "42",   { 0x40040000, 0xa8000000, 0 },          42, 42, 42, false },
    };
    const uint8_t fills[] = { 0x00, 0xFF, 0xA5, 0x3C };
    auto w16 = [&](uint32_t a, uint16_t v) { mem.write8(a, uint8_t(v >> 8)); mem.write8(a + 1, uint8_t(v)); };
    int failures = 0;
    for (const auto& op : ops)
    for (const auto& v : vals)
    for (uint8_t fill : fills) {
        w16(X, op.op); w16(X + 2, op.ext); w16(X + 4, 0x60FE); w16(X + 6, 0x4E71);
        for (uint32_t a = X + 0x100; a < X + 0x110; a++) mem.write8(a, 0x11);
        cpu.setSR(0x2700);
        const uint32_t sp = cpu.getSP();
        for (uint32_t a = sp - 0x200; a < sp; a++) mem.write8(a, fill);
        cpu.setFP(0, v.w); cpu.setFPCR(0); cpu.setFPSR(0);
        cpu.setD(0, 0x11111111); cpu.setA(0, X + 0x100);
        cpu.setPC(X); cpu.setPC0(X); cpu.setIRD(op.op); cpu.setIRC(op.ext);
        cpu.execute();
        const uint32_t entry = cpu.getPC0();
        int n = 1;
        while (cpu.getPC0() != X + 4 && n < 200000 && !cpu.isHalted()) { cpu.execute(); n++; }
        const uint32_t got = op.mem ? peek32(mem, X + 0x100) : cpu.getD(0);
        const uint32_t res = op.mask == 0xFFFFFFFF ? v.l : op.mask == 0xFFFF ? v.w16 : v.b;
        const uint32_t want = (0x11111111 & ~op.mask) | res;
        const bool trapped = entry >= 0x40800000;
        const bool ok = cpu.getPC0() == X + 4 && cpu.getSP() == sp && got == want &&
                        trapped == v.trap;
        if (!ok) {
            failures++;
            std::printf("FAIL %s %s, stack fill %02X: destination %08X (want %08X), %s, "
                        "%d instruction(s), SP %+d, PC0 %08X\n", op.name, v.name, fill, got, want,
                        trapped ? "exception taken" : "no exception", n,
                        int(cpu.getSP() - sp), cpu.getPC0());
        }
    }
    std::printf("%s\n", failures ? "FAILED" : "PASSED — the ROM FPSP completes the nonmaskable "
                "FMOVE to an integer from the state frame, whatever the stack held");
    return failures ? 1 : 0;
}
