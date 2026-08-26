// POM68K — deterministic behavioural GUI gate driver
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "GuiSmokeScenario.h"

#include "GuiSessionState.h"
#include "SaveStateSlot.h"

#include <GLFW/glfw3.h>

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace pom68k::gui {

void GuiSmokeScenario::frame(GuiSessionState& session, GLFWwindow* window,
                             SaveStateSlot& saveState) {
    if (!enabled()) return;
    ++frames_;
    std::fprintf(stderr, "gui-smoke: frame %d\n", frames_);
    saveState.path = *report_ + ".pomss";

    if (!engineRequested_ && session.cpu.setCpuEngine &&
        session.cpu.getCpuEngine) {
        targetEngine_ = session.cpu.getCpuEngine() == 0 ? 1 : 0;
        session.cpu.setCpuEngine(targetEngine_);
        engineRequested_ = true;
        return;
    }
    if (engineRequested_ && !engineSwitched_ && session.cpu.getCpuEngine &&
        session.cpu.getCpuEngine() == targetEngine_) {
        engineSwitched_ = true;
        saveState.request(false);
        saveRequested_ = true;
        return;
    }
    if (saveRequested_ && !saveCompleted_ &&
        saveState.message().starts_with("État sauvé")) {
        saveCompleted_ = true;
        session.relaunch.switchArguments = session.relaunch.launchArguments;
        if (session.relaunch.switchArguments.empty())
            session.relaunch.switchArguments = {"--gui-smoke=" + *report_};
        closeRequested_ = true;
        glfwSetWindowShouldClose(window, GLFW_TRUE);
        return;
    }
    if (frames_ >= 120) {
        closeRequested_ = true;
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
}

int GuiSmokeScenario::finish(bool relaunchRequested) const {
    if (!enabled()) return 0;
    const bool stateFile = std::filesystem::is_regular_file(*report_ + ".pomss");
    const bool ok = windowOpened_ && frames_ >= 3 && engineRequested_ &&
        engineSwitched_ && saveRequested_ && saveCompleted_ && stateFile &&
        closeRequested_ && windowClosed_ && relaunchRequested;

    std::ofstream output(*report_, std::ios::trunc);
    if (!output) return 1;
    output << "window_opened=" << windowOpened_ << '\n'
           << "frames=" << frames_ << '\n'
           << "engine_requested=" << engineRequested_ << '\n'
           << "engine_switched=" << engineSwitched_ << '\n'
           << "save_requested=" << saveRequested_ << '\n'
           << "save_completed=" << saveCompleted_ << '\n'
           << "state_file=" << stateFile << '\n'
           << "close_requested=" << closeRequested_ << '\n'
           << "window_closed=" << windowClosed_ << '\n'
           << "relaunch_requested=" << relaunchRequested << '\n'
           << "result=" << (ok ? "PASS" : "FAIL") << '\n';
    return ok ? 0 : 1;
}

} // namespace pom68k::gui
