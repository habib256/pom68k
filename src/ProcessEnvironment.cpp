// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "ProcessEnvironment.h"
#include "StartupOptions.h"

#include <cstdlib>
#include <utility>
#include <vector>

namespace pom68k::app {

StartupSnapshot captureRuntimeEnvironment() {
    std::vector<StartupSnapshot::Entry> entries;
    for (const StartupOptionSpec option : startup_option::kAll)
        if (const char* value = std::getenv(option.name.data()))
            entries.emplace_back(option.name, value);
    return StartupSnapshot(std::move(entries));
}

} // namespace pom68k::app
