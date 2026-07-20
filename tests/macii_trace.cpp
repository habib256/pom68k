// POM68K — dev tool: trace Mac II ROM POST (EXCLUDE_FROM_ALL).

#include "MacIIMemory.h"
#include "Cpu020.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

struct TraceCpu : Cpu020 {
    using Cpu020::Cpu020;
    long intHist[8] = {};
    long irqLog = 0;
    long a06eLog = 0;
    MacIIMemory& memRef;
    TraceCpu(MacIIMemory& m, bool fpu) : Cpu020(m, fpu), memRef(m) {}
    MacIIMemory& mem() { return memRef; }

    void willInterrupt(moira::u8 level) override {
        if (level < 8) intHist[level]++;
        if (irqLog++ < 20) {
            std::printf("[%10lld] IRQ lvl=%d PC0=$%08X VIA1=%02X/%02X VIA2=%02X/%02X\n",
                        (long long)getClock(), level, getPC0(),
                        mem().via1().ifrRaw(), mem().via1().ierRaw(),
                        mem().via2().ifrRaw(), mem().via2().ierRaw());
        }
    }
    void willExecute(moira::M68kException, moira::u16 vector) override {
        if (vector != 10) return;
        uint16_t op = uint16_t(mem().peek8(getPC0()) << 8 | mem().peek8(getPC0() + 1));
        if (op == 0xA06E && a06eLog++ < 15)
            std::printf("[%10lld] _SlotManager sel=$%02X PC0=$%08X\n",
                        (long long)getClock(), (unsigned)getD(0) & 0xFF, getPC0());
    }
};

int main(int argc, char** argv) {
    std::string romPath = "roms/256KB ROMs/1987-12 - 9779D2C4 - MacII (800k v2).ROM";
    long long cycles = 120'000'000;
    long long waitExtra = 2'000'000;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--cycles" && i + 1 < argc)
            cycles = std::atoll(argv[++i]);
        else if (std::string(argv[i]) == "--wait-extra" && i + 1 < argc)
            waitExtra = std::atoll(argv[++i]);
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
    TraceCpu cpu(mem, true);
    mem.setCpu(&cpu);
    cpu.hardReset();

    std::map<uint32_t, long> hits;
    bool inWait = false;
    long long waitEntryClk = 0;
    long hits6E16 = 0, hits6EDA = 0;
    int declPcLog = 0;

    while (cpu.getClock() < cycles && !cpu.isHalted()) {
        if (inWait && cpu.getClock() >= waitEntryClk + waitExtra)
            break;
        uint32_t pc = cpu.getPC();
        hits[pc]++;
        if (pc == 0x40006E16) hits6E16++;
        if (pc == 0x40006EDA) hits6EDA++;

        if ((pc & 0xFF000000u) == 0xF9000000u && declPcLog < 20) {
            std::printf("DeclROM exec PC=$%08X\n", pc);
            declPcLog++;
        }

        if ((pc & 0xFFFF) == 0x6DD8 && !inWait) {
            inWait = true;
            waitEntryClk = cpu.getClock();
            auto p32 = [&](uint32_t a) {
                return (uint32_t(mem.peek8(a)) << 24) |
                       (uint32_t(mem.peek8(a + 1)) << 16) |
                       (uint32_t(mem.peek8(a + 2)) << 8) |
                       uint32_t(mem.peek8(a + 3));
            };
            std::printf("ENTER wait@6DD8 t=%lld soft=$%02X VIA1=%02X/%02X "
                        "Toby vblDis=%d enWr=%ld vramWr=%ld\n",
                        (long long)cpu.getClock(),
                        mem.peek8(cpu.getA(3) + 0x15D),
                        mem.via1().ifrRaw(), mem.via1().ierRaw(),
                        mem.toby() && mem.toby()->vblDisabled() ? 1 : 0,
                        mem.toby() ? mem.toby()->vblEnableWrites : 0,
                        mem.toby() ? mem.toby()->vramWrites : 0);
            std::printf("  A3=$%08X +134=$%08X $162=$%08X slotIrq=$%02X\n",
                        (unsigned)cpu.getA(3), p32(cpu.getA(3) + 0x134),
                        p32(0x162), mem.nubusIrqState());
        }

        cpu.runCycles(inWait ? 1 : 500);
    }

    uint32_t a3 = (uint32_t(mem.peek8(0xCF8)) << 24) |
                  (uint32_t(mem.peek8(0xCF9)) << 16) |
                  (uint32_t(mem.peek8(0xCFA)) << 8) |
                  uint32_t(mem.peek8(0xCFB));
    uint8_t soft = a3 ? mem.peek8(a3 + 0x15D) : 0;

    std::printf("done t=%lld PC=$%08X soft=$%02X hits6E16=%ld hits6EDA=%ld "
                "vblPulses=%ld Toby enWr=%ld vramWr=%ld declExec=%d\n",
                (long long)cpu.getClock(), cpu.getPC(), soft,
                hits6E16, hits6EDA, mem.vblPulses(),
                mem.toby() ? mem.toby()->vblEnableWrites : 0,
                mem.toby() ? mem.toby()->vramWrites : 0, declPcLog);
    std::printf("IRQ:");
    for (int i = 0; i < 8; i++)
        if (cpu.intHist[i]) std::printf(" L%d=%ld", i, cpu.intHist[i]);
    std::printf("\n");

    std::vector<std::pair<long, uint32_t>> top;
    for (auto& kv : hits) top.push_back({ kv.second, kv.first });
    std::sort(top.begin(), top.end(), std::greater<>());
    for (int i = 0; i < 10 && i < int(top.size()); i++)
        std::printf("  $%08X  %ld\n", top[i].second, top[i].first);
    return 0;
}
