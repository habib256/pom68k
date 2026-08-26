// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Phase C gate: the Macintosh IIvx boots System 7.5 to the Finder on the
// VASP machine (`VaspMemory`/`VaspCpu`/`VaspVideo`) — 68030 + 68882 @
// 31.3344 MHz, VASP gate array ("V8 video on Sonora addressing"), SCSI
// 5380 + pseudo-DMA, SWIM1, ASC-V8, Egret 341S0851 firmware LLE, model
// longword $A55A2015 (MAME maciivx.cpp), 640×480 13" monitor (sense 6).
// POM68K_IIVI=1 runs the IIvi flavor (15.6672 MHz, $A55A2016) — the
// iivi_boot_etalon gate wraps that. Luminance Finder signature (color
// desktop weave, lc520_boot_etalon rationale). Soft-skips without the
// 4957EB49 ROM or a bootable hdv/ image.

#include "AssetFingerprint.h"
#include "VaspMemory.h"
#include "VaspVideo.h"
#include "VaspCpu.h"
#include "JitTestConfig.h"

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
    const bool vi = getenv("POM68K_IIVI") != nullptr;
    std::string rom = find("roms/maciivx.rom");
    if (rom.empty())
        rom = find("roms/1MB ROMs/1992-10 - 4957EB49 - Mac IIvx & IIvi or Performa 600.ROM");
    std::string img = find("hdv/lc3-boot.vhd");
    if (img.empty()) img = find("hdv/GISTPERSO-boot.vhd");
    if (img.empty()) img = find("hdv/boot.vhd");
    if (img.empty()) img = find("hdv/System 7.5 HD.dsk");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs the 1 MB 4957EB49 ROM + a bootable hdv/ image\n");
        return 0;
    }
    testasset::report({ rom, img });

    std::ifstream in(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
    if (romData.size() != VaspMemory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM is %zu bytes, want 1 MB\n", romData.size());
        return 1;
    }

    uint32_t boxId = vi ? VaspMemory::kIdIIvi : VaspMemory::kIdIIvx;
    if (const char* b = getenv("POM68K_BOXID"))
        boxId = uint32_t(strtoul(b, nullptr, 16));
    const int64_t cpuHz = vi ? VaspMemory::kCpuHzVi : VaspMemory::kCpuHzVx;
    VaspMemory mem(pom68k::defaultCoreConfig(), 0x800000, cpuHz, boxId);
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    int sense = 6;                           // 13" 640×480 RGB
    if (const char* s = getenv("POM68K_SENSE")) sense = atoi(s);
    mem.setMonitorSense(uint8_t(sense));
    std::printf("model ID $%08X (want $%08X)\n",
                uint32_t(mem.peek8(0x5FFFFFFC)) << 24 |
                uint32_t(mem.peek8(0x5FFFFFFD)) << 16 |
                uint32_t(mem.peek8(0x5FFFFFFE)) << 8 | mem.peek8(0x5FFFFFFF),
                boxId);
    std::printf("ADB: %s\n", mem.egretLleActive() ? "Egret firmware LLE" : "HLE");
    const jit::ResolvedConfig jitConfig = testjit::resolveFromEnvironment();
    VaspCpu cpu(mem, jitConfig, pom68k::defaultCoreConfig().cpu,
                /*withFpu=*/true);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk image\n"); return 1; }
    ensureBootDriverType(mem.scsiDisk().image());

    while (mem.cpuHeld()) mem.tick(1000);
    const int64_t kFrame = cpuHz / 60;
    long limit = 16000;
    if (const char* n = getenv("POM68K_FRAMES")) limit = atol(n);
    bool diag = getenv("POM68K_DIAG");
    for (long f = 0; f < limit && !cpu.isHalted(); f++) {
        cpu.runCycles(kFrame);
        if (diag && (f < 400 ? !(f % 20) : !(f % 400)))
            std::fprintf(stderr, "[diag] f=%ld pc=$%08X SCSI=%ld depth=%d "
                         "d7=$%08X\n", f, cpu.getPC(), mem.scsi().commands,
                         mem.videoDepth(), cpu.getD(7));
    }
    if (cpu.isHalted()) { std::fprintf(stderr, "FAIL: CPU halted (double fault)\n"); return 1; }

    VaspVideo video(mem);
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
    double desktop = darkRatio(W - 112, W, 40, H - 44);
    if (getenv("POM68K_DUMP")) {
        FILE* fp = fopen(vi ? "iivi_screen.ppm" : "iivx_screen.ppm", "wb");
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

    bool ok = W == 640 && H == 480 && mem.videoDepth() == 3
           && menuBar < 0.30 && desktop > 0.35 && desktop < 0.85
           && mem.scsi().commands > 50;
    std::printf("%s — Macintosh %s %s\n", ok ? "PASSED" : "FAILED",
                vi ? "IIvi" : "IIvx",
                ok ? "booted to the Finder" : "did not reach the Finder");
    return ok ? 0 : 1;
}
