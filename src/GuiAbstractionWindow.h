// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The « Abstraction Levels (LLE / HLE) » window: docs/LLE_VS_HLE.md's master
// table, live. For every subsystem it shows the class the document gives,
// and — where the session decides it — the class actually running now: a
// microcontroller on its firmware dump or on the HLE substitute, the
// interpreter or the accelerated engine, a fallback-only hack engaged or
// dormant. The data is AbstractionCatalog.*; the live answers come from the
// session's lle::Registry and the CPU binding, never from a second copy of
// the device logic. Changing a firmware choice stays the Peripherals
// window's job (PeripheralWindow.h); this one only explains.

#pragma once

#include "AbstractionCatalog.h"

#include <array>

struct GuiSessionState;

namespace pom68k::gui {

struct GuiAbstractionState {
    bool showWindow = false;
    bool allMachines = false;   // false: only what this machine carries
    std::array<char, 64> search{};
};

extern const char* kAbstractionWindowTitle;

// The shell's entry point: builds the view from the session (registry, CPU
// binding) and draws when `state.abstraction.showWindow` is set.
void drawAbstractionWindow(GuiSessionState& state, SnapMachine current);

// The same window over an explicit view, for the headless gate.
void drawAbstractionWindow(GuiAbstractionState& state,
                           const abstraction::SessionView& view);

} // namespace pom68k::gui
