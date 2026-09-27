// POM68K — gate `calibration_040_etalon`: the 68040 boards' I/O timing as
// their own ROM measures it. SetUpTimeK (mac-rom OS/StartMgr/StartInit.a)
// counts, per millisecond of VIA T2, a DBRA loop with an SCC read in it
// (TimeSCCDB, $0D02 — "used by AppleTalk") and one with a VIA read in it
// (TimeVIADB, $0CEA). Both loops are bound by the I/O glue, which on these
// boards synchronizes an SCC access to the VIA clock as it does a VIA
// access (MAME macquadra605.cpp:90-97, macquadra800.cpp, macquadra700.cpp
// quadrax00::via_sync, f108.cpp:196-207).
//
// Oracle: MAME 0.287 `macqd605`, `macqd630` (and `macqd700`, `macqd800`)
// on the same ROM images (Lua read, no disk): TimeSCCDB = TimeVIADB =
// $030E on all four. POM68K reads $030F — the one-tick residual TimeVIADB
// carried before the SCC change too — so the gate allows ±1. Before
// 2026-09-27 the Quadra 605's TimeSCCDB was $1BDC: an SCC read cost nothing.
// Only the two Cuda boards are here: on the Centris and the Quadra 700,
// whose ADB is a PIC1654S, the same sync killed mouse delivery, so it is
// withheld there until that is understood (TODO § Fidélité).
// TimeDBRA and TimeSCSIDB are left out: they follow the 040 cycle model,
// which MAME approximates differently, not the glue.
// Soft-skips per board whose ROM is absent. No disk.

#include "AssetFingerprint.h"
#include "Cpu040.h"
#include "JitTestConfig.h"
#include "Q605Memory.h"
#include "Q630Cpu.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace {

constexpr unsigned kMame = 0x030E;
int gFailures = 0, gRan = 0;

std::vector<uint8_t> loadRom(const char* rel, std::string& path) {
    path = testasset::find(rel);
    if (path.empty()) return {};
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), {});
}

template <class Mem, class Cpu>
void measure(const char* name, Mem& mem, Cpu& cpu, int64_t cpuHz) {
    mem.setCpu(&cpu);
    cpu.hardReset();
    while (mem.cpuHeld()) mem.tick(1000);
    for (int f = 0; f < 480 && !cpu.isHalted(); f++) cpu.runCycles(int(cpuHz / 60));
    auto w16 = [&](uint32_t a) { return unsigned(mem.peek8(a) << 8 | mem.peek8(a + 1)); };
    const unsigned scc = w16(0x0D02), via = w16(0x0CEA);
    auto near = [](unsigned v) { return v + 1 >= kMame && v <= kMame + 1; };
    const bool ok = near(scc) && near(via);
    std::printf("%s %s: TimeSCCDB=$%04X TimeVIADB=$%04X (MAME $%04X) TimeDBRA=$%04X\n",
                ok ? "ok  " : "FAIL", name, scc, via, kMame, w16(0x0D00));
    gFailures += !ok;
    ++gRan;
}

} // namespace

int main() {
    const auto jit = testjit::resolveFromEnvironment();
    const auto core = pom68k::defaultCoreConfig();
    std::string path;

    if (auto rom = loadRom("roms/1MB ROMs/1993-10 - FF7439EE - LC475,575,Quadra 605,Performa 475,476,575,577,578.ROM", path); !rom.empty()) {
        testasset::report({ path });
        Q605Memory mem(core, 32u << 20);
        if (!mem.loadRom(rom)) return 1;
        Cpu040 cpu(mem, jit, core.cpu, core.diagnostics);
        measure("Quadra 605", mem, cpu, Q605Memory::kCpuHz);
    } else std::printf("SKIP Quadra 605: ROM absent\n");

    if (auto rom = loadRom("roms/1MB ROMs/1994-07 - 06684214 - LC,Quadra,Performa 630.ROM", path); !rom.empty()) {
        testasset::report({ path });
        Q630Memory mem(core, 32u << 20);
        if (!mem.loadRom(rom)) return 1;
        Q630Cpu cpu(mem, jit, core.cpu);
        measure("Quadra 630", mem, cpu, Q630Memory::kCpuHz);
    } else std::printf("SKIP Quadra 630: ROM absent\n");

    if (gFailures) { std::printf("calibration_040_etalon: %d board(s) off\n", gFailures); return 1; }
    std::printf("calibration_040_etalon: %d board(s) within one tick of MAME\n", gRan);
    return 0;
}
