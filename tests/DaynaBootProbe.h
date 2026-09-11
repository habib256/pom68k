// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── A DaynaPort on the bus of any boot etalon ──
// POM68K_TEST_DAYNAPORT=<id> (0-6) makes a boot etalon build its machine
// with a DaynaPort SCSI/Link at that ID and, on top of its own Finder
// verdict, require that the guest's SCSI traffic reached the card — the
// ROM's bus probe at the least. The <family>_dayna_boot_etalon variants set
// it (cmake/Pom68kMachineGates.cmake). Unset, the etalon is exactly the gate
// it was: `config()` is then `defaultCoreConfig()`, field for field.

#pragma once
#include "CoreConfig.h"

#include <cstdio>
#include <cstdlib>
#include <optional>

namespace daynaboot {

inline std::optional<int> id() {
    const char* v = std::getenv("POM68K_TEST_DAYNAPORT");
    if (!v || !*v) return std::nullopt;
    const int n = std::atoi(v);
    if (n < 0 || n > 6) return std::nullopt;
    return n;
}

// Drop-in for defaultCoreConfig(): a reference to a process-lifetime value,
// because the machines keep the configuration they are built with.
inline const pom68k::CoreConfig& config() {
    static const pom68k::CoreConfig c = [] {
        pom68k::CoreConfig k = pom68k::defaultCoreConfig();
        k.bus.daynaPortId = id();
        return k;
    }();
    return c;
}

// The verdict on top of the etalon's own.
template <class M>
bool check(M& mem, bool ok) {
    if (!id()) return ok;
    const long n = mem.daynaPort().commands;
    std::printf("DaynaPort at SCSI ID %d: %ld commands from the guest\n", *id(), n);
    if (n > 0) return ok;
    std::fprintf(stderr, "FAIL: the DaynaPort on the bus never saw a command\n");
    return false;
}

} // namespace daynaboot
