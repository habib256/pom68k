// POM68K — dev tool: verify LC II ADB mouse delivery (EXCLUDE_FROM_ALL).
// Baseline reference for the Mac II mouse work: boots to Finder, injects
// motion, watches low-mem Mouse globals move.

#include "V8Memory.h"
#include "Cpu030.h"
#include <cstdio>
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
    std::string rom = find("roms/512KB ROMs/1992-03 - 35C28F5F - Mac LC II.ROM");
    std::string img = find("hdv/lcii-boot.vhd");
    if (img.empty()) img = find("hdv/GISTPERSO-boot.vhd");
    if (img.empty()) img = find("hdv/System 7.5 HD.dsk");
    if (rom.empty() || img.empty()) { std::printf("SKIP: needs ROM+disk\n"); return 0; }

    std::ifstream rin(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(rin)), {});
    V8Memory mem;
    mem.loadRom(romData);
    Cpu030 cpu(mem, true);
    mem.setCpu(&cpu);
    cpu.hardReset();
    mem.attachScsi(img);

    auto rd16 = [&](uint32_t a) {
        return int16_t(uint16_t(mem.peek8(a) << 8 | mem.peek8(a + 1)));
    };

    while (mem.cpuHeld()) mem.tick(1000);
    const int64_t kFrame = 640 * 407;
    for (long f = 0; f < 12000 && !cpu.isHalted(); f++) cpu.runCycles(kFrame);
    std::printf("post-boot: RawMouse=(%d,%d) Mouse=(%d,%d)\n",
                rd16(0x082C), rd16(0x082E), rd16(0x0830), rd16(0x0832));

    int16_t startX = rd16(0x0832), startY = rd16(0x0830);
    for (long f = 0; f < 4000 && !cpu.isHalted(); f++) {
        mem.adb().mouseMove(3, 2);
        cpu.runCycles(kFrame);
        if (f % 500 == 0)
            std::printf("  f=%ld Mouse=(%d,%d)\n", f, rd16(0x0830), rd16(0x0832));
    }
    int16_t endX = rd16(0x0832), endY = rd16(0x0830);
    std::printf("delta Mouse=(%d,%d)\n", endX - startX, endY - startY);
    bool moved = (endX != startX) || (endY != startY);
    std::printf("%s\n", moved ? "PASS: mouse moved" : "FAIL: mouse frozen");
    return moved ? 0 : 1;
}
