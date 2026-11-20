// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// RomSearch::current() alone in its translation unit: it is the store's only
// link to MachineFactory (pom68k_app), and a gate that builds its RomSearch
// by hand must not drag the application layer into its link.

#include "RomStore.h"

#include "MachineFactory.h"

namespace pom68k::roms {

RomSearch RomSearch::current() {
    RomSearch s;
    std::error_code ec;
    s.workingDir = std::filesystem::current_path(ec);
    const std::string exe = app::MachineFactory::executableDirectory();
    if (!exe.empty()) s.executableDir = std::filesystem::path(exe);
    return s;
}

} // namespace pom68k::roms
