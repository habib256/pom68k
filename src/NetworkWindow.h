// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The "AppleTalk / Ethernet" window: hub services live, DaynaPort card staged
// for the next boot. See NetworkWindow.cpp for the two contracts.

#pragma once

#include "GuiSessionState.h"

namespace pom68k::gui {

// The window's title, shared with the Fenêtres menu.
extern const char* kNetworkWindowTitle;

// Draws the window when `state.showWindow`. Safe to call every frame.
void drawAppleTalkWindow(GuiNetworkState& state);

// A green/red bullet before a label — the status idiom this window and the
// engine window share.
void statusDot(bool ok, const char* label);

} // namespace pom68k::gui
