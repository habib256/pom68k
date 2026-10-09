// POM68K — the « Débogueur » window
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Renders a DebugSession's latest snapshot and posts commands to it. The
// window never touches the CPU or the memory map: what it shows is what
// the machine thread published, and every button is a queued command
// (DebugSession.h). Gate: tests/gui_windows_test.cpp (headless).

#pragma once

#include "DebugSession.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace pom68k::gui {

struct GuiDebuggerState {
    pom68k::dbg::Session* session = nullptr;
    bool showWindow = false;
    std::array<char, 16> breakpointText{};
    std::array<char, 16> memoryText{};
    int memorySpace = 0;                     // 0 = logique, 1 = physique
    std::string inputError;

    bool bound() const noexcept { return session != nullptr; }
};

extern const char* kDebuggerWindowTitle;

// "$1234", "0x1234" and "1234" are all hexadecimal; nullopt otherwise.
std::optional<std::uint32_t> parseGuestAddress(const char* text);

void drawDebuggerWindow(GuiDebuggerState& state);

} // namespace pom68k::gui
