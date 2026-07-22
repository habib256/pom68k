// POM68K — gate `macii_mouse_etalon`: Mac II ADB mouse delivery.
// Boots to the Finder, then injects mouse motion each frame and requires the
// low-memory mouse globals (RawMouse $082C, Mouse $0830) to move. Under the
// default LLE ADB this exercises the whole chain: AdbLine wire → PIC1654S
// firmware → VIA1 shifter → ADB Manager → mouse driver → MTemp → slot VBL
// (VIA2 CA1) → jCrsrTask. Soft-skips without ROM+disk assets.

#include "MacIIMemory.h"
#include "TobyVideo.h"
#include "Cpu020.h"
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
    std::string rom = find("roms/256KB ROMs/1987-12 - 9779D2C4 - MacII (800k v2).ROM");
    if (rom.empty()) rom = find("roms/256KB ROMs/1987-03 - 97851DB6 - MacII (800k v1).ROM");
    std::string img = find("hdv/HD20SC.vhd");
    if (img.empty()) img = find("hdv/GISTPERSO-boot.vhd");
    if (img.empty()) img = find("hdv/boot.vhd");
    if (rom.empty() || img.empty()) { std::printf("SKIP: needs ROM+disk\n"); return 0; }

    std::ifstream rin(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(rin)), {});
    MacIIMemory mem;
    mem.loadRom(romData);
    mem.installTobyVideo();
    Cpu020 cpu(mem, true);
    mem.setCpu(&cpu);
    cpu.hardReset();
    mem.attachScsi(img);

    auto rd16 = [&](uint32_t a) {
        return int16_t(uint16_t(mem.peek8(a) << 8 | mem.peek8(a + 1)));
    };

    const int64_t kFrame = 800 * 525;
    // Boot to Finder.
    for (long f = 0; f < 12000 && !cpu.isHalted(); f++) cpu.runCycles(kFrame);
    std::printf("post-boot: RawMouse=(%d,%d) Mouse=(%d,%d)\n",
                rd16(0x082C), rd16(0x082E), rd16(0x0830), rd16(0x0832));

    // Inject rightward+downward motion for many frames.
    int16_t startX = rd16(0x0832), startY = rd16(0x0830);
    for (long f = 0; f < 4000 && !cpu.isHalted(); f++) {
        mem.mouseMove(3, 2);
        cpu.runCycles(kFrame);
        if (f % 500 == 0)
            std::printf("  f=%ld RawMouse=(%d,%d) Mouse=(%d,%d)\n", f,
                        rd16(0x082C), rd16(0x082E), rd16(0x0830), rd16(0x0832));
    }
    int16_t endX = rd16(0x0832), endY = rd16(0x0830);
    std::printf("delta Mouse=(%d,%d)\n", endX - startX, endY - startY);
    bool moved = (endX != startX) || (endY != startY);
    std::printf("%s\n", moved ? "PASS: mouse moved" : "FAIL: mouse frozen");
    return moved ? 0 : 1;
}
