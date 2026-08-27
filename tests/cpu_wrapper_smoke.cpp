// POM68K — the six CPU wrappers no asset-free gate ever executed.
//
// Why this exists. The coverage measurement of 2026-08-27 (TODO.md § 0·B item
// 3) produced a list rather than a percentage: 48 src/ files with no executed
// line in the `asset-none` tier, and five of the ten CPU wrappers were in it —
// `Cpu020`, `IIfxCpu`, `MscCpu`, `RbvCpu`, `SonoraCpu`, `VaspCpu` at 0.00 %,
// while `Cpu68k` read 94.87 % and `Cpu040` 72.55 %. Those five are only ever
// run by the machine etalons, which need a private ROM, so on a clone with no
// assets — every CI runner this project has — they were compiled and never
// executed once.
//
// What it proves, and what it deliberately does not. Each wrapper is built on
// its real board memory with a SYNTHETIC ROM: a reset vector pointing eight
// bytes in, and `BRA.S *` there. While the boot overlay is up, address 8 reads
// ROM+8 on every one of these boards, so the CPU fetches a real instruction,
// decodes it, and spins. The gate then asserts the machine clock advanced and
// the core did not halt. That is a SMOKE, not a conformance check: it does not
// know a Sonora from a VASP, and the etalons remain the only proof that a
// platform boots. It catches the class of defect that costs the most to find
// late — a wrapper that no longer constructs, resets, fetches or advances its
// clock — on a host with no ROM at all.
//
// The instruction is 68000-compatible on purpose: `BRA.S *` is $60FE on every
// member of the family, so the same two bytes serve the 020, the 030 boards
// and the Duo's 030 alike.

#include "Cpu020.h"
#include "IIfxCpu.h"
#include "IIfxMemory.h"
#include "MacIIMemory.h"
#include "MscCpu.h"
#include "MscMemory.h"
#include "RbvCpu.h"
#include "RbvMemory.h"
#include "SonoraCpu.h"
#include "SonoraMemory.h"
#include "VaspCpu.h"
#include "VaspMemory.h"
#include "jit/JitConfig.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%-4s %s\n", ok ? "ok" : "FAIL", what.c_str());
    if (!ok) failures++;
}

// SSP, then PC = 8, then `BRA.S *` — the smallest program that keeps a 68k
// fetching from ROM without touching a single peripheral.
std::vector<std::uint8_t> spinRom(std::uint32_t size) {
    std::vector<std::uint8_t> rom(size, 0);
    const std::uint32_t ssp = 0x00001000, pc = 0x00000008;
    for (int i = 0; i < 4; i++) {
        rom[size_t(i)]     = std::uint8_t(ssp >> (24 - 8 * i));
        rom[size_t(4 + i)] = std::uint8_t(pc  >> (24 - 8 * i));
    }
    rom[8] = 0x60; rom[9] = 0xFE;            // BRA.S *
    return rom;
}

// One wrapper: build, load the spin ROM, reset, run, and look at the clock.
template <class Mem, class Cpu, class Make>
void exercise(const char* name, Make&& make) {
    Mem mem(pom68k::defaultCoreConfig());
    const bool loaded = mem.loadRom(spinRom(Mem::kRomSize));
    check(loaded, std::string(name) + ": board accepts a synthetic ROM");
    if (!loaded) return;

    Cpu cpu = make(mem);
    mem.setCpu(&cpu);
    cpu.hardReset();

    const auto before = cpu.machineClock();
    for (int frame = 0; frame < 8 && !cpu.isHalted(); frame++)
        cpu.runCycles(20000);

    check(!cpu.isHalted(),
          std::string(name) + ": still running after 160k cycles of ROM");
    check(cpu.machineClock() > before,
          std::string(name) + ": machine clock advanced");
}

}  // namespace

int main() {
    std::printf("cpu_wrapper_smoke — the wrappers only the etalons used to run\n");

    const auto& jit = jit::defaultResolvedConfig();
    const auto& cpuCfg = pom68k::defaultCoreConfig().cpu;

    exercise<SonoraMemory, SonoraCpu>("SonoraCpu", [&](auto& m) {
        return SonoraCpu(m, jit, cpuCfg);
    });
    exercise<VaspMemory, VaspCpu>("VaspCpu", [&](auto& m) {
        return VaspCpu(m, jit, cpuCfg);
    });
    exercise<RbvMemory, RbvCpu>("RbvCpu", [&](auto& m) {
        return RbvCpu(m, jit, cpuCfg);
    });
    exercise<MacIIMemory, Cpu020>("Cpu020", [&](auto& m) {
        return Cpu020(m, jit, cpuCfg);
    });
    exercise<IIfxMemory, IIfxCpu>("IIfxCpu", [&](auto& m) {
        // The IIfx wrapper takes no CoreCpuConfig: its 030 policy is the
        // board's, not a per-CPU knob.
        return IIfxCpu(m, jit);
    });
    exercise<MscMemory, MscCpu>("MscCpu", [&](auto& m) {
        return MscCpu(m, jit, cpuCfg);
    });

    std::printf(failures ? "FAILED (%d)\n" : "PASS\n", failures);
    return failures ? 1 : 0;
}
