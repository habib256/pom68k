// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// O6.8 gate: boot the real Mac LC II ROM to the Finder desktop off a
// SCSI hard-disk image, exercising the whole V8 machine — 68030+PMMU
// (24-bit map through Apple's MMU tables), Egret HLE (reset release,
// RTC/PRAM, ReadXPram), V8 RAM controller + overlay, pseudo-VIA IRQs,
// ASC boot beep, SCSI pseudo-DMA with the IRQ-latch end-of-transfer,
// SWIM probe, Ariel video. The 68882 is attached (the target volume
// issues FPU ops; a bare LC II faults with system-error 10).
//
// Soft-skips unless both the LC II ROM (docs/512KB ROMs/...) and a
// bootable System image (hdv/boot.vhd) are present. The ROM's boot scan
// only loads a driver whose DDM ddType is $6A ($A07264); the etalon
// adds that entry to an in-memory copy of the disk before attaching it,
// the same fixup tools/wrap_hfs.py bakes into an image.
//
// Finder signature at 512×384×1bpp: a mostly-white menu bar with black
// glyphs across the top, a 50%-gray dithered desktop below it, and a
// non-trivial SCSI command count (the volume was read, not just probed).
// Exit 0 = pass / soft-skip, 1 = fail.

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

// Ensure the DDM ('ER' at block 0) carries a driver descriptor whose
// ddType is $6A — the LC II ROM's boot scan ($A07264) only loads that
// one, and disk images made by other tools usually carry type $0001.
// Non-destructive: patches the in-memory image only.
static void ensureBootDriverType(std::vector<uint8_t>& img) {
    if (img.size() < 512 || img[0] != 'E' || img[1] != 'R') return;
    int count = (img[0x10] << 8) | img[0x11];        // sbDrvrCount
    for (int i = 0; i < count && 0x12 + i * 8 + 8 <= 512; i++) {
        int e = 0x12 + i * 8;
        if (((img[e + 6] << 8) | img[e + 7]) == 0x6A) return;   // already there
    }
    // Append a driver entry mirroring entry 0 but with ddType $6A.
    if (count >= 1 && 0x12 + count * 8 + 8 <= 512) {
        int src = 0x12, dst = 0x12 + count * 8;
        for (int k = 0; k < 8; k++) img[dst + k] = img[src + k];
        img[dst + 6] = 0x00; img[dst + 7] = 0x6A;    // ddType = $6A
        img[0x10] = uint8_t((count + 1) >> 8);
        img[0x11] = uint8_t(count + 1);
    }
}

int main() {
    std::string rom = find("docs/512KB ROMs/1992-03 - 35C28F5F - Mac LC II.ROM");
    // A System disk the machine boots to the Finder. boot.vhd (volume
    // "MacPack") is the reference; the user's own volumes can be wrapped
    // bootable with tools/wrap_hfs.py and dropped in as hdv/lcii-boot.vhd.
    std::string img = find("hdv/lcii-boot.vhd");
    if (img.empty()) img = find("hdv/boot.vhd");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs the 512 KB LC II ROM + a bootable hdv/ image\n");
        return 0;
    }

    std::ifstream in(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
    if (romData.size() != V8Memory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM is %zu bytes, want 512 KB\n", romData.size());
        return 1;
    }

    V8Memory mem;
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    Cpu030 cpu(mem, /*withFpu=*/true);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk image\n"); return 1; }
    ensureBootDriverType(mem.scsiDisk().image());

    // Egret holds the CPU at power-on; release it, then run enough
    // machine time to complete POST + System load to the Finder. The
    // cold-boot full-RAM burn-in dominates; the Finder is up and stable
    // by ~4.2e9 cycles in tracing (an idle-screen dim blanks it later,
    // so sample inside that window rather than running longer).
    while (mem.cpuHeld()) mem.tick(1000);
    const int64_t kFrame = 640 * 407;        // 60.15 Hz @ 15.6672 MHz
    const long kFrames = 16000;              // ≈4.17e9 cycles
    for (long f = 0; f < kFrames && !cpu.isHalted(); f++)
        cpu.runCycles(kFrame);

    if (cpu.isHalted()) { std::fprintf(stderr, "FAIL: CPU halted (double fault)\n"); return 1; }

    V8Video video(mem);
    std::vector<uint32_t> fb;
    video.decode(fb);                        // 512×384, 00RRGGBB (1bpp: black/white)
    const int W = 512;
    auto blackRatio = [&](int x0, int x1, int y0, int y1) {
        long black = 0;
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++)
                if ((fb[y * W + x] & 0xFF) < 0x80) black++;
        return double(black) / (double(x1 - x0) * (y1 - y0));
    };
    // Full-width menu bar: mostly white with black menu-title glyphs.
    double menuBar = blackRatio(0, W, 2, 16);
    // Desktop 50% gray dither, sampled to the right of any open window
    // (the top-right corner is always bare desktop on a fresh boot).
    double desktop = blackRatio(400, W, 40, 340);

    std::printf("menu bar black %.2f (want <0.30), desktop %.2f (want 0.35-0.65), "
                "SCSI commands %ld\n", menuBar, desktop, mem.scsi().commands);

    bool ok = menuBar < 0.30 && desktop > 0.35 && desktop < 0.65
           && mem.scsi().commands > 50;
    std::printf("%s\n", ok ? "PASSED — booted to the Finder" : "FAILED");
    return ok ? 0 : 1;
}
