// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The shell owns the whole menu bar. Runners bind callbacks (bindCpuMenu,
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
#include "GuiEngineWindow.h"
#include "GuiMachineControls.h"
#include "LleSession.h"
#include "MachineFactory.h"
#include "NetworkWindow.h"
#include "PeripheralWindow.h"

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

// « Changer de machine »: one submenu per catalogue group, in catalogue
// order. A profile whose ROM is absent stays visible but disabled, so the
// roster reads the same on every host.
void drawMachineCatalogue(GuiSessionState& state, SnapMachine current,
                          GLFWwindow* window) {
    if (!ImGui::BeginMenu("Changer de machine")) return;
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
            glfwSetWindowShouldClose(window, GLFW_TRUE);
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
        ImGui::TextColored(speedColour(speed), "Vitesse : ×%.2f temps réel",
                           speed);
        ImGui::TextDisabled(
            "mesurée sur l'horloge machine, sans modifier son rythme");
        ImGui::Separator();
    }
    if (ImGui::MenuItem("Interpréteur (Moira)", nullptr, engine == 0,
                        hasAcceleratedEngine && !engineLocked) &&
        engine != 0)
        cpu.setCpuEngine(0);
    char label[80];
    const bool codeGenerator =
        cpu.jitBackend && std::strcmp(cpu.jitBackend, "threaded");
    std::snprintf(label, sizeof label,
                  codeGenerator ? "Moteur accéléré - JIT %s"
                                : "Moteur accéléré - fenêtres (%s)",
                  cpu.jitBackend ? cpu.jitBackend : "?");
    if (ImGui::MenuItem(label, nullptr, engine == 1, hasAcceleratedEngine) &&
        engine != 1)
        cpu.setCpuEngine(1);
    ImGui::Separator();
    ImGui::MenuItem("Statistiques du moteur...", nullptr, &cpu.showJit,
                    hasAcceleratedEngine);
    if (!hasAcceleratedEngine)
        ImGui::TextDisabled("(interrupteur : machines 68030/68040 -\n"
                            "Mac II / compacts : interpréteur seul)");
    ImGui::EndMenu();
}

// Right edge of the bar: what the user glances at, never a control.
void drawMenuBarStatus(const GuiSessionState& state, double speed,
                       const lle::Registry& registry) {
    char speedText[32] = "";
    if (state.cpu.speedSample)
        std::snprintf(speedText, sizeof speedText, "×%.2f", speed);
    const char* lleText = !registry.requested() ? ""
        : registry.qualified() ? "LLE : qualifié" : "LLE : NON QUALIFIÉ";
    const char* hint = "Clic molette / Ctrl+Alt+G / Suppr : capture souris";
    const float gap = ImGui::GetStyle().ItemSpacing.x * 2;
    float width = ImGui::CalcTextSize(hint).x;
    if (*speedText) width += ImGui::CalcTextSize(speedText).x + gap;
    if (*lleText) width += ImGui::CalcTextSize(lleText).x + gap;
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - width);
    if (*lleText) {
        ImGui::TextColored(registry.qualified() ? ImVec4(0.3f, 0.85f, 0.35f, 1)
                                                : ImVec4(0.95f, 0.35f, 0.3f, 1),
                           "%s", lleText);
        ImGui::SameLine(0, gap);
    }
    if (*speedText) {
        ImGui::TextColored(speedColour(speed), "%s", speedText);
        ImGui::SameLine(0, gap);
    }
    ImGui::TextDisabled("%s", hint);
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

void GuiShell::drawMachineMenu(SnapMachine current, GLFWwindow* window) {
    const double speed = realtimeRatio(state_.cpu);
    if (state_.cpu.speedMeasurementDone)
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    kioskFrame(state_.display, window);
    if (state_.display.kiosk) {
        // No bar, no dock space, no window: the machine owns the monitor.
        // A relaunch staged before the switch still goes through.
        if (state_.relaunch.showWindow) {
            state_.relaunch.showWindow = false;
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
        return;
    }
    // The session's registry, carried by the same struct the Périphériques
    // window renders — one binding, not two.
    lle::Registry& registry = state_.peripherals.registry
        ? *state_.peripherals.registry : lle::processRegistry();
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Machine")) {
            drawMachineControlItems(state_.machine);
            ImGui::Separator();
            drawMachineCatalogue(state_, current, window);
            const bool sounds = !state_.audio.floppySfx.isMuted();
            if (ImGui::MenuItem("Sons des lecteurs", nullptr, sounds,
                                state_.audio.floppySfx.isLoaded())) {
                state_.audio.floppySfx.setMuted(sounds);
                state_.audio.hddSfx.setMuted(sounds);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Périphériques")) {
            diskBaysMenuItem("Disques...");
            // Always reachable: with POM68K_APPLETALK=0 and no card the
            // window says so, and is where a card gets staged for the next
            // boot.
            ImGui::MenuItem("Réseau : AppleTalk / Ethernet...", nullptr,
                            &state_.network.showWindow);
            if (!state_.network.appleTalkEnabled &&
                !state_.network.ethernetEnabled)
                ImGui::TextDisabled("(POM68K_APPLETALK=0, aucune carte réseau)");
            peripheralMenuItem("Contrôleurs LLE / HLE...");
            ImGui::EndMenu();
        }
        drawCpuMenu(state_, speed, registry.requested() && registry.qualified());
        drawDisplayMenu(state_.display);
        if (ImGui::BeginMenu("Fenêtres")) {
            ImGui::MenuItem(kMachineControlWindowTitle, nullptr,
                            &state_.machine.showWindow, state_.machine.bound());
            diskBaysMenuItem(kDiskWindowTitle);
            ImGui::MenuItem(kNetworkWindowTitle, nullptr,
                            &state_.network.showWindow);
            peripheralMenuItem(kPeripheralWindowTitle);
            ImGui::MenuItem("Moteur accéléré", nullptr, &state_.cpu.showJit,
                            bool(state_.cpu.setCpuEngine));
            ImGui::Separator();
            if (ImGui::MenuItem("Réinitialiser la disposition"))
                dockLayoutReset();
            ImGui::EndMenu();
        }
        drawMenuBarStatus(state_, speed, registry);
        ImGui::EndMainMenuBar();
    }

    dockLayoutFrame();
    drawCrtWindow(state_.display);
    drawMachineControlWindow(state_.machine);
    drawAppleTalkWindow(state_.network);
    drawEngineWindow(state_.cpu);
    peripheralWindow(state_.peripherals);
    if (state_.relaunch.showWindow) {
        state_.relaunch.showWindow = false;
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
}

} // namespace pom68k::gui
