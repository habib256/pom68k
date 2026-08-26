// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Fixed-cycle Macintosh Plus / 68000 stopwatch. Unlike a boot etalon it runs
// the same number of cycle-exact frame quanta on every engine and prints the
// shared architectural fingerprint plus the cross-host real-time ratio.

#include "BenchHarness.h"
#include "Cpu68k.h"
#include "JitTestConfig.h"
#include "MacFrame.h"
#include "MacMemory.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

void ensureBootDriverType(std::vector<uint8_t>& img) {
    if (img.size() < 512 || img[0] != 'E' || img[1] != 'R') return;
    int count = (img[0x10] << 8) | img[0x11];
    for (int i = 0; i < count && 0x12 + i * 8 + 8 <= 512; i++) {
        const int e = 0x12 + i * 8;
        if (((img[e + 6] << 8) | img[e + 7]) == 0x6A) return;
    }
    if (count < 1 || 0x12 + count * 8 + 8 > 512) return;
    const int dst = 0x12 + count * 8;
    for (int k = 0; k < 8; k++) img[dst + k] = img[0x12 + k];
    img[dst + 6] = 0; img[dst + 7] = 0x6A;
    img[0x10] = uint8_t((count + 1) >> 8);
    img[0x11] = uint8_t(count + 1);
}

template <class Report>
bench::Result runPlus(const std::vector<uint8_t>& rom,
                      const std::string& disk, int frames, int engine,
                      Report&& report) {
    MacMemory mem(pom68k::defaultCoreConfig());
    mem.loadRom(rom);
    const jit::ResolvedConfig jitConfig = testjit::resolveFromEnvironment();
    Cpu68k cpu(mem, jitConfig);
    mem.setCpu(&cpu);
    cpu.setEngine(engine);
    cpu.hardReset();
    mem.attachScsi(disk);
    ensureBootDriverType(mem.scsiDisk().image());
    MacFrameClock frameClock;
    frameClock.resync(cpu);
    const bench::Result r = bench::runWith(
        cpu, frames, kCyclesPerFrame,
        [&] { frameClock.runFrame(cpu, mem); });
    report(mem, cpu, r);
    return r;
}

}  // namespace

int main() {
    const std::string romPath = std::getenv("POM68K_BENCH_ROM")
        ? std::getenv("POM68K_BENCH_ROM")
        : bench::findAsset({"roms/macplus.rom", "roms/mac128k/macplus.rom"});
    std::string diskPath = std::getenv("POM68K_BENCH_DISK")
        ? std::getenv("POM68K_BENCH_DISK")
        : bench::findAsset({"hdv/System 7.1 HD.dsk", "hdv/HD20SC.vhd",
                            "hdv/boot.vhd"});
    if (romPath.empty() || diskPath.empty()) {
        std::printf("SKIP: needs the Mac Plus ROM + a bootable hdv/ image\n");
        return 0;
    }
    std::ifstream in(romPath, std::ios::binary);
    const std::vector<uint8_t> rom((std::istreambuf_iterator<char>(in)), {});
    const int frames = bench::frames(6000);

    if (bench::compareRepeats() > 0) {
        std::printf("plus %s, %d repeats x 2 arms ABBA, %d frames per run, "
                    "built %s\n",
                    bench::nullExperiment() ? "NULL experiment (A vs A)"
                                            : "interleaved A/B",
                    bench::compareRepeats(), frames, bench::buildStamp());
        return bench::compare("plus", "interp", "threaded", [&](int arm) {
            const int engine = bench::nullExperiment() ? 0 : arm;
            const bench::Result r = runPlus(
                rom, diskPath, frames, engine,
                [](auto&, auto&, const auto&) {});
            return bench::Sample{r.secs, r.fp};
        });
    }

    const int engine = std::getenv("POM68K_CPU_ENGINE") &&
                       !std::strcmp(std::getenv("POM68K_CPU_ENGINE"), "jit");
    runPlus(rom, diskPath, frames, engine,
            [&](MacMemory& mem, Cpu68k& cpu, const bench::Result& r) {
        bench::report("plus", engine ? "plus_threaded" : "plus_interp",
                      "68000", cpu, r, mem.cpuHz());
        std::printf("  SCSI=%ld  pc=$%08X  %s\n", mem.scsi().commands,
                    cpu.getPC(), cpu.isHalted() ? "HALTED" : "running");
    });
    return 0;
}
