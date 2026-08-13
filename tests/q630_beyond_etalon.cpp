// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Beyond-boot on the Quadra 630 (F108 + PrimeTime II + Valkyrie, Cuda
// 341S0060) — shared-engine gate (BeyondBoot.h). Rig, GDevice decode and
// signature from q630_boot_etalon; Time is physical (q605 pattern).
// POM68K_BEYOND=soak|persist. Soft-skips without assets.

#include "AssetFingerprint.h"
#include "BeyondBoot.h"
#include "Q630Cpu.h"
#include "Q630Memory.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {

uint32_t peek32(const Q630Memory& mem, uint32_t a) {
    return uint32_t(mem.peek8(a)) << 24 | uint32_t(mem.peek8(a + 1)) << 16
         | uint32_t(mem.peek8(a + 2)) << 8 | mem.peek8(a + 3);
}

struct Screen { int width = 0, height = 0; std::vector<uint32_t> pixels; };

// q630_boot_etalon.cpp:92-112, verbatim — this gate judges the Finder on
// the same statistic as the boot gate for the same machine.
struct Stats { double mean = 0, deviation = 0; };
inline Stats luminanceStats(const Screen& s, int x0, int x1, int y0, int y1) {
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
Screen decodeScreen(const Q630Memory& mem) {
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
    int depth = mem.videoDepth();
    uint32_t stride = mem.videoStride();
    uint32_t offset = (pmBase ? pmBase : scrnBase) & (Q630Memory::kVramSize - 1);
    if (W <= 0 || W > 1600 || H <= 0 || H > 1200 ||
        (depth != 1 && depth != 2 && depth != 4 && depth != 8) ||
        stride < uint32_t((W * depth + 7) / 8) ||
        uint64_t(offset) + uint64_t(H) * stride > Q630Memory::kVramSize)
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
    // q630_boot_etalon's own list, verbatim. This gate carried a fourth
    // spelling of the 06684214 dump — "LC630 & Quadra630.ROM" — that no
    // archive uses, so with the ROM sitting right there under its real
    // name both legs SKIPped, and a SKIP exits 0: two green CTest rows
    // that had never run a Quadra 630.
    std::string rom = testasset::findAny({
        "roms/1MB ROMs/1994-07 - 06684214 - LC,Quadra,Performa 630.ROM",
        "roms/mame/macqd630/06684214.bin",
        "roms/quadra630.rom" });
    std::string img = testasset::find("hdv/MacOS-8.1-boot.vhd");
    if (img.empty()) img = testasset::find("hdv/boot.vhd");
    if (img.empty()) img = testasset::find("hdv/GISTPERSO-boot.vhd");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs the Quadra 630 ROM + a bootable hdv/ image\n");
        return 0;
    }
    testasset::report({ rom, img });

    std::ifstream in(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
    Q630Memory mem(32u << 20);
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    Q630Cpu cpu(mem);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk image\n"); return 1; }
    beyondboot::ensureBootDriverType(mem.scsiDisk().image());
    const int64_t kFrame = Q630Memory::kCpuHz / 60;

    auto frames = [&](long n) {
        for (long f = 0; f < n && !cpu.isHalted(); f++) cpu.runCycles(kFrame);
    };
    // q630_boot_etalon's OWN signature, luminance mean/deviation over the
    // menu bar and the right desktop strip (:202-216). The dark-ratio pair
    // this gate used to carry was invented, not inherited, and it sampled a
    // bottom-CENTRE band the 8.1 desktop leaves plain: the Finder was up
    // and the gate read desk 0.06 against a 0.15 floor. That never showed
    // because the gate had also never found its ROM.
    auto finderUp = [&]() {
        Screen s = decodeScreen(mem);
        if (s.width != 640 || s.height != 480) return false;
        const Stats menu = luminanceStats(s, 0, s.width, 2, 16);
        const Stats desk = luminanceStats(s, 520, 630, 40, 430);
        static double lm = -1, ld = -1;
        if (menu.mean != lm || desk.mean != ld) {
            std::fprintf(stderr, "[finder] menu %.1f/%.1f desk %.1f/%.1f\n",
                         menu.mean, menu.deviation, desk.mean, desk.deviation);
            lm = menu.mean; ld = desk.mean;
        }
        return menu.mean > 170 && menu.mean < 235 &&
               menu.deviation > 40 && menu.deviation < 100 &&
               desk.mean > 100 && desk.mean < 190 &&
               desk.deviation > 30 && desk.deviation < 90 &&
               menu.mean - desk.mean > 35;
    };
    auto boot = [&]() {
        while (mem.cpuHeld()) mem.tick(1000);
        // 16000 frames then a single look was this gate's whole boot, and
        // it was short: q630_boot_etalon budgets 30000 for the same machine
        // on the same 8.1 volume. Poll instead, tapping Return for the
        // alerts a dirty volume raises after the persist reboot — 150-frame
        // holds, the engine's rule everywhere (pom68k-81-image-slow-keys).
        // Nobody had seen this: the gate looked for a ROM under a name no
        // archive uses, so both its legs SKIPped green without ever
        // starting a Quadra 630.
        frames(16000);
        for (int poll = 0; poll < 20; poll++) {
            if (cpu.isHalted()) return false;
            if (finderUp()) return true;
            mem.keyEvent(0x24, true);
            frames(150);
            mem.keyEvent(0x24, false);
            frames(850);
        }
        return !cpu.isHalted() && finderUp();
    };

    if (!boot()) { std::fprintf(stderr, "FAIL: no Finder after boot\n"); return 1; }
    std::printf("Finder up, ADB %s, SCSI %ld\n",
                mem.cudaLleActive() ? "Cuda LLE" : "HLE", mem.scsi().commands);

    beyondboot::Hooks h;
    h.name = "Quadra 630";
    h.frames = frames;
    h.halted = [&]() { return cpu.isHalted(); };
    h.finderUp = finderUp;
    h.time = [&](uint32_t* out) { *out = peek32(mem, 0x20C); return true; };
    h.key = [&](uint8_t code, bool down) { mem.keyEvent(code, down); };
    h.disk = [&]() -> std::vector<uint8_t>& { return mem.scsiDisk().image(); };
    h.writes = [&]() { return mem.scsiDisk().writeBlocks; };
    h.reboot = [&]() { cpu.hardReset(); return boot(); };
    // Same idle blanking as Sonora: three minutes of nothing and the 8.1
    // desktop goes to an all-black frame (measured 0.0/0.0 against
    // 201.8/72.8 menu, 157.9/73.4 desk). Rouse it the way a user would
    // before judging — waking from idle is part of what the soak proves.
    h.wake = [&]() {
        for (int i = 0; i < 8; i++) {
            mem.mouseMove(i & 1 ? 5 : -5, i & 2 ? 2 : -2);
            frames(15);
        }
        mem.mouseButton(true);
        frames(30);
        mem.mouseButton(false);
        frames(300);                         // ~5 s to repaint
    };
    h.dump = [&](const char* mode) {
        Screen s = decodeScreen(mem);
        beyondboot::dumpPpm((std::string("q630_beyond_") + mode + ".ppm").c_str(),
                            s.pixels, s.width, s.height);
    };
    return beyondboot::run(h);
}
