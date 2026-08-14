// PeripheralWindow -- see PeripheralWindow.h for the contract.

#include "PeripheralWindow.h"
#include "LleSession.h"

#include "imgui.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace pom68k {
namespace {

// Closed by default — but see openIfFallback() below.
bool gOpen = false;

// LLE_VS_HLE § 2's policy is that a fallback is "kept but never silent": every
// HLE entry prints a NON-CONFORMANT notice to stderr. A user who launched from
// a desktop icon never sees stderr, so for them the policy did not hold. This
// window is that notice in visible form, so it opens ITSELF the first frame a
// machine reports a substitute — once per session, never re-opening after the
// user closes it, and never on a machine that is fully LLE.
void openIfFallback(const std::vector<lle::Device>& devs) {
    static bool considered = false;
    if (considered) return;
    considered = true;
    for (const lle::Device& d : devs)
        if (d.mode == lle::Mode::Hle) { gOpen = true; return; }
}

// The staged selection. Empty until the user touches a radio button, so a
// machine whose devices are rebuilt (a relaunch) never inherits a stale
// choice: staging is dropped with the process that made it.
std::vector<lle::Choice> gStaged;

lle::Mode stagedMode(const lle::Device& d) {
    for (const lle::Choice& c : gStaged)
        if (c.module == d.module) return c.mode;
    return d.mode;                        // untouched rows track what is live
}

void stage(const lle::Device& d, lle::Mode mode) {
    for (lle::Choice& c : gStaged)
        if (c.module == d.module) { c.mode = mode; return; }
    gStaged.push_back({d.module, mode});
}

// The colour vocabulary is the AppleTalk window's: green = the conformant
// path, orange = a working but non-conformant substitute.
constexpr ImVec4 kGreen{0.30f, 0.85f, 0.35f, 1.0f};
constexpr ImVec4 kOrange{0.95f, 0.75f, 0.30f, 1.0f};
constexpr ImVec4 kRed{0.95f, 0.35f, 0.30f, 1.0f};

void badge(bool lle_) {
    ImGui::PushStyleColor(ImGuiCol_Text, lle_ ? kGreen : kOrange);
    ImGui::TextUnformatted(lle_ ? "[LLE]" : "[HLE]");
    ImGui::PopStyleColor();
}

// The one line that says why this row is where it is. Deliberately names the
// dump or the knob — a reason the user cannot act on is not a reason.
std::string reasonText(const lle::Device& d) {
    switch (d.why) {
    case lle::Why::LleFirmware:
        return d.firmware.empty()
                   ? std::string("firmware d'origine en cours d'exécution")
                   : "firmware d'origine : " + d.firmware;
    case lle::Why::HleNoDump:
        return "aucun dump trouvé — substitut HLE non conformant";
    case lle::Why::HleForced:
        return d.knob + "=0 — substitut HLE non conformant forcé";
    }
    return {};
}

} // namespace

void peripheralMenuItem() {
    if (ImGui::MenuItem("Périphériques (LLE / HLE)...", nullptr, gOpen))
        gOpen = !gOpen;
}

void peripheralWindow(const PeripheralHost& host) {
    const std::vector<lle::Device> devs = lle::devices();
    openIfFallback(devs);
    if (!gOpen) return;

    ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Périphériques (LLE / HLE)", &gOpen)) {
        ImGui::End();
        return;
    }

    // ── Verdict first: the question the window exists to answer ──────────
    int hle = 0;
    for (const lle::Device& d : devs)
        if (d.mode == lle::Mode::Hle) hle++;
    if (devs.empty()) {
        ImGui::TextUnformatted("Cette machine n'a aucun périphérique à "
                               "substitut HLE.");
        ImGui::TextDisabled("Tout ce qu'elle contient est déjà modélisé au "
                            "niveau du signal (LLE).");
    } else if (hle == 0) {
        ImGui::TextColored(kGreen, "Session conforme : %d périphérique(s), "
                                   "tous en LLE.", int(devs.size()));
    } else {
        ImGui::TextColored(kOrange,
                           "%d périphérique(s) sur %d en substitut HLE "
                           "NON CONFORMANT.", hle, int(devs.size()));
    }

    // Product mode's own verdict, when it is the promise being made.
    if (lle::requested()) {
        ImGui::SameLine();
        const bool ok = lle::qualified();
        ImGui::TextColored(ok ? kGreen : kRed, ok ? "  [--lle-aarch64 : OK]"
                                                  : "  [--lle-aarch64 : REFUSÉ]");
    }

    ImGui::Separator();

    // ── One row per LLE-capable device this machine built ────────────────
    for (const lle::Device& d : devs) {
        ImGui::PushID(int(d.module));

        badge(d.mode == lle::Mode::Lle);
        ImGui::SameLine();
        ImGui::TextUnformatted(d.name.c_str());

        ImGui::Indent();
        ImGui::TextDisabled("%s", reasonText(d).c_str());

        // The selector. LLE is offered only when a dump exists — a radio the
        // user can click into a state the relaunch would silently undo is
        // worse than a disabled one, so the paths searched are shown instead.
        const bool canLle = lle::dumpAvailable(d);
        lle::Mode want = stagedMode(d);

        ImGui::BeginDisabled(!canLle);
        if (ImGui::RadioButton("LLE (composant d'origine)",
                               want == lle::Mode::Lle))
            stage(d, lle::Mode::Lle);
        ImGui::EndDisabled();
        if (!canLle && ImGui::IsItemHovered()) {
            std::string tip = "Aucun dump trouvé. Chemins cherchés :";
            for (const std::string& p : d.candidates) tip += "\n  " + p;
            tip += "\n\nLes dumps de MCU ne sont pas distribuables : "
                   "\ndéposez le fichier puis rouvrez cette fenêtre.";
            ImGui::SetTooltip("%s", tip.c_str());
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("HLE (substitut, non conformant)",
                               want == lle::Mode::Hle))
            stage(d, lle::Mode::Hle);

        if (want != d.mode)
            ImGui::TextColored(kOrange, "→ %s au prochain démarrage (%s=%s)",
                               want == lle::Mode::Lle ? "LLE" : "HLE",
                               d.knob.c_str(),
                               want == lle::Mode::Lle ? "1" : "0");

        ImGui::Unindent();
        ImGui::PopID();
        ImGui::Separator();
    }

    // ── Footer: nothing changes without an explicit relaunch ─────────────
    const int pending = lle::pendingCount(devs, gStaged);
    if (pending > 0) {
        ImGui::TextColored(kOrange, "%d modification(s) en attente", pending);
        ImGui::TextDisabled("Les périphériques sont construits une seule fois, "
                            "avant la première instruction :\nappliquer "
                            "redémarre l'émulateur sur la même machine.");
        ImGui::BeginDisabled(!host.relaunch);
        if (ImGui::Button("Appliquer et redémarrer")) {
            for (const auto& [knob, value] : lle::envForSelection(devs, gStaged))
                setenv(knob.c_str(), value.c_str(), 1);
            gStaged.clear();
            host.relaunch();
        }
        ImGui::EndDisabled();
        if (!host.relaunch && ImGui::IsItemHovered())
            ImGui::SetTooltip("Relance indisponible sur cette plateforme.");
        ImGui::SameLine();
        if (ImGui::Button("Annuler")) gStaged.clear();
    } else if (!devs.empty()) {
        ImGui::TextDisabled("Sélection identique à la session en cours.");
    }

    ImGui::End();
}

} // namespace pom68k
