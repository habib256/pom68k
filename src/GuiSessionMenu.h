// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// « Machine → Session »: save the running machine as a `.pomsession` file
// and reopen one (SessionFile.h). Without a window system, like the rest
// of the shell frame: opening a session leaves a verbatim relaunch request
// (`--session=<file>`) in the relaunch state, which the shell carries out.
// Gated headlessly by gui_machine_window_test.

#pragma once

#include "GuiSessionState.h"
#include "RuntimeConfig.h"

#include <filesystem>

namespace pom68k::gui {

inline constexpr const char* kSessionWindowTitle = "Enregistrer la session";

// Binds `state.sessionFile` to the process: the session it opened, the
// directory listed (that session's, else `fallbackDirectory`), and the
// capture — the startup configuration with the GUI's live choices written
// over it, what a relaunch would carry. `config` and `state` outlive it.
void bindSessionFile(GuiSessionState& state, const app::RuntimeConfig& config,
                     const std::filesystem::path& fallbackDirectory);
// The submenu, drawn inside an open « Machine » menu.
void drawSessionMenu(GuiSessionState& state);
// The save window, when « Enregistrer la session... » opened it.
void drawSessionWindow(GuiSessionFileState& session);
// What opening `file` stages: a verbatim relaunch on `--session=<file>`.
void openSession(GuiRelaunchState& relaunch, const std::string& file);

} // namespace pom68k::gui
