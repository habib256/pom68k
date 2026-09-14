// POM68K — machine control menu block and "Tableau de bord" window
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// See GuiMachineControls.h. Two rendering rules, both inherited from the
// runners this replaces: the item shown follows the machine's OWN state
// (running, turbo, recordingActive — set by the machine thread), never the
// click; and the line under a request is that subsystem's last outcome,
// the save-state row's convention.

#include "GuiMachineControls.h"

#include "imgui.h"

namespace pom68k::gui {

const char* kMachineControlWindowTitle = "Tableau de bord";

namespace {

void drawSaveStateMessage(const GuiMachineControls& controls) {
    if (!controls.saveState) return;
    const std::string message = controls.saveState->message();
    if (!message.empty()) ImGui::TextWrapped("%s", message.c_str());
}

void drawRecordingMessage(const GuiMachineControls& controls) {
    if (!controls.recordingMessage) return;
    const std::string message = controls.recordingMessage();
    if (!message.empty()) ImGui::TextDisabled("%s", message.c_str());
}

} // namespace

void drawMachineControlItems(GuiMachineControls& controls) {
    if (!controls.bound()) return;
    if (ImGui::MenuItem("Redémarrer")) controls.hardReset();
    const bool running = controls.isRunning();
    if (ImGui::MenuItem(running ? "Pause" : "Reprendre"))
        controls.setRunning(!running);
    bool fast = controls.isFastForward();
    if (ImGui::MenuItem("Avance rapide", nullptr, &fast))
        controls.setFastForward(fast);
    if (controls.saveState) {
        ImGui::Separator();
        if (ImGui::MenuItem("Sauver l'état")) controls.saveState->request(false);
        if (ImGui::MenuItem("Restaurer l'état"))
            controls.saveState->request(true);
        drawSaveStateMessage(controls);
    }
    if (controls.recordingActive) {
        ImGui::Separator();
        if (!controls.recordingActive()) {
            if (ImGui::MenuItem("Démarrer l'enregistrement"))
                controls.recordingStart();
        } else if (ImGui::MenuItem("Arrêter l'enregistrement")) {
            controls.recordingStop();
        }
        drawRecordingMessage(controls);
    }
}

void drawMachineControlWindow(GuiMachineControls& controls) {
    if (!controls.bound() || !controls.showWindow) return;
    ImGui::SetNextWindowPos(ImVec2(20, 830), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(kMachineControlWindowTitle, &controls.showWindow,
                      ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    if (controls.drawStatus) controls.drawStatus();
    ImGui::Separator();
    const bool running = controls.isRunning();
    if (ImGui::Button(running ? "Pause" : "Reprendre"))
        controls.setRunning(!running);
    ImGui::SameLine();
    if (ImGui::Button("Redémarrer")) controls.hardReset();
    ImGui::SameLine();
    bool fast = controls.isFastForward();
    if (ImGui::Checkbox("Avance rapide", &fast)) controls.setFastForward(fast);
    if (controls.saveState) {
        if (ImGui::Button("Sauver l'état")) controls.saveState->request(false);
        ImGui::SameLine();
        if (ImGui::Button("Restaurer l'état"))
            controls.saveState->request(true);
        drawSaveStateMessage(controls);
    }
    ImGui::End();
}

} // namespace pom68k::gui
