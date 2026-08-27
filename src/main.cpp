// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "GuiMachineRuntime.h"
#include "MachineFactory.h"
#include "MachineSession.h"
#include "ProcessEnvironment.h"

#include <cstdio>
#include <utility>

int main(int argc, char** argv) {
#ifndef POM68K_VERSION_STRING
#define POM68K_VERSION_STRING "dev"
#endif
    auto startup = pom68k::app::captureRuntimeEnvironment();
    pom68k::app::RuntimeConfig config =
        pom68k::app::RuntimeConfig::parse(argc, argv, startup);
    if (config.showVersion()) {
        std::printf("POM68K %s — Macintosh 68k emulator "
                    "(37 profiles, Mac Plus to Quadra 950)\n",
                    POM68K_VERSION_STRING);
        return 0;
    }

    std::printf("POM68K — Macintosh 68k emulator\n");
    pom68k::app::MachineSession session =
        pom68k::app::MachineFactory::create(
            std::move(config), pom68k::gui::makeGuiMachineRuntime());
    return session.run();
}
