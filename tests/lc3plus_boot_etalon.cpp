// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Phase C gate: the Macintosh LC III+ boots to the Finder on the SAME
// Sonora machine as the LC III, clocked at 33.33 MHz with the $A55A0003
// model longword (maclc3.cpp maclc3plus). Everything else — gate array,
// Egret firmware LLE (roms/egret/341s0851.bin), Sonora video, SWIM2,
// NCR 5380 + pseudo-DMA — is shared with lc3_boot_etalon. Same Finder
// signature. Soft-skips without the ROM or a bootable hdv/ image.

#include "AssetFingerprint.h"
#include "SonoraMemory.h"
#include "SonoraVideo.h"
#include "SonoraCpu.h"

#include <cstdint>
#include <cstdio>
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
    std::string rom = find("roms/maclc3.rom");
    if (rom.empty())
        rom = find("roms/1MB ROMs/1993-02 - ECBBC41C - Mac LC III.ROM");
    std::string img = find("hdv/lc3-boot.vhd");
    if (img.empty()) img = find("hdv/lcii-boot.vhd");
    if (img.empty()) img = find("hdv/boot.vhd");
    if (img.empty()) img = find("hdv/GISTPERSO-boot.vhd");
    if (img.empty()) img = find("hdv/System 7.5 HD.dsk");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs the 1 MB LC III ROM + a bootable hdv/ image\n");
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

    // The ONLY deltas from the LC III: 33.33 MHz CPU clock + the $A55A0003
    // model longword at $5FFFFFFC.
    SonoraMemory mem(pom68k::defaultCoreConfig(), 0x800000, // 8 MB
                     SonoraMemory::kCpuHzPlus, SonoraMemory::kIdLc3Plus);
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    mem.setMonitorSense(2);                  // 512×384 12" RGB (etalon frame)

    // Confirm the machine really identifies as the LC III+ ($A55A0003).
    uint32_t id = uint32_t(mem.peek8(0x5FFFFFFC)) << 24 |
                  uint32_t(mem.peek8(0x5FFFFFFD)) << 16 |
                  uint32_t(mem.peek8(0x5FFFFFFE)) << 8 | mem.peek8(0x5FFFFFFF);
    std::printf("model ID $%08X (want $A55A0003); ADB: %s\n", id,
                mem.egretLleActive() ? "Egret firmware LLE" : "HLE");
    if (id != SonoraMemory::kIdLc3Plus) {
        std::fprintf(stderr, "FAIL: model longword is not the LC III+\n");
        return 1;
    }

    SonoraCpu cpu(mem, jit::defaultResolvedConfig(),
                  pom68k::defaultCoreConfig().cpu, /*withFpu=*/true);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk image\n"); return 1; }
    ensureBootDriverType(mem.scsiDisk().image());

    while (mem.cpuHeld()) mem.tick(1000);
    // Run-quantum only; the real cadence comes from the programmed modeline.
    const int64_t kFrame = SonoraMemory::kCpuHzPlus / 60;
    const long kFrames = 16000;
    for (long f = 0; f < kFrames && !cpu.isHalted(); f++)
        cpu.runCycles(kFrame);

    if (cpu.isHalted()) { std::fprintf(stderr, "FAIL: CPU halted (double fault)\n"); return 1; }

    SonoraVideo video(mem);
    std::vector<uint32_t> fb;
    video.decode(fb);
    int W = 0, H = 0;
    video.size(W, H);
    auto blackRatio = [&](int x0, int x1, int y0, int y1) {
        long black = 0;
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++)
                if ((fb[size_t(y) * W + x] & 0xFF) < 0x80) black++;
        return double(black) / (double(x1 - x0) * (y1 - y0));
    };
    double menuBar = blackRatio(0, W, 2, 16);
    double desktop = blackRatio(W - 112, W, 40, H - 44);

    std::printf("mode %dx%d depth %d; menu bar black %.2f (want <0.30), "
                "desktop %.2f (want 0.35-0.65), SCSI commands %ld\n",
                W, H, mem.videoDepth(), menuBar, desktop, mem.scsi().commands);

    bool ok = menuBar < 0.30 && desktop > 0.35 && desktop < 0.65
           && mem.scsi().commands > 50;
    std::printf("%s\n", ok ? "PASSED — Macintosh LC III+ booted to the Finder"
                           : "FAILED");
    return ok ? 0 : 1;
}
