// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// GuiDisplay -- how the emulated screen is shown: the docked window of the
// desktop, or the whole monitor in cabinet ("kiosk") mode, with the CRT
// glass pass on either. Ported from NeoST's kiosk mode and CRT look on
// 2026-09-16 (docs/KIOSK.md there); what POM68K keeps of it:
//
//   • --kiosk / POM68K_KIOSK=1, or F8 at any time: exclusive full screen on
//     the primary monitor, no menu bar, no window, the mouse captured and
//     the cursor hidden. F8 again, Alt+F4 or Ctrl+Shift+Q (held ~0.7 s)
//     leave. The switch is a GLFW monitor change between two frames; the
//     machine runs on, the docked layout is kept alive for the return.
//   • --crt=<preset> / POM68K_CRT, or the Affichage menu: the CrtEffectStack
//     pass — presets light / arcade / phosphor, every slider live in
//     « Réglages CRT ». Opt-in; when the shader cannot compile the raw
//     screen is shown and the menu says so.
//
// The kiosk flag is process-wide on purpose: the windows a runner draws
// (Disques) ask it before drawing, the shell skips its bar and every
// secondary window, and the smoke scenario keeps the window hidden — a
// hidden window is never sent to a monitor.

#pragma once

#include "CrtEffectStack.h"
#include "CrtParams.h"

#include <atomic>
#include <string>

struct GLFWwindow;

namespace pom68k::gui {

// True while the cabinet mode is on. Read by every window that must not
// draw over the full-screen machine.
inline std::atomic<bool>& kioskFlag() {
    static std::atomic<bool> on{false};
    return on;
}
inline bool kioskActive() { return kioskFlag().load(std::memory_order_relaxed); }

struct GuiDisplayState {
    bool kiosk = false;              // requested state (F8, menu, startup)
    bool kioskApplied = false;       // the window is on its monitor
    int windowedX = 100, windowedY = 100, windowedW = 1280, windowedH = 800;
    int quitHold = 0;                // Ctrl+Shift+Q frames held

    bool crtOn = false;
    std::string crtPreset = "off";   // last preset applied, "custom" after a slider
    CrtParams crt{};
    CrtEffectStack stack;
    bool showCrtWindow = false;

    // The texture to draw for a srcW × srcH frame shown at dstW × dstH: the
    // CRT pass's output, or the raw texture when the pass is off or failed.
    unsigned int shown(unsigned int tex, int srcW, int srcH, int dstW, int dstH) {
        if (!crtOn || tex == 0) return tex;
        if (!stack.available()) {
            if (stack.attempted()) return tex;
            if (!stack.initialize()) return tex;
        }
        stack.setParams(crt);
        const unsigned int out = stack.process(tex, srcW, srcH, dstW, dstH);
        return out ? out : tex;
    }

    bool selectPreset(const std::string& name) {
        if (!applyCrtPreset(name, crt, crtOn)) return false;
        crtPreset = crtOn ? name : "off";
        return true;
    }
};

// Where a srcW × srcH screen lands inside an availW × availH surface, the
// pixel ratio kept and the image centred: the letterbox the kiosk and the
// docked window both use. Pure, gated by gui_windows_test.
struct Letterbox {
    float x = 0, y = 0, w = 0, h = 0;
};
inline Letterbox letterbox(float availW, float availH, float srcW, float srcH) {
    Letterbox b;
    if (availW <= 0 || availH <= 0 || srcW <= 0 || srcH <= 0) return b;
    float scale = availW / srcW;
    if (availH / srcH < scale) scale = availH / srcH;
    b.w = srcW * scale;
    b.h = srcH * scale;
    b.x = (availW - b.w) * 0.5f;
    b.y = (availH - b.h) * 0.5f;
    return b;
}

// GuiDisplayWindow.cpp: once per frame, before anything is drawn — the
// F8 / quit chords and the monitor switch (only on a visible window).
void kioskFrame(GuiDisplayState& d, GLFWwindow* window);
// The « Affichage » menu (inside an open menu bar) and the settings window.
void drawDisplayMenu(GuiDisplayState& d);
void drawCrtWindow(GuiDisplayState& d);

} // namespace pom68k::gui
