// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Phase C gate: the Macintosh LC 520 boots System 7.5 to the Finder on the
// Sonora machine — an all-in-one sibling of the LC III (68030 @ 25 MHz,
// same Sonora gate array, SWIM2, NCR 5380 + pseudo-DMA), differing by the
// separate universal ROM ($EDE66CBD — also LC 550 / Color Classic II /
// Performa 275/550/560 / Macintosh TV), the model longword $A55A0100
// (maclc3.cpp maclc520_map — IS in this ROM's machine table, entries vid
// $32/$4B selected by monitor sense 6/2), the built-in 640×480 display
// (comes up 8-bpp color) and a CUDA MCU instead of the Egret (maclc3.cpp:379
// CUDA_V2XX 341s0060 — Cuda 2.40; 2.37 livelocks on pseudo-cmd $0E, see
// docs/LC520_BRINGUP.md). Soft-skips without the ROM or a bootable image.

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

    uint32_t boxId = SonoraMemory::kIdLc520;
    if (const char* b = getenv("POM68K_BOXID"))
        boxId = uint32_t(strtoul(b, nullptr, 16));
    // The AIO family carries a Cuda (341S0060/341S0788), not the LC III's
    // Egret — the ROM's reset handshake only a Cuda answers (MAME
    // maclc3.cpp:379 CUDA_V2XX). POM68K_AIO_EGRET=1 probes the Egret wiring
    // instead (the $2000-$2003 machine-table entries carry MCU type 0).
    SonoraMemory mem(0x800000, SonoraMemory::kCpuHz, boxId,
                     /*cudaAdb=*/getenv("POM68K_AIO_EGRET") == nullptr);
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    int sense = 6;                           // built-in 640×480 RGB
    if (const char* s = getenv("POM68K_SENSE")) sense = atoi(s);
    mem.setMonitorSense(uint8_t(sense));
    std::printf("model ID $%08X (want $A55A0100)\n",
                uint32_t(mem.peek8(0x5FFFFFFC)) << 24 |
                uint32_t(mem.peek8(0x5FFFFFFD)) << 16 |
                uint32_t(mem.peek8(0x5FFFFFFE)) << 8 | mem.peek8(0x5FFFFFFF));
    std::printf("ADB: %s\n", mem.egretLleActive() ? "Egret firmware LLE" : "HLE");
    SonoraCpu cpu(mem, /*withFpu=*/true);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk image\n"); return 1; }
    ensureBootDriverType(mem.scsiDisk().image());

    while (mem.cpuHeld()) mem.tick(1000);
    const int64_t kFrame = SonoraMemory::kCpuHz / 60;
    const long kFrames = 16000;
    bool diag = getenv("POM68K_DIAG");
    uint32_t ring[24] = {}; int ri = 0; bool trapped = false;
    // Trap the first time execution reaches the halt ($4B90 bra-self) and dump
    // the instruction stream + registers that decided to hang there.
    uint32_t haltPc = 0;
    if (const char* h = getenv("POM68K_HALT")) haltPc = uint32_t(strtoul(h, nullptr, 16));
    if (haltPc) {
        // Instruction-level trail: 1-cycle quanta + dedup ring, stop on the
        // exact PC or on the monitor flag (d7 bit 17) — whichever fires first.
        // POM68K_HALT=2: print the first branch targets from reset and exit —
        // the from-scratch "where does the boot flow diverge" view.
        if (haltPc == 2) {
            uint32_t last2 = 0, prev1 = 0, prev2 = 0; int n = 0;
            for (long f = 0; f < 400 && !cpu.isHalted() && n < 400; f++) {
                for (int s = 0; s < kFrame && n < 400; s++) {
                    cpu.runCycles(1);
                    uint32_t pc = cpu.getPC();
                    if ((pc + 10 < last2 || pc > last2 + 10)
                        && pc != prev1 && pc != prev2) {
                        std::fprintf(stderr, "%s$%08X", (n % 8) ? " " : "\n",
                                     pc);
                        prev2 = prev1; prev1 = pc; n++;
                    }
                    last2 = pc;
                }
            }
            std::fprintf(stderr, "\n");
            return 0;
        }
        // Record only control transfers (|delta| > 10) — a much deeper history
        // than a straight PC ring; dedup consecutive identical targets so a
        // polling loop occupies two slots, not the whole ring.
        uint32_t trail[128] = {}; int ti = 0; uint32_t last = 0;
        for (long f = 0; f < 400 && !cpu.isHalted() && !trapped; f++) {
            for (int s = 0; s < kFrame && !trapped; s++) {
                cpu.runCycles(1);
                uint32_t pc = cpu.getPC();
                if ((pc + 10 < last || pc > last + 10)
                    && pc != trail[(ti + 127) % 128]
                    && pc != trail[(ti + 126) % 128])
                    trail[ti++ % 128] = pc;
                last = pc;
                bool hit = haltPc == 1 ? cpu.getA(7) == 0x2600
                         : (pc >= haltPc && pc <= haltPc + 8)
                           || (cpu.getD(7) & 0x20000);
                if (hit) {
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
                    break;
                }
            }
        }
        return 0;
    }
    long limit = getenv("POM68K_PROBE") ? 200 : kFrames;
    if (const char* n = getenv("POM68K_FRAMES")) limit = atol(n);
    static int cmpHits = 0;
    for (long f = 0; f < limit && !cpu.isHalted(); f++) {
        if (getenv("POM68K_PROBE")) {
            for (int s = 0; s < kFrame / 20; s++) {
                cpu.runCycles(20);
                if (cpu.getPC() == 0x40804B0C && cmpHits < 40) {
                    uint32_t a1 = cpu.getA(1);
                    uint16_t w = uint16_t(mem.peek8(a1 + 0x3c) << 8 | mem.peek8(a1 + 0x3d));
                    std::fprintf(stderr, "[cmp] d0=$%08X box[3c].w=$%04X "
                        "vid[12].b=$%02X a1=$%08X\n", cpu.getD(0), w,
                        mem.peek8(a1 + 0x12), a1);
                    cmpHits++;
                }
            }
            if (f == limit - 1) {
                std::fprintf(stderr, "[probe] boxId=$%08X -> SCSI=%ld pc=$%08X "
                    "d7=$%08X monitor=%d\n", boxId, mem.scsi().commands,
                    cpu.getPC(), cpu.getD(7), int((cpu.getD(7) & 0x20000) != 0));
                uint32_t pc = cpu.getPC() & ~1u;
                std::fprintf(stderr, "  RAM @ $%08X:", pc - 0x10);
                for (int k = -16; k < 24; k++)
                    std::fprintf(stderr, "%s%02X", (k % 2) ? "" : " ",
                                 mem.peek8(pc + k));
                std::fprintf(stderr, "\n  regs a0=$%08X a1=$%08X a2=$%08X "
                    "d0=$%08X d1=$%08X\n", cpu.getA(0), cpu.getA(1), cpu.getA(2),
                    cpu.getD(0), cpu.getD(1));
            }
            continue;
        }
        // Near the fatal, step in tiny quanta with a PC ring buffer so we can
        // reconstruct the instruction stream that jumps into the ROM monitor.
        if (diag && f >= 100 && f < 220 && !trapped) {
            for (int s = 0; s < kFrame / 40 && !trapped; s++) {
                cpu.runCycles(40);
                ring[ri++ & 15] = cpu.getPC();
                if (cpu.getD(7) & 0x20000) {
                    trapped = true;
                    std::fprintf(stderr, "[TRAP] monitor entry f=%ld d7=$%08X "
                        "a0=$%08X a1=$%08X a4=$%08X d0=$%08X\n", f, cpu.getD(7),
                        cpu.getA(0), cpu.getA(1), cpu.getA(4), cpu.getD(0));
                    std::fprintf(stderr, "  recent PCs:");
                    for (int k = 0; k < 16; k++)
                        std::fprintf(stderr, " $%08X", ring[(ri + k) & 15]);
                    std::fprintf(stderr, "\n");
                }
            }
        } else {
            cpu.runCycles(kFrame);
        }
        if (diag && (f < 400 ? !(f % 20) : !(f % 400)))
            std::fprintf(stderr, "[diag] f=%ld pc=$%08X SCSI=%ld depth=%d "
                "d7=$%08X mcu=$%04X\n", f, cpu.getPC(), mem.scsi().commands,
                mem.videoDepth(), cpu.getD(7),
                mem.egretLleActive() ? mem.egretLle().mcu().pc() : 0);
    }

    if (cpu.isHalted()) { std::fprintf(stderr, "FAIL: CPU halted (double fault)\n"); return 1; }

    SonoraVideo video(mem);
    std::vector<uint32_t> fb;
    video.decode(fb);
    int W = 0, H = 0;
    video.size(W, H);
    // Luminance-based signature: the LC 520 comes up in 8-bpp COLOR and the
    // System 7.5 desktop pattern is an orange/green weave — the sibling
    // gates' blue-channel blackRatio reads it as near-solid black. Menu bar
    // stays near-white; the desktop weave dithers to mid luminance.
    auto darkRatio = [&](int x0, int x1, int y0, int y1) {
        long dark = 0;
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++) {
                uint32_t p = fb[size_t(y) * W + x];
                // Perceptual weights (~ITU 601): the weave is green-heavy.
                int luma = (2 * int((p >> 16) & 0xFF) + 5 * int((p >> 8) & 0xFF)
                          + int(p & 0xFF)) / 8;
                if (luma < 0x80) dark++;
            }
        return double(dark) / (double(x1 - x0) * (y1 - y0));
    };
    double menuBar = darkRatio(0, W, 2, 16);
    double desktop = darkRatio(W - 112, W, 40, H - 44);
    if (getenv("POM68K_DUMP")) {          // screenshot for eyeballing
        FILE* fp = fopen("lc520_screen.ppm", "wb");
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
                "desktop %.2f (want 0.35-0.80), SCSI commands %ld\n",
                W, H, mem.videoDepth(), menuBar, desktop, mem.scsi().commands);

    bool ok = W == 640 && H == 480 && mem.videoDepth() == 3
           && menuBar < 0.30 && desktop > 0.35 && desktop < 0.80
           && mem.scsi().commands > 50;
    std::printf("%s\n", ok ? "PASSED — Macintosh LC 520 booted to the Finder"
                           : "FAILED");
    return ok ? 0 : 1;
}
