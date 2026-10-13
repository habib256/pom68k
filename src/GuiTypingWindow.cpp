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
    if (ImGui::Button("Paste clipboard")) {
        const std::string pasted = typing.hostClipboard
            ? typing.hostClipboard()
            : std::string(ImGui::GetClipboardText() ? ImGui::GetClipboardText() : "");
        std::snprintf(typing.text.data(), typing.text.size(), "%s", pasted.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) typing.text[0] = 0;
    ImGui::InputTextMultiline("##texte", typing.text.data(), typing.text.size(),
                              ImVec2(-1, ImGui::GetTextLineHeight() * 8));
    // The guest's KCHR cannot be read from low memory: the user says which
    // keyboard the guest System expects.
    ImGui::TextUnformatted("Guest keyboard:");
    ImGui::SameLine();
    ImGui::RadioButton("US", &typing.layout, int(GuestLayout::Us));
    ImGui::SameLine();
    ImGui::RadioButton("French (AZERTY)", &typing.layout, int(GuestLayout::FrenchAzerty));

    const TypingPlan plan = planTyping(typing.text.data(), GuestLayout(typing.layout));
    ImGui::Text("%zu characters to type", plan.characters);
    if (plan.skipped) {
        std::string shown;
        for (const std::string& c : plan.unrepresentable) {
            if (shown.size() > 120) { shown += " ..."; break; }
            shown += (shown.empty() ? "" : " ") + c;
        }
        ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.3f, 1),
                           "%zu characters unavailable on this keyboard, skipped: %s",
                           plan.skipped, shown.c_str());
    }
    if (plan.truncated)
        ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.3f, 1),
                           "text truncated to %zu characters", kMaxTypedCharacters);

    const std::size_t left = typing.charactersLeft ? typing.charactersLeft() : 0;
    ImGui::BeginDisabled(plan.characters == 0);
    if (ImGui::Button("Type"))
        typing.type(std::string(typing.text.data()), GuestLayout(typing.layout));
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(left == 0);
    if (ImGui::Button("Cancel typing")) typing.cancel();
    ImGui::EndDisabled();
    if (left) {
        ImGui::SameLine();
        ImGui::Text("%zu characters remaining", left);
    }
    ImGui::TextDisabled("Types at machine speed (~9 characters/s).");

    if (typing.readScrap && typing.scrap) {
        ImGui::Separator();
        ImGui::TextUnformatted("Guest clipboard (TEXT)");
        if (ImGui::Button("Read guest clipboard")) {
            typing.scrapAsked = typing.scrap().second + 1;
            typing.readScrap();
        }
        const auto [scrap, reads] = typing.scrap();
        if (typing.scrapAsked && reads < typing.scrapAsked) {
            ImGui::TextDisabled("reading on the next machine timeslice...");
        } else if (typing.scrapAsked && !scrap.ok()) {
            ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.3f, 1), "%s", scrap.reason.c_str());
        } else if (typing.scrapAsked) {
            ImGui::Text("%u MacRoman bytes%s", scrap.macBytes,
                        scrap.truncated ? " - truncated" : "");
            std::string shown = scrap.utf8.substr(0, 4096);
            ImGui::InputTextMultiline("##scrap", shown.data(), shown.size() + 1,
                                      ImVec2(-1, ImGui::GetTextLineHeight() * 6),
                                      ImGuiInputTextFlags_ReadOnly);
            if (ImGui::Button("Copy to host")) {
                if (typing.setHostClipboard) typing.setHostClipboard(scrap.utf8);
                else ImGui::SetClipboardText(scrap.utf8.c_str());
            }
        }
    }
    ImGui::End();
}

} // namespace pom68k::gui
