// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Beyond-boot on the discrete-040 board — shared-engine gate (BeyondBoot.h).
// Rig, GDevice decode and signature from q700_boot_etalon; Time is physical
// (q605 pattern). POM68K_BEYOND=soak|persist. Soft-skips without assets.
//
// `POM68K_Q700_MODEL` picks the profile, exactly as it does for the GUI:
// `q700` (default) is the Spike, `q900`/`q950` the Eclipse towers — the same
// board with the IIfx's front end. The towers are worth their own pair
// because nothing else in the suite keeps two Apple PIC IOPs, an Egret
// firmware LLE and a second 53C96 alive past the boot screen: the soak is the
// only thing that would notice an IOP that stops answering three minutes in,
// and the persist leg is the only one that drives the Eclipse's ADB — the
// SWIM IOP's bit-banged wire — from the Toolbox rather than from a test.

#include "AssetFingerprint.h"
#include "BeyondBoot.h"
#include "Q700Cpu.h"
#include "Q700Memory.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {

uint32_t peek32(const Q700Memory& mem, uint32_t a) {
    return uint32_t(mem.peek8(a)) << 24 | uint32_t(mem.peek8(a + 1)) << 16
         | uint32_t(mem.peek8(a + 2)) << 8 | mem.peek8(a + 3);
}

struct Screen { int width = 0, height = 0; std::vector<uint32_t> pixels; };
Screen decodeScreen(Q700Memory& mem) {
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
    int W = int(boundsB & 0xFFFF) - int(boundsA & 0xFFFF);
    int H = int(boundsB >> 16) - int(boundsA >> 16);
    int depth = mem.dafbDepth();
    uint32_t rowBytes = (peek32(mem, pmap + 4) >> 16) & 0x3FFF;
    uint32_t stride = rowBytes ? rowBytes : mem.dafbStride();
    uint32_t offset = (pmBase ? pmBase : scrnBase) & (Q700Memory::kVramSize - 1);
    const uint32_t minStride = depth == 24 ? uint32_t(W) * 4
                                           : uint32_t((W * depth + 7) / 8);
    if (W <= 0 || W > 1600 || H <= 0 || H > 1200 ||
        (depth != 1 && depth != 2 && depth != 4 && depth != 8 &&
         depth != 16 && depth != 24) ||
        stride < minStride ||
        uint64_t(offset) + uint64_t(H) * stride > Q700Memory::kVramSize)
        return s;
    const uint8_t* vram = mem.vram();
    const uint8_t (*clut)[3] = mem.clut();
    s.width = W; s.height = H;
    s.pixels.resize(size_t(W) * H);
    for (int y = 0; y < H; y++) {
        uint32_t row = offset + uint32_t(y) * stride;
        for (int x = 0; x < W; x++) {
            if (depth == 24) {
                const uint32_t p = row + uint32_t(4 * x);
                s.pixels[size_t(y) * W + x] =
                    uint32_t(vram[p + 1]) << 16 |
                    uint32_t(vram[p + 2]) << 8 | vram[p + 3];
                continue;
            }
            if (depth == 16) {
                const uint32_t p = row + uint32_t(2 * x);
                const uint16_t rgb = uint16_t(uint16_t(vram[p]) << 8 |
                                              vram[p + 1]);
                s.pixels[size_t(y) * W + x] =
                    uint32_t((rgb >> 10) & 0x1F) << 19 |
                    uint32_t((rgb >> 5) & 0x1F) << 11 |
                    uint32_t(rgb & 0x1F) << 3;
                continue;
            }
            uint8_t packed = vram[row + uint32_t(x * depth / 8)];
            uint8_t pen;
            if (depth == 1) pen = (packed >> (7 - (x & 7))) & 1;
            else if (depth == 2) pen = (packed >> (6 - 2 * (x & 3))) & 3;
            else if (depth == 4) pen = (x & 1) ? packed & 0x0F : packed >> 4;
            else pen = packed;
            const uint8_t* c = clut[pen];
            s.pixels[size_t(y) * W + x] =
                uint32_t(c[0]) << 16 | uint32_t(c[1]) << 8 | c[2];
        }
    }
    return s;
}

}  // namespace

int main() {
    const char* modelEnv = std::getenv("POM68K_Q700_MODEL");
    const std::string which = modelEnv ? modelEnv : "q700";
    const bool q950 = which == "q950", q900 = which == "q900";
    const auto model = q950 ? Q700Memory::Model::Q950
                    : q900 ? Q700Memory::Model::Q900
                           : Q700Memory::Model::Spike;
    const int64_t cpuHz = q950 ? Q700Memory::kCpuHzQ950 : Q700Memory::kCpuHz;
    const char* name = q950 ? "Quadra 950" : q900 ? "Quadra 900" : "Quadra 700";
    const char* tag = q950 ? "q950" : q900 ? "q900" : "q700";

    // The Q900 shares the Quadra 700's ROM; the Q950 has its own.
    std::string rom = testasset::find(q950 ? "roms/quadra950.rom"
                                           : "roms/quadra700.rom");
    if (rom.empty())
        rom = testasset::find(q950
            ? "roms/1MB ROMs/1992-03 - 3DC27823 - Quadra 950.ROM"
            : "roms/1MB ROMs/1991-10 - 420DBFF3 - Quadra 700&900 & PB140&170.ROM");
    std::string img = testasset::find("hdv/MacOS-8.1-boot.vhd");
    if (img.empty()) img = testasset::find("hdv/boot.vhd");
    if (img.empty()) img = testasset::find("hdv/GISTPERSO-boot.vhd");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: %s needs its 1 MB ROM + a bootable hdv/ image\n", name);
        return 0;
    }
    testasset::report({ rom, img });

    std::ifstream in(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
    Q700Memory mem(pom68k::defaultCoreConfig(), 8u << 20, cpuHz, model);
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    if (mem.eclipse())
        std::printf("Machine: %s, ADB: SWIM IOP wire, Egret: %s\n", name,
                    mem.egretLleActive() ? "341S0851 firmware LLE"
                                         : "HLE (NON-CONFORMANT)");
    Q700Cpu cpu(mem, jit::defaultResolvedConfig(),
                pom68k::defaultCoreConfig().cpu);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk image\n"); return 1; }
    beyondboot::ensureBootDriverType(mem.scsiDisk().image());
    const int64_t kFrame = cpuHz / 60;

    auto frames = [&](long n) {
        for (long f = 0; f < n && !cpu.isHalted(); f++) cpu.runCycles(kFrame);
    };
    auto finderUp = [&]() {
        Screen s = decodeScreen(mem);
        if (s.width < 512 || s.height < 342) return false;
        const int W = s.width, H = s.height;
        const double menu = beyondboot::darkRatio(s.pixels, W, 0, W, 2, 16);
        const double desk = beyondboot::darkRatio(s.pixels, W, W / 4, 3 * W / 4,
                                                  H - 140, H - 60);
        return menu >= 0.0 && menu < 0.30 && desk > 0.15 && desk < 0.95;
    };
    auto boot = [&]() {
        while (mem.cpuHeld()) mem.tick(1000);
        frames(16000);
        return !cpu.isHalted() && finderUp();
    };

    if (!boot()) { std::fprintf(stderr, "FAIL: no Finder after boot\n"); return 1; }
    std::printf("Finder up, SCSI %ld\n", mem.scsi().commands);

    beyondboot::Hooks h;
    h.name = name;
    h.frames = frames;
    h.halted = [&]() { return cpu.isHalted(); };
    h.finderUp = finderUp;
    h.time = [&](uint32_t* out) { *out = peek32(mem, 0x20C); return true; };
    h.key = [&](uint8_t code, bool down) { mem.keyEvent(code, down); };
    h.disk = [&]() -> std::vector<uint8_t>& { return mem.scsiDisk().image(); };
    h.writes = [&]() { return mem.scsiDisk().writeBlocks; };
    h.reboot = [&]() { cpu.hardReset(); return boot(); };
    h.dump = [&](const char* mode) {
        Screen s = decodeScreen(mem);
        beyondboot::dumpPpm((std::string(tag) + "_beyond_" + mode + ".ppm").c_str(),
                            s.pixels, s.width, s.height);
    };
    return beyondboot::run(h);
}
