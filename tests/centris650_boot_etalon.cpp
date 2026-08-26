// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Phase C gate: the Macintosh Centris 650 boots to the Finder on the new
// djMEMC + IOSB machine (CentrisMemory/CentrisCpu) — 68LC040 @ 25 MHz,
// the Q605's DAFB/TurboSCSI-53C96/SWIM2/ASC/pseudo-VIA2, a discrete
// 343-0042 RTC and a PIC1654S ADB transceiver (firmware LLE) on VIA1,
// model ID pins $46 (MAME macquadra800.cpp macct650), shared F1A6F343 /
// F1ACAD13 ROM. POM68K_CENTRIS610=1 runs the Centris 610 (20 MHz, ID
// $40) — the centris610_boot_etalon gate wraps that. Mac OS 8.1/System 7
// Finder at 640×480 DAFB (same signature as q605_boot_etalon). Soft-skips
// without the ROM or a bootable image.

#include "AssetFingerprint.h"
#include "CentrisMemory.h"
#include "CentrisCpu.h"
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

static uint32_t peek32(const CentrisMemory& mem, uint32_t a) {
    return uint32_t(mem.peek8(a)) << 24 | uint32_t(mem.peek8(a + 1)) << 16
         | uint32_t(mem.peek8(a + 2)) << 8 | mem.peek8(a + 3);
}

// The q605_boot_etalon decode: read the live screen geometry from the Mac
// low-memory globals (ScrnBase / MainDevice → PixMap bounds), then walk
// VRAM through the DAFB CLUT. Returns W=H=0 if no valid GDevice yet.
struct Screen { int width = 0, height = 0; std::vector<uint32_t> pixels; };
static Screen decodeScreen(CentrisMemory& mem) {
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
    // Authoritative stride = the PixMap's rowBytes (pmap+4, low 14 bits) —
    // the DAFB register stride can differ from what QuickDraw actually
    // writes, which tears the decode.
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

int main() {
    // Model selector: POM68K_CENTRIS_MODEL = c650 (default) / c610 / q650 /
    // q610 / q800. The Quadra variants are the same djMEMC+IOSB machine with
    // a full 68040 (POM68K_CENTRIS_FPU, set here before the CPU is built) and
    // their own VIA1 port-A ID pins + clock; the Quadra 800 adds SONIC
    // Ethernet and NuBus, neither of which the boot path binds.
    // POM68K_CENTRIS610 stays as a legacy alias for c610.
    std::string model = getenv("POM68K_CENTRIS_MODEL")
                      ? getenv("POM68K_CENTRIS_MODEL")
                      : (getenv("POM68K_CENTRIS610") ? "c610" : "c650");
    const bool c610 = model == "c610";
    const bool q650 = model == "q650";
    const bool q610 = model == "q610";
    const bool q800 = model == "q800";
    const bool isQuadra = q650 || q610 || q800;
    pom68k::CoreConfig core;
    core.cpu.centrisFull040 = isQuadra;
    std::string rom = find("roms/centris650.rom");
    if (rom.empty())
        rom = find("roms/1MB ROMs/1993-02 - F1A6F343 - Quadra, Centris 610,650.ROM");
    if (rom.empty())
        rom = find("roms/1MB ROMs/1993-02 - F1ACAD13 - Quadra, Centris 610,650,800.ROM");
    std::string img = find("hdv/MacOS-8.1-boot.vhd");
    if (img.empty()) img = find("hdv/boot.vhd");
    if (img.empty()) img = find("hdv/GISTPERSO-boot.vhd");
    if (img.empty()) img = find("hdv/MacOS-7.6-boot.vhd");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs the F1A6F343/F1ACAD13 ROM + a bootable hdv/ image\n");
        return 0;
    }
    testasset::report({ rom, img });

    std::ifstream in(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
    if (romData.size() != CentrisMemory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM is %zu bytes, want 1 MB\n", romData.size());
        return 1;
    }

    const int64_t cpuHz = (q650 || q800) ? CentrisMemory::kCpuHzQ650
                        : q610 ? CentrisMemory::kCpuHzQ610
                        : c610 ? CentrisMemory::kCpuHz610
                               : CentrisMemory::kCpuHz650;
    const uint8_t pins  = q800 ? CentrisMemory::kIdQuadra800
                        : q650 ? CentrisMemory::kIdQuadra650
                        : q610 ? CentrisMemory::kIdQuadra610
                        : c610 ? CentrisMemory::kIdCentris610
                               : CentrisMemory::kIdCentris650;
    CentrisMemory mem(core, 36u << 20, cpuHz, pins);
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    std::printf("ADB: %s\n", mem.adbLleActive() ? "PIC1654S firmware LLE" : "HLE");
    if (getenv("POM68K_BERR")) {
        static long n = 0;
        mem.onBusError = [](uint32_t a, bool w) {
            if (n++ < 40)
                std::fprintf(stderr, "[BERR] %s $%08X\n", w ? "write" : "read", a);
        };
    }
    const jit::ResolvedConfig jitConfig = testjit::resolveFromEnvironment();
    CentrisCpu cpu(mem, jitConfig, core.cpu);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk image\n"); return 1; }

    const int64_t kFrame = cpuHz / 60;
    long limit = 16000;
    if (const char* n = getenv("POM68K_FRAMES")) limit = atol(n);
    bool diag = getenv("POM68K_DIAG");
    // POM68K_HALT=<hex>: run 1-cycle quanta until PC enters [halt,halt+0x800)
    // (the ROM serial monitor sits at $408B9800+) and dump the dedup'd
    // branch-target trail that routed there — the LC 520 bring-up技法.
    if (const char* h = getenv("POM68K_HALT")) {
        uint32_t halt = uint32_t(strtoul(h, nullptr, 16));
        uint32_t trail[96] = {}; int ti = 0; uint32_t last = 0;
        for (long f = 0; f < 3000 && !cpu.isHalted(); f++) {
            for (int s = 0; s < kFrame; s++) {
                cpu.runCycles(1);
                uint32_t pc = cpu.getPC();
                if ((pc + 12 < last || pc > last + 12)
                    && pc != trail[(ti + 95) % 96] && pc != trail[(ti + 94) % 96])
                    trail[ti++ % 96] = pc;
                last = pc;
                if (pc >= halt && pc < halt + 0x800) {
                    std::fprintf(stderr, "[HALT] f=%ld pc=$%08X d7=$%08X:", f, pc,
                                 cpu.getD(7));
                    for (int k = 0; k < 96; k++)
                        std::fprintf(stderr, "%s$%08X", (k % 8) ? " " : "\n  ",
                                     trail[(ti + k) % 96]);
                    std::fprintf(stderr, "\n  a0=$%08X a1=$%08X a2=$%08X a3=$%08X "
                        "d0=$%08X d1=$%08X d2=$%08X\n", cpu.getA(0), cpu.getA(1),
                        cpu.getA(2), cpu.getA(3), cpu.getD(0), cpu.getD(1), cpu.getD(2));
                    return 0;
                }
            }
        }
        std::fprintf(stderr, "[HALT] never reached $%08X\n", halt);
        return 0;
    }
    long trailAt = getenv("POM68K_TRAIL") ? atol(getenv("POM68K_TRAIL")) : -1;
    for (long f = 0; f < limit && !cpu.isHalted(); f++) {
        if (f == trailAt) {                  // dump the spin's branch trail
            uint32_t trail[96] = {}; int ti = 0; uint32_t last = 0;
            for (int s = 0; s < int(kFrame) * 20; s++) {
                cpu.runCycles(1);
                uint32_t pc = cpu.getPC();
                if ((pc + 12 < last || pc > last + 12)
                    && pc != trail[(ti + 95) % 96] && pc != trail[(ti + 94) % 96])
                    trail[ti++ % 96] = pc;
                last = pc;
            }
            std::fprintf(stderr, "[trail@%ld] ipl=%d via2irq=%d SCSI=%ld:", f,
                         mem.iplLevel(), mem.via2IrqAsserted(), mem.scsi().commands);
            for (int k = 0; k < 96; k++)
                std::fprintf(stderr, "%s$%08X", (k % 8) ? " " : "\n  ",
                             trail[(ti + k) % 96]);
            std::fprintf(stderr, "\n  a0=$%08X a1=$%08X a2=$%08X d0=$%08X d1=$%08X\n",
                         cpu.getA(0), cpu.getA(1), cpu.getA(2), cpu.getD(0), cpu.getD(1));
        }
        cpu.runCycles(kFrame);
        if (diag && (f < 400 ? !(f % 20) : !(f % 400)))
            std::fprintf(stderr, "[diag] f=%ld pc=$%08X SCSI=%ld ipl=%d hres=%u "
                         "vres=%u depth=%u blank=%d\n", f, cpu.getPC(),
                         mem.scsi().commands, mem.iplLevel(), mem.dafbHres(),
                         mem.dafbVres(), mem.dafbDepth(), mem.dafbBlanked());
    }
    if (cpu.isHalted()) { std::fprintf(stderr, "FAIL: CPU halted (double fault)\n"); return 1; }

    // Decode the DAFB framebuffer from the live GDevice (q605 path).
    Screen sc = decodeScreen(mem);
    int W = sc.width, H = sc.height;
    std::vector<uint32_t> fb = std::move(sc.pixels);
    // Fallback: no GDevice yet but the DAFB is live — decode the raw VRAM
    // at the DAFB base with the programmed geometry (or a 640×480×8 guess)
    // so the diagnostic dump shows whatever is on screen.
    if ((W <= 0 || H <= 0) && !mem.dafbBlanked()) {
        W = mem.dafbHres() ? int(mem.dafbHres()) : 640;
        H = mem.dafbVres() ? int(mem.dafbVres()) : 480;
        uint32_t stride = mem.dafbStride() ? mem.dafbStride() : uint32_t(W);
        uint32_t base = mem.dafbBase() & (CentrisMemory::kVramSize - 1);
        int depth = mem.dafbDepth() ? mem.dafbDepth() : 8;
        const uint8_t* vram = mem.vram();
        const uint8_t (*clut)[3] = mem.clut();
        if (uint64_t(base) + uint64_t(H) * stride <= CentrisMemory::kVramSize) {
            fb.assign(size_t(W) * H, 0);
            for (int y = 0; y < H; y++)
                for (int x = 0; x < W; x++) {
                    uint8_t packed = vram[base + uint32_t(y) * stride
                                        + uint32_t(x * depth / 8)];
                    uint8_t pen = depth == 8 ? packed
                                : depth == 1 ? ((packed >> (7 - (x & 7))) & 1)
                                : packed;
                    const uint8_t* c = clut[pen];
                    fb[size_t(y) * W + x] =
                        uint32_t(c[0]) << 16 | uint32_t(c[1]) << 8 | c[2];
                }
        }
    }
    if (W <= 0 || H <= 0 || fb.empty()) {
        std::printf("FAILED — no active GDevice yet (SCSI=%ld, hres=%u vres=%u "
                    "blank=%d)\n", mem.scsi().commands, mem.dafbHres(),
                    mem.dafbVres(), mem.dafbBlanked());
        return 1;
    }
    if (getenv("POM68K_DUMP")) {
        FILE* fp = fopen(c610 ? "centris610_screen.ppm" : "centris650_screen.ppm", "wb");
        std::fprintf(fp, "P6\n%d %d\n255\n", W, H);
        for (uint32_t p : fb) {
            uint8_t rgb[3] = { uint8_t(p >> 16), uint8_t(p >> 8), uint8_t(p) };
            fwrite(rgb, 1, 3, fp);
        }
        fclose(fp);
    }

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

    std::printf("mode %dx%d depth %u; menu bar dark %.2f (want <0.30), "
                "desktop %.2f (want 0.20-0.85), SCSI commands %ld\n",
                W, H, mem.dafbDepth(), menuBar, desktop, mem.scsi().commands);

    bool ok = W >= 512 && H >= 342 && menuBar < 0.30
           && desktop > 0.30 && desktop < 0.85 && mem.scsi().commands > 50;
    const char* name = q800 ? "Quadra 800" : q650 ? "Quadra 650"
                     : q610 ? "Quadra 610"
                     : c610 ? "Centris 610" : "Centris 650";
    std::printf("%s — Macintosh %s %s\n", ok ? "PASSED" : "FAILED", name,
                ok ? "booted to the Finder" : "did not reach the Finder");
    return ok ? 0 : 1;
}
