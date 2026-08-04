// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Cuda firmware-LLE integration gate (blueprint step 2): the real
// 341S0788 firmware inside Q605Memory behind POM68K_CUDA_LLE=1 —
// power-on holds the 68040, the firmware boots on the MCU core, releases
// the host reset by its own PC3 write (MAME cuda.cpp pc_w), installs the
// staged battery PRAM into its internal RAM at $0100-$01FF, and idles
// /TREQ high on VIA1 PB3. Soft-skips without the dump.

#include "Q605Memory.h"
#include <cstdio>
#include <cstdlib>

namespace {
int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-62s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}
} // namespace

int main() {
    std::printf("cuda_lle_test — real Cuda firmware behind the Quadra VIA\n");
    setenv("POM68K_CUDA_LLE", "1", 1);

    Q605Memory mem(1u << 20);
    if (!mem.cudaLleActive()) {
        std::printf("SKIP: needs roms/cuda/341s0788.bin\n");
        return 0;
    }
    mem.reset();
    check(mem.cpuHeld(), "power-on: firmware holds the 68040 in reset");

    long spent = 0;
    while (mem.cpuHeld() && spent < 300000000) {   // ≤ 12 s machine time
        mem.tick(10000);
        spent += 10000;
    }
    check(!mem.cpuHeld(), "firmware releases the host reset on its own");
    std::printf("  [released after %.1f ms machine time, %ld MCU instructions]\n",
                spent / 25000.0, mem.cudaLle().mcu().instructions);

    // Staged factory PRAM must now live in MCU RAM $0100-$01FF: the
    // constructor seeds XPRAM $8A with the 32-bit-clean bits (|= $05).
    check((mem.cudaLle().pram(0x8A) & 0x05) == 0x05,
          "staged PRAM installed at reset release (XPRAM $8A 32-bit bits)");
    check((mem.cudaLle().mcu().ramByte(0x100 + 0x8A) & 0x05) == 0x05,
          "PRAM lives in the E1's internal RAM ($0100-$01FF)");

    // VIA1 PB3 reads the MCU's /TREQ — idle high after boot.
    uint8_t orb = mem.read8(0x50000000);
    check(orb & 0x08, "VIA1 PB3 (/TREQ) idles high");

    // The MCU keeps running: instructions advance across further ticks.
    long i0 = mem.cudaLle().mcu().instructions;
    mem.tick(2500000);                              // 100 ms
    check(mem.cudaLle().mcu().instructions > i0, "MCU keeps executing after release");
    check(!mem.cudaLle().mcu().illegal(), "no undefined opcode throughout");

    // Deadline contract: no MCU cycle can land before the reported machine
    // cycle, and one does land exactly at it. This is what lets Cpu030/040
    // skip the old fixed peripheral batch without moving Cuda/VIA phase.
    {
        const int due = mem.cudaLle().cyclesToNextEvent();
        const int64_t c0 = mem.cudaLle().mcu().cycleCount();
        if (due > 1) mem.cudaLle().tick(due - 1);
        check(mem.cudaLle().mcu().cycleCount() == c0,
              "Cuda deadline never wakes after an unreported MCU event");
        mem.cudaLle().tick(1);
        check(mem.cudaLle().mcu().cycleCount() > c0,
              "Cuda deadline wakes on the first executable MCU cycle");
    }

    // PFW (PA0) must stay an input whatever the firmware's DDRA says — the
    // real Cuda is a customized E1 (MAME "cudapfw" tap, cuda.cpp:146-152).
    // A stock E1 drives PFW low and the boot parks in the shutdown wait.
    check((mem.cudaLle().mcu().ddr(0) & 0x01) == 0,
          "PFW (PA0) stays an input despite the firmware's DDRA write");

    if (gFails) { std::printf("FAILED (%d)\n", gFails); return 1; }
    std::printf("PASS\n");
    return 0;
}
