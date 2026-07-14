// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Dev tool: disassemble a ROM range through Moira's disassembler.
//   dasm <rom> <hexaddr> [count]

#include "Cpu68k.h"
#include "MacMemory.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: dasm <rom> <hexaddr> [count]\n"); return 2; }
    std::ifstream in(argv[1], std::ios::binary);
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
    MacMemory mem;
    if (rom.empty() || !mem.loadRom(rom)) { std::fprintf(stderr, "bad ROM\n"); return 2; }

    Cpu68k cpu(mem);
    uint32_t addr = uint32_t(std::strtoul(argv[2], nullptr, 16));
    int count = (argc > 3) ? std::atoi(argv[3]) : 16;
    char line[128];
    for (int i = 0; i < count; i++) {
        int bytes = cpu.disassemble(line, addr);
        std::printf("$%06X  %s\n", addr, line);
        addr += uint32_t(bytes);
    }
    return 0;
}
