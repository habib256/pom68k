// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Phase C gate: the Macintosh Color Classic boots to the Finder. The
// machine is the SPICE V8 derivative (MAME v8.cpp:693-929 + maclc.cpp
// maccclas): 68030 @ 15.6672 MHz, built-in 512×384 Trinitron (fixed
// sense 2), SWIM2 in the gate array, a 1 MB ROM ($ECD99DC0) — and a
// Cuda MCU instead of the Egret, running the FACTORY 341s0417 firmware
// (Cuda 2.35, default since 2026-07-29 — it needs the DFAC2 I2C ACK,
// CudaLle::setI2cDfac; 341s0788 is the no-0417 fallback). Same Finder
// signature as
// lc_boot_etalon: white menu bar with glyphs, 50% dithered desktop,
// non-trivial SCSI traffic. Soft-skips without the ROM or a bootable
// hdv/ image.

#include "AssetFingerprint.h"
#include "V8Memory.h"
#include "V8Video.h"
#include "Cpu030.h"

#include <cstdint>
#include <cstdio>
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

// Same DDM ddType $6A fixup as lcii_boot_etalon (the V8-era ROM boot
// scan only loads that driver type).
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
    std::string rom = find("roms/cclassic.rom");
    if (rom.empty())
        rom = find("roms/1MB ROMs/1993-02 - ECD99DC0 - Color Classic.ROM");
    std::string img = find("hdv/cclassic-boot.vhd");
    if (img.empty()) img = find("hdv/lcii-boot.vhd");
    if (img.empty()) img = find("hdv/boot.vhd");
    if (img.empty()) img = find("hdv/GISTPERSO-boot.vhd");
    if (img.empty()) img = find("hdv/System 7.5 HD.dsk");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs the 1 MB Color Classic ROM + a bootable hdv/ image\n");
        return 0;
    }
    testasset::report({ rom, img });

    std::ifstream in(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
    if (romData.size() != 0x100000) {
        std::fprintf(stderr, "FAIL: ROM is %zu bytes, want 1 MB\n", romData.size());
        return 1;
    }

    V8Memory mem(0xA00000, V8Memory::Model::ColorClassic);
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    std::printf("ADB: %s\n", mem.egretLleActive() ? "Cuda firmware LLE" : "HLE");
    Cpu030 cpu(mem, /*withFpu=*/true);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk image\n"); return 1; }
    ensureBootDriverType(mem.scsiDisk().image());

    while (mem.cpuHeld()) mem.tick(1000);
    const int64_t kFrame = 640 * 407;        // 60.15 Hz @ 15.6672 MHz
    const long kFrames = 16000;              // ≈4.17e9 cycles
    for (long f = 0; f < kFrames && !cpu.isHalted(); f++)
        cpu.runCycles(kFrame);

    if (cpu.isHalted()) { std::fprintf(stderr, "FAIL: CPU halted (double fault)\n"); return 1; }

    V8Video video(mem);
    std::vector<uint32_t> fb;
    video.decode(fb);                        // 512×384 (fixed sense 2)
    const int W = 512;
    auto blackRatio = [&](int x0, int x1, int y0, int y1) {
        long black = 0;
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++)
                if ((fb[y * W + x] & 0xFF) < 0x80) black++;
        return double(black) / (double(x1 - x0) * (y1 - y0));
    };
    double menuBar = blackRatio(0, W, 2, 16);
    double desktop = blackRatio(400, W, 40, 340);

    std::printf("menu bar black %.2f (want <0.30), desktop %.2f (want 0.35-0.65), "
                "SCSI commands %ld\n", menuBar, desktop, mem.scsi().commands);

    bool ok = menuBar < 0.30 && desktop > 0.35 && desktop < 0.65
           && mem.scsi().commands > 50;
    std::printf("%s\n", ok ? "PASSED — Macintosh Color Classic booted to the Finder"
                           : "FAILED");
    return ok ? 0 : 1;
}
