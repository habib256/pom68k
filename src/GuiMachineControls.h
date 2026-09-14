// POM68K — the one machine control surface every platform runner binds
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Before 2026-09-13 each of the six GUI runners emitted its own copy of the
// same twelve menu lines (Redémarrer, Sauver/Restaurer l'état, the
// recording pair) straight into the main menu bar, and its own copy of a
// "CPU" window with Pause / Reset / Avance rapide. Six copies drifted (Toby
// had no save-state menu, the compact runner said "x8", the VASP/RBV boards
// hid the panel entirely) and the bar itself read as a flat list of eleven
// items, five of which acted the instant they were clicked.
//
// This struct is the runner's whole contribution: a set of callbacks bound
// once at setup, exactly like GuiShell::bindCpuMenu. The shell owns the
// menu bar and the control window and renders them from here. A runner
// only supplies `drawStatus`, the family-specific status lines and controls
// (Toby's IOP cycle counters, the LC/Sonora monitor-sense buttons, the
// Duo's PG&E hold flag) that have no shared shape.

#pragma once

#include "SaveStateSlot.h"

#include <atomic>
#include <functional>
#include <string>

namespace pom68k::gui {

struct GuiMachineControls {
    std::function<void()> hardReset;
    std::function<bool()> isRunning;
    std::function<void(bool)> setRunning;
    std::function<bool()> isFastForward;
    std::function<void(bool)> setFastForward;
    SaveStateSlot* saveState = nullptr;
    std::function<bool()> recordingActive;
    std::function<void()> recordingStart;
    std::function<void()> recordingStop;
    std::function<std::string()> recordingMessage;
    // Family-specific lines drawn at the top of the control window.
    std::function<void()> drawStatus;
    // The "Tableau de bord" window, listed under Fenêtres.
    bool showWindow = true;

    bool bound() const noexcept { return bool(hardReset); }
};

// Bind a MachineHost-derived machine. Every command crosses to the machine
// thread through its queue or its atomics; nothing here touches the guest
// directly (DEV.md § 6, the GUI ↔ machine-thread contract).
template <class MachineT, class DrawStatus>
void bindMachineControls(GuiMachineControls& controls, MachineT& machine,
                         DrawStatus&& drawStatus) {
    controls.hardReset = [&machine] {
        machine.push({MachineT::Cmd::HardReset});
    };
    controls.isRunning = [&machine] {
        return machine.running.load(std::memory_order_relaxed);
    };
    controls.setRunning = [&machine](bool on) { machine.running.store(on); };
    controls.isFastForward = [&machine] {
        return machine.turbo.load(std::memory_order_relaxed);
    };
    controls.setFastForward = [&machine](bool on) { machine.turbo.store(on); };
    controls.saveState = &machine.state;
    controls.recordingActive = [&machine] { return machine.recordingActive(); };
    controls.recordingStart = [&machine] { machine.requestRecordingStart(); };
    controls.recordingStop = [&machine] { machine.requestRecordingStop(); };
    controls.recordingMessage = [&machine] { return machine.recordingMessage(); };
    controls.drawStatus = std::forward<DrawStatus>(drawStatus);
}

// The Machine menu's control block: reset, pause, fast-forward, save state
// and the input-journal recording. Call inside an open menu.
void drawMachineControlItems(GuiMachineControls& controls);

// The "Tableau de bord" window: status lines from the runner, then the
// same controls as buttons. No-op while hidden or unbound.
void drawMachineControlWindow(GuiMachineControls& controls);

// The window's title, for the Fenêtres menu.
extern const char* kMachineControlWindowTitle;

} // namespace pom68k::gui
