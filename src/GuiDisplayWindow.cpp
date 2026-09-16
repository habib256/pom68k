// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The cabinet mode's chords (Ctrl+Alt+F, the quit chords), the
// « Affichage » menu and the « Réglages CRT » window — see GuiDisplay.h.
// Out of GuiShell.cpp so the shell stays the menu bar's owner and nothing
// more; ported from NeoST's kiosk and CRT controls on 2026-09-16.

#include "GuiDisplay.h"

#include "imgui.h"


namespace pom68k::gui {

// ── Cabinet mode ─────────────────────────────────────────────────────
// Ctrl+Alt+F toggles at any time (the twin of Ctrl+Alt+G, the mouse grab);
// Alt+F4 and a Ctrl+Shift+Q chord held ~0.7 s leave (an exclusive full
// screen does not always relay the window manager's close).
bool kioskChords(GuiDisplayState& d) {
    ImGuiIO& io = ImGui::GetIO();
    const bool alt = ImGui::IsKeyDown(ImGuiKey_LeftAlt) || ImGui::IsKeyDown(ImGuiKey_RightAlt);
    // The physical Ctrl key, like the Ctrl+Alt+G grab reads it from GLFW:
    // with ConfigMacOSXBehaviors ImGui swaps Ctrl and Cmd at AddKeyEvent
    // (imgui.cpp), so on macOS the key called Ctrl here is Cmd — accept
    // both, and the chord reads Ctrl+Alt+F on every host.
    const bool ctrlHeld = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl) ||
                          ImGui::IsKeyDown(ImGuiKey_LeftSuper) || ImGui::IsKeyDown(ImGuiKey_RightSuper);
    if (!io.WantTextInput && ctrlHeld && alt && ImGui::IsKeyPressed(ImGuiKey_F, false)) d.kiosk = !d.kiosk;
    if (!d.kiosk) {
        d.quitHold = 0;
        return false;
    }
    bool quit = alt && ImGui::IsKeyPressed(ImGuiKey_F4, false);
    const bool shift = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);
    d.quitHold = (ctrlHeld && shift && ImGui::IsKeyDown(ImGuiKey_Q)) ? d.quitHold + 1 : 0;
    if (d.quitHold >= 42) quit = true;   // ~0.7 s at 60 Hz
    return quit;
}

// « Affichage »: the cabinet mode and the CRT glass, presets and the
// settings window.
void drawDisplayMenu(GuiDisplayState& d) {
    if (!ImGui::BeginMenu("Affichage")) return;
    if (ImGui::MenuItem("Mode borne (plein écran)", "Ctrl+Alt+F", d.kiosk)) d.kiosk = !d.kiosk;
    ImGui::TextDisabled("Sortie : Ctrl+Alt+F, Alt+F4 ou Ctrl+Maj+Q maintenu");
    ImGui::Separator();
    ImGui::TextDisabled("Effets CRT");
    const char* labels[] = {"Aucun", "Léger", "Arcade", "Phosphore"};
    for (int i = 0; i < 4; i++) {
        const bool current = (i == 0) ? !d.crtOn : (d.crtOn && d.crtPreset == kCrtPresetNames[i]);
        if (ImGui::MenuItem(labels[i], nullptr, current)) d.selectPreset(kCrtPresetNames[i]);
    }
    ImGui::MenuItem("Réglages CRT...", nullptr, &d.showCrtWindow);
    if (const std::string err = d.crtOn && d.passError ? d.passError() : std::string(); !err.empty())
        ImGui::TextDisabled("(shader indisponible : écran brut — %s)", err.c_str());
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
    if (const std::string err = d.crtOn && d.passError ? d.passError() : std::string(); !err.empty())
        ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.35f, 1), "Shader indisponible : %s",
                           err.c_str());
    ImGui::End();
}

} // namespace pom68k::gui
