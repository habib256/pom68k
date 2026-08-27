// POM68K — common GUI shell, window and input mechanics
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Window setup, common menus and input mechanics. Family render loops live
// in GuiRunner*.h; host I/O enters through GuiHostServices.

#pragma once

#include "DiskBays.h"
#include "DockLayout.h"
#include "MachineCatalog.h"
#include "GuiSessionObjects.h"
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
          smoke_(config.diagnostics().smokeReport) {}

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

    template <class MenuFn>
    void drawMachineMenu(SnapMachine current, GLFWwindow* window,
                         MenuFn&& extraMenus) {
        drawMachineMenuImpl(
            current, window,
            std::function<void()>(std::forward<MenuFn>(extraMenus)));
    }

    void drawSaveState(SaveStateSlot& state) const;

    // Drives the command-line GUI smoke scenario once per rendered frame.
    // Normal sessions pay one predictable empty-optional branch.
    void runSmokeFrame(GLFWwindow* window, SaveStateSlot& state) {
        smoke_.frame(state_, window, state);
    }
    void noteWindowClosed() noexcept { smoke_.noteWindowClosed(); }
    bool smokeEnabled() const noexcept { return smoke_.enabled(); }
    int finishSmoke(bool relaunchRequested) const {
        return smoke_.finish(relaunchRequested);
    }

private:
    void drawMachineMenuImpl(SnapMachine current, GLFWwindow* window,
                             const std::function<void()>& extraMenus);

    GuiSessionState& state_;
    GuiSessionObjects& objects_;
    GuiSmokeScenario smoke_;
};

// An emulated screen is an InvisibleButton with the image drawn over it.
// A drag started on the Mac screen owns the mouse until release.  The middle
// mouse button, Ctrl+Alt+G, or Delete toggles hard GLFW cursor capture.
struct ScreenInput {
    bool captured = false;
    bool midWas = false;
    bool grabWas = false;
    float accX = 0;
    float accY = 0;
    float zoom = 2.0f;
    double lastX = 0;
    double lastY = 0;

    template <typename MoveFn, typename ButtonFn>
    void frame(GLFWwindow* win, GLuint tex, ImVec2 size,
               MoveFn move, ButtonFn button) {
        ImGuiIO& io = ImGui::GetIO();
        const ImVec2 native(size.x * 0.5f, size.y * 0.5f);
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        if (size.x > 0 && size.y > 0 && avail.x > 32 && avail.y > 32) {
            float scale = avail.x / size.x;
            if (avail.y / size.y < scale) scale = avail.y / size.y;
            size = ImVec2(size.x * scale, size.y * scale);
        }
        zoom = native.x > 0 ? size.x / native.x : 2.0f;
        if (zoom < 0.05f) zoom = 0.05f;

        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("screen", size);
        ImGui::GetWindowDrawList()->AddImage(
            ImTextureID(intptr_t(tex)), pos,
            ImVec2(pos.x + size.x, pos.y + size.y));

        // GLFW polling remains active while ImGui mouse input is disabled,
        // so every capture shortcut can also release an existing capture.
        const bool mid = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_MIDDLE)
                             == GLFW_PRESS;
        const bool midEdge = mid && !midWas;
        midWas = mid;
        const bool ctrl = glfwGetKey(win, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS
                       || glfwGetKey(win, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
        const bool alt = glfwGetKey(win, GLFW_KEY_LEFT_ALT) == GLFW_PRESS
                      || glfwGetKey(win, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
        const bool grab = ctrl && alt &&
                          glfwGetKey(win, GLFW_KEY_G) == GLFW_PRESS;
        const bool grabEdge = grab && !grabWas;
        grabWas = grab;
        if ((midEdge || grabEdge) && (captured || ImGui::IsItemHovered()))
            setCaptured(win, !captured);
        else if (!io.WantTextInput &&
                 ImGui::IsKeyPressed(ImGuiKey_Delete, false))
            setCaptured(win, !captured);

        if (captured) {
            double x = 0;
            double y = 0;
            glfwGetCursorPos(win, &x, &y);
            feed(float(x - lastX), float(y - lastY), move);
            lastX = x;
            lastY = y;
            button(0, glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT)
                          == GLFW_PRESS);
            button(1, glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT)
                          == GLFW_PRESS);
        } else if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            feed(io.MouseDelta.x, io.MouseDelta.y, move);
            button(0, io.MouseDown[0]);
            button(1, io.MouseDown[1]);
        }
    }

    void setCaptured(GLFWwindow* win, bool on) {
        captured = on;
        glfwSetInputMode(win, GLFW_CURSOR,
                         on ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        if (on) glfwGetCursorPos(win, &lastX, &lastY);
        ImGuiIO& io = ImGui::GetIO();
        if (on) io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
        else io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    }

private:
    template <typename MoveFn>
    void feed(float hostX, float hostY, MoveFn move) {
        accX += hostX / zoom;
        accY += hostY / zoom;
        const int dx = int(accX);
        const int dy = int(accY);
        if (dx || dy) {
            move(dx, dy);
            accX -= dx;
            accY -= dy;
        }
    }
};

// Two host keys may share one Mac transition code.  Reference counts keep a
// modifier down until its last physical host key is released.
class AdbKeyboard {
public:
    template <class MachineT, class TraceFn>
    void frame(MachineT& machine, TraceFn&& trace) {
        if (ImGui::GetIO().WantTextInput) return;
        dispatch(machine, trace, kKeys);
        dispatch(machine, trace, kKeypad);
    }

    // The original Mac II/IIfx and Duo tables predate keypad forwarding but
    // include Escape. Keep that exact host-key surface while still sharing
    // the transition/refcount implementation with the later runners.
    template <class MachineT, class TraceFn>
    void frameLegacy(MachineT& machine, TraceFn&& trace) {
        if (ImGui::GetIO().WantTextInput) return;
        dispatch(machine, trace, kKeys);
        static constexpr Key escape[] = {{ImGuiKey_Escape, 0x6B}};
        dispatch(machine, trace, escape);
    }

private:
    struct Key {
        ImGuiKey key;
        uint8_t m0110;
    };

    template <class MachineT, class TraceFn, size_t N>
    void dispatch(MachineT& machine, TraceFn&& trace,
                  const Key (&keys)[N]) {
        for (const Key& entry : keys) {
            if (keyDown(entry.m0110, entry.key)) {
                trace(uint8_t(entry.m0110 >> 1), true);
                machine.push({MachineT::Cmd::Key, entry.m0110 >> 1, 1});
            }
            if (keyUp(entry.m0110, entry.key)) {
                trace(uint8_t(entry.m0110 >> 1), false);
                machine.push({MachineT::Cmd::Key, entry.m0110 >> 1, 0});
            }
        }
    }

    inline static constexpr Key kKeys[] = {
        {ImGuiKey_A,0x01},{ImGuiKey_S,0x03},{ImGuiKey_D,0x05},{ImGuiKey_F,0x07},
        {ImGuiKey_H,0x09},{ImGuiKey_G,0x0B},{ImGuiKey_Z,0x0D},{ImGuiKey_X,0x0F},
        {ImGuiKey_C,0x11},{ImGuiKey_V,0x13},{ImGuiKey_B,0x17},{ImGuiKey_Q,0x19},
        {ImGuiKey_W,0x1B},{ImGuiKey_E,0x1D},{ImGuiKey_R,0x1F},{ImGuiKey_Y,0x21},
        {ImGuiKey_T,0x23},{ImGuiKey_1,0x25},{ImGuiKey_2,0x27},{ImGuiKey_3,0x29},
        {ImGuiKey_4,0x2B},{ImGuiKey_6,0x2D},{ImGuiKey_5,0x2F},{ImGuiKey_Equal,0x31},
        {ImGuiKey_9,0x33},{ImGuiKey_7,0x35},{ImGuiKey_Minus,0x37},{ImGuiKey_8,0x39},
        {ImGuiKey_0,0x3B},{ImGuiKey_RightBracket,0x3D},{ImGuiKey_O,0x3F},
        {ImGuiKey_U,0x41},{ImGuiKey_LeftBracket,0x43},{ImGuiKey_I,0x45},
        {ImGuiKey_P,0x47},{ImGuiKey_Enter,0x49},{ImGuiKey_L,0x4B},{ImGuiKey_J,0x4D},
        {ImGuiKey_Apostrophe,0x4F},{ImGuiKey_K,0x51},{ImGuiKey_Semicolon,0x53},
        {ImGuiKey_Backslash,0x55},{ImGuiKey_Comma,0x57},{ImGuiKey_Slash,0x59},
        {ImGuiKey_N,0x5B},{ImGuiKey_M,0x5D},{ImGuiKey_Period,0x5F},
        {ImGuiKey_Tab,0x61},{ImGuiKey_Space,0x63},{ImGuiKey_GraveAccent,0x65},
        {ImGuiKey_Backspace,0x67},{ImGuiKey_LeftSuper,0x6F},
        {ImGuiKey_RightSuper,0x6F},{ImGuiKey_LeftCtrl,0x6D},
        {ImGuiKey_LeftShift,0x71},{ImGuiKey_RightShift,0xF7},
        {ImGuiKey_CapsLock,0x73},{ImGuiKey_LeftAlt,0x75},
        {ImGuiKey_RightAlt,0xF9},{ImGuiKey_RightCtrl,0xFB},
        {ImGuiKey_LeftArrow,0x76},{ImGuiKey_RightArrow,0x78},
        {ImGuiKey_DownArrow,0x7A},{ImGuiKey_UpArrow,0x7C},
    };

    inline static constexpr Key kKeypad[] = {
        {ImGuiKey_Keypad0,0xA4},{ImGuiKey_Keypad1,0xA6},
        {ImGuiKey_Keypad2,0xA8},{ImGuiKey_Keypad3,0xAA},
        {ImGuiKey_Keypad4,0xAC},{ImGuiKey_Keypad5,0xAE},
        {ImGuiKey_Keypad6,0xB0},{ImGuiKey_Keypad7,0xB2},
        {ImGuiKey_Keypad8,0xB6},{ImGuiKey_Keypad9,0xB8},
    };

    bool keyDown(uint8_t code, ImGuiKey key) {
        if (!ImGui::IsKeyPressed(key, false)) return false;
        return ++held_[code & 0x7F] == 1;
    }

    bool keyUp(uint8_t code, ImGuiKey key) {
        if (!ImGui::IsKeyReleased(key)) return false;
        uint8_t& count = held_[code & 0x7F];
        if (!count) return false;
        return --count == 0;
    }

    uint8_t held_[128]{};
};

// Compact keyboard. It preserves the original M0110 host-key surface
// (including shared left/right Shift) but emits the same queued ADB-code
// command used by every MachineHost; MacMemory translates it for the Plus.
class CompactKeyboard {
    struct Key { ImGuiKey key; uint8_t m0110; };

public:
    template <class MachineT, class TraceFn>
    void frame(MachineT& machine, TraceFn&& trace) {
        if (ImGui::GetIO().WantTextInput) return;
        for (const Key& entry : kKeys) {
            const uint8_t code = uint8_t(entry.m0110 >> 1);
            if (keyDown(entry)) {
                trace(code, true);
                machine.push({MachineT::Cmd::Key, code, 1});
            }
            if (keyUp(entry)) {
                trace(code, false);
                machine.push({MachineT::Cmd::Key, code, 0});
            }
        }
    }

private:
    bool keyDown(const Key& entry) {
        if (!ImGui::IsKeyPressed(entry.key, false)) return false;
        return ++held_[entry.m0110 & 0x7F] == 1;
    }
    bool keyUp(const Key& entry) {
        if (!ImGui::IsKeyReleased(entry.key)) return false;
        uint8_t& count = held_[entry.m0110 & 0x7F];
        if (!count) return false;
        return --count == 0;
    }

    inline static constexpr Key kKeys[] = {
        {ImGuiKey_A,0x01},{ImGuiKey_S,0x03},{ImGuiKey_D,0x05},{ImGuiKey_F,0x07},
        {ImGuiKey_H,0x09},{ImGuiKey_G,0x0B},{ImGuiKey_Z,0x0D},{ImGuiKey_X,0x0F},
        {ImGuiKey_C,0x11},{ImGuiKey_V,0x13},{ImGuiKey_B,0x17},{ImGuiKey_Q,0x19},
        {ImGuiKey_W,0x1B},{ImGuiKey_E,0x1D},{ImGuiKey_R,0x1F},{ImGuiKey_Y,0x21},
        {ImGuiKey_T,0x23},{ImGuiKey_1,0x25},{ImGuiKey_2,0x27},{ImGuiKey_3,0x29},
        {ImGuiKey_4,0x2B},{ImGuiKey_6,0x2D},{ImGuiKey_5,0x2F},{ImGuiKey_Equal,0x31},
        {ImGuiKey_9,0x33},{ImGuiKey_7,0x35},{ImGuiKey_Minus,0x37},{ImGuiKey_8,0x39},
        {ImGuiKey_0,0x3B},{ImGuiKey_RightBracket,0x3D},{ImGuiKey_O,0x3F},
        {ImGuiKey_U,0x41},{ImGuiKey_LeftBracket,0x43},{ImGuiKey_I,0x45},
        {ImGuiKey_P,0x47},{ImGuiKey_Enter,0x49},{ImGuiKey_L,0x4B},{ImGuiKey_J,0x4D},
        {ImGuiKey_Apostrophe,0x4F},{ImGuiKey_K,0x51},{ImGuiKey_Semicolon,0x53},
        {ImGuiKey_Backslash,0x55},{ImGuiKey_Comma,0x57},{ImGuiKey_Slash,0x59},
        {ImGuiKey_N,0x5B},{ImGuiKey_M,0x5D},{ImGuiKey_Period,0x5F},
        {ImGuiKey_Tab,0x61},{ImGuiKey_Space,0x63},{ImGuiKey_GraveAccent,0x65},
        {ImGuiKey_Backspace,0x67},{ImGuiKey_LeftSuper,0x6F},
        {ImGuiKey_LeftShift,0x71},{ImGuiKey_RightShift,0x71},
        {ImGuiKey_CapsLock,0x73},{ImGuiKey_LeftAlt,0x75},
    };
    uint8_t held_[128]{};
};


} // namespace pom68k::gui
