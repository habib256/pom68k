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
    if (relaunch_) {
        relaunchFrame(session, window);
        return;
    }
    // Once, and through the slot's own setter. Re-assigning a shared member
    // every frame let the GUI thread rewrite the string while the machine
    // thread was reading it inside apply() — the TSan report of nightly run
    // 33605191940 (2026-09-02). DEV.md § 6: the machine thread is reached by
    // the queue and by the slot's locked requests, never by a bare field.
    if (frames_ == 1) saveState.setPath(*report_ + ".pomss");

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

// The relaunch scenario. Generation 1: after three rendered frames, stage
// the card exactly as « Appliquer » in the AppleTalk window does
// (relaunchWithDaynaPort, SCSI 3) — the shell closes the window, and
// processRelaunch() re-executes the process for real with the relaunch
// line (`--daynaport=3` in front of the same arguments). Generation 2:
// three frames, then attest that the session sees the card the line
// carried, and close. Both generations write the same report; the wrapper
// reads the last one and the log for the first.
void GuiSmokeScenario::relaunchFrame(GuiSessionState& session, GLFWwindow* window) {
    if (generation_ == 1) {
        if (frames_ < 3 || cardStaged_) return;
        if (!session.network.relaunchWithDaynaPort) {
            std::fprintf(stderr, "gui-smoke: no relaunchWithDaynaPort binding\n");
            closeRequested_ = true;
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            return;
        }
        std::fprintf(stderr, "gui-smoke: generation 1 stages the DaynaPort card at SCSI 3 and relaunches\n");
        session.network.relaunchWithDaynaPort(3);   // stages, and asks the shell to close
        cardStaged_ = true;
        closeRequested_ = true;
        return;
    }
    if (frames_ < 3) return;
    cardSeen_ = session.network.ethernetEnabled && session.network.ethernetScsiId == *daynaPort_;
    std::fprintf(stderr, "gui-smoke: generation 2 sees the card: enabled=%d id=%d (line carried %d)\n",
                 session.network.ethernetEnabled ? 1 : 0, session.network.ethernetScsiId, *daynaPort_);
    closeRequested_ = true;
    glfwSetWindowShouldClose(window, GLFW_TRUE);
}

int GuiSmokeScenario::finishRelaunch(bool relaunchRequested) const {
    const bool ok = generation_ == 1
        ? windowOpened_ && frames_ >= 3 && cardStaged_ && closeRequested_ && windowClosed_ && relaunchRequested
        : windowOpened_ && frames_ >= 3 && cardSeen_ && closeRequested_ && windowClosed_;
    std::ofstream output(*report_, std::ios::trunc);
    if (!output) return 1;
    output << "generation=" << generation_ << '\n'
           << "window_opened=" << windowOpened_ << '\n'
           << "frames=" << frames_ << '\n'
           << "card_staged=" << cardStaged_ << '\n'
           << "relaunch_requested=" << relaunchRequested << '\n'
           << "card_seen=" << cardSeen_ << '\n'
           << "daynaport_id=" << (daynaPort_ ? *daynaPort_ : -1) << '\n'
           << "window_closed=" << windowClosed_ << '\n'
           << "result=" << (ok ? "PASS" : "FAIL") << '\n';
    if (generation_ == 1)
        std::fprintf(stderr, "gui-smoke: generation 1 %s, re-executing\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

int GuiSmokeScenario::finish(bool relaunchRequested) const {
    if (!enabled()) return 0;
    if (relaunch_) return finishRelaunch(relaunchRequested);
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
