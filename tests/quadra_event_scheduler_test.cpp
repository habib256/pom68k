// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "CentrisMemory.h"
#include "Q630Memory.h"
#include "Q700Memory.h"

#include <cstdio>

template <class Memory>
static int exercise(const char* name, Memory& mem) {
    int failures = 0;
    const auto check = [&](bool ok, const char* what) {
        std::printf("  %-10s %-48s %s\n", name, what, ok ? "ok" : "FAIL");
        if (!ok) failures++;
    };

    mem.reset();
    check(mem.deferredSccCycles() == 0, "reset clears serialized SCC debt");
    check(mem.deferredScsiCycles() == 0, "reset clears serialized SCSI debt");
    mem.tick(37);
    check(mem.deferredSccCycles() == 37, "idle SCC time is deferred");
    check(mem.deferredScsiCycles() == 37, "idle 53C96 time is deferred");
    (void)mem.scc().wr(0, 0);
    check(mem.deferredSccCycles() == 0, "SCC observation flushes SCC debt");
    check(mem.deferredScsiCycles() == 37, "SCC flush leaves SCSI debt intact");
    (void)mem.scsi().irq();
    check(mem.deferredScsiCycles() == 0, "SCSI observation flushes SCSI debt");
    check(mem.cyclesToNextEvent() >= 1, "scheduler always returns a future bound");
    return failures;
}

int main() {
    CentrisMemory centris(1u << 20);
    Q630Memory q630(1u << 20);
    Q700Memory q700(1u << 20);
    int failures = exercise("Centris", centris)
                 + exercise("Q630", q630)
                 + exercise("Q700", q700);
    std::printf("quadra_event_scheduler_test: %s\n", failures ? "FAIL" : "OK");
    return failures ? 1 : 0;
}
