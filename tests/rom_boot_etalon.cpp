// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// M4 gate: boot a real Mac Plus ROM headless to the blinking-? floppy
// icon. Soft-skips when roms/macplus.rom is absent (ROMs are user-provided
// — POMIIGS soft-skip convention). Checks the boot outcome by signature:
// overlay cleared, gray 50% dither desktop, white icon box at screen
// centre. Pixel-perfect etalons come with M8 (NeoST pattern).

#include "Cpu68k.h"
#include "MacMemory.h"
#include "MacVideo.h"
#include "MacFrame.h"
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    std::string path = (argc > 1) ? argv[1] : "";
    if (path.empty()) {
        for (const char* p : { "roms/macplus.rom", "../roms/macplus.rom" }) {
            std::ifstream f(p, std::ios::binary);
            if (f) { path = p; break; }
        }
    }
    if (path.empty()) {
        std::printf("SKIP: roms/macplus.rom not found (user-provided)\n");
        return 0;                                  // soft skip
    }
    std::ifstream in(path, std::ios::binary);
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
    MacMemory mem;
    if (!mem.loadRom(rom)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }

    Cpu68k cpu(mem);
    mem.setCpu(&cpu);
    cpu.hardReset();
    MacFrameClock fc;
    fc.resync(cpu);
    // 4 MB RAM test ≈ 45 s of machine time; 3600 frames ≈ 60 s.
    for (long f = 0; f < 3600; f++) fc.runFrame(cpu, mem);

    if (mem.overlay()) { std::fprintf(stderr, "FAIL: overlay still on\n"); return 1; }

    MacVideo video;
    const uint32_t* fb = video.render(mem);
    long black = 0;
    for (int i = 0; i < 512 * 342; i++) if (!(fb[i] & 0xFF)) black++;
    double ratio = double(black) / (512 * 342);
    std::printf("black ratio %.3f (gray desktop ≈ 0.50)\n", ratio);
    if (ratio < 0.45 || ratio > 0.55) {
        std::fprintf(stderr, "FAIL: not the gray desktop\n");
        return 1;
    }
    // The ?-floppy icon: a mostly-white box around screen centre, which a
    // pure 50% dither can't produce. Patch coordinates measured from the
    // v3 ROM's icon (rows 156-175, cols 245-266); the "?" glyph keeps the
    // ratio below 1.0 but well above the dither's 0.50.
    long white = 0;
    for (int y = 156; y < 176; y++)
        for (int x = 245; x < 267; x++)
            if (fb[y * 512 + x] & 0xFF) white++;
    double iconWhite = double(white) / (20 * 22);
    std::printf("icon patch white ratio %.2f (dither would be 0.50)\n", iconWhite);
    if (iconWhite < 0.60) {
        std::fprintf(stderr, "FAIL: no icon at screen centre\n");
        return 1;
    }
    std::printf("rom_boot_etalon: blinking-? reached, gate passed\n");
    return 0;
}
