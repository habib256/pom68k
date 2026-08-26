// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Q605 event-scheduler gate: elapsed SCC/53C96 time is carried explicitly,
// and an observable access must consume the matching debt before touching
// the device.

#include "Q605Memory.h"

#include <cstdio>

int main() {
    pom68k::CoreConfig core;
    core.bus.q605SccEventDriven = true;
    core.bus.q605ScsiEventDriven = true;
    int failures = 0;
    const auto check = [&](bool ok, const char* what) {
        std::printf("  %-62s %s\n", what, ok ? "ok" : "FAIL");
        if (!ok) failures++;
    };

    Q605Memory mem(core, 1u << 20);
    mem.reset();
    check(mem.deferredSccCycles() == 0, "reset clears serialized SCC debt");
    check(mem.deferredScsiCycles() == 0, "reset clears serialized SCSI debt");

    mem.tick(37);
    check(mem.deferredSccCycles() == 37,
          "tick carries time while the SCC has no due event");
    check(mem.deferredScsiCycles() == 37,
          "tick carries time while the 53C96 has no due event");

    (void)mem.scc().wr(0, 0); // external wire/debug access at current time
    check(mem.deferredSccCycles() == 0,
          "external SCC access consumes all deferred time");
    check(mem.deferredScsiCycles() == 37,
          "SCC access does not consume the 53C96 debt");

    (void)mem.scsi().irq();
    check(mem.deferredScsiCycles() == 0,
          "external 53C96 access consumes all deferred time");

    mem.tick(19);
    check(mem.deferredSccCycles() == 19,
          "scheduler starts a new debt interval after an access");
    check(mem.deferredScsiCycles() == 19,
          "53C96 scheduler starts a new debt interval after an access");

    // Direct TurboSCSI MMIO is a separate observation path from the public
    // device accessor. It must own its flush rather than relying on callers.
    (void)mem.read8(0x50010040); // 53C96 destination-ID register
    check(mem.deferredScsiCycles() == 0,
          "53C96 register MMIO consumes all deferred time");
    check(mem.deferredSccCycles() == 19,
          "53C96 MMIO does not consume the SCC debt");


    std::printf("q605_event_scheduler_test: %s\n",
                failures ? "FAIL" : "OK");
    return failures ? 1 : 0;
}
