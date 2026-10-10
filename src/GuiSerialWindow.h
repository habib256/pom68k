// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// « Ports série » — the two SCC channels as the host sees them: the
// configured endpoint and its owner, the connection, the counters, and a
// bounded terminal (SerialTerminal.h). The terminal observes a pty/tcp
// port and is the endpoint of a `terminal` one; only then is its input
// line enabled. Ports are configured at startup (POM68K_SERIAL_PRINTER,
// POM68K_SERIAL_MODEM or the session's serial-* keys). Gated headlessly by
// gui_windows_test.

#pragma once

#include "GuiSessionState.h"

namespace pom68k::gui {

inline constexpr const char* kSerialWindowTitle = "Ports série";

void drawSerialWindow(GuiSerialState& serial);

} // namespace pom68k::gui
