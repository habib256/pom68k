// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Dev tool: disassemble a ROM range through Moira's disassembler.
// A 128 KB image is the Plus ROM (68000 syntax at $400000); a 512 KB
// image is the LC II ROM (68030 syntax at $A00000).
//   dasm <rom> <hexaddr> [count]

#include "Cpu68k.h"
#include "MacMemory.h"
#include "Cpu030.h"
#include "V8Memory.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: dasm <rom> <hexaddr> [count]\n"); return 2; }
    std::ifstream in(argv[1], std::ios::binary);
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
    uint32_t addr = uint32_t(std::strtoul(argv[2], nullptr, 16));
    int count = (argc > 3) ? std::atoi(argv[3]) : 16;
    char line[128];

    if (rom.size() == V8Memory::kRomSize) {
        V8Memory mem;
        if (!mem.loadRom(rom)) { std::fprintf(stderr, "bad ROM\n"); return 2; }
        Cpu030 cpu(mem);
        for (int i = 0; i < count; i++) {
            int bytes = cpu.disassemble(line, addr);
            std::printf("$%06X  %s\n", addr, line);
            addr += uint32_t(bytes);
        }
        return 0;
    }

    MacMemory mem;
    if (rom.empty() || !mem.loadRom(rom)) { std::fprintf(stderr, "bad ROM\n"); return 2; }
    Cpu68k cpu(mem);
    for (int i = 0; i < count; i++) {
        int bytes = cpu.disassemble(line, addr);
        std::printf("$%06X  %s\n", addr, line);
        addr += uint32_t(bytes);
    }
    return 0;
}
