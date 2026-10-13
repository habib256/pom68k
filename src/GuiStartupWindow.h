// POM68K — guided machine preparation, independent of the window system.
#pragma once

#include "MachineCatalog.h"
#include <array>
#include <string>
#include <vector>

struct GuiSessionState;
namespace pom68k::gui {

struct GuiStartupState {
    bool considered = false, open = false, popupRequested = false;
    bool missingRom = false, floppy = false, ready = false;
    SnapMachine profile = SnapMachine::LcII;
    std::array<char, 1024> rom{}, media{}, folder{};
    std::array<char, 64> modelSearch{};
    int browsing = 0; // 1: ROM, 2: system medium
    std::string message, browseError;
    std::vector<std::string> arguments;
};

inline constexpr const char* kStartupWindowTitle = "Set Up a Macintosh";
void openStartupWindow(GuiSessionState& state, SnapMachine current);
bool validateStartup(GuiStartupState& startup);
void drawStartupWindow(GuiSessionState& state, SnapMachine current);
} // namespace pom68k::gui
