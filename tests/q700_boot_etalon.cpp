// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Gate: the Macintosh Quadra 700 boots to the Finder on the new "Spike"
// machine (Q700Memory/Q700Cpu) — a full 68040 @ 25 MHz on DISCRETE chips:
// the Mac II's VIA1+VIA2 / 343-0042 RTC / PIC1654S ADB transceiver (firmware
// LLE) front end, the Quadra I/O block (DAFB video with its own TurboSCSI
// cell for the 53C96, SWIM1, EASC, SCC, SONIC stubbed) and 2 MB VRAM.
// $420DBFF3 ROM. Mac OS 8.1 / System 7 Finder at 640×480 DAFB (the
// q605/centris signature). Soft-skips without the ROM or a bootable image.

#include "Q700Memory.h"
#include "Q700Cpu.h"

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

static uint32_t peek32(const Q700Memory& mem, uint32_t a) {
    return uint32_t(mem.peek8(a)) << 24 | uint32_t(mem.peek8(a + 1)) << 16
         | uint32_t(mem.peek8(a + 2)) << 8 | mem.peek8(a + 3);
}

// The q605_boot_etalon decode: read the live screen geometry from the Mac
// low-memory globals (ScrnBase / MainDevice → PixMap bounds), then walk
// VRAM through the DAFB CLUT. Returns W=H=0 if no valid GDevice yet.
struct Screen { int width = 0, height = 0; std::vector<uint32_t> pixels; };
static Screen decodeScreen(Q700Memory& mem) {
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
    uint32_t offset = (pmBase ? pmBase : scrnBase) & (Q700Memory::kVramSize - 1);
    if (W <= 0 || W > 1600 || H <= 0 || H > 1200 ||
        (depth != 1 && depth != 2 && depth != 4 && depth != 8) ||
        stride < uint32_t((W * depth + 7) / 8) ||
        uint64_t(offset) + uint64_t(H) * stride > Q700Memory::kVramSize)
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
    std::string rom = find("roms/quadra700.rom");
    if (rom.empty())
        rom = find("roms/1MB ROMs/1991-10 - 420DBFF3 - Quadra 700&900 & PB140&170.ROM");
    std::string img = find("hdv/MacOS-8.1-boot.vhd");
    if (img.empty()) img = find("hdv/boot.vhd");
    if (img.empty()) img = find("hdv/GISTPERSO-boot.vhd");
    if (img.empty()) img = find("hdv/MacOS-7.6-boot.vhd");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs the $420DBFF3 ROM + a bootable hdv/ image\n");
        return 0;
    }

    std::ifstream in(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
    if (romData.size() != Q700Memory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM is %zu bytes, want 1 MB\n", romData.size());
        return 1;
    }

    const int64_t cpuHz = Q700Memory::kCpuHz;
    Q700Memory mem(32u << 20, cpuHz);
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    std::printf("ADB: %s\n", mem.adbLleActive() ? "PIC1654S firmware LLE" : "HLE");
    if (getenv("POM68K_BERR")) {
        static long n = 0;
        mem.onBusError = [](uint32_t a, bool w) {
            if (n++ < 40)
                std::fprintf(stderr, "[BERR] %s $%08X\n", w ? "write" : "read", a);
        };
    }
    Q700Cpu cpu(mem);
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
                         mem.iplLevel(), mem.via2().irqAsserted(), mem.scsi().commands);
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
        uint32_t base = mem.dafbBase() & (Q700Memory::kVramSize - 1);
        int depth = mem.dafbDepth() ? mem.dafbDepth() : 8;
        const uint8_t* vram = mem.vram();
        const uint8_t (*clut)[3] = mem.clut();
        if (uint64_t(base) + uint64_t(H) * stride <= Q700Memory::kVramSize) {
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
        FILE* fp = fopen("q700_screen.ppm", "wb");
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
    const char* name = "Quadra 700";
    std::printf("%s — Macintosh %s %s\n", ok ? "PASSED" : "FAILED", name,
                ok ? "booted to the Finder" : "did not reach the Finder");
    return ok ? 0 : 1;
}
