// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The shell opens the GLFW window and wraps the frame GuiShellMenu.cpp
// draws; the menu bar itself is decided there. The shell owns the whole menu bar. Runners bind callbacks (bindCpuMenu,
// bindMachineControls) and draw nothing into the bar themselves; what a
// user can reach is decided once, here:
//
//   Machine        run/pause/fast-forward, save state, recording,
//                  « Changer de machine » (one submenu per board family),
//                  drive sounds
//   Périphériques  disks, network, controller provenance (LLE / HLE)
//   CPU            measured speed, engine choice, engine statistics
//   Fenêtres       every secondary window, and the layout reset
//   (right edge)   LLE qualification, live speed, mouse-capture hint
//
// The catalogue lives in a submenu on purpose: 39 profiles plus their
// separators are taller than a 900 px screen, and an item appended after
// them landed under the scroll (measured under Xvfb, DEV.md § 6).

#include "GuiShell.h"

#include "GuiDisplay.h"
#include "GuiShellMenu.h"

#include <cstdio>
#include <cstring>

namespace pom68k::gui {
namespace {

void glfwErrorCallback(int error, const char* description) {
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

const char* configureGlfwOpenGl() {
#ifdef __APPLE__
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    return "#version 150";
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    return "#version 130";
#endif
}

} // namespace

GuiWindowSession* GuiShell::openWindow(
    int width, int height, const std::string& title) {
    glfwSetErrorCallback(glfwErrorCallback);
    GuiWindowSession& window = objects_.make<GuiWindowSession>();
    if (!window.open(width, height, title.c_str(), configureGlfwOpenGl,
                     !smokeEnabled())) return nullptr;
    smoke_.noteWindowOpened();
    return &window;
}

// The kiosk monitor switch, between two frames and only on a visible
// window: the smoke scenario's hidden window is never sent to a monitor
// (GLFW would show it).
void applyKioskWindow(GuiDisplayState& d, GLFWwindow* window) {
    if (d.kiosk == d.kioskApplied) return;
    const bool visible = glfwGetWindowAttrib(window, GLFW_VISIBLE) == GLFW_TRUE;
    if (d.kiosk) {
        glfwGetWindowPos(window, &d.windowedX, &d.windowedY);
        glfwGetWindowSize(window, &d.windowedW, &d.windowedH);
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
        if (visible && monitor && mode)
            glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        std::fprintf(stderr, "[kiosk] cabinet mode ON%s - Ctrl+Alt+F, Alt+F4 or Ctrl+Shift+Q (held) leaves\n",
                     visible ? "" : " (window hidden: no monitor switch)");
    } else {
        if (visible)
            glfwSetWindowMonitor(window, nullptr, d.windowedX, d.windowedY,
                                 d.windowedW, d.windowedH, 0);
        std::fprintf(stderr, "[kiosk] cabinet mode OFF\n");
    }
    d.kioskApplied = d.kiosk;
}

void GuiShell::drawMachineMenu(SnapMachine current, GLFWwindow* window) {
    drawShellFrame(state_, current);
    applyKioskWindow(state_.display, window);
    if (state_.relaunch.closeWindow) {
        state_.relaunch.closeWindow = false;
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
}

} // namespace pom68k::gui
