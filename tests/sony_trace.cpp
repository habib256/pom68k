// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Dev tool: instruction-level trace of the ROM's Sony driver. Runs the ROM
// with a disk until the drive delivers nibbles, then logs every instruction
// (PC + disassembly) for N instructions. The microscope for M5 debugging.
//   sony_trace <rom> <disk> [--count N] [--skip-nibbles K]

#include "Cpu68k.h"
#include "MacMemory.h"
#include "MacFrame.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: sony_trace <rom> <disk> [--count N] [--skip-nibbles K]\n"); return 2; }
    long count = 3000, skipNib = 0;
    for (int i = 3; i < argc; i++) {
        if (!std::strcmp(argv[i], "--count") && i + 1 < argc) count = std::atol(argv[++i]);
        else if (!std::strcmp(argv[i], "--skip-nibbles") && i + 1 < argc) skipNib = std::atol(argv[++i]);
    }
    std::ifstream in(argv[1], std::ios::binary);
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    MacMemory mem;
    if (!mem.loadRom(rom)) { std::fprintf(stderr, "bad rom\n"); return 2; }
    Cpu68k cpu(mem);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.insertDisk(argv[2])) { std::fprintf(stderr, "bad disk\n"); return 2; }

    MacFrameClock fc;
    fc.resync(cpu);
    // run until the drive starts feeding nibbles (past RAM test + probing)
    while (mem.internalDrive().nibblesRead <= skipNib) fc.runFrame(cpu, mem);

    // Log everything except the idle wait loops, until the disk is ejected
    // or `count` interesting instructions have been printed.
    char line[128];
    long printed = 0;
    while (printed < count && mem.internalDrive().hasDisk()) {
        uint32_t pc = cpu.getPC0();
        bool idle = (pc >= 0x402418 && pc <= 0x402426) ||
                    (pc >= 0x401A70 && pc <= 0x401A7A);
        if (!idle) {
            cpu.disassemble(line, pc);
            std::printf("%06X  %-46s D0=%08X D5=%02X A0=%08X A2=%08X A4=%08X A5=%08X nib=%ld\n",
                        pc, line, cpu.getD(0), cpu.getD(5) & 0xFF,
                        cpu.getA(0), cpu.getA(2), cpu.getA(4), cpu.getA(5),
                        mem.internalDrive().nibblesRead);
            printed++;
        }
        cpu.execute();
    }
    std::printf("== end: disk=%d printed=%ld\n", mem.internalDrive().hasDisk() ? 1 : 0, printed);
    return 0;
}
