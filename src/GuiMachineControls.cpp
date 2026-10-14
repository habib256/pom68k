// POM68K — machine control menu block and "Dashboard" window
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

const char* kMachineControlWindowTitle = "Dashboard";

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

void drawLidStatus(const GuiMachineControls& controls) {
    if (const char* text = lidStatusText(controls))
        ImGui::TextDisabled("%s", text);
}

} // namespace

// The lid's state and, while the machine sleeps, how to wake it: a key,
// not the lid (docs/DUO_BRINGUP.md milestone 6) — so the hint says so
// rather than leaving « Open lid » to look broken. ASCII only: the
// default ImGui font has no em dash.
const char* lidStatusText(const GuiMachineControls& controls) {
    if (!controls.lidOpen) return nullptr;
    const bool open = controls.lidOpen();
    if (controls.asleep && controls.asleep())
        return open ? "Asleep - press a key to wake"
                    : "Lid closed, asleep - open the lid and press a key "
                      "to wake";
    return open ? nullptr : "Lid closed";
}

void drawMachineControlItems(GuiMachineControls& controls) {
    if (!controls.bound()) return;
    if (ImGui::MenuItem("Restart")) controls.hardReset();
    const bool running = controls.isRunning();
    if (ImGui::MenuItem(running ? "Pause" : "Resume"))
        controls.setRunning(!running);
    bool fast = controls.isFastForward();
    if (ImGui::MenuItem("Fast forward", nullptr, &fast))
        controls.setFastForward(fast);
    if (controls.lidOpen) {
        ImGui::Separator();
        const bool open = controls.lidOpen();
        if (ImGui::MenuItem(open ? "Close lid" : "Open lid"))
            controls.setLidOpen(!open);
        drawLidStatus(controls);
    }
    if (controls.saveState) {
        ImGui::Separator();
        if (ImGui::MenuItem("Save state")) controls.saveState->request(false);
        if (ImGui::MenuItem("Restore state"))
            controls.saveState->request(true);
        drawSaveStateMessage(controls);
    }
    if (controls.typing.bound()) {
        ImGui::Separator();
        ImGui::MenuItem("Type text...", nullptr, &controls.typing.showWindow);
    }
    if (controls.recordingActive) {
        ImGui::Separator();
        if (!controls.recordingActive()) {
            if (ImGui::MenuItem("Start recording"))
                controls.recordingStart();
        } else if (ImGui::MenuItem("Stop recording")) {
            controls.recordingStop();
        }
        drawRecordingMessage(controls);
    }
}

void drawMachineControlWindow(GuiMachineControls& controls) {
    if (!controls.bound() || !controls.showWindow) return;
    ImGui::SetNextWindowPos(ImVec2(20, 560), ImGuiCond_FirstUseEver);   // undocked fallback, inside a 1100x800 window
    if (!ImGui::Begin(kMachineControlWindowTitle, &controls.showWindow,
                      ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    if (controls.drawStatus) controls.drawStatus();
    drawLidStatus(controls);
    ImGui::Separator();
    const bool running = controls.isRunning();
    if (ImGui::Button(running ? "Pause" : "Resume"))
        controls.setRunning(!running);
    ImGui::SameLine();
    if (ImGui::Button("Restart")) controls.hardReset();
    ImGui::SameLine();
    bool fast = controls.isFastForward();
    if (ImGui::Checkbox("Fast forward", &fast)) controls.setFastForward(fast);
    if (controls.saveState) {
        if (ImGui::Button("Save state")) controls.saveState->request(false);
        ImGui::SameLine();
        if (ImGui::Button("Restore state"))
            controls.saveState->request(true);
        drawSaveStateMessage(controls);
    }
    ImGui::End();
}

} // namespace pom68k::gui
