// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Egret firmware-LLE gate: the real 341S0850 (Egret 1.01, LC/LC II)
// behind the V8's VIA — power-on hold, the FALLING-edge PC3 release
// (egret.cpp pc_w — opposite of the Cuda), staged-PRAM install into the
// E1's internal RAM, XCVR_SESSION idle on VIA1 PB3. Soft-skips without
// the dump.

#include "V8Memory.h"
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
    std::printf("egret_lle_test — real Egret firmware behind the V8 VIA\n");
    setenv("POM68K_EGRET_LLE", "1", 1);

    V8Memory mem(4u << 20);
    if (!mem.egretLleActive()) {
        std::printf("SKIP: needs roms/egret/341s0850.bin\n");
        return 0;
    }
    mem.reset();
    check(mem.cpuHeld(), "power-on: firmware holds the 68030 in reset");

    long spent = 0;
    while (mem.cpuHeld() && spent < 300000000) {
        mem.tick(10000);
        spent += 10000;
    }
    check(!mem.cpuHeld(), "firmware releases the host on the PC3 falling edge");
    std::printf("  [released after %.1f ms machine time, %ld MCU instructions]\n",
                spent / 15667.2, mem.egretLle().mcu().instructions);

    // Factory battery: SPConfig XPRAM $13 = $22 must land in MCU RAM.
    check(mem.egretLle().pram(0x13) == 0x22,
          "staged factory PRAM installed (SPConfig $13 = $22)");
    check(mem.egretLle().mcu().ramByte(0x100 + 0x13) == 0x22,
          "PRAM lives in the E1's internal RAM ($0100-$01FF)");

    long i0 = mem.egretLle().mcu().instructions;
    mem.tick(1566720);                              // 100 ms
    check(mem.egretLle().mcu().instructions > i0, "MCU keeps executing");
    check(!mem.egretLle().mcu().illegal(), "no undefined opcode throughout");

    if (gFails) { std::printf("FAILED (%d)\n", gFails); return 1; }
    std::printf("PASS\n");
    return 0;
}
