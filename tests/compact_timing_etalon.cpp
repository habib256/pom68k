// POM68K — gate `compact_timing_etalon`: the compacts' bus timing as their
// own ROM measures it. SetUpTimeK (mac-rom OS/StartMgr/StartInit.a) loads
// VIA T2 with one millisecond of E ticks and counts DBRA iterations — bare,
// then with an SCC read in the loop — until T2 interrupts, and stores the
// counts at TimeDBRA ($0D00) and TimeSCCDB ($0D02). Every later driver
// delay is scaled by them. Two mechanisms decide the counts: the 6522's
// T2 write → IRQ latency (N+3 ticks, Via6522.cpp T2CH) and the /VPA cycle
// that lands each VIA access on an E-clock edge (Cpu68k::vpaTarget).
//
// Oracle: MAME 0.287 `macse`, `macsefd`, `macclasc` on the same ROM images
// (romsets built from them, Lua read of the two words, no disk) — $0312 and
// $0165 on all three. Before 2026-09-26 POM68K stored $030F / $0164.
// Runs without a disk: the calibration precedes the boot-device search.
// Soft-skips per model whose ROM is absent.

#include "AssetFingerprint.h"
#include "Cpu68k.h"
#include "JitTestConfig.h"
#include "MacFrame.h"
#include "MacMemory.h"

#include <cstdio>
#include <fstream>
#include <vector>

int main() {
    struct Case { const char* name; MacMemory::Model model; const char* rom; };
    const Case cases[] = {
        { "SE", MacMemory::Model::SE, "roms/256KB ROMs/1987-03 - B2E362A8 - Mac SE.ROM" },
        { "SE FDHD", MacMemory::Model::SEFDHD,
          "roms/256KB ROMs/1989-08 - B306E171 - Mac SE FDHD.ROM" },
        { "Classic", MacMemory::Model::Classic,
          "roms/512KB ROMs/1990-10 - A49F9914 - Mac Classic.rom" },
    };
    constexpr unsigned kTimeDbra = 0x0312, kTimeSccDb = 0x0165;   // MAME
    int failures = 0, ran = 0;
    for (const Case& c : cases) {
        const std::string rom = testasset::find(c.rom);
        if (rom.empty()) { std::printf("SKIP %s: needs %s\n", c.name, c.rom); continue; }
        testasset::report({ rom });
        std::ifstream in(rom, std::ios::binary);
        std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)), {});
        MacMemory mem(pom68k::defaultCoreConfig(), c.model);
        if (!mem.loadRom(romData)) { std::printf("FAIL %s: bad ROM\n", c.name); return 1; }
        Cpu68k cpu(mem, testjit::resolveFromEnvironment());
        mem.setCpu(&cpu);
        cpu.hardReset();
        MacFrameClock fc;
        fc.resync(cpu);
        for (int f = 0; f < 300; f++) fc.runFrame(cpu, mem);   // calibrated by ~240
        auto w16 = [&](uint32_t a) { return unsigned(mem.peek8(a) << 8 | mem.peek8(a + 1)); };
        const unsigned dbra = w16(0x0D00), scc = w16(0x0D02);
        const bool ok = dbra == kTimeDbra && scc == kTimeSccDb;
        std::printf("%s %s: TimeDBRA=$%04X TimeSCCDB=$%04X (MAME $%04X $%04X)\n",
                    ok ? "ok  " : "FAIL", c.name, dbra, scc, kTimeDbra, kTimeSccDb);
        failures += !ok;
        ++ran;
    }
    if (failures) { std::printf("compact_timing_etalon: %d model(s) off\n", failures); return 1; }
    std::printf("compact_timing_etalon: %d model(s) match MAME\n", ran);
    return 0;
}
