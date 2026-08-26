// POM68K — dev tool: trace Mac IIfx ROM POST (EXCLUDE_FROM_ALL).
// Platform #12 bring-up eyes (docs/IOP_BRINGUP.md M3): watches the two
// IOP firmware uploads (/RSTPIC edges + 65C02 liveness), the OSS state
// and the usual PC histogram.

#include "IIfxMemory.h"
#include "IIfxCpu.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    std::string romPath = "roms/512KB ROMs/1990-03 - 4147DD77 - Mac IIfx.ROM";
    long long cycles = 400'000'000;   // 10 s at 40 MHz
    std::string disk;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--cycles" && i + 1 < argc)
            cycles = std::atoll(argv[++i]);
        else if (std::string(argv[i]) == "--disk" && i + 1 < argc)
            disk = argv[++i];
        else if (argv[i][0] != '-') romPath = argv[i];
    }

    std::ifstream in(romPath, std::ios::binary);
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(in)), {});
    if (rom.size() != IIfxMemory::kRomSize) {
        std::fprintf(stderr, "bad ROM %zu bytes\n", rom.size());
        return 1;
    }

    IIfxMemory mem(pom68k::defaultCoreConfig());
    mem.loadRom(rom);
    mem.installTobyVideo();
    if (!disk.empty()) mem.attachScsi(disk);
    IIfxCpu cpu(mem, jit::defaultResolvedConfig(), true);
    mem.setCpu(&cpu);
    cpu.hardReset();

    std::map<uint32_t, long> hits;
    bool sccHeld = true, swimHeld = true;
    long long sccRelClk = -1, swimRelClk = -1;

    while (cpu.getClock() < cycles && !cpu.isHalted()) {
        hits[cpu.getPC()]++;
        if (sccHeld && !mem.sccPic().cpuHeld()) {
            sccHeld = false;
            sccRelClk = cpu.getClock();
            std::printf("[%10lld] SCC PIC released — 65C02 PC=$%04X\n",
                        sccRelClk, mem.sccPic().cpu().getProgramCounter());
        }
        if (swimHeld && !mem.swimPic().cpuHeld()) {
            swimHeld = false;
            swimRelClk = cpu.getClock();
            std::printf("[%10lld] SWIM PIC released — 65C02 PC=$%04X\n",
                        swimRelClk, mem.swimPic().cpu().getProgramCounter());
        }
        cpu.runCycles(500);
    }

    std::printf("done t=%lld PC=$%08X overlay=%d vbl=%ld tobyVram=%ld tobyEn=%ld\n",
                (long long)cpu.getClock(), cpu.getPC(),
                mem.overlay() ? 1 : 0, mem.vblPulses(),
                mem.toby() ? mem.toby()->vramWrites : 0,
                mem.toby() ? mem.toby()->vblEnableWrites : 0);
    std::printf("adbHostEdges=%ld line=%d linestate=%d\n",
                mem.adbHostEdges(), mem.adbLine().line() ? 1 : 0,
                mem.adbLine().dbgLinestate());
    std::printf("OSS pend $203=%02X $202=%02X pri:",
                mem.ossReg(0x203), mem.ossReg(0x202));
    for (int i = 0; i < 16; i++) std::printf(" %d", mem.ossReg(i));
    std::printf("\nSCC PIC: held=%d cyc=%lld pc=$%04X  SWIM PIC: held=%d cyc=%lld pc=$%04X\n",
                mem.sccPic().cpuHeld() ? 1 : 0,
                (long long)mem.sccPic().cpu().cycleCount(),
                mem.sccPic().cpu().getProgramCounter(),
                mem.swimPic().cpuHeld() ? 1 : 0,
                (long long)mem.swimPic().cpu().cycleCount(),
                mem.swimPic().cpu().getProgramCounter());
    std::printf("VIA1 IFR=%02X IER=%02X ACR=%02X\n",
                mem.via1().ifrRaw(), mem.via1().ierRaw(), mem.via1().acr());

    std::vector<std::pair<long, uint32_t>> top;
    for (auto& kv : hits) top.push_back({kv.second, kv.first});
    std::sort(top.begin(), top.end(), std::greater<>());
    std::printf("top PCs:\n");
    for (int i = 0; i < 12 && i < int(top.size()); i++)
        std::printf("  $%08X  %ld\n", top[i].second, top[i].first);

    // Firmware-upload verification (the Duo BORG pattern): dump both PIC
    // RAMs, then locate each image inside the system ROM byte-for-byte.
    for (int p = 0; p < 2; p++) {
        ApplePic& pic = p ? mem.swimPic() : mem.sccPic();
        const char* name = p ? "swimpic" : "sccpic";
        std::vector<uint8_t> picRam(0x8000);
        for (int i = 0; i < 0x8000; i++)
            picRam[i] = pic.ramByte(uint16_t(i));
        std::string path = std::string("iifx_") + name + ".ram";
        if (FILE* f = std::fopen(path.c_str(), "wb")) {
            std::fwrite(picRam.data(), 1, picRam.size(), f);
            std::fclose(f);
        }
        // Search the ROM for the longest run starting at the reset vector's
        // target — cheap heuristic: try to find 64 bytes from the 65C02
        // reset entry in the ROM image.
        const uint16_t rv = uint16_t(picRam[0x7FFC] | (picRam[0x7FFD] << 8));
        const uint8_t* code = &picRam[rv & 0x7FFF];
        long found = -1;
        for (size_t o = 0; o + 64 <= rom.size(); o++) {
            if (std::equal(code, code + 64, rom.data() + o)) { found = long(o); break; }
        }
        std::printf("%s: resetVec=$%04X 64B-at-entry %s ROM offset $%lX\n",
                    name, rv, found >= 0 ? "FOUND at" : "NOT found;",
                    found >= 0 ? found : 0L);
    }
    return 0;
}
