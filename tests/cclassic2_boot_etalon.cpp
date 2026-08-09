// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Phase C gate: the Macintosh Color Classic II / Performa 275 boots
// System 7.5 to the Finder — the LC 550 board (Sonora AIO, 68030 @
// 33.33 MHz, Cuda 341S0060 firmware LLE, EDE66CBD universal ROM, model
// longword $A55A0101) in the Color Classic case with the built-in
// 512×384 Trinitron: monitor sense 2 selects the ROM machine-table entry
// with video type $4D (sense 6 would pick the LC 550's $4A — see
// docs/LC520_BRINGUP.md). Comes up 8-bpp color like the LC 520/550.
// Soft-skips without the ROM or a bootable hdv/ image.

#include "AssetFingerprint.h"
#include "SonoraMemory.h"
#include "SonoraVideo.h"
#include "SonoraCpu.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

static std::string find(const char* rel) {
    for (const std::string base : { std::string(), std::string("../") }) {
        std::string p = base + rel;
        if (std::ifstream(p, std::ios::binary)) return p;
    }
    return {};
}

// Same DDM ddType $6A fixup as lc3_boot_etalon.
static void ensureBootDriverType(std::vector<uint8_t>& img) {
    if (img.size() < 512 || img[0] != 'E' || img[1] != 'R') return;
    int count = (img[0x10] << 8) | img[0x11];
    for (int i = 0; i < count && 0x12 + i * 8 + 8 <= 512; i++) {
        int e = 0x12 + i * 8;
        if (((img[e + 6] << 8) | img[e + 7]) == 0x6A) return;
    }
    if (count >= 1 && 0x12 + count * 8 + 8 <= 512) {
        int src = 0x12, dst = 0x12 + count * 8;
        for (int k = 0; k < 8; k++) img[dst + k] = img[src + k];
        img[dst + 6] = 0x00; img[dst + 7] = 0x6A;
        img[0x10] = uint8_t((count + 1) >> 8);
        img[0x11] = uint8_t(count + 1);
    }
}

int main() {
    std::string rom = find("roms/maclc520.rom");
    if (rom.empty())
        rom = find("roms/1MB ROMs/1993-10 - EDE66CBD - Color Classic II & LC 550 & Performa 275,550,560 & Macintosh TV.ROM");
    std::string img = find("hdv/lc3-boot.vhd");
    if (img.empty()) img = find("hdv/GISTPERSO-boot.vhd");
    if (img.empty()) img = find("hdv/boot.vhd");
    if (img.empty()) img = find("hdv/System 7.5 HD.dsk");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs the 1 MB EDE66CBD ROM + a bootable hdv/ image\n");
        return 0;
    }
    testasset::report({ rom, img });

    std::ifstream in(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
    if (romData.size() != SonoraMemory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM is %zu bytes, want 1 MB\n", romData.size());
        return 1;
    }

    SonoraMemory mem(0x800000, SonoraMemory::kCpuHzPlus,
                     SonoraMemory::kIdLc550, /*cudaAdb=*/true);
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    mem.setMonitorSense(2);                  // built-in 512×384 Trinitron
    std::printf("ADB: %s\n", mem.egretLleActive() ? "Cuda firmware LLE" : "HLE");
    SonoraCpu cpu(mem, /*withFpu=*/true);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk image\n"); return 1; }
    ensureBootDriverType(mem.scsiDisk().image());

    while (mem.cpuHeld()) mem.tick(1000);
    const int64_t kFrame = SonoraMemory::kCpuHzPlus / 60;
    long limit = 16000;
    if (const char* n = getenv("POM68K_FRAMES")) limit = atol(n);
    for (long f = 0; f < limit && !cpu.isHalted(); f++)
        cpu.runCycles(kFrame);
    if (cpu.isHalted()) { std::fprintf(stderr, "FAIL: CPU halted (double fault)\n"); return 1; }

    SonoraVideo video(mem);
    std::vector<uint32_t> fb;
    video.decode(fb);
    int W = 0, H = 0;
    video.size(W, H);
    // Luminance-weighted signature — 8-bpp color desktop weave rationale in
    // lc520_boot_etalon.cpp.
    auto darkRatio = [&](int x0, int x1, int y0, int y1) {
        long dark = 0;
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++) {
                uint32_t p = fb[size_t(y) * W + x];
                int luma = (2 * int((p >> 16) & 0xFF) + 5 * int((p >> 8) & 0xFF)
                          + int(p & 0xFF)) / 8;
                if (luma < 0x80) dark++;
            }
        return double(dark) / (double(x1 - x0) * (y1 - y0));
    };
    double menuBar = darkRatio(0, W, 2, 16);
    // Desktop weave: sample the BOTTOM strip, not the right-hand column the
    // 640×480 siblings use. At 512×384 the reference volume's saved Finder
    // layout puts an open window over the right edge, so that column measured
    // window interior (0.32 — a hair under the band) instead of the desktop
    // pattern; the strip below the windows is unambiguously desktop (0.79).
    double desktop = darkRatio(0, W, H - 110, H - 10);
    if (getenv("POM68K_DUMP")) {
        FILE* fp = fopen("cclassic2_screen.ppm", "wb");
        std::fprintf(fp, "P6\n%d %d\n255\n", W, H);
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                uint32_t p = fb[size_t(y) * W + x];
                uint8_t rgb[3] = { uint8_t(p >> 16), uint8_t(p >> 8), uint8_t(p) };
                fwrite(rgb, 1, 3, fp);
            }
        fclose(fp);
    }

    std::printf("mode %dx%d depth %d; menu bar dark %.2f (want <0.30), "
                "desktop %.2f (want 0.35-0.85), SCSI commands %ld\n",
                W, H, mem.videoDepth(), menuBar, desktop, mem.scsi().commands);

    bool ok = W == 512 && H == 384 && mem.videoDepth() == 3
           && menuBar < 0.30 && desktop > 0.35 && desktop < 0.85
           && mem.scsi().commands > 50;
    std::printf("%s\n", ok ? "PASSED — Macintosh Color Classic II booted to the Finder"
                           : "FAILED");
    return ok ? 0 : 1;
}
