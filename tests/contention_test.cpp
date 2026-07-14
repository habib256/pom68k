// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// M4 gate: the RAM/video contention model. Simulates a CPU issuing
// back-to-back 4-cycle RAM accesses for one full frame and checks the
// stolen-cycle budget against the hardware numbers (DEV.md § Timing):
// 128 cycles stolen per visible line (342) + 4 per line for the sound
// fetch (370) = 45 256 of 130 240, i.e. the GttMFH 2.56 MB/s average
// RAM bandwidth (Table 5-3).

#include "Cpu68k.h"
#include <cstdio>

int main() {
    constexpr long kFrame = 130240;
    long accesses = 0, waited = 0;
    moira::i64 t = 0;
    while (t < kFrame) {
        int d = Cpu68k::contentionDelay(t);
        waited += d;
        t += d + 4;                    // one word access = 4 CPU cycles
        accesses++;
    }
    long stolen = 342L * 128 + 370L * 4;               // 45 256
    long expected = (kFrame - stolen) / 4;             // 21 246 accesses
    double mbps = double(accesses) * 2 * 60.1474 / 1e6; // words → bytes/s
    std::printf("accesses/frame=%ld (expected %ld), waited=%ld (stolen budget %ld)\n",
                accesses, expected, waited, stolen);
    std::printf("average RAM bandwidth: %.2f MB/s (GttMFH target 2.56)\n", mbps);
    if (accesses < expected - 2 || accesses > expected + 2) {
        std::fprintf(stderr, "FAIL: contention model off budget\n");
        return 1;
    }
    // ROM/IO accesses are never delayed — spot-check the vblank region.
    if (Cpu68k::contentionDelay(342L * 352 + 100) != 0) {
        std::fprintf(stderr, "FAIL: contention applied during vblank\n");
        return 1;
    }
    std::printf("contention_test: all checks passed\n");
    return 0;
}
