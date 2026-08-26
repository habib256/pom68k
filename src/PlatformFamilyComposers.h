// POM68K — internal boundary between platform-family composers and dispatch
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#pragma once

#include "MachineCatalog.h"

#include <cstdint>
#include <string>
#include <vector>

namespace pom68k::gui {

class GuiHostServices;

struct PlatformLaunch {
    std::vector<std::uint8_t> rom;
    std::string romName;
    std::vector<std::string> media;
    PlatformKind platform;
    SnapMachine selected;
};

int composeCompact(PlatformLaunch launch, GuiHostServices& services);
int composeToby(PlatformLaunch launch, GuiHostServices& services);
int composeV8(PlatformLaunch launch, GuiHostServices& services);
int composeSonora(PlatformLaunch launch, GuiHostServices& services);
int composeDafb(PlatformLaunch launch, GuiHostServices& services);
int composeDuo(PlatformLaunch launch, GuiHostServices& services);

} // namespace pom68k::gui
