// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The menu bar and the shell's windows, without GLFW: what the user can
// reach is decided once, here, and gui_machine_window_test drives it
// headlessly (tests/ImGuiHeadless.h). GuiShell.cpp wraps this for the
// real window. The shell owns the whole menu bar. Runners bind callbacks (bindCpuMenu,
// bindMachineControls) and draw nothing into the bar themselves; what a
// user can reach is decided once, here:
//
//   Machine        run/pause/fast-forward, save state, typing, recording,
//                  « Change machine » (one submenu per board family),
//                  « Session » (save / reopen a .pomsession),
//                  drive sounds
//   Peripherals  disks, network, serial ports, controller provenance (LLE / HLE)
//   CPU            measured speed, engine choice, engine statistics
//   Windows       every secondary window, and the layout reset
//   (right edge)   LLE qualification, live speed, mouse-capture hint
//
// The catalogue lives in a submenu on purpose: 39 profiles plus their
// separators are taller than a 900 px screen, and an item appended after
// them landed under the scroll (measured under Xvfb, DEV.md § 6).

#include "GuiShellMenu.h"

#include "DiskBays.h"
#include "DockLayout.h"
#include "GuiDisplay.h"
#include "GuiEngineWindow.h"
#include "GuiMachineControls.h"
#include "GuiSerialWindow.h"
#include "GuiSessionMenu.h"
#include "LleSession.h"
#include "MachineFactory.h"
#include "NetworkWindow.h"
#include "PeripheralWindow.h"
#include "imgui.h"

#include <cstdio>
#include <cstring>

namespace pom68k::gui {
namespace {

double realtimeRatio(GuiCpuPanelState& state) {
    if (!state.speedSample) return 0.0;
    const auto [machineClock, machineHz] = state.speedSample();
    const double ratio = state.speedGauge.observe(machineClock, machineHz);
    state.speedMeasurementDone = state.speedGauge.done();
    return ratio;
}

ImVec4 speedColour(double speed) {
    return speed >= 1.0 ? ImVec4(0.3f, 0.85f, 0.35f, 1)
                        : ImVec4(0.95f, 0.75f, 0.3f, 1);
}

// « Change machine »: one submenu per catalogue group, in catalogue
// order. A profile whose ROM is absent stays visible but disabled, so the
// roster reads the same on every host.
void drawMachineCatalogue(GuiSessionState& state, SnapMachine current) {
    if (!ImGui::BeginMenu("Change machine")) return;
    const char* openGroup = nullptr;
    bool groupOpen = false;
    for (const MachineProfile& profile : kMachineProfiles) {
        if (!openGroup || std::strcmp(openGroup, profile.group) != 0) {
            if (openGroup && groupOpen) ImGui::EndMenu();
            openGroup = profile.group;
            groupOpen = ImGui::BeginMenu(profile.group);
        }
        if (!groupOpen) continue;
        const bool isCurrent = profile.snapshot == current;
        std::string path = app::MachineFactory::findPath(profile.romPath);
        if (path.empty() && profile.romCrc32)
            path = app::MachineFactory::findRomBySignature(profile.romCrc32);
        if (ImGui::MenuItem(profile.label, nullptr, isCurrent,
                            isCurrent || !path.empty()) &&
            !isCurrent) {
            state.relaunch.targetProfile = profile.snapshot;
            state.relaunch.switchArguments = {path};
            state.relaunch.closeWindow = true;
        }
    }
    if (openGroup && groupOpen) ImGui::EndMenu();
    ImGui::EndMenu();
}

void drawCpuMenu(GuiSessionState& state, double speed, bool engineLocked) {
    if (!ImGui::BeginMenu("CPU")) return;
    GuiCpuPanelState& cpu = state.cpu;
    const bool hasAcceleratedEngine = bool(cpu.setCpuEngine);
    const int engine = hasAcceleratedEngine ? cpu.getCpuEngine() : 0;
    if (cpu.speedSample) {
        ImGui::TextColored(speedColour(speed), "Speed: ×%.2f real time",
                           speed);
        ImGui::TextDisabled(
            "measured from the machine clock without changing its pace");
        ImGui::Separator();
    }
    if (ImGui::MenuItem("Interpreter (Moira)", nullptr, engine == 0,
                        hasAcceleratedEngine && !engineLocked) &&
        engine != 0)
        cpu.setCpuEngine(0);
    char label[80];
    const bool codeGenerator =
        cpu.jitBackend && std::strcmp(cpu.jitBackend, "threaded");
    std::snprintf(label, sizeof label,
                  codeGenerator ? "Accelerated engine - JIT %s"
                                : "Accelerated engine - windows (%s)",
                  cpu.jitBackend ? cpu.jitBackend : "?");
    if (ImGui::MenuItem(label, nullptr, engine == 1, hasAcceleratedEngine) &&
        engine != 1)
        cpu.setCpuEngine(1);
    ImGui::Separator();
    ImGui::MenuItem("Engine statistics...", nullptr, &cpu.showJit,
                    hasAcceleratedEngine);
    if (!hasAcceleratedEngine)
        ImGui::TextDisabled("(engine switch: 68030/68040 machines -\n"
                            "Mac II / compact Macs: interpreter only)");
    ImGui::EndMenu();
}

// Right edge of the bar: what the user glances at, never a control.
void drawMenuBarStatus(const GuiSessionState& state, double speed,
                       const lle::Registry& registry) {
    char speedText[32] = "";
    if (state.cpu.speedSample)
        std::snprintf(speedText, sizeof speedText, "×%.2f", speed);
    const char* lleText = !registry.requested() ? ""
        : registry.qualified() ? "LLE: qualified" : "LLE: NOT QUALIFIED";
    const char* hint = state.display.mouseCaptured
        ? "Mouse captured - Delete to release"
        : "Mouse: middle click / Ctrl+Alt+G / Delete";
    const float gap = ImGui::GetStyle().ItemSpacing.x * 2;
    bool showSpeed = *speedText;
    bool showLle = *lleText;
    float width = ImGui::CalcTextSize(hint).x;
    if (showSpeed) width += ImGui::CalcTextSize(speedText).x + gap;
    if (showLle) width += ImGui::CalcTextSize(lleText).x + gap;
    const float right = ImGui::GetWindowContentRegionMax().x;
    const float minimum = ImGui::GetCursorPosX() + gap;
    if (right - width < minimum) {
        const char* shorter = state.display.mouseCaptured
            ? "Delete: release mouse" : "Middle click: capture mouse";
        width += ImGui::CalcTextSize(shorter).x - ImGui::CalcTextSize(hint).x;
        hint = shorter;
    }
    if (right - width < minimum && showSpeed) {
        width -= ImGui::CalcTextSize(speedText).x + gap;
        showSpeed = false;
    }
    if (right - width < minimum && showLle) {
        width -= ImGui::CalcTextSize(lleText).x + gap;
        showLle = false;
    }
    if (right - width < minimum) return;
    ImGui::SetCursorPosX(right - width);
    if (showLle) {
        ImGui::TextColored(registry.qualified() ? ImVec4(0.3f, 0.85f, 0.35f, 1)
                                                : ImVec4(0.95f, 0.35f, 0.3f, 1),
                           "%s", lleText);
        ImGui::SameLine(0, gap);
    }
    if (showSpeed) {
        ImGui::TextColored(speedColour(speed), "%s", speedText);
        ImGui::SameLine(0, gap);
    }
    ImGui::TextDisabled("%s", hint);
}

} // namespace

void drawShellFrame(GuiSessionState& state, SnapMachine current) {
    GuiSessionState& state_ = state;
    const double speed = realtimeRatio(state_.cpu);
    if (state_.cpu.speedMeasurementDone) state_.relaunch.closeWindow = true;
    if (kioskChords(state_.display)) state_.relaunch.closeWindow = true;
    // The process-wide flag the runner's windows (Disques) ask: set the
    // frame the mode is requested, so nothing draws over the machine.
    kioskFlag().store(state_.display.kiosk, std::memory_order_relaxed);
    if (state_.display.kiosk) {
        // No bar, no dock space, no window: the machine owns the monitor.
        // A relaunch staged before the switch still goes through.
        if (state_.relaunch.showWindow) {
            state_.relaunch.showWindow = false;
            state_.relaunch.closeWindow = true;
        }
        return;
    }
    // The session's registry, carried by the same struct the Peripherals
    // window renders — one binding, not two.
    lle::Registry& registry = state_.peripherals.registry
        ? *state_.peripherals.registry : lle::processRegistry();
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Machine")) {
            drawMachineControlItems(state_.machine);
            ImGui::Separator();
            if (ImGui::MenuItem("Set Up a Macintosh..."))
                openStartupWindow(state_, current);
            drawMachineCatalogue(state_, current);
            drawSessionMenu(state_);
            const bool sounds = !state_.audio.floppySfx.isMuted();
            if (ImGui::MenuItem("Drive sounds", nullptr, sounds,
                                state_.audio.floppySfx.isLoaded())) {
                state_.audio.floppySfx.setMuted(sounds);
                state_.audio.hddSfx.setMuted(sounds);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Peripherals")) {
            diskBaysMenuItem("Disks...");
            // Always reachable: with POM68K_APPLETALK=0 and no card the
            // window says so, and is where a card gets staged for the next
            // boot.
            ImGui::MenuItem("Network: AppleTalk / Ethernet...", nullptr,
                            &state_.network.showWindow);
            if (!state_.network.appleTalkEnabled &&
                !state_.network.ethernetEnabled)
                ImGui::TextDisabled("(POM68K_APPLETALK=0, no network card)");
            ImGui::MenuItem("Serial ports...", nullptr, &state_.serial.showWindow);
            peripheralMenuItem("LLE / HLE controllers...");
            ImGui::EndMenu();
        }
        drawCpuMenu(state_, speed, registry.requested() && registry.qualified());
        drawDisplayMenu(state_.display);
        if (ImGui::BeginMenu("Windows")) {
            ImGui::MenuItem(kMachineControlWindowTitle, nullptr,
                            &state_.machine.showWindow, state_.machine.bound());
            diskBaysMenuItem(kDiskWindowTitle);
            ImGui::MenuItem(kNetworkWindowTitle, nullptr,
                            &state_.network.showWindow);
            peripheralMenuItem(kPeripheralWindowTitle);
            ImGui::MenuItem("Accelerated engine", nullptr, &state_.cpu.showJit,
                            bool(state_.cpu.setCpuEngine));
            ImGui::MenuItem(kSerialWindowTitle, nullptr, &state_.serial.showWindow);
            ImGui::MenuItem(kTypingWindowTitle, nullptr,
                            &state_.machine.typing.showWindow,
                            state_.machine.typing.bound());
            ImGui::MenuItem(kDebuggerWindowTitle, nullptr,
                            &state_.machine.debugger.showWindow,
                            state_.machine.debugger.bound());
            ImGui::Separator();
            if (ImGui::MenuItem("Reset layout"))
                dockLayoutReset();
            ImGui::EndMenu();
        }
        drawMenuBarStatus(state_, speed, registry);
        ImGui::EndMainMenuBar();
    }

    dockLayoutFrame();
    drawCrtWindow(state_.display);
    drawMachineControlWindow(state_.machine);
    drawDebuggerWindow(state_.machine.debugger);
    drawSessionWindow(state_.sessionFile);
    drawTypingWindow(state_.machine.typing);
    drawSerialWindow(state_.serial);
    drawAppleTalkWindow(state_.network);
    drawEngineWindow(state_.cpu);
    peripheralWindow(state_.peripherals);
    drawStartupWindow(state_, current);
    if (state_.relaunch.showWindow) {
        state_.relaunch.showWindow = false;
        state_.relaunch.closeWindow = true;
    }
}

} // namespace pom68k::gui
