// PeripheralWindow -- see PeripheralWindow.h for the contract.

#include "PeripheralWindow.h"
#include "FirmwareChoice.h"

#include "imgui.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
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

// Untouched rows track what is live, so opening the window stages nothing
// and "Appliquer" stays disabled until the user actually chooses.
lle::Choice stagedOf(const lle::Device& d) {
    for (const lle::Choice& c : gStaged)
        if (c.module == d.module) return c;
    return {d.module, d.mode, d.firmwareForced};
}

void stage(const lle::Choice& c) {
    for (lle::Choice& e : gStaged)
        if (e.module == c.module) { e = c; return; }
    gStaged.push_back(c);
}

// Dumps offered for a device, discovered once per session: a directory scan
// per frame would be a syscall storm behind a window nobody is looking at,
// and dropping a file in while the picker is open is not a case worth
// paying for (the row says where to put it; reopening rescans).
const std::vector<std::string>& dumpsFor(const lle::Device& d) {
    static std::map<std::uint32_t, std::vector<std::string>> cache;
    auto it = cache.find(std::uint32_t(d.module));
    if (it == cache.end())
        it = cache.emplace(std::uint32_t(d.module),
                           fw::discoverDumps(d.candidates)).first;
    return it->second;
}

// Just the filename, which is what identifies an MCU part to a reader —
// "341s0417.bin" says more than "../roms/cuda/341s0417.bin" repeated eight
// times down a combo.
std::string shortName(const std::string& path) {
    return std::filesystem::path(path).filename().string();
}

bool isCandidate(const lle::Device& d, const std::string& path) {
    for (const std::string& c : d.candidates)
        if (shortName(c) == shortName(path)) return true;
    return false;
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
        lle::Choice want = stagedOf(d);

        ImGui::BeginDisabled(!canLle);
        if (ImGui::RadioButton("LLE (composant d'origine)",
                               want.mode == lle::Mode::Lle)) {
            want.mode = lle::Mode::Lle;
            stage(want);
        }
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
                               want.mode == lle::Mode::Hle)) {
            want.mode = lle::Mode::Hle;
            stage(want);
        }

        // ── Which dump. Only meaningful on the LLE side, and only when the
        // device HAS a path knob — so the control is disabled rather than
        // hidden: a picker that vanishes when you choose HLE reads like a
        // bug, one that greys out reads like the truth.
        if (!d.pathKnob.empty()) {
            const std::vector<std::string>& dumps = dumpsFor(d);
            ImGui::BeginDisabled(want.mode != lle::Mode::Lle);
            ImGui::SetNextItemWidth(330);
            const std::string label =
                want.firmware.empty()
                    ? std::string("Automatique (premier de la liste d'origine)")
                    : shortName(want.firmware);
            if (ImGui::BeginCombo("Dump", label.c_str())) {
                if (ImGui::Selectable("Automatique (premier de la liste "
                                      "d'origine)", want.firmware.empty())) {
                    want.firmware.clear();
                    stage(want);
                }
                for (const std::string& p : dumps) {
                    // The factory parts this machine looks for are marked, so
                    // "which one is the right one" does not need the manual.
                    char row[320];
                    std::snprintf(row, sizeof row, "%s%s%s", shortName(p).c_str(),
                                  isCandidate(d, p) ? "   (d'origine)" : "",
                                  p == d.firmware ? "   ← chargé" : "");
                    if (ImGui::Selectable(row, want.firmware == p)) {
                        want.firmware = p;
                        stage(want);
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.c_str());
                }
                ImGui::EndCombo();
            }
            // A dump that lives nowhere near roms/ — the diagnostic path the
            // env knob always allowed, now reachable without a shell.
            char custom[512];
            std::snprintf(custom, sizeof custom, "%s", want.firmware.c_str());
            ImGui::SetNextItemWidth(330);
            if (ImGui::InputText("Chemin", custom, sizeof custom,
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                want.firmware = custom;
                stage(want);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", d.pathKnob.c_str());
            ImGui::EndDisabled();
        }

        if (want.mode != d.mode)
            ImGui::TextColored(kOrange, "→ %s au prochain démarrage (%s=%s)",
                               want.mode == lle::Mode::Lle ? "LLE" : "HLE",
                               d.knob.c_str(),
                               want.mode == lle::Mode::Lle ? "1" : "0");
        if (want.firmware != d.firmwareForced)
            ImGui::TextColored(kOrange, "→ dump : %s",
                               want.firmware.empty()
                                   ? "automatique (choix imposé retiré)"
                                   : want.firmware.c_str());

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
            auto overrides =
                lle::firmwareOverridesForSelection(devs, gStaged);
            gStaged.clear();
            host.relaunch(std::move(overrides));
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
