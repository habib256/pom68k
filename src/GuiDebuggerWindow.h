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
#include <vector>

namespace pom68k::gui {

struct GuiDebuggerState {
    pom68k::dbg::Session* session = nullptr;
    bool showWindow = false;
    std::array<char, 16> breakpointText{};
    std::array<char, 16> memoryText{};
    int memorySpace = 0;                     // 0 = logique, 1 = physique
    // Edits, offered only while the machine is stopped.
    int editRegister = 0;                    // a pom68k::dbg::Reg
    std::array<char, 16> editValue{};
    std::array<char, 16> pokeAddress{};
    std::array<char, 64> pokeBytes{};
    // Access and exception stops.
    std::array<char, 16> watchText{};
    int watchLength = 4;
    int watchAccess = 2;                     // a pom68k::dbg::Access
    int catchPreset = 0;                     // index into the window's list
    std::array<char, 16> catchText{};        // vector, or the A-line word
    std::string inputError;

    bool bound() const noexcept { return session != nullptr; }
};

extern const char* kDebuggerWindowTitle;

// "$1234", "0x1234" and "1234" are all hexadecimal; nullopt otherwise.
std::optional<std::uint32_t> parseGuestAddress(const char* text);

// "54 81", "5481" and "$54 $81": whole hexadecimal bytes, at least one and
// at most pom68k::dbg::kMaxEditBytes; nullopt otherwise.
std::optional<std::vector<std::uint8_t>> parseHexBytes(const char* text);

void drawDebuggerWindow(GuiDebuggerState& state);

} // namespace pom68k::gui
