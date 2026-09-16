// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The cabinet mode's frame (F8, the quit chords, the monitor switch), the
// « Affichage » menu and the « Réglages CRT » window — see GuiDisplay.h.
// Out of GuiShell.cpp so the shell stays the menu bar's owner and nothing
// more; ported from NeoST's kiosk and CRT controls on 2026-09-16.

#include "GuiDisplay.h"

#include "imgui.h"

#include <GLFW/glfw3.h>

#include <cstdio>

namespace pom68k::gui {

// ── Cabinet mode ─────────────────────────────────────────────────────
// F8 toggles at any time; Alt+F4 and a Ctrl+Shift+Q chord held ~0.7 s leave
// (an exclusive full screen does not always relay the window manager's
// close). The monitor switch happens here, between two frames, and only on
// a visible window: the smoke scenario's hidden window is never sent to a
// monitor (GLFW would show it).
void kioskFrame(GuiDisplayState& d, GLFWwindow* window) {
    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F8, false)) d.kiosk = !d.kiosk;
    if (d.kiosk) {
        const bool alt = ImGui::IsKeyDown(ImGuiKey_LeftAlt) || ImGui::IsKeyDown(ImGuiKey_RightAlt);
        if (alt && ImGui::IsKeyPressed(ImGuiKey_F4, false)) glfwSetWindowShouldClose(window, GLFW_TRUE);
        const bool ctrl = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl);
        const bool shift = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);
        d.quitHold = (ctrl && shift && ImGui::IsKeyDown(ImGuiKey_Q)) ? d.quitHold + 1 : 0;
        if (d.quitHold >= 42) glfwSetWindowShouldClose(window, GLFW_TRUE);   // ~0.7 s at 60 Hz
    }
    if (d.kiosk == d.kioskApplied) {
        kioskFlag().store(d.kiosk, std::memory_order_relaxed);
        return;
    }
    const bool visible = glfwGetWindowAttrib(window, GLFW_VISIBLE) == GLFW_TRUE;
    if (d.kiosk) {
        glfwGetWindowPos(window, &d.windowedX, &d.windowedY);
        glfwGetWindowSize(window, &d.windowedW, &d.windowedH);
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
        if (visible && monitor && mode)
            glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        std::fprintf(stderr, "[kiosk] cabinet mode ON%s - F8, Alt+F4 or Ctrl+Shift+Q (held) leaves\n",
                     visible ? "" : " (window hidden: no monitor switch)");
    } else {
        if (visible)
            glfwSetWindowMonitor(window, nullptr, d.windowedX, d.windowedY,
                                 d.windowedW, d.windowedH, 0);
        std::fprintf(stderr, "[kiosk] cabinet mode OFF\n");
    }
    d.kioskApplied = d.kiosk;
    kioskFlag().store(d.kiosk, std::memory_order_relaxed);
}

// « Affichage »: the cabinet mode and the CRT glass, presets and the
// settings window.
void drawDisplayMenu(GuiDisplayState& d) {
    if (!ImGui::BeginMenu("Affichage")) return;
    if (ImGui::MenuItem("Mode borne (plein écran)", "F8", d.kiosk)) d.kiosk = !d.kiosk;
    ImGui::TextDisabled("Sortie : F8, Alt+F4 ou Ctrl+Maj+Q maintenu");
    ImGui::Separator();
    ImGui::TextDisabled("Effets CRT");
    const char* labels[] = {"Aucun", "Léger", "Arcade", "Phosphore"};
    for (int i = 0; i < 4; i++) {
        const bool current = (i == 0) ? !d.crtOn : (d.crtOn && d.crtPreset == kCrtPresetNames[i]);
        if (ImGui::MenuItem(labels[i], nullptr, current)) d.selectPreset(kCrtPresetNames[i]);
    }
    ImGui::MenuItem("Réglages CRT...", nullptr, &d.showCrtWindow);
    if (d.crtOn && d.stack.attempted() && !d.stack.available())
        ImGui::TextDisabled("(shader indisponible : écran brut — %s)", d.stack.lastError().c_str());
    ImGui::EndMenu();
}

void drawCrtWindow(GuiDisplayState& d) {
    if (!d.showCrtWindow) return;
    ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Réglages CRT", &d.showCrtWindow, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    if (ImGui::Checkbox("Effets CRT actifs", &d.crtOn) && d.crtOn && d.crtPreset == "off")
        d.crtPreset = "custom";
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", d.crtPreset.c_str());
    const char* labels[] = {"Aucun", "Léger", "Arcade", "Phosphore"};
    for (int i = 0; i < 4; i++) {
        if (i) ImGui::SameLine();
        if (ImGui::SmallButton(labels[i])) d.selectPreset(kCrtPresetNames[i]);
    }
    CrtParams& p = d.crt;
    bool touched = false;
    ImGui::PushItemWidth(220);
    touched |= ImGui::SliderFloat("Scanlines", &p.scanlines, 0.0f, 1.0f);
    touched |= ImGui::SliderFloat("Baril", &p.barrel, 0.0f, 0.25f);
    touched |= ImGui::SliderFloat("Rémanence", &p.persistence, 0.0f, 0.98f);
    int mask = int(p.shadowMask);
    const char* masks[] = {"Aucun", "Triade", "Grille", "Points"};
    if (ImGui::Combo("Masque", &mask, masks, 4)) { p.shadowMask = CrtParams::ShadowMask(mask); touched = true; }
    touched |= ImGui::SliderFloat("Force du masque", &p.shadowMaskStrength, 0.0f, 1.0f);
    touched |= ImGui::SliderFloat("Gain de luminance", &p.luminanceGain, 1.0f, 2.0f);
    touched |= ImGui::SliderFloat("Vignette", &p.centerLighting, 0.5f, 1.0f);
    touched |= ImGui::SliderFloat("Gamma phosphore", &p.phosphorGamma, 0.6f, 2.6f);
    touched |= ImGui::SliderFloat("Luminosité", &p.brightness, -0.5f, 0.5f);
    touched |= ImGui::SliderFloat("Contraste", &p.contrast, 0.5f, 1.5f);
    touched |= ImGui::SliderFloat("Saturation", &p.saturation, 0.0f, 2.0f);
    touched |= ImGui::SliderFloat("Teinte", &p.hue, -0.5f, 0.5f);
    touched |= ImGui::SliderFloat("Netteté", &p.sharpness, 0.0f, 1.0f);
    ImGui::PopItemWidth();
    if (touched) { d.crtPreset = "custom"; d.crtOn = true; }
    if (d.crtOn && d.stack.attempted() && !d.stack.available())
        ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.35f, 1), "Shader indisponible : %s",
                           d.stack.lastError().c_str());
    ImGui::End();
}

} // namespace pom68k::gui
