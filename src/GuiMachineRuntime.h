// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Cold factory for the concrete GUI runtime. Keeping the class private to its
// translation unit prevents main and the headless application layer from
// depending on any core, GLFW, ImGui or host-service implementation type.

#pragma once

#include <memory>

namespace pom68k::app {
class MachineSessionRuntime;
}

namespace pom68k::gui {
std::unique_ptr<app::MachineSessionRuntime> makeGuiMachineRuntime();
}
