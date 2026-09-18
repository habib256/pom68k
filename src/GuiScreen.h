// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The machine window without a window system: the screen window (docked
// or the whole viewport in kiosk mode), the mouse surface and the two
// keyboard translators. Dear ImGui only — the host window (buttons,
// cursor capture) enters ScreenInput::frame as a small `Host` object, so
// gui_machine_window_test drives this with a fake and the runners with
// GlfwScreenHost (GuiShellCommon.h). Split out of GuiShellCommon.h on
// 2026-09-16 for that gate.

#pragma once

#include "DockLayout.h"
#include "GuiDisplay.h"
#include "imgui.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace pom68k::gui {

// The window the runner draws the screen in: the docked one on the
// desktop, a chromeless window covering the whole viewport in kiosk mode.
// Every runner calls this instead of dockLayoutScreenWindow + Begin, and
// ImGui::End as before.
inline void screenWindowBegin(const GuiDisplayState& display, const char* title) {
    if (display.kiosk) {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->Pos);
        ImGui::SetNextWindowSize(vp->Size);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::Begin(title, nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                     ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar(2);
        return;
    }
    pom68k::dockLayoutScreenWindow(title);
    ImGui::Begin(title);
}

// ── The frame upload, with the driver held at arm's length ──────────────
// The last piece of the GUI that lived only inside a GL context, and the
// sixth copy of one block: every family runner latched a frame and called
// glTexImage2D itself, and the copies had drifted apart. Two guarded the
// geometry (`w > 0 && h > 0`) and four did not; one uploaded `GL_RGBA`
// where five uploaded `GL_BGRA` — indistinguishable only because a compact
// publishes greyscale, where both orders are the same four bytes; and none
// of them checked that the buffer holds the pixels the geometry promises.
// That last one has no diagnostic when it is wrong: glTexImage2D takes a
// bare pointer and reads w × h × 4 bytes from it.
//
// `Host` is the driver side — `GlTextureHost` in GuiShellCommon.h for the
// product, a recording fake in gui_machine_window_test. Same seam as
// ScreenInput::frame above.

// A frame may be uploaded when the driver can read exactly what it is told
// to read: a positive geometry, and a buffer that holds it.
inline bool frameUploadable(const std::vector<std::uint32_t>& fb,
                            int w, int h) {
    return w > 0 && h > 0 &&
           fb.size() >= std::size_t(w) * std::size_t(h);
}

// True when the frame reached the texture — the two runners that draw the
// screen only on a fresh frame test it exactly as they tested their own
// condition before.
template <class Host>
bool uploadFrameTexture(Host host, unsigned int texture,
                        const std::vector<std::uint32_t>& fb, int w, int h) {
    if (!frameUploadable(fb, w, h)) return false;
    host.bindTexture(texture);
    host.uploadBgra(w, h, fb.data());
    return true;
}

// An emulated screen is an InvisibleButton with the image drawn over it.
// A drag started on the Mac screen owns the mouse until release.  The middle
// mouse button, Ctrl+Alt+G, or Delete toggles hard host cursor capture.
// In kiosk mode the capture is imposed and the image fills the monitor
// (letterboxed, pixel ratio kept); with the CRT pass on, the texture drawn
// is the pass's output at the drawn size (GuiDisplay.h).
struct ScreenInput {
    bool captured = false;
    bool midWas = false;
    bool grabWas = false;
    float accX = 0;
    float accY = 0;
    float zoom = 2.0f;
    double lastX = 0;
    double lastY = 0;

    bool kioskCaptured = false;   // the capture this mode imposed, to undo on exit

    // `host` is the window's side (GlfwScreenHost in GuiShellCommon.h; the
    // gate's fake): buttons, cursor position, cursor capture.
    template <class Host, typename MoveFn, typename ButtonFn>
    void frame(GuiDisplayState& display, Host host, unsigned int tex, ImVec2 size,
               MoveFn move, ButtonFn button) {
        ImGuiIO& io = ImGui::GetIO();
        const ImVec2 native(size.x * 0.5f, size.y * 0.5f);
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        ImVec2 offset(0, 0);
        if (display.kiosk) {
            // The whole surface, the image centred with its pixel ratio.
            const Letterbox box = letterbox(avail.x, avail.y, size.x, size.y);
            size = ImVec2(box.w, box.h);
            offset = ImVec2(box.x, box.y);
        } else if (size.x > 0 && size.y > 0 && avail.x > 32 && avail.y > 32) {
            float scale = avail.x / size.x;
            if (avail.y / size.y < scale) scale = avail.y / size.y;
            size = ImVec2(size.x * scale, size.y * scale);
        }
        zoom = native.x > 0 ? size.x / native.x : 2.0f;
        if (zoom < 0.05f) zoom = 0.05f;

        // Kiosk imposes the capture; leaving it releases what it imposed.
        if (display.kiosk && !captured) { setCaptured(host, true); kioskCaptured = true; }
        if (!display.kiosk && kioskCaptured) { setCaptured(host, false); kioskCaptured = false; }

        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 pos(origin.x + offset.x, origin.y + offset.y);
        if (offset.x > 0 || offset.y > 0) ImGui::SetCursorScreenPos(pos);
        ImGui::InvisibleButton("screen", ImVec2(std::max(size.x, 1.0f), std::max(size.y, 1.0f)));
        const unsigned int shown = display.shown(tex, int(native.x), int(native.y),
                                           int(size.x + 0.5f), int(size.y + 0.5f));
        ImGui::GetWindowDrawList()->AddImage(
            ImTextureID(intptr_t(shown)), pos,
            ImVec2(pos.x + size.x, pos.y + size.y));

        // Host polling remains active while ImGui mouse input is disabled,
        // so every capture shortcut can also release an existing capture.
        const bool mid = host.middleButton();
        const bool midEdge = mid && !midWas;
        midWas = mid;
        const bool grab = host.grabChord();
        const bool grabEdge = grab && !grabWas;
        grabWas = grab;
        if ((midEdge || grabEdge) && (captured || ImGui::IsItemHovered()))
            setCaptured(host, !captured);
        else if (!io.WantTextInput &&
                 ImGui::IsKeyPressed(ImGuiKey_Delete, false))
            setCaptured(host, !captured);

        if (captured) {
            double x = 0;
            double y = 0;
            host.cursorPos(x, y);
            feed(float(x - lastX), float(y - lastY), move);
            lastX = x;
            lastY = y;
            button(0, host.leftButton());
            button(1, host.rightButton());
        } else if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            feed(io.MouseDelta.x, io.MouseDelta.y, move);
            button(0, io.MouseDown[0]);
            button(1, io.MouseDown[1]);
        }
    }

    template <class Host>
    void setCaptured(Host host, bool on) {
        captured = on;
        host.setCursorCaptured(on);
        if (on) host.cursorPos(lastX, lastY);
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

    // Index by the Mac virtual key code (`m0110 >> 1`), the value actually
    // pushed: masking the wire byte instead collided the moment keypad
    // entries (codes >= $40, so a wire byte >= $80) joined the tables.
    bool keyDown(uint8_t code, ImGuiKey key) {
        if (!ImGui::IsKeyPressed(key, false)) return false;
        return ++held_[code >> 1] == 1;
    }

    bool keyUp(uint8_t code, ImGuiKey key) {
        if (!ImGui::IsKeyReleased(key)) return false;
        uint8_t& count = held_[code >> 1];
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
    // See AdbKeyboard::keyDown: refcount per virtual key code.
    bool keyDown(const Key& entry) {
        if (!ImGui::IsKeyPressed(entry.key, false)) return false;
        return ++held_[entry.m0110 >> 1] == 1;
    }
    bool keyUp(const Key& entry) {
        if (!ImGui::IsKeyReleased(entry.key)) return false;
        uint8_t& count = held_[entry.m0110 >> 1];
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
        // M0110A keypad and arrow block. These are `code << 1` like the ADB
        // keypad table above — the odd wire byte does not survive a code
        // >= $40, and MacMemory hands the code to the M0110A model, which
        // owns the $79 framing (MacInput.cpp). Gate: m0110_keypad_test.
        {ImGuiKey_LeftArrow,0x76},{ImGuiKey_RightArrow,0x78},
        {ImGuiKey_DownArrow,0x7A},{ImGuiKey_UpArrow,0x7C},
        {ImGuiKey_Keypad0,0xA4},{ImGuiKey_Keypad1,0xA6},
        {ImGuiKey_Keypad2,0xA8},{ImGuiKey_Keypad3,0xAA},
        {ImGuiKey_Keypad4,0xAC},{ImGuiKey_Keypad5,0xAE},
        {ImGuiKey_Keypad6,0xB0},{ImGuiKey_Keypad7,0xB2},
        {ImGuiKey_Keypad8,0xB6},{ImGuiKey_Keypad9,0xB8},
        {ImGuiKey_KeypadDecimal,0x82},{ImGuiKey_KeypadMultiply,0x86},
        {ImGuiKey_KeypadAdd,0x8A},{ImGuiKey_NumLock,0x8E},
        {ImGuiKey_KeypadDivide,0x96},{ImGuiKey_KeypadEnter,0x98},
        {ImGuiKey_KeypadSubtract,0x9C},{ImGuiKey_KeypadEqual,0xA2},
    };
    uint8_t held_[128]{};
};


} // namespace pom68k::gui
