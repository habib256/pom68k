// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Gate `q605_cudalle_mouse_etalon`: mouse motion through the REAL Cuda
// firmware (blueprint step 4). Boots Mac OS 8.1 to the Finder with
// POM68K_CUDA_LLE=1, then injects deltas into the bit-serial AdbLine and
// requires the low-memory mouse globals (RawMouse $082C, Mouse $0830) to
// move — the whole chain: AdbLine wire → 341S0788 autopoll → VIA1 SR →
// ADB Manager → mouse driver → jCrsrTask. Soft-skips without assets.

#include "Q605Memory.h"
#include "Cpu040.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

static std::string find(const char* rel) {
    for (const std::string base : { "", "../" }) {
        std::string p = base + rel;
        if (std::ifstream(p, std::ios::binary)) return p;
    }
    return {};
}

int main() {
    setenv("POM68K_CUDA_LLE", "1", 1);
    std::string rom = find("roms/1MB ROMs/1993-10 - FF7439EE - LC475,575,Quadra 605,Performa 475,476,575,577,578.ROM");
    std::string img = find("hdv/MacOS-8.1-boot.vhd");
    if (rom.empty() || img.empty()) { std::printf("SKIP: needs ROM+disk\n"); return 0; }

    std::ifstream rin(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(rin)), {});
    Q605Memory mem(32u << 20);
    if (!mem.loadRom(romData) || !mem.attachScsi(img)) return 1;
    if (!mem.cudaLleActive()) { std::printf("SKIP: needs roms/cuda/341s0788.bin\n"); return 0; }
    Cpu040 cpu(mem);
    mem.setCpu(&cpu);
    cpu.hardReset();
    while (mem.cpuHeld()) mem.tick(1000);

    auto rd16 = [&](uint32_t a) {
        return int16_t(uint16_t(mem.peek8(a) << 8 | mem.peek8(a + 1)));
    };

    constexpr int kFrame = 416667;                   // 25 MHz / ~60 Hz
    // Boot to the Finder (the boot etalon reaches it well before 9000).
    for (long f = 0; f < 9000 && !cpu.isHalted(); f++) cpu.runCycles(kFrame);
    std::printf("post-boot: RawMouse=(%d,%d) Mouse=(%d,%d)\n",
                rd16(0x082C), rd16(0x082E), rd16(0x0830), rd16(0x0832));

    int16_t startX = rd16(0x0832), startY = rd16(0x0830);
    for (long f = 0; f < 4000 && !cpu.isHalted(); f++) {
        mem.mouseMove(3, 2);                         // routes to AdbLine (LLE)
        cpu.runCycles(kFrame);
        if (f % 500 == 0)
            std::printf("  f=%ld RawMouse=(%d,%d) Mouse=(%d,%d)\n", f,
                        rd16(0x082C), rd16(0x082E), rd16(0x0830), rd16(0x0832));
    }
    int16_t endX = rd16(0x0832), endY = rd16(0x0830);
    std::printf("delta Mouse=(%d,%d)\n", endX - startX, endY - startY);
    bool moved = (endX != startX) || (endY != startY);
    std::printf("%s\n", moved ? "PASS: mouse moved on the real firmware"
                              : "FAIL: mouse frozen");
    return moved ? 0 : 1;
}
