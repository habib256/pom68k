// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#pragma once

namespace pom68k::app {
class MachineSession;
}

namespace pom68k::gui {

class GuiHostServices;

class PlatformComposers {
public:
    static int run(app::MachineSession& session, GuiHostServices& services);
};

} // namespace pom68k::gui
