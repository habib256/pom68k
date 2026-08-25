// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Fixed-cycle Macintosh II / 68020 stopwatch, completing the family matrix
// beside jit_bench_plus (68000), jit_bench_lcii (68030) and jit_bench (68040).

#include "BenchHarness.h"
#include "Cpu020.h"
#include "MacIIMemory.h"

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
bench::Result runMacII(const std::vector<uint8_t>& rom,
                       const std::string& disk, int frames, int engine,
                       Report&& report) {
    MacIIMemory mem(0x400000);
    mem.loadRom(rom);
    mem.installTobyVideo();
    Cpu020 cpu(mem, true);
    mem.setCpu(&cpu);
    cpu.setEngine(engine);
    cpu.hardReset();
    mem.attachScsi(disk);
    ensureBootDriverType(mem.scsiDisk().image());
    const int64_t frameCycles = MacIIMemory::kCpuHz / 60;
    const bench::Result r = bench::run(cpu, frames, frameCycles);
    report(mem, cpu, r);
    return r;
}

}  // namespace

int main() {
    const std::string romPath = std::getenv("POM68K_BENCH_ROM")
        ? std::getenv("POM68K_BENCH_ROM")
        : bench::findAsset({
            "roms/macii.rom",
            "roms/256KB ROMs/1987-12 - 9779D2C4 - MacII (800k v2).ROM",
            "roms/256KB ROMs/1987-03 - 97851DB6 - MacII (800k v1).ROM"});
    const std::string diskPath = std::getenv("POM68K_BENCH_DISK")
        ? std::getenv("POM68K_BENCH_DISK")
        : bench::findAsset({"hdv/System 7.1 HD.dsk", "hdv/HD20SC.vhd"});
    if (romPath.empty() || diskPath.empty()) {
        std::printf("SKIP: needs the Mac II ROM + a bootable hdv/ image\n");
        return 0;
    }
    std::ifstream in(romPath, std::ios::binary);
    const std::vector<uint8_t> rom((std::istreambuf_iterator<char>(in)), {});
    if (rom.size() != MacIIMemory::kRomSize) {
        std::fprintf(stderr, "FAIL: Mac II ROM is %zu bytes\n", rom.size());
        return 1;
    }
    const int frames = bench::frames(6000);

    if (bench::compareRepeats() > 0) {
        std::printf("macii %s, %d repeats x 2 arms ABBA, %d frames per run, "
                    "built %s\n",
                    bench::nullExperiment() ? "NULL experiment (A vs A)"
                                            : "interleaved A/B",
                    bench::compareRepeats(), frames, bench::buildStamp());
        return bench::compare("macii", "interp", "threaded", [&](int arm) {
            const int engine = bench::nullExperiment() ? 0 : arm;
            const bench::Result r = runMacII(
                rom, diskPath, frames, engine,
                [](auto&, auto&, const auto&) {});
            return bench::Sample{r.secs, r.fp};
        });
    }

    const int engine = std::getenv("POM68K_CPU_ENGINE") &&
                       !std::strcmp(std::getenv("POM68K_CPU_ENGINE"), "jit");
    runMacII(rom, diskPath, frames, engine,
             [&](MacIIMemory& mem, Cpu020& cpu, const bench::Result& r) {
        bench::report("macii", engine ? "macii_threaded" : "macii_interp",
                      "68020", cpu, r, mem.cpuHz());
        std::printf("  SCSI=%ld  pc=$%08X  %s\n", mem.scsi().commands,
                    cpu.getPC(), cpu.isHalted() ? "HALTED" : "running");
    });
    return 0;
}
