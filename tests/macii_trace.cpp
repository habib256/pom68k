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
    int samples = 0;
    uint16_t lastD5 = 0xFFFF;
    int waitSamples = 0;
    while (cpu.getClock() < cycles && !cpu.isHalted()) {
        uint32_t pc = cpu.getPC();
        hits[pc]++;
        if ((pc & 0xFFFF) == 0x6DD8 && waitSamples < 8) {
            uint32_t a3 = cpu.getA(3);
            uint8_t st = mem.peek8(a3 + 0x15D);
            std::printf("wait@6DD8 t=%lld [+15D]=$%02X IPLpin=%d VIA1=%02X/%02X "
                        "vblPulses=%ld tickCalls=%ld\n",
                        (long long)cpu.getClock(), st, (int)cpu.getIPL(),
                        mem.via1().read(Via6522::IFR), mem.via1().read(Via6522::IER),
                        mem.vblPulses(), mem.tickCalls());
            waitSamples++;
        }
        if (pc == 0x40002EE0 || pc == 0x40003296) {
            uint16_t d5 = uint16_t(cpu.getD(5));
            if (d5 != lastD5 && samples < 40) {
                std::printf("t=%lld PC=$%08X D5=$%04X D7=$%08X A6=$%08X overlay=%d\n",
                            (long long)cpu.getClock(), pc, d5,
                            (unsigned)cpu.getD(7), (unsigned)cpu.getA(6),
                            mem.overlay());
                lastD5 = d5;
                samples++;
            }
        }
        cpu.runCycles(500);
    }

    std::printf("Mac II trace — %lld cycles, halted=%d PC=$%08X\n",
                (long long)cpu.getClock(), cpu.isHalted(), cpu.getPC());
    long h607C = 0;
    for (auto& kv : hits) {
        if ((kv.first & 0xFFFF) == 0x607C) h607C += kv.second;
    }
    std::printf("VIA1 IFR=$%02X IER=$%02X  VIA2 IFR=$%02X IER=$%02X  "
                "hits607C=%ld (any mirror) hits6E16=%ld hits6EDA=%ld vblPulses=%ld tickCalls=%ld\n",
                mem.via1().read(Via6522::IFR), mem.via1().read(Via6522::IER),
                mem.via2().read(Via6522::IFR), mem.via2().read(Via6522::IER),
                h607C, hits[0x40006E16], hits[0x40006EDA],
                mem.vblPulses(), mem.tickCalls());
    std::vector<std::pair<long, uint32_t>> top;
    for (auto& kv : hits) top.push_back({ kv.second, kv.first });
    std::sort(top.begin(), top.end(), std::greater<>());
    for (int i = 0; i < 20 && i < int(top.size()); i++)
        std::printf("  $%08X  %ld\n", top[i].second, top[i].first);
    // Prove raiseCa1 latches IFR when idle.
    mem.via1().raiseCa1();
    std::printf("after forced raiseCa1: IFR=$%02X irq=%d iplLvl=%d ca1Cleared=%ld vblNoIrq=%ld\n",
                mem.via1().read(Via6522::IFR),
                mem.via1().irqAsserted() ? 1 : 0, mem.iplLevel(),
                mem.via1().ca1Cleared, mem.vblPulseNoIrq());
    return 0;
}
