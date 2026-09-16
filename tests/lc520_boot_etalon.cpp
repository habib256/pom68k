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

#include "AgentBootProbe.h"
#include "AssetFingerprint.h"
#include "InfiniteHdCompanion.h"
#include "BeyondBoot.h"
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
    // Stock System 7.5.3 first (pinned 2026-09-16); GIST PERSO behind it.
    std::string img = find("hdv/System 7.5.3 HD.dsk");
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
    SonoraMemory mem(pom68k::defaultCoreConfig(), 0x800000,
                     SonoraMemory::kCpuHz, boxId,
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
    SonoraCpu cpu(mem, jit::defaultResolvedConfig(),
                  pom68k::defaultCoreConfig().cpu, /*withFpu=*/true);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk image\n"); return 1; }
    if (!infinitehd::attach(mem, img)) return 1;   // the Startup Items alias
    if (!agentboot::install(mem)) return 1;
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
    int W = 0, H = 0;
    // A near-white run wider than a dialog is either a Finder window the
    // volume opens at boot (stock 7.5.3's Startup Items alias opens the
    // "Infinite HD" companion, 2026-09-16) or a modal alert. Cmd-Option-W
    // closes every window and is harmless to an alert; Return takes an
    // alert's default button. Alternate them, at most three gestures, and
    // a clean desktop passes straight through — the beyond gate's boot
    // loop, in miniature.
    for (int attempt = 0; attempt < 4; attempt++) {
        video.decode(fb);
        video.size(W, H);
        const int run = beyondboot::lightRun(fb, W, H);
        if (attempt == 3 || run < beyondboot::kDialogRun) break;
        const bool closeWindows = (attempt % 2) == 0;
        std::printf("window or alert on screen (run %d) — %s\n", run,
                    closeWindows ? "Cmd-Option-W" : "Return");
        if (closeWindows) {
            // The Finder front first (Stickies is a Startup Item of the
            // stock image): a Return for any modal box, then a click on
            // the lower-right desktop.
            mem.keyEvent(0x24, true);
            for (int f = 0; f < 12; f++) cpu.runCycles(kFrame);
            mem.keyEvent(0x24, false);
            for (int f = 0; f < 90; f++) cpu.runCycles(kFrame);
            for (int i = 0; i < 90; i++) { mem.mouseMove(8, 6); for (int f = 0; f < 2; f++) cpu.runCycles(kFrame); }
            for (int i = 0; i < 6; i++) { mem.mouseMove(-6, -5); for (int f = 0; f < 2; f++) cpu.runCycles(kFrame); }
            mem.mouseButton(true);
            for (int f = 0; f < 10; f++) cpu.runCycles(kFrame);
            mem.mouseButton(false);
            for (int f = 0; f < 60; f++) cpu.runCycles(kFrame);
            mem.keyEvent(0x37, true);            // Cmd
            for (int f = 0; f < 6; f++) cpu.runCycles(kFrame);
            mem.keyEvent(0x3A, true);            // Option
            for (int f = 0; f < 6; f++) cpu.runCycles(kFrame);
            mem.keyEvent(0x0D, true);            // W (US layout on the stock image)
            for (int f = 0; f < 30; f++) cpu.runCycles(kFrame);
            mem.keyEvent(0x0D, false);
            mem.keyEvent(0x3A, false);
            mem.keyEvent(0x37, false);
        } else {
            mem.keyEvent(0x24, true);
            for (int f = 0; f < 6; f++) cpu.runCycles(kFrame);
            mem.keyEvent(0x24, false);
        }
        for (int f = 0; f < 240; f++) cpu.runCycles(kFrame);
    }
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
    // The shared signature reads a light colour desktop too (the grey
    // checkerboard of stock 7.5.3) — BeyondBoot.h, 2026-09-16.
    const beyondboot::DesktopSignature sig =
        beyondboot::desktopSignature(fb, W, H, W - 112, W, 40, H - 44);
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

    std::printf("mode %dx%d depth %d; menu bar dark %.2f (want <0.30) white %.2f, "
                "desktop dark %.2f ink %.2f (want ink >0.35), run %d, SCSI commands %ld\n",
                W, H, mem.videoDepth(), menuBar, sig.menuWhite, desktop, sig.deskInk,
                sig.lightRun, mem.scsi().commands);

    // Under the agent probe « POM68K Disques » is front with its own window,
    // a light run the dialog rule cannot tell from an alert: the probe's
    // own check (agentboot::check) says whether the Finder launched it.
    const bool finder = sig.finder ||
        (agentboot::enabled() && sig.menuDark > 0.01 && sig.menuDark < 0.30 &&
         sig.menuWhite > 0.60 && sig.deskInk > 0.35);
    bool ok = W == 640 && H == 480 && mem.videoDepth() == 3
           && finder && mem.scsi().commands > 50;
    std::printf("%s\n", ok ? "PASSED — Macintosh LC 520 booted to the Finder"
                           : "FAILED");
    ok = agentboot::check(mem, cpu, kFrame, ok);
    return ok ? 0 : 1;
}
