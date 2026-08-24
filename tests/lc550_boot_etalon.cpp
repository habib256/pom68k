// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Phase C gate: the Macintosh LC 550 / Performa 550 boots System 7.5 to the
// Finder — the LC 520's Sonora AIO board clocked at 33.33 MHz with the
// model longword $A55A0101 (maclc3.cpp maclc550_map; in the EDE66CBD ROM's
// machine table as video types $4A = sense-6 640×480 / $4D = sense-2).
// Same Cuda 341S0060 firmware LLE and 8-bpp color Finder signature as
// lc520_boot_etalon. Soft-skips without the ROM or a bootable hdv/ image.

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
    return testasset::find(rel);
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

    uint32_t boxId = SonoraMemory::kIdLc550;
    if (const char* b = getenv("POM68K_BOXID"))
        boxId = uint32_t(strtoul(b, nullptr, 16));
    SonoraMemory mem(0x800000, SonoraMemory::kCpuHzPlus, boxId,
                     /*cudaAdb=*/true);
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    int sense = 6;                           // built-in 640×480 RGB
    if (const char* s = getenv("POM68K_SENSE")) sense = atoi(s);
    mem.setMonitorSense(uint8_t(sense));
    std::printf("model ID $%08X (want $A55A0101)\n",
                uint32_t(mem.peek8(0x5FFFFFFC)) << 24 |
                uint32_t(mem.peek8(0x5FFFFFFD)) << 16 |
                uint32_t(mem.peek8(0x5FFFFFFE)) << 8 | mem.peek8(0x5FFFFFFF));
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
    bool diag = getenv("POM68K_DIAG");
    for (long f = 0; f < limit && !cpu.isHalted(); f++) {
        cpu.runCycles(kFrame);
        if (diag && (f < 400 ? !(f % 20) : !(f % 400)))
            std::fprintf(stderr, "[diag] f=%ld pc=$%08X SCSI=%ld depth=%d\n",
                         f, cpu.getPC(), mem.scsi().commands, mem.videoDepth());
    }
    if (cpu.isHalted()) { std::fprintf(stderr, "FAIL: CPU halted (double fault)\n"); return 1; }

    SonoraVideo video(mem);
    std::vector<uint32_t> fb;
    video.decode(fb);
    int W = 0, H = 0;
    video.size(W, H);
    // Luminance-weighted signature — the 8-bpp color desktop weave rationale
    // in lc520_boot_etalon.cpp.
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
    double desktop = darkRatio(W - 112, W, 40, H - 44);

    std::printf("mode %dx%d depth %d; menu bar dark %.2f (want <0.30), "
                "desktop %.2f (want 0.35-0.80), SCSI commands %ld\n",
                W, H, mem.videoDepth(), menuBar, desktop, mem.scsi().commands);

    bool ok = W == 640 && H == 480 && mem.videoDepth() == 3
           && menuBar < 0.30 && desktop > 0.35 && desktop < 0.80
           && mem.scsi().commands > 50;
    std::printf("%s\n", ok ? "PASSED — Macintosh LC 550 booted to the Finder"
                           : "FAILED");
    return ok ? 0 : 1;
}
