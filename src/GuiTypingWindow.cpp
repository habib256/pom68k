// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "GuiTypingWindow.h"

#include "imgui.h"

#include <cstdio>
#include <cstring>

namespace pom68k::gui {

void drawTypingWindow(GuiTypingState& typing) {
    if (!typing.bound() || !typing.showWindow) return;
    ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(kTypingWindowTitle, &typing.showWindow)) {
        ImGui::End();
        return;
    }
    if (ImGui::Button("Coller le presse-papiers")) {
        const std::string pasted = typing.hostClipboard
            ? typing.hostClipboard()
            : std::string(ImGui::GetClipboardText() ? ImGui::GetClipboardText() : "");
        std::snprintf(typing.text.data(), typing.text.size(), "%s", pasted.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Effacer")) typing.text[0] = 0;
    ImGui::InputTextMultiline("##texte", typing.text.data(), typing.text.size(),
                              ImVec2(-1, ImGui::GetTextLineHeight() * 8));
    // The guest's KCHR cannot be read from low memory: the user says which
    // keyboard the guest System expects.
    ImGui::TextUnformatted("Clavier de l'invité :");
    ImGui::SameLine();
    ImGui::RadioButton("US", &typing.layout, int(GuestLayout::Us));
    ImGui::SameLine();
    ImGui::RadioButton("Français (AZERTY)", &typing.layout, int(GuestLayout::FrenchAzerty));

    const TypingPlan plan = planTyping(typing.text.data(), GuestLayout(typing.layout));
    ImGui::Text("%zu caractère(s) à taper", plan.characters);
    if (plan.skipped) {
        std::string shown;
        for (const std::string& c : plan.unrepresentable) {
            if (shown.size() > 120) { shown += " ..."; break; }
            shown += (shown.empty() ? "" : " ") + c;
        }
        ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.3f, 1),
                           "%zu sans touche sur ce clavier, ignoré(s) : %s",
                           plan.skipped, shown.c_str());
    }
    if (plan.truncated)
        ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.3f, 1),
                           "texte tronqué à %zu caractères", kMaxTypedCharacters);

    const std::size_t left = typing.charactersLeft ? typing.charactersLeft() : 0;
    ImGui::BeginDisabled(plan.characters == 0);
    if (ImGui::Button("Taper"))
        typing.type(std::string(typing.text.data()), GuestLayout(typing.layout));
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(left == 0);
    if (ImGui::Button("Annuler la saisie")) typing.cancel();
    ImGui::EndDisabled();
    if (left) {
        ImGui::SameLine();
        ImGui::Text("encore %zu caractère(s)", left);
    }
    ImGui::TextDisabled("Frappe au rythme de la machine (~9 caractères/s).");

    if (typing.readScrap && typing.scrap) {
        ImGui::Separator();
        ImGui::TextUnformatted("Presse-papiers de l'invité (TEXT)");
        if (ImGui::Button("Lire le presse-papiers de l'invité")) {
            typing.scrapAsked = typing.scrap().second + 1;
            typing.readScrap();
        }
        const auto [scrap, reads] = typing.scrap();
        if (typing.scrapAsked && reads < typing.scrapAsked) {
            ImGui::TextDisabled("lecture à la prochaine tranche machine...");
        } else if (typing.scrapAsked && !scrap.ok()) {
            ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.3f, 1), "%s", scrap.reason.c_str());
        } else if (typing.scrapAsked) {
            ImGui::Text("%u octet(s) MacRoman%s", scrap.macBytes,
                        scrap.truncated ? " - tronqué" : "");
            std::string shown = scrap.utf8.substr(0, 4096);
            ImGui::InputTextMultiline("##scrap", shown.data(), shown.size() + 1,
                                      ImVec2(-1, ImGui::GetTextLineHeight() * 6),
                                      ImGuiInputTextFlags_ReadOnly);
            if (ImGui::Button("Copier vers l'hôte")) {
                if (typing.setHostClipboard) typing.setHostClipboard(scrap.utf8);
                else ImGui::SetClipboardText(scrap.utf8.c_str());
            }
        }
    }
    ImGui::End();
}

} // namespace pom68k::gui
