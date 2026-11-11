// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Portable / PowerBook 100 slicing gate (PortableCpu::runCycles). The claim
// under test: what the machine does between two clock values does not
// depend on how the host cut the run in between.
//
// Two machines boot the same ROM, Power Manager firmware and disk. Both are
// run to the same absolute clock targets, one frame apart: the first in one
// call per frame, the second in random slices of 1 to 3 000 cycles. After
// every frame the complete serialized board and CPU are compared, byte for
// byte. The run starts at power-on, so it covers the stretch where the
// Power Manager holds the 68000 and then releases it — the stretch where
// 256-cycle device lumps, counted from the host call, used to make the
// result depend on the slicing (CHANGELOG 2026-11-11 (thirteenth)).
//
// Soft-skips without the ROMs, the PMU firmware or the System 7.1 volume.

#include "AssetFingerprint.h"
#include "PortableCpu.h"
#include "SaveState.h"
#include "jit/JitConfig.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {
std::vector<uint8_t> bytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(f), {} };
}

struct Machine {
    PortableMemory mem;
    std::unique_ptr<PortableCpu> cpu;
    Machine() : mem(pom68k::defaultCoreConfig()) {}
    bool start(const std::string& rom, const std::string& pmu, const std::string& disk) {
        auto firmware = pom68k::defaultCoreConfig().firmware;
        firmware.pmuPath = pmu;
        if (!mem.loadRom(bytes(rom)) || !mem.selectPmuFirmware(firmware) ||
            !mem.attachScsi(disk)) return false;
        cpu = std::make_unique<PortableCpu>(mem, jit::defaultResolvedConfig());
        mem.setCpu(cpu.get());
        cpu->hardReset();
        return true;
    }
    std::vector<sav::u8> state() {
        std::vector<sav::u8> out;
        sav::Writer ar(out);
        ar(mem);
        ar(*cpu);
        return out;
    }
};

bool runModel(const char* name, const std::string& rom, const std::string& pmu,
              const std::string& disk, long frames) {
    auto whole = std::make_unique<Machine>(), sliced = std::make_unique<Machine>();
    if (!whole->start(rom, pmu, disk) || !sliced->start(rom, pmu, disk)) {
        std::printf("  FAIL %s: machine setup\n", name);
        return false;
    }
    uint64_t rng = 0x9E3779B97F4A7C15ull;
    const int64_t frame = PortableMemory::kCpuHz / 60;
    int64_t target = whole->cpu->getClock();
    long slices = 0, heldFrames = 0;
    for (long f = 0; f < frames; f++) {
        target += frame;
        if (whole->mem.cpuHeld()) heldFrames++;
        whole->cpu->runUntil(target);
        while (sliced->cpu->getClock() < target) {
            rng = rng * 6364136223846793005ull + 1442695040888963407ull;
            const int64_t room = target - sliced->cpu->getClock();
            const int64_t n = 1 + int64_t((rng >> 33) % 3000);
            sliced->cpu->runCycles(n < room ? n : room);
            slices++;
        }
        if (whole->cpu->getClock() != sliced->cpu->getClock() ||
            whole->state() != sliced->state()) {
            std::printf("  FAIL %s: the two slicings differ after frame %ld "
                        "(clock %lld / %lld, pc %08X / %08X)\n", name, f,
                        (long long)whole->cpu->getClock(), (long long)sliced->cpu->getClock(),
                        whole->cpu->getPC0(), sliced->cpu->getPC0());
            return false;
        }
    }
    std::printf("  %-14s %ld frames identical · %ld slices against %ld calls · "
                "%ld frame(s) began with the CPU held · %ld SCSI commands\n",
                name, frames, slices, frames, heldFrames, whole->mem.scsi().commands);
    // Not vacuous: the run must cross the Power Manager's hold and reach
    // the disk, or it compared two machines that did nothing.
    if (heldFrames == 0 || whole->mem.cpuHeld() || whole->mem.scsi().commands == 0) {
        std::printf("  FAIL %s: the run did not cross the hold and reach the disk\n", name);
        return false;
    }
    return true;
}
} // namespace

int main(int argc, char** argv) {
    const long frames = argc > 1 ? std::atol(argv[1]) : 1500;
    const auto portable = testasset::find("roms/256KB ROMs/1989-02 - 96CA3846 - Mac Portable.ROM");
    const auto pb100 = testasset::find("roms/256KB ROMs/1991-10 - 96645F9C - PowerBook 100.ROM");
    const auto pmu = testasset::find("roms/archive/macroms/pmu/Macintosh-Portable__PMU.bin");
    const auto disk = testasset::find("hdv/System 7.1 HD.dsk");
    if (portable.empty() || pb100.empty() || pmu.empty() || disk.empty()) {
        std::puts("SKIP: Portable and PowerBook 100 ROMs, PMU firmware and System 7.1 required");
        return 0;
    }
    testasset::report({ portable, pb100, pmu, disk });
    std::printf("portable_slicing_etalon — one call per frame against random slices\n");
    bool ok = runModel("Portable", portable, pmu, disk, frames);
    ok = runModel("PowerBook 100", pb100, pmu, disk, frames) && ok;
    std::puts(ok ? "PASS" : "FAILED");
    return ok ? 0 : 1;
}
