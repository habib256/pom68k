// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Phase C gate: the Macintosh IIci boots System 7.5 to the Finder on the
// RBV machine in its IIci flavor (MAME maciici.cpp maciici + rbv.cpp +
// adbmodem.cpp): 68030 @ 25 MHz, RBV RAM-based video (framebuffer = start
// of system RAM, Bt478 CLUT), the **ADB modem** (PIC1654S firmware LLE)
// and a **discrete 343-0042 RTC** instead of the IIsi's Egret, NCR 5380 +
// pseudo-DMA, SWIM1, discrete ASC, three empty NuBus slots. The 512 KB ROM
// is $368CADFE. Soft-skips without the ROM or a bootable image.
//
// Debug env knobs (the lc520_boot_etalon harness):
// - POM68K_SENSE=<n>  — monitor type (1/2/6, default 6 = 640×480).
// - POM68K_DIAG=1     — periodic PC/SCSI/depth trace.
// - POM68K_PROBE=1    — 200-frame run + final-state dump.
// - POM68K_HALT=2     — print the first 400 branch targets from reset.
// - POM68K_HALT=<pc>  — run to PC, dump a 128-deep branch-target trail.
// - POM68K_FRAMES=<n> — override the 16 000-frame budget.
// - POM68K_DUMP=1     — write iici_screen.ppm at the end.

#include "RbvMemory.h"
#include "RbvVideo.h"
#include "RbvCpu.h"

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

// Same DDM ddType $6A fixup as the sibling gates.
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
    std::string rom = find("roms/maciici.rom");
    if (rom.empty())
        rom = find("roms/512KB ROMs/1989-09 - 368CADFE - Mac IIci.ROM");
    std::string img = find("hdv/iici-boot.vhd");
    if (img.empty()) img = find("hdv/lc3-boot.vhd");
    if (img.empty()) img = find("hdv/GISTPERSO-boot.vhd");
    if (img.empty()) img = find("hdv/boot.vhd");
    if (img.empty()) img = find("hdv/System 7.5 HD.dsk");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs the 512 KB IIci ROM + a bootable hdv/ image\n");
        return 0;
    }

    std::ifstream in(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
    if (romData.size() != RbvMemory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM is %zu bytes, want 512 KB\n", romData.size());
        return 1;
    }

    RbvMemory mem(0x800000, RbvMemory::kCpuHzCi, /*iici=*/true);
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    int sense = 6;                           // 13" RGB 640×480
    if (const char* s = getenv("POM68K_SENSE")) sense = atoi(s);
    mem.setMonitorSense(uint8_t(sense));
    std::printf("ADB: %s\n", mem.adbLleActive() ? "PIC1654S modem LLE" : "HLE");
    RbvCpu cpu(mem, /*withFpu=*/true);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk image\n"); return 1; }
    ensureBootDriverType(mem.scsiDisk().image());

    while (mem.cpuHeld()) mem.tick(1000);
    const int64_t kFrame = RbvMemory::kCpuHzCi / 60;
    bool diag = getenv("POM68K_DIAG");
    uint32_t haltPc = 0;
    if (const char* h = getenv("POM68K_HALT")) haltPc = uint32_t(strtoul(h, nullptr, 16));
    if (haltPc) {
        if (haltPc == 2) {                   // first branch targets from reset
            uint32_t last2 = 0, prev1 = 0, prev2 = 0; int n = 0;
            for (long f = 0; f < 400 && !cpu.isHalted() && n < 400; f++) {
                for (int s = 0; s < kFrame && n < 400; s++) {
                    cpu.runCycles(1);
                    uint32_t pc = cpu.getPC();
                    if ((pc + 10 < last2 || pc > last2 + 10)
                        && pc != prev1 && pc != prev2) {
                        std::fprintf(stderr, "%s$%08X", (n % 8) ? " " : "\n", pc);
                        prev2 = prev1; prev1 = pc; n++;
                    }
                    last2 = pc;
                }
            }
            std::fprintf(stderr, "\n");
            return 0;
        }
        uint32_t trail[128] = {}; int ti = 0; uint32_t last = 0; bool trapped = false;
        for (long f = 0; f < 400 && !cpu.isHalted() && !trapped; f++) {
            for (int s = 0; s < kFrame && !trapped; s++) {
                cpu.runCycles(1);
                uint32_t pc = cpu.getPC();
                if ((pc + 10 < last || pc > last + 10)
                    && pc != trail[(ti + 127) % 128]
                    && pc != trail[(ti + 126) % 128])
                    trail[ti++ % 128] = pc;
                last = pc;
                if (pc >= haltPc && pc <= haltPc + 8) {
                    trapped = true;
                    std::fprintf(stderr, "[HALT] f=%ld pc=$%08X d7=$%08X\n"
                        "  trail:", f, pc, cpu.getD(7));
                    for (int k = 0; k < 128; k++)
                        std::fprintf(stderr, "%s $%08X", (k % 8) ? "" : "\n   ",
                                     trail[(ti + k) % 128]);
                    std::fprintf(stderr, "\n  a0=$%08X a1=$%08X a2=$%08X a3=$%08X "
                        "a4=$%08X a6=$%08X\n  d0=$%08X d1=$%08X d2=$%08X "
                        "d3=$%08X d7=$%08X sp=$%08X\n", cpu.getA(0), cpu.getA(1),
                        cpu.getA(2), cpu.getA(3), cpu.getA(4), cpu.getA(6),
                        cpu.getD(0), cpu.getD(1), cpu.getD(2), cpu.getD(3),
                        cpu.getD(7), cpu.getA(7));
                }
            }
        }
        return 0;
    }
    long limit = getenv("POM68K_PROBE") ? 200 : 16000;
    if (const char* n = getenv("POM68K_FRAMES")) limit = atol(n);
    for (long f = 0; f < limit && !cpu.isHalted(); f++) {
        cpu.runCycles(kFrame);
        if (diag && (f < 400 ? !(f % 20) : !(f % 400)))
            std::fprintf(stderr, "[diag] f=%ld pc=$%08X SCSI=%ld depth=%d "
                "vidcfg=$%02X\n", f, cpu.getPC(), mem.scsi().commands,
                mem.videoDepth(), mem.videoConfig());
    }
    if (getenv("POM68K_PROBE")) {
        uint32_t pc = cpu.getPC() & ~1u;
        std::fprintf(stderr, "[probe] SCSI=%ld pc=$%08X vidcfg=$%02X\n",
                     mem.scsi().commands, cpu.getPC(), mem.videoConfig());
        std::fprintf(stderr, "  RAM @ $%08X:", pc - 0x10);
        for (int k = -16; k < 24; k++)
            std::fprintf(stderr, "%s%02X", (k % 2) ? "" : " ", mem.peek8(pc + k));
        std::fprintf(stderr, "\n  regs a0=$%08X a1=$%08X a2=$%08X d0=$%08X "
            "d1=$%08X d7=$%08X\n", cpu.getA(0), cpu.getA(1), cpu.getA(2),
            cpu.getD(0), cpu.getD(1), cpu.getD(7));
    }

    if (cpu.isHalted()) { std::fprintf(stderr, "FAIL: CPU halted (double fault)\n"); return 1; }

    RbvVideo video(mem);
    std::vector<uint32_t> fb;
    video.decode(fb);
    int W = 0, H = 0;
    video.size(W, H);
    auto luma = [&](int x, int y) {
        uint32_t p = fb[size_t(y) * W + x];
        return (2 * int((p >> 16) & 0xFF) + 5 * int((p >> 8) & 0xFF)
              + int(p & 0xFF)) / 8;
    };
    auto darkRatio = [&](int x0, int x1, int y0, int y1) {
        long dark = 0;
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++)
                if (luma(x, y) < 0x80) dark++;
        return double(dark) / (double(x1 - x0) * (y1 - y0));
    };
    // 8×8 block AVERAGE luma in the mid range — reads a solid-gray or color
    // desktop (System 7.5 default). The 1-bpp B&W dither reads instead via
    // the per-pixel darkRatio (≈50% black). Sample a clean lower-centre band
    // that clears the menu bar, the left Finder window and the right-column
    // disk/trash icons.
    auto midBlocks = [&](int x0, int x1, int y0, int y1) {
        long mid = 0, total = 0;
        for (int y = y0; y + 8 <= y1; y += 8)
            for (int x = x0; x + 8 <= x1; x += 8) {
                long sum = 0;
                for (int dy = 0; dy < 8; dy++)
                    for (int dx = 0; dx < 8; dx++) sum += luma(x + dx, y + dy);
                int avg = int(sum / 64);
                total++;
                if (avg >= 0x20 && avg <= 0xDF) mid++;
            }
        return total ? double(mid) / total : 0.0;
    };
    double menuBar = darkRatio(0, W, 2, 16);
    double deskDark = darkRatio(W / 4, 3 * W / 4, H - 140, H - 60);
    double deskGray = midBlocks(W / 4, 3 * W / 4, H - 140, H - 60);
    // A live desktop is either a structured B&W dither (mixes black+white
    // pixels — this System's default is a dense ~86%-black weave, not a 50%
    // checkerboard) or a solid mid-tone colour desktop. A white boot screen
    // (deskDark≈0) and dead-black video (deskDark≈1) both fail both tests.
    bool desktopAlive = (deskDark > 0.15 && deskDark < 0.95) || deskGray > 0.60;
    if (getenv("POM68K_DUMP")) {
        FILE* fp = fopen("iici_screen.ppm", "wb");
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
                "desktop dither %.2f / gray %.2f (want alive), SCSI commands %ld\n",
                W, H, mem.videoDepth(), menuBar, deskDark, deskGray,
                mem.scsi().commands);

    bool ok = menuBar < 0.30 && desktopAlive && mem.scsi().commands > 50;
    std::printf("%s\n", ok ? "PASSED — Macintosh IIci booted to the Finder"
                           : "FAILED");
    return ok ? 0 : 1;
}
