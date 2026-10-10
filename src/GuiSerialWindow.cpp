// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "GuiSerialWindow.h"

#include "imgui.h"

#include <cstdio>
#include <string>

namespace pom68k::gui {
namespace {

void drawPort(GuiSerialPortState& port, int index) {
    ImGui::PushID(index);
    ImGui::SeparatorText(port.name);
    if (port.requested.empty()) {
        ImGui::TextDisabled("non configuré - POM68K_SERIAL_%s=pty | tcp:<port> | terminal,",
                            index == 0 ? "PRINTER" : "MODEM");
        ImGui::TextDisabled("ou la clé serial-%s d'une session", index == 0 ? "printer" : "modem");
        ImGui::PopID();
        return;
    }
    if (!port.message.empty() || !port.terminal) {
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.3f, 1), "%s : %s", port.requested.c_str(),
                           port.message.empty() ? "non ouvert" : port.message.c_str());
        ImGui::PopID();
        return;
    }
    SerialTerminal& terminal = *port.terminal;
    const SerialTerminal::View view = terminal.view();
    if (terminal.endpoint()) {
        ImGui::TextUnformatted("Point de terminaison : ce terminal (aucun pont hôte)");
    } else {
        ImGui::Text("Point de terminaison : %s - %s", port.endpoint.c_str(),
                    view.connected ? "client connecté" : "en attente d'un client");
        ImGui::TextDisabled("Ce terminal observe seulement : il ne prend aucun octet au pont.");
    }
    ImGui::Text("invité -> hôte %llu octet(s), hôte -> invité %llu octet(s)",
                (unsigned long long)view.fromGuest, (unsigned long long)view.toGuest);
    if (view.transportDropped)
        ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.3f, 1),
                           "%llu octet(s) perdus côté hôte (aucun client, ou file pleine)",
                           (unsigned long long)view.transportDropped);
    if (view.logDropped)
        ImGui::TextDisabled("(%llu octet(s) plus anciens hors de l'historique)",
                            (unsigned long long)view.logDropped);
    std::string shown = view.text;
    ImGui::InputTextMultiline("##log", shown.data(), shown.size() + 1,
                              ImVec2(-1, ImGui::GetTextLineHeight() * 10),
                              ImGuiInputTextFlags_ReadOnly);
    if (ImGui::Button("Effacer l'historique")) terminal.clearLog();
    if (terminal.endpoint()) {
        ImGui::SetNextItemWidth(-160);
        const bool enter = ImGui::InputText("##line", port.line.data(), port.line.size(),
                                            ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if (ImGui::Button("Envoyer") || enter) {
            std::string bytes(port.line.data());
            if (port.appendCr) bytes += '\r';
            const std::size_t taken = terminal.send(bytes);
            port.sendStatus = taken == bytes.size() ? std::string()
                : "file pleine : " + std::to_string(bytes.size() - taken) +
                      " octet(s) refusé(s)";
            if (taken == bytes.size()) port.line[0] = 0;
        }
        ImGui::SameLine();
        ImGui::Checkbox("CR", &port.appendCr);
        if (view.pendingInput)
            ImGui::TextDisabled("%zu octet(s) en attente que l'invité les lise",
                                view.pendingInput);
        if (!port.sendStatus.empty())
            ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.3f, 1), "%s", port.sendStatus.c_str());
    }
    ImGui::PopID();
}

} // namespace

void drawSerialWindow(GuiSerialState& serial) {
    if (!serial.showWindow) return;
    ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(kSerialWindowTitle, &serial.showWindow)) {
        ImGui::End();
        return;
    }
    for (int i = 0; i < 2; ++i) drawPort(serial.ports[std::size_t(i)], i);
    ImGui::End();
}

} // namespace pom68k::gui
