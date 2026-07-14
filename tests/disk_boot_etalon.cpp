// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// M5 gate: the real ROM boots a synthetic floppy through the FULL chain —
// drive detect (sense), motor, recalibrate/seek (LSTRB commands), GCR
// nibble stream, address hunt, 6&2 de-nibblize, checksum — then jumps to
// our boot-block code, which paints the demo diagonal pattern from
// ScrnBase. If the pattern is on screen, the whole floppy path works.
// Self-contained except the ROM (soft-skips without roms/macplus.rom).

#include "Cpu68k.h"
#include "MacMemory.h"
#include "MacVideo.h"
#include "MacFrame.h"
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

// Boot block: proper header — bbID 'LK', bbEntry = BRA.W over the header
// (the ROM jumps to bootBlocks+2 after validating the signature AND the
// bbVersion word at +6; code placed directly at +2 gets rejected), then
// our code at +$92 reads ScrnBase ($0824) and paints the demo pattern.
static std::vector<uint8_t> makeBootDisk() {
    std::vector<uint8_t> img(819200, 0);
    img[0] = 0x4C; img[1] = 0x4B;            // bbID 'LK'
    img[2] = 0x60; img[3] = 0x00;            // bbEntry: bra.w +$8E → code at +$92
    img[4] = 0x00; img[5] = 0x8E;
    img[6] = 0x44; img[7] = 0x18;            // bbVersion $4418 (standard)
    static const uint8_t boot[] = {
        0x20, 0x78, 0x08, 0x24,              // movea.l ScrnBase.w,a0
        0x76, 0x00,                          // moveq #0,d3
        // frame:
        0x22, 0x48,                          // movea.l a0,a1
        0x24, 0x3C, 0xF0, 0xF0, 0xF0, 0xF0,  // move.l #$F0F0F0F0,d2
        0x22, 0x03,                          // move.l d3,d1
        0x02, 0x81, 0x00, 0x00, 0x00, 0x1F,  // andi.l #31,d1
        0xE3, 0xBA,                          // rol.l d1,d2
        0x30, 0x3C, 0x01, 0x55,              // move.w #341,d0
        // rows:
        0x72, 0x0F,                          // moveq #15,d1
        0x22, 0xC2,                          // move.l d2,(a1)+
        0x51, 0xC9, 0xFF, 0xFC,              // dbra d1,rowl
        0xE3, 0x9A,                          // rol.l #1,d2
        0x51, 0xC8, 0xFF, 0xF4,              // dbra d0,rows
        0x52, 0x83,                          // addq.l #1,d3
        0x22, 0x3C, 0x00, 0x00, 0x4E, 0x20,  // move.l #20000,d1
        0x53, 0x81,                          // subq.l #1,d1
        0x66, 0xFC,                          // bne.s delay
        0x60, 0xCE,                          // bra.s frame
    };
    for (size_t i = 0; i < sizeof boot; i++) img[0x92 + i] = boot[i];
    return img;
}

int main(int argc, char** argv) {
    std::string rom = (argc > 1) ? argv[1] : "";
    if (rom.empty()) {
        for (const char* p : { "roms/macplus.rom", "../roms/macplus.rom" }) {
            std::ifstream f(p, std::ios::binary);
            if (f) { rom = p; break; }
        }
    }
    if (rom.empty()) { std::printf("SKIP: roms/macplus.rom not found\n"); return 0; }
    std::ifstream in(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
    MacMemory mem;
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    if (!mem.internalDrive().insertImage(makeBootDisk())) {
        std::fprintf(stderr, "FAIL: insertImage\n"); return 1;
    }

    Cpu68k cpu(mem);
    mem.setCpu(&cpu);
    cpu.hardReset();
    MacFrameClock fc;
    fc.resync(cpu);
    // 4 MB RAM test (~45 s) + boot from floppy; 4200 frames ≈ 70 s.
    for (long f = 0; f < 4200; f++) fc.runFrame(cpu, mem);

    MacVideo video;
    const uint32_t* fb = video.render(mem);
    // The boot code's signature: consecutive rows are rol-by-1 of each other
    // and each row repeats every 4 bytes — impossible for the gray desktop
    // or any ROM screen. Check rows 100/101 and 200/201.
    auto row = [&](int y) {
        uint32_t v = 0;
        for (int i = 0; i < 32; i++) v = (v << 1) | (fb[y * 512 + i] & 1 ? 0 : 1);
        return v;
    };
    int good = 0;
    for (int y : { 40, 100, 200, 300 }) {
        uint32_t a = row(y), b = row(y + 1);
        if (a != 0 && a != 0xFFFFFFFF && b == ((a << 1) | (a >> 31))) good++;
    }
    std::printf("diagonal row pairs matched: %d/4\n", good);
    if (good < 3) {
        std::fprintf(stderr, "FAIL: boot code pattern not on screen\n");
        return 1;
    }
    std::printf("disk_boot_etalon: ROM booted our floppy code, gate passed\n");
    return 0;
}
