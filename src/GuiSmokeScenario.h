// POM68K — deterministic behavioural GUI gate driver
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct GLFWwindow;
struct GuiSessionState;
struct SaveStateSlot;

namespace pom68k::gui {

class GuiSmokeScenario {
public:
    // `relaunch`: the relaunch scenario (--gui-smoke-relaunch=); `daynaPort`
    // is the card the command line carries — present only in the second
    // generation, which is how the generations are told apart.
    GuiSmokeScenario(std::optional<std::string> report, bool relaunch = false,
                     std::optional<int> daynaPort = std::nullopt)
        : report_(std::move(report)), relaunch_(relaunch),
          generation_(daynaPort ? 2 : 1), daynaPort_(daynaPort) {}

    // The session scenario (--gui-smoke-session=): the session file this
    // process opened and the media it configured. Generation 1 is the
    // process whose session is not `second.pomsession`.
    void observeSessions(std::filesystem::path session, std::vector<std::string> media) {
        sessions_ = true;
        generation_ = session.stem() == "second" ? 2 : 1;
        session_ = std::move(session);
        media_ = std::move(media);
    }

    bool enabled() const noexcept { return report_.has_value(); }
    // The relaunch scenario's first generation must really re-execute:
    // processRelaunch() carries on to exec after finish() when this holds.
    bool execs() const noexcept {
        return enabled() && (relaunch_ || sessions_) && generation_ == 1;
    }
    void noteWindowOpened() noexcept { windowOpened_ = enabled(); }
    void noteWindowClosed() noexcept { windowClosed_ = enabled(); }
    void frame(GuiSessionState& session, GLFWwindow* window,
               SaveStateSlot& saveState);
    int finish(bool relaunchRequested) const;

private:
    void relaunchFrame(GuiSessionState& session, GLFWwindow* window);
    int finishRelaunch(bool relaunchRequested) const;
    void sessionFrame(GuiSessionState& session, GLFWwindow* window);
    int finishSessions(bool relaunchRequested) const;

    std::optional<std::string> report_;
    bool relaunch_ = false;
    int generation_ = 1;
    std::optional<int> daynaPort_;
    bool sessions_ = false;
    std::filesystem::path session_;
    std::vector<std::string> media_;
    std::string opened_; // generation 1: the session it asked for
    bool cardStaged_ = false;
    bool cardSeen_ = false;
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
