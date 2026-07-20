// Dev probe: InitResources / BlockMove master-pointer state on Mac II.
#include "MacIIMemory.h"
#include "Cpu020.h"
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

static std::string find(const char* rel) {
    for (const std::string base : {"", "../"}) {
        std::string p = base + rel;
        if (std::ifstream(p, std::ios::binary)) return p;
    }
    return {};
}

static uint32_t r32(MacIIMemory& m, uint32_t a) {
    return (uint32_t(m.peek8(a)) << 24) | (uint32_t(m.peek8(a + 1)) << 16) |
           (uint32_t(m.peek8(a + 2)) << 8) | uint32_t(m.peek8(a + 3));
}

int main() {
    std::string rom = find("roms/256KB ROMs/1987-12 - 9779D2C4 - MacII (800k v2).ROM");
    std::string img = find("hdv/GISTPERSO-boot.vhd");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP\n");
        return 0;
    }
    std::ifstream rin(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(rin)), {});
    MacIIMemory mem;
    mem.loadRom(romData);
    mem.installTobyVideo();
    Cpu020 cpu(mem, true);
    mem.setCpu(&cpu);
    cpu.hardReset();
    mem.attachScsi(img);

    const int64_t limit = 80'000'000;
    while (cpu.getClock() < limit && !cpu.isHalted()) {
        uint32_t pc = cpu.getPC();
        if (pc == 0x40812388) {
            std::printf("after A11E A0=%08X D0=%08X $31A=%08X $118=%08X\n",
                        cpu.getA(0), cpu.getD(0), r32(mem, 0x31A), r32(mem, 0x118));
        }
        if (pc == 0x4081238E) {
            uint32_t h = cpu.getA(0);
            std::printf("after A122 A0=%08X (A0)=%08X D0=%08X $31A=%08X\n",
                        h, r32(mem, h), cpu.getD(0), r32(mem, 0x31A));
        }
        if (pc == 0x408123B0) {
            uint32_t h = r32(mem, 0xB06);
            std::printf("before A024 A0=%08X $B06=%08X (*)=%08X $31A=%08X\n",
                        cpu.getA(0), h, r32(mem, h), r32(mem, 0x31A));
        }
        if (pc == 0x408123B4) {
            uint32_t a4 = cpu.getA(4);
            std::printf("after A024 A0=%08X A4=%08X (A4)=%08X D2=%08X\n",
                        cpu.getA(0), a4, r32(mem, a4), cpu.getD(2));
        }
        if (pc == 0x408123B8) {
            uint32_t mask = r32(mem, 0x31A);
            std::printf("BlockMove A0=%08X A1=%08X D0=%08X mask=%08X -> dst=%08X\n",
                        cpu.getA(0), cpu.getA(1), cpu.getD(0), mask,
                        cpu.getA(1) & mask);
            std::printf("VIA2 PB=%02X PB3=%d (0=HMMU 24-bit)\n",
                        mem.via2().portB(), (mem.via2().portB() >> 3) & 1);
        }
        if (pc == 0x408123BA) {
            std::printf("after BM $215C=%08X A1=%08X [A1]=%08X $A50=%08X\n",
                        r32(mem, 0x215C), cpu.getA(1), r32(mem, cpu.getA(1)),
                        r32(mem, 0xA50));
            std::printf("$2A6 TheZone=%08X $2A2 SysZone=%08X $B06=%08X\n",
                        r32(mem, 0x2A6), r32(mem, 0x2A2), r32(mem, 0xB06));
            break;
        }
        cpu.execute();
    }
    return 0;
}
