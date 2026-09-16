// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The shell's frame without a window system: the menu bar (Machine,
// Périphériques, CPU, Affichage, Fenêtres, the status edge), the dock
// space and every shell-owned window. Anything that needs the real window
// is left as a request in the session state — `relaunch.closeWindow` — for
// GuiShell::drawMachineMenu (GuiShell.cpp) to carry out. Gated headlessly
// by gui_machine_window_test.

#pragma once

#include "GuiSessionState.h"
#include "MachineCatalog.h"

namespace pom68k::gui {

// Once per frame, before the runner draws the screen window. In kiosk mode
// nothing is drawn: the chords are read and the frame returns.
void drawShellFrame(GuiSessionState& state, SnapMachine current);

} // namespace pom68k::gui
