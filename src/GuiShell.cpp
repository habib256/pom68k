// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "GuiShell.h"

#include "LleSession.h"
#include "MachineFactory.h"
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

void statusDot(bool ok, const char* label) {
    ImGui::PushStyleColor(ImGuiCol_Text,
                         ok ? ImVec4(0.3f, 0.85f, 0.35f, 1)
                            : ImVec4(0.9f, 0.4f, 0.35f, 1));
    ImGui::Bullet();
    ImGui::PopStyleColor();
    ImGui::TextUnformatted(label);
}

void drawAppleTalkWindow(GuiNetworkState& state) {
    if (!state.showWindow) return;
    const AtalkHub::Snapshot snapshot = state.atalk.snapshot();
    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("AppleTalk", &state.showWindow,
                      ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    if (!snapshot.attached || !state.appleTalkEnabled) {
        ImGui::TextUnformatted(
            "Pile AppleTalk interne désactivée (POM68K_APPLETALK=0).");
        ImGui::End();
        return;
    }

    bool stackOn = snapshot.cfg.stack;
    if (ImGui::Checkbox("Réseau AppleTalk actif", &stackOn))
        state.atalk.setService("stack", stackOn);
    ImGui::SameLine();
    ImGui::TextDisabled(snapshot.cableUp ? "(câble LToUDP: relié)"
                                        : "(câble LToUDP: local)");

    ImGui::SeparatorText("Noeud / routeur");
    char guest[16];
    if (snapshot.net.guestNode)
        std::snprintf(guest, sizeof guest, "%u", snapshot.net.guestNode);
    else
        std::strcpy(guest, "aucun");
    char routerLine[80];
    std::snprintf(routerLine, sizeof routerLine,
                  "Reseau 2, noeud serveur %u, zone \"%s\"", snapshot.node,
                  snapshot.zone.c_str());
    statusDot(snapshot.cfg.stack, routerLine);
    ImGui::Text("Invite vu : %s   -   trames recues %ld / emises %ld", guest,
                snapshot.net.framesIn, snapshot.net.framesOut);
    ImGui::Text("Recherches NBP servies : %ld   -   transactions ATP : %ld",
                snapshot.net.nbpLookups, snapshot.net.atpReqIn);
    if (snapshot.net.atpDupReqs || snapshot.net.atpDupPending) {
        ImGui::TextColored(
            ImVec4(0.95f, 0.75f, 0.3f, 1),
            "Retransmissions client : %ld  (dernier retard %ld ms, max %ld ms)",
            snapshot.net.atpDupReqs, snapshot.net.atpDupLagLastMs,
            snapshot.net.atpDupLagMaxMs);
        if (snapshot.net.atpDupPending)
            ImGui::TextDisabled(
                "  dont %ld pendant le service (serveur lent, pas le fil)",
                snapshot.net.atpDupPending);
        ImGui::TextDisabled(
            "  file d'injection %zu (max %zu)  -  attente max %ld ms  -  "
            "POM68K_ATALK_DEBUG=1 pour le detail",
            snapshot.wire.backlog, snapshot.wire.backlogMax,
            snapshot.wireHoldMaxMs);
    } else {
        ImGui::TextDisabled("Retransmissions client : 0 (fil sans perte)");
    }
    if (snapshot.wire.drops)
        ImGui::TextColored(
            ImVec4(0.95f, 0.5f, 0.35f, 1),
            "Debordement du fil : %ld trames  (l'invite a cesse d'ecouter "
            "assez longtemps pour saturer la file)",
            snapshot.wire.drops);

    ImGui::SeparatorText("Partage de fichiers (AppleShare / AFP)");
    bool afpOn = snapshot.cfg.afp;
    if (ImGui::Checkbox("Activer AppleShare", &afpOn))
        state.atalk.setService("afp", afpOn);
    statusDot(snapshot.afp.registered,
              "Visible dans le Sélecteur (NBP AFPServer)");
    statusDot(snapshot.afp.dirOk,
              snapshot.afp.dirOk
                  ? "Dossier partagé accessible en écriture"
                  : "Dossier partagé INTROUVABLE / lecture seule");
    ImGui::Text("Nom serveur : %s", snapshot.afp.serverName.c_str());
    ImGui::Text("Volume : %s", snapshot.afp.volName.c_str());
    ImGui::TextWrapped("Dossier hôte : %s",
                       snapshot.afp.dirPath.empty()
                           ? "(non défini)"
                           : snapshot.afp.dirPath.c_str());
    ImGui::Text("Sessions : %d%s   ·   utilisateur : %s",
                snapshot.afp.sessions,
                snapshot.afp.volMounted ? " (volume monté)" : "",
                snapshot.afp.lastUser.empty() ? "-"
                                              : snapshot.afp.lastUser.c_str());
    ImGui::Text("Dernière commande : %s   ·   lu %ld o / écrit %ld o",
                snapshot.afp.lastCmd.empty() ? "-"
                                             : snapshot.afp.lastCmd.c_str(),
                snapshot.afp.bytesRead, snapshot.afp.bytesWritten);

    ImGui::SeparatorText("Imprimante (LaserWriter / PAP)");
    bool papOn = snapshot.cfg.pap;
    if (ImGui::Checkbox("Activer l'imprimante", &papOn))
        state.atalk.setService("pap", papOn);
    statusDot(snapshot.pap.registered,
              "Visible dans le Sélecteur (NBP LaserWriter)");
    ImGui::Text("Nom : %s", snapshot.pap.printerName.c_str());
    ImGui::Text("État : %s%s", snapshot.pap.state.c_str(),
                snapshot.pap.busy ? "  (occupée)" : "");
    ImGui::Text("Travaux imprimés : %ld   ·   dernier : %s",
                snapshot.pap.jobs,
                snapshot.pap.lastJob.empty() ? "-"
                                             : snapshot.pap.lastJob.c_str());
    ImGui::TextDisabled("Spool → CUPS (lp) si présent, sinon %s/",
                        snapshot.pap.spoolDir.c_str());

    ImGui::SeparatorText("Internet (MacIP / IP-in-DDP)");
    bool ipOn = snapshot.cfg.macip;
    if (ImGui::Checkbox("Activer la passerelle MacIP", &ipOn))
        state.atalk.setService("macip", ipOn);
    statusDot(snapshot.macip.registered,
              "Passerelle visible (NBP IPGATEWAY)");
    const bool ipWorks = snapshot.macip.registered && snapshot.macip.leases > 0;
    statusDot(ipWorks, ipWorks ? "MacIP fonctionne (bail attribué)"
                               : "MacIP en attente (aucun invité connecté)");
    ImGui::Text("Passerelle : %s   ·   DNS : %s",
                snapshot.macip.gwIp.c_str(), snapshot.macip.dns.c_str());
    ImGui::Text("Baux : %d   ·   dernier : %s   ·   flux UDP %d / TCP %d",
                snapshot.macip.leases,
                snapshot.macip.lastLease.empty()
                    ? "-"
                    : snapshot.macip.lastLease.c_str(),
                snapshot.macip.udpFlows, snapshot.macip.tcpConns);
    ImGui::Text("IP invite -> net %ld   -   net -> invite %ld",
                snapshot.macip.ipFromGuest, snapshot.macip.ipToGuest);
    ImGui::TextDisabled("HTTP uniquement (TLS 2026 hors d'atteinte) — "
                        "frogfind.com, theoldnet.com");
    ImGui::End();
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
        ImGui::MenuItem("AppleTalk...", nullptr, &state_.network.showWindow,
                        state_.network.appleTalkEnabled);
        if (!state_.network.appleTalkEnabled)
            ImGui::TextDisabled("(POM68K_APPLETALK=0)");
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
