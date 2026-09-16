// POM68K — common GUI shell, window and input mechanics
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Window setup, common menus and input mechanics. Family render loops live
// in GuiRunner*.h; host I/O enters through GuiHostServices.

#pragma once

#include "DiskBays.h"
#include "DockLayout.h"
#include "GuiMachineControls.h"
#include "MachineCatalog.h"
#include "GuiSessionObjects.h"
#include "GuiDisplay.h"
#include "GuiScreen.h"
#include "GuiShellMenu.h"
#include "GuiSessionState.h"
#include "GuiSmokeScenario.h"
#include "GuiWindowSession.h"
#include "RuntimeConfig.h"
#include "SaveStateSlot.h"
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#ifdef _WIN32
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#endif

namespace pom68k::gui {

class GuiShell {
public:
    GuiShell(GuiSessionState& state, GuiSessionObjects& objects,
             const app::RuntimeConfig& config)
        : state_(state), objects_(objects),
          smoke_(config.diagnostics().smokeReport, config.diagnostics().smokeRelaunch,
                 config.core().bus.daynaPortId) {}

    GuiWindowSession* openWindow(int width, int height,
                                 const std::string& title);

    template <class MachineT, class Cpu>
    void bindCpuMenu(MachineT& machine, Cpu& cpu) {
        state_.cpu.setCpuEngine = [&machine](int engine) {
            machine.push({MachineT::Cmd::CpuEngine, engine});
        };
        state_.cpu.getCpuEngine = [&machine] { return machine.cpuEngine(); };
        state_.cpu.jitStats = [&machine] { return machine.jitStats(); };
        state_.cpu.speedSample = [&machine] {
            return std::pair{machine.machineClock(), machine.machineHz()};
        };
        state_.cpu.jitBackend = cpu.jit().backendName();
    }

    // The runner's whole contribution to the menu bar and the control
    // window: callbacks bound once, the family status lines as a lambda.
    template <class MachineT, class DrawStatus>
    void bindMachineControls(MachineT& machine, DrawStatus&& drawStatus) {
        gui::bindMachineControls(state_.machine, machine,
                                 std::forward<DrawStatus>(drawStatus));
    }

    // Menu bar, dock space and every shell-owned window, once per frame,
    // before the runner draws the screen window (drawShellFrame, GLFW-free
    // and gated by gui_machine_window_test), then what only a real window
    // can do: the kiosk monitor switch and the close the frame asked for.
    void drawMachineMenu(SnapMachine current, GLFWwindow* window);
    GuiDisplayState& display() noexcept { return state_.display; }


    // Drives the command-line GUI smoke scenario once per rendered frame.
    // Normal sessions pay one predictable empty-optional branch.
    void runSmokeFrame(GLFWwindow* window, SaveStateSlot& state) {
        smoke_.frame(state_, window, state);
    }
    void noteWindowClosed() noexcept { smoke_.noteWindowClosed(); }
    bool smokeEnabled() const noexcept { return smoke_.enabled(); }
    bool smokeExecs() const noexcept { return smoke_.execs(); }
    int finishSmoke(bool relaunchRequested) const {
        return smoke_.finish(relaunchRequested);
    }

private:
    GuiSessionState& state_;
    GuiSessionObjects& objects_;
    GuiSmokeScenario smoke_;
};

// The GLFW side of ScreenInput (GuiScreen.h): the host window's buttons,
// cursor and capture. The headless gate substitutes its own.
struct GlfwScreenHost {
    GLFWwindow* win;
    bool middleButton() const {
        return glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
    }
    bool leftButton() const {
        return glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    }
    bool rightButton() const {
        return glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    }
    bool grabChord() const {
        const bool ctrl = glfwGetKey(win, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS
                       || glfwGetKey(win, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
        const bool alt = glfwGetKey(win, GLFW_KEY_LEFT_ALT) == GLFW_PRESS
                      || glfwGetKey(win, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
        return ctrl && alt && glfwGetKey(win, GLFW_KEY_G) == GLFW_PRESS;
    }
    void cursorPos(double& x, double& y) const { glfwGetCursorPos(win, &x, &y); }
    void setCursorCaptured(bool on) const {
        glfwSetInputMode(win, GLFW_CURSOR,
                         on ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    }
};

} // namespace pom68k::gui
