// POM68K — the "Moteur accéléré" statistics window
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Out of GuiShell.cpp since 2026-09-13, when the shell took over the menu
// bar the runners used to fill. The window renders one Stats snapshot per
// frame from the callbacks GuiShell::bindCpuMenu installed; it never
// reaches the engine itself.

#pragma once

struct GuiCpuPanelState;

namespace pom68k::gui {

// Draws the window when `state.showJit` is set. Safe every frame.
void drawEngineWindow(GuiCpuPanelState& state);

} // namespace pom68k::gui
