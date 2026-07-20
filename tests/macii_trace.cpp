// POM68K — dev tool: trace Mac II ROM POST (EXCLUDE_FROM_ALL).

#include "MacIIMemory.h"
#include "Cpu020.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    std::string romPath = "roms/256KB ROMs/1987-12 - 9779D2C4 - MacII (800k v2).ROM";
    long long cycles = 500'000'000;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--cycles" && i + 1 < argc)
            cycles = std::atoll(argv[++i]);
        else if (argv[i][0] != '-') romPath = argv[i];
    }

    std::ifstream in(romPath, std::ios::binary);
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(in)), {});
    if (rom.size() != MacIIMemory::kRomSize) {
        std::fprintf(stderr, "bad ROM %zu bytes\n", rom.size());
        return 1;
    }

    MacIIMemory mem;
    mem.loadRom(rom);
    mem.installTobyVideo();
    Cpu020 cpu(mem, true);
    mem.setCpu(&cpu);
    cpu.hardReset();

    std::map<uint32_t, long> hits;
    bool dumped = false;
    while (cpu.getClock() < cycles && !cpu.isHalted()) {
        uint32_t pc = cpu.getPC();
        hits[pc]++;
        if (!dumped && (pc == 0x40005F20 || pc == 0x40005EF2 || pc == 0x40005E70)) {
            dumped = true;
            std::printf("regs @ $%08X:\n", pc);
            for (int i = 0; i < 8; i++)
                std::printf("  D%d=$%08X A%d=$%08X\n",
                            i, (unsigned)cpu.getD(i), i, (unsigned)cpu.getA(i));
            std::printf("  overlay=%d VIA1 PA=$%02X PB=$%02X\n",
                        mem.overlay(), mem.via1().portA(), mem.via1().portB());
        }
        cpu.runCycles(1000);
    }

    std::printf("Mac II trace — %lld cycles, halted=%d PC=$%08X\n",
                (long long)cpu.getClock(), cpu.isHalted(), cpu.getPC());
    std::vector<std::pair<long, uint32_t>> top;
    for (auto& kv : hits) top.push_back({ kv.second, kv.first });
    std::sort(top.begin(), top.end(), std::greater<>());
    for (int i = 0; i < 25 && i < int(top.size()); i++)
        std::printf("  $%08X  %ld\n", top[i].second, top[i].first);
    return 0;
}
