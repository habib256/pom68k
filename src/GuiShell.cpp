// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "GuiShell.h"

#include "LleSession.h"
#include "MachineFactory.h"
#include "NetworkWindow.h"
#include "PeripheralWindow.h"

#include <algorithm>
#include <chrono>
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

void drawJitWindow(GuiCpuPanelState& state) {
    if (!state.showJit || !state.jitStats) return;
    const jit::Stats::Snapshot snapshot = state.jitStats();
    const int engine = state.getCpuEngine ? state.getCpuEngine() : 0;
    const auto now = std::chrono::steady_clock::now();
    const std::uint64_t total = snapshot.instrs + snapshot.interpInstrs;
    if (state.jitLastAt.time_since_epoch().count()) {
        const double elapsed =
            std::chrono::duration<double>(now - state.jitLastAt).count();
        if (elapsed >= 0.25) {
            state.jitRate = double(total - state.jitLastTotal) / elapsed;
            state.jitLastTotal = total;
            state.jitLastAt = now;
        }
    } else {
        state.jitLastTotal = total;
        state.jitLastAt = now;
    }

    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Moteur accéléré", &state.showJit,
                      ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    ImGui::SeparatorText("Moteur");
    statusDot(engine == 1, engine == 1 ? "Moteur accéléré actif"
                                      : "Interpréteur Moira (référence)");
    ImGui::Text("Backend : %s", state.jitBackend ? state.jitBackend : "-");
    if (engine == 1)
        ImGui::Text("Instructions/s : %.2f M", state.jitRate / 1e6);
    else
        ImGui::TextDisabled(
            "Instructions/s : — (compteurs du moteur accéléré, à l'arrêt)");

    ImGui::SeparatorText("Répartition");
    const double all = double(total ? total : 1);
    ImGui::Text("Par le moteur       : %llu  (%.1f %%)",
                static_cast<unsigned long long>(snapshot.instrs),
                100.0 * double(snapshot.instrs) / all);
    ImGui::Text("Par l'interpréteur  : %llu  (%.1f %%)",
                static_cast<unsigned long long>(snapshot.interpInstrs),
                100.0 * double(snapshot.interpInstrs) / all);
    ImGui::SeparatorText("Blocs");
    ImGui::Text("Compilés %llu   ·   vivants %llu   ·   rejoués %llu",
                static_cast<unsigned long long>(snapshot.blocksCompiled),
                static_cast<unsigned long long>(snapshot.blocksLive),
                static_cast<unsigned long long>(snapshot.blocksRun));
    const double reuse = snapshot.blocksCompiled
        ? double(snapshot.blocksRun) / double(snapshot.blocksCompiled)
        : 0.0;
    ImGui::Text("Réutilisation : %.1f rejeux par bloc compilé", reuse);
    ImGui::Text("Purges %llu   ·   invalidations %llu",
                static_cast<unsigned long long>(snapshot.flushes),
                static_cast<unsigned long long>(snapshot.invalidations));
    ImGui::SeparatorText("Fenêtre de code");
    ImGui::Text("Validations %llu   ·   refusées %llu  (%.1f %%)",
                static_cast<unsigned long long>(snapshot.windowArmed),
                static_cast<unsigned long long>(snapshot.windowFailed),
                snapshot.windowArmed
                    ? 100.0 * double(snapshot.windowFailed) /
                          double(snapshot.windowArmed)
                    : 0.0);
    ImGui::TextDisabled("Refus = I/O, VRAM, overlay encore actif, ou page pas "
                        "encore dans l'ATC.");
    ImGui::SeparatorText("Sorties de bloc (par cause)");
    for (int i = 0; i < int(jit::Exit::Count); ++i) {
        if (!snapshot.exits[i]) continue;
        ImGui::Text("%-16s %llu", jit::exitName(jit::Exit(i)),
                    static_cast<unsigned long long>(snapshot.exits[i]));
    }
    ImGui::TextDisabled("Toute sortie se fait à une frontière d'instruction, "
                        "état invité exact.");
    ImGui::End();
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

void GuiShell::drawSaveState(SaveStateSlot& state) const {
    if (ImGui::Button("Sauver l'état")) state.request(false);
    ImGui::SameLine();
    if (ImGui::Button("Restaurer l'état")) state.request(true);
    const std::string message = state.message();
    if (!message.empty()) ImGui::TextWrapped("%s", message.c_str());
}

void GuiShell::drawMachineMenuImpl(
    SnapMachine current, GLFWwindow* window,
    const std::function<void()>& extraMenus) {
    const double speed = realtimeRatio(state_.cpu);
    if (state_.cpu.speedMeasurementDone)
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    if (!ImGui::BeginMainMenuBar()) return;
    // The session's registry, carried by the same struct the Périphériques
    // window renders — one binding, not two.
    lle::Registry& lleReg = state_.peripherals.registry
        ? *state_.peripherals.registry : lle::processRegistry();
    if (lleReg.requested()) {
        const bool qualified = lleReg.qualified();
        ImGui::TextColored(qualified ? ImVec4(0.3f, 0.85f, 0.35f, 1)
                                     : ImVec4(0.95f, 0.35f, 0.3f, 1),
                           qualified ? "LLE AArch64 : QUALIFIÉ"
                                     : "LLE AArch64 : NON QUALIFIÉ");
        ImGui::Separator();
    }
    if (ImGui::BeginMenu("Machine")) {
        const char* lastGroup = nullptr;
        for (const MachineProfile& profile : kMachineProfiles) {
            if (!lastGroup || std::strcmp(lastGroup, profile.group) != 0) {
                ImGui::SeparatorText(profile.group);
                lastGroup = profile.group;
            }
            const bool isCurrent = profile.snapshot == current;
            std::string path = app::MachineFactory::findPath(profile.romPath);
            if (path.empty() && profile.romCrc32)
                path = app::MachineFactory::findRomBySignature(
                    profile.romCrc32);
            if (ImGui::MenuItem(profile.label, nullptr, isCurrent,
                                isCurrent || !path.empty()) &&
                !isCurrent) {
                state_.relaunch.targetProfile = profile.snapshot;
                state_.relaunch.switchArguments = {path};
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
        }
        ImGui::Separator();
        const bool sounds = !state_.audio.floppySfx.isMuted();
        if (ImGui::MenuItem("Sons des lecteurs", nullptr, sounds,
                            state_.audio.floppySfx.isLoaded())) {
            state_.audio.floppySfx.setMuted(sounds);
            state_.audio.hddSfx.setMuted(sounds);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("CPU")) {
        const bool hasAcceleratedEngine = bool(state_.cpu.setCpuEngine);
        const int engine = hasAcceleratedEngine ? state_.cpu.getCpuEngine() : 0;
        if (state_.cpu.speedSample) {
            ImGui::TextColored(
                speed >= 1.0 ? ImVec4(0.3f, 0.85f, 0.35f, 1)
                             : ImVec4(0.95f, 0.75f, 0.3f, 1),
                "Vitesse : ×%.2f temps réel", speed);
            ImGui::TextDisabled(
                "mesurée sur l'horloge machine, sans modifier son rythme");
            ImGui::Separator();
        }
        lle::Registry& reg = state_.peripherals.registry
            ? *state_.peripherals.registry : lle::processRegistry();
        const bool engineLocked = reg.requested() && reg.qualified();
        if (ImGui::MenuItem("Interpréteur (Moira)", nullptr, engine == 0,
                            hasAcceleratedEngine && !engineLocked) &&
            engine != 0)
            state_.cpu.setCpuEngine(0);
        char label[80];
        const bool codeGenerator = state_.cpu.jitBackend &&
            std::strcmp(state_.cpu.jitBackend, "threaded");
        std::snprintf(label, sizeof label,
                      codeGenerator ? "Moteur accéléré — JIT %s"
                                    : "Moteur accéléré — fenêtres (%s)",
                      state_.cpu.jitBackend ? state_.cpu.jitBackend : "?");
        if (ImGui::MenuItem(label, nullptr, engine == 1,
                            hasAcceleratedEngine) &&
            engine != 1)
            state_.cpu.setCpuEngine(1);
        ImGui::Separator();
        ImGui::MenuItem("Statistiques du moteur...", nullptr,
                        &state_.cpu.showJit, hasAcceleratedEngine);
        if (!hasAcceleratedEngine)
            ImGui::TextDisabled(
                "(interrupteur : machines 68030/68040 —\n"
                "Mac II / compacts : interpréteur seul)");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Réseau")) {
        // Always reachable: with POM68K_APPLETALK=0 and no card the window
        // says so, and is where a card gets staged for the next boot.
        ImGui::MenuItem("AppleTalk / Ethernet...", nullptr,
                        &state_.network.showWindow);
        if (!state_.network.appleTalkEnabled && !state_.network.ethernetEnabled)
            ImGui::TextDisabled("(POM68K_APPLETALK=0, aucune carte réseau)");
        ImGui::EndMenu();
    }
    peripheralMenuItem();
    dockLayoutMenu();
    if (extraMenus) extraMenus();
    ImGui::TextDisabled(
        "|  Clic molette / Ctrl+Alt+G (ou Suppr) : capture souris");
    ImGui::EndMainMenuBar();

    dockLayoutFrame();
    drawAppleTalkWindow(state_.network);
    drawJitWindow(state_.cpu);
    peripheralWindow(state_.peripherals);
    if (state_.relaunch.showWindow) {
        state_.relaunch.showWindow = false;
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
}

} // namespace pom68k::gui
