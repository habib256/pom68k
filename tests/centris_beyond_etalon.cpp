// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Beyond-boot on the Centris 650 (djMEMC + IOSB + 68040 @ 25 MHz, DAFB) —
// the shared-engine gate (BeyondBoot.h). Rig, boot loop, GDevice screen
// decode and Finder signature are centris650_boot_etalon's; the Time global
// is physical here, as on the Q605 (no RAM-based video, Mac OS 8.1 runs the
// 040 without relocating low memory). POM68K_BEYOND=soak|persist.
// Soft-skips without the Centris ROM + a bootable hdv/ image.

#include "AssetFingerprint.h"
#include "BeyondBoot.h"
#include "CentrisCpu.h"
#include "CentrisMemory.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {

uint32_t peek32(CentrisMemory& mem, uint32_t a) {
    return uint32_t(mem.peek8(a)) << 24 | uint32_t(mem.peek8(a + 1)) << 16
         | uint32_t(mem.peek8(a + 2)) << 8 | mem.peek8(a + 3);
}

// centris650_boot_etalon's decode: walk the live GDevice → PixMap, stride
// from rowBytes (the DAFB register stride can differ and tears the decode).
struct Screen { int width = 0, height = 0; std::vector<uint32_t> pixels; };
Screen decodeScreen(CentrisMemory& mem) {
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
    int W = right - left, H = bottom - top, depth = mem.dafbDepth();
    uint32_t rowBytes = (peek32(mem, pmap + 4) >> 16) & 0x3FFF;
    uint32_t stride = rowBytes ? rowBytes : mem.dafbStride();
    uint32_t offset = (pmBase ? pmBase : scrnBase) & (CentrisMemory::kVramSize - 1);
    if (W <= 0 || W > 1600 || H <= 0 || H > 1200 ||
        (depth != 1 && depth != 2 && depth != 4 && depth != 8) ||
        stride < uint32_t((W * depth + 7) / 8) ||
        uint64_t(offset) + uint64_t(H) * stride > CentrisMemory::kVramSize)
        return s;
    const uint8_t* vram = mem.vram();
    const uint8_t (*clut)[3] = mem.clut();
    s.width = W; s.height = H;
    s.pixels.resize(size_t(W) * H);
    for (int y = 0; y < H; y++) {
        uint32_t row = offset + uint32_t(y) * stride;
        for (int x = 0; x < W; x++) {
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
    std::string rom = testasset::find("roms/centris650.rom");
    if (rom.empty())
        rom = testasset::find("roms/1MB ROMs/1993-02 - F1A6F343 - Quadra, Centris 610,650.ROM");
    if (rom.empty())
        rom = testasset::find("roms/1MB ROMs/1993-02 - F1ACAD13 - Quadra, Centris 610,650,800.ROM");
    std::string img = testasset::find("hdv/MacOS-8.1-boot.vhd");
    if (img.empty()) img = testasset::find("hdv/boot.vhd");
    if (img.empty()) img = testasset::find("hdv/GISTPERSO-boot.vhd");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs the Centris ROM + a bootable hdv/ image\n");
        return 0;
    }
    testasset::report({ rom, img });

    std::ifstream in(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
    CentrisMemory mem(36u << 20, CentrisMemory::kCpuHz650,
                      CentrisMemory::kIdCentris650);
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    CentrisCpu cpu(mem);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk image\n"); return 1; }
    beyondboot::ensureBootDriverType(mem.scsiDisk().image());
    const int64_t kFrame = CentrisMemory::kCpuHz650 / 60;

    auto frames = [&](long n) {
        for (long f = 0; f < n && !cpu.isHalted(); f++) cpu.runCycles(kFrame);
    };
    auto finderUp = [&]() {
        Screen s = decodeScreen(mem);
        if (s.width < 512 || s.height < 342) {
            std::fprintf(stderr, "[finder] no GDevice (%dx%d)\n",
                         s.width, s.height);
            return false;
        }
        const int W = s.width, H = s.height;
        // centris650_boot_etalon's EXACT bands: luma menu rows 2-16, and the
        // icon-free RIGHT strip (W-112..W, 40..H-44) at 0.30-0.85. A centre
        // band read 0.06 on the platinum 8.1 desktop and failed for it.
        const double menu = beyondboot::darkRatio(s.pixels, W, 0, W, 2, 16);
        const double desk = beyondboot::darkRatio(s.pixels, W, W - 112, W,
                                                  40, H - 44);
        std::fprintf(stderr, "[finder] %dx%d menu %.2f desk %.2f\n",
                     W, H, menu, desk);
        return menu >= 0.0 && menu < 0.30 && desk > 0.30 && desk < 0.85;
    };
    auto boot = [&]() {
        frames(16000);
        if (cpu.isHalted()) {
            std::fprintf(stderr, "[boot] HALTED at PC=$%08X\n", cpu.getPC());
            return false;
        }
        return finderUp();
    };

    if (!boot()) { std::fprintf(stderr, "FAIL: no Finder after boot\n"); return 1; }
    std::printf("Finder up, ADB %s, SCSI %ld\n",
                mem.adbLleActive() ? "PIC LLE" : "HLE", mem.scsi().commands);

    beyondboot::Hooks h;
    h.name = "Centris 650";
    h.frames = frames;
    h.halted = [&]() { return cpu.isHalted(); };
    h.finderUp = finderUp;
    h.time = [&](uint32_t* out) { *out = peek32(mem, 0x20C); return true; };
    h.key = [&](uint8_t code, bool down) { mem.keyEvent(code, down); };
    h.disk = [&]() -> std::vector<uint8_t>& { return mem.scsiDisk().image(); };
    h.reboot = [&]() { cpu.hardReset(); return boot(); };
    h.dump = [&](const char* mode) {
        Screen s = decodeScreen(mem);
        beyondboot::dumpPpm((std::string("centris_beyond_") + mode + ".ppm").c_str(),
                            s.pixels, s.width, s.height);
    };
    // 8.1's display sleep blacks the screen after ~3 idle minutes (menu
    // 1.00 / desk 0.99 in round 4) — same as the Sonora, same user-shaped
    // cure: a mouse click, then time to repaint.
    h.wake = [&]() {
        for (int i = 0; i < 6; i++) {
            mem.mouseMove(i & 1 ? 5 : -5, 0);
            frames(15);
        }
        mem.mouseButton(true);
        frames(30);
        mem.mouseButton(false);
        frames(300);
    };
    return beyondboot::run(h);
}
