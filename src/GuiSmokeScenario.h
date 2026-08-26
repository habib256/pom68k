// POM68K — deterministic behavioural GUI gate driver
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#pragma once

#include <optional>
#include <string>
#include <utility>

struct GLFWwindow;
struct GuiSessionState;
struct SaveStateSlot;

namespace pom68k::gui {

class GuiSmokeScenario {
public:
    explicit GuiSmokeScenario(std::optional<std::string> report)
        : report_(std::move(report)) {}

    bool enabled() const noexcept { return report_.has_value(); }
    void noteWindowOpened() noexcept { windowOpened_ = enabled(); }
    void noteWindowClosed() noexcept { windowClosed_ = enabled(); }
    void frame(GuiSessionState& session, GLFWwindow* window,
               SaveStateSlot& saveState);
    int finish(bool relaunchRequested) const;

private:
    std::optional<std::string> report_;
    int frames_ = 0;
    int targetEngine_ = -1;
    bool windowOpened_ = false;
    bool engineRequested_ = false;
    bool engineSwitched_ = false;
    bool saveRequested_ = false;
    bool saveCompleted_ = false;
    bool closeRequested_ = false;
    bool windowClosed_ = false;
};

} // namespace pom68k::gui
