// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "GuiSessionMenu.h"

#include "GuiNetworkConfig.h"
#include "SessionFile.h"
#include "imgui.h"

#include <cstdio>
#include <cstring>

namespace pom68k::gui {
namespace {

void prepareSavePath(GuiSessionFileState& session) {
    std::string path = session.current;
    if (path.empty()) {
        const MachineProfile* profile =
            session.profile ? machineProfile(*session.profile) : nullptr;
        const std::filesystem::path directory =
            app::pathFromUtf8(session.directory.empty() ? "sessions"
                                                        : session.directory);
        path = app::utf8FromPath(
            directory / app::pathFromUtf8(std::string(profile ? profile->slug
                                                               : "machine") +
                                          std::string(app::kSessionExtension)));
    }
    std::snprintf(session.savePath.data(), session.savePath.size(), "%s",
                  path.c_str());
    session.status.clear();
}

} // namespace

void bindSessionFile(GuiSessionState& state, const app::RuntimeConfig& config,
                     const std::filesystem::path& fallbackDirectory) {
    GuiSessionFileState& session = state.sessionFile;
    session.current = app::utf8FromPath(config.sessionPath());
    std::error_code error;
    session.directory = app::utf8FromPath(config.sessionPath().empty()
        ? std::filesystem::weakly_canonical(fallbackDirectory, error)
        : config.sessionPath().parent_path());
    session.capture = [&state, &config] {
        app::SessionCapture capture = app::SessionCapture::from(config);
        const GuiSessionFileState& session = state.sessionFile;
        capture.profile = session.profile;
        if (!session.romName.empty()) capture.rom = session.romName;
        if (state.cpu.getCpuEngine && state.cpu.setCpuEngine)
            capture.engine = state.cpu.getCpuEngine() == 0
                ? jit::EngineKind::Interp : jit::EngineKind::Jit;
        for (const FirmwareOverride& staged : state.relaunch.firmwareOverrides)
            for (FirmwareOverride& policy : capture.firmware)
                if (policy.target == staged.target) policy = staged;
        capture.daynaPortId = state.relaunch.daynaPortId;
        capture.appleTalk = state.network.appleTalkEnabled;
        capture.ltoUdp = state.network.ltoUdpEnabled;
        capture.network = networkConfigOf(state.network.atalk.config());
        if (capture.network.shareDirectory.empty())
            capture.network.shareDirectory = state.network.shareDirectory;
        capture.kiosk = state.display.kiosk;
        const std::string& preset = state.display.crtPreset;
        capture.crtPreset = preset == "custom" ? std::string() : preset;
        if (state.audio.floppySfx.isLoaded())
            capture.driveSounds = !state.audio.floppySfx.isMuted();
        return capture.entries();
    };
}

void openSession(GuiRelaunchState& relaunch, const std::string& file) {
    relaunch.switchArguments = {std::string(app::kSessionOption) + file};
    relaunch.verbatim = true;
    relaunch.closeWindow = true;
}

void drawSessionMenu(GuiSessionState& state) {
    if (!ImGui::BeginMenu("Session")) return;
    GuiSessionFileState& session = state.sessionFile;
    if (ImGui::MenuItem("Enregistrer la session...", nullptr, false,
                        bool(session.capture))) {
        prepareSavePath(session);
        session.showSaveWindow = true;
    }
    ImGui::Separator();
    // Listed each time the menu is open: a file saved meanwhile, or copied
    // in from elsewhere, appears without a restart.
    const auto files = app::listSessions(app::pathFromUtf8(session.directory));
    if (files.empty())
        ImGui::TextDisabled("(aucune session dans %s)", session.directory.c_str());
    for (const std::filesystem::path& file : files) {
        const std::string path = app::utf8FromPath(file);
        const std::string name = app::utf8FromPath(file.stem());
        const bool isCurrent = path == session.current;
        if (ImGui::MenuItem(name.c_str(), nullptr, isCurrent) && !isCurrent)
            openSession(state.relaunch, path);
    }
    ImGui::EndMenu();
}

void drawSessionWindow(GuiSessionFileState& session) {
    if (!session.showSaveWindow) return;
    ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(kSessionWindowTitle, &session.showSaveWindow)) {
        ImGui::End();
        return;
    }
    ImGui::TextWrapped("Le profil, la ROM et les médias de démarrage, le moteur, "
                       "le réseau, les ports série et l'affichage. L'état "
                       "d'exécution reste dans l'état sauvé (.pomss).");
    ImGui::InputText("Fichier", session.savePath.data(), session.savePath.size());
    if (ImGui::Button("Enregistrer") && session.capture) {
        const std::string path(session.savePath.data());
        const std::string error = app::writeSessionFile(
            app::pathFromUtf8(path), session.capture());
        session.status = error.empty() ? "Session enregistrée : " + path
                                       : "Échec : " + error;
    }
    if (!session.status.empty()) ImGui::TextWrapped("%s", session.status.c_str());
    ImGui::End();
}

} // namespace pom68k::gui
