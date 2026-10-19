// PeripheralWindow -- see PeripheralWindow.h for the contract.

#include "PeripheralWindow.h"
#include "FirmwareChoice.h"

#include "imgui.h"

#include <cfloat>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace pom68k {

const char* kPeripheralWindowTitle = "Peripherals (LLE / HLE)";

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
// and "Apply" stays disabled until the user actually chooses.
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
                   ? std::string("original firmware running")
                   : "original firmware: " + d.firmware;
    case lle::Why::HleNoDump:
        return "no firmware dump found - nonconforming HLE substitute";
    case lle::Why::HleForced:
        return d.knob + "=0 - nonconforming HLE substitute forced";
    }
    return {};
}

} // namespace

void peripheralMenuItem(const char* label) {
    if (ImGui::MenuItem(label, nullptr, gOpen))
        gOpen = !gOpen;
}

void peripheralWindow(const PeripheralHost& host) {
    lle::Registry& registry = host.registry ? *host.registry
                                            : lle::processRegistry();
    const std::vector<lle::Device> devs = registry.devices();
    openIfFallback(devs);
    if (!gOpen) return;

    // Auto-height: the footer (« Apply and restart ») appears only
    // once something is staged, and a height fixed on the first frame hid
    // it behind a scrollbar (found by gui_windows_test, 2026-09-16). The
    // width is pinned so the rows keep their layout; long lines wrap to it.
    // The viewport bounds the height; longer device lists remain scrollable.
    ImGui::SetNextWindowSizeConstraints(ImVec2(560, 0),
        ImVec2(560, ImGui::GetMainViewport()->WorkSize.y - 40));
    if (!ImGui::Begin(kPeripheralWindowTitle, &gOpen,
                      ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }

    // ── Verdict first: the question the window exists to answer ──────────
    int hle = 0;
    for (const lle::Device& d : devs)
        if (d.mode == lle::Mode::Hle) hle++;
    if (devs.empty()) {
        ImGui::TextUnformatted("This machine has no devices using an "
                               "HLE substitute.");
        ImGui::TextDisabled("All its devices are already modeled at "
                            "signal level (LLE).");
    } else if (hle == 0) {
        ImGui::TextColored(kGreen, "Qualified session: %d devices, "
                                   "all using LLE.", int(devs.size()));
    } else {
        ImGui::TextColored(kOrange,
                           "%d of %d devices use a "
                           "NONCONFORMING HLE substitute.", hle, int(devs.size()));
    }

    if (hle > 0)
        ImGui::TextWrapped("A software substitute replaces firmware that is missing "
                           "or disabled. Known limitations appear "
                           "below each controller. To use the "
                           "original firmware, provide its ROM file.");

    // Product mode's own verdict, when it is the promise being made.
    if (registry.requested()) {
        ImGui::SameLine();
        const bool ok = registry.qualified();
        ImGui::TextColored(ok ? kGreen : kRed, ok ? "  [--lle-aarch64 : OK]"
                                                  : "  [--lle-aarch64: REJECTED]");
    }

    ImGui::Separator();

    // Keep the staged action visible even when the controller list scrolls.
    const int pending = lle::pendingCount(devs, gStaged);
    if (pending > 0) {
        ImGui::TextColored(kOrange, "Pending restart: %d changes", pending);
        ImGui::TextDisabled("Applying restarts this Mac. Save your work first.");
        ImGui::BeginDisabled(!host.relaunch);
        if (ImGui::Button("Apply and restart")) {
            auto overrides = lle::firmwareOverridesForSelection(devs, gStaged);
            gStaged.clear();
            host.relaunch(std::move(overrides));
        }
        ImGui::EndDisabled();
        if (!host.relaunch && ImGui::IsItemHovered())
            ImGui::SetTooltip("Relaunch is unavailable on this platform.");
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) gStaged.clear();
        ImGui::Separator();
    }

    // ── One row per LLE-capable device this machine built ────────────────
    for (const lle::Device& d : devs) {
        ImGui::PushID(int(d.module));

        badge(d.mode == lle::Mode::Lle);
        ImGui::SameLine();
        ImGui::TextUnformatted(d.name.c_str());

        ImGui::Indent();
        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 520);
        ImGui::TextDisabled("%s", reasonText(d).c_str());
        // The device's own statement of what its substitute cannot do — the
        // Toby fallback boots System 7.0 but not 7.5.5, and a user staring
        // at "Welcome to Macintosh" deserves that sentence here, not in a
        // changelog.
        if (d.mode == lle::Mode::Hle && !d.consequence.empty())
            ImGui::TextColored(kOrange, "%s", d.consequence.c_str());
        ImGui::PopTextWrapPos();

        if (d.mode == lle::Mode::Hle && !lle::dumpAvailable(d) &&
            !d.candidates.empty())
            ImGui::TextWrapped("Expected file: %s", d.candidates.front().c_str());

        // The selector. LLE is offered only when a dump exists — a radio the
        // user can click into a state the relaunch would silently undo is
        // worse than a disabled one, so the paths searched are shown instead.
        const bool canLle = lle::dumpAvailable(d);
        lle::Choice want = stagedOf(d);

        ImGui::BeginDisabled(!canLle);
        if (ImGui::RadioButton("Original firmware (LLE)",
                               want.mode == lle::Mode::Lle)) {
            want.mode = lle::Mode::Lle;
            stage(want);
        }
        ImGui::EndDisabled();
        if (!canLle && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            std::string tip = "No firmware dump found. Searched paths:";
            for (const std::string& p : d.candidates) tip += "\n  " + p;
            tip += "\n\nMCU dumps cannot be distributed:"
                   "\nplace the file and reopen this window.";
            ImGui::SetTooltip("%s", tip.c_str());
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(d.target == FirmwareTarget::Pmu);
        if (ImGui::RadioButton("Software substitute (HLE)",
                               want.mode == lle::Mode::Hle)) {
            want.mode = lle::Mode::Hle;
            stage(want);
        }
        ImGui::EndDisabled();

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
                    ? std::string("Automatic (first original candidate)")
                    : shortName(want.firmware);
            if (ImGui::BeginCombo("Dump", label.c_str())) {
                if (ImGui::Selectable("Automatic (first original "
                                      "candidate)", want.firmware.empty())) {
                    want.firmware.clear();
                    stage(want);
                }
                for (const std::string& p : dumps) {
                    // The factory parts this machine looks for are marked, so
                    // "which one is the right one" does not need the manual.
                    char row[320];
                    std::snprintf(row, sizeof row, "%s%s%s", shortName(p).c_str(),
                                  isCandidate(d, p) ? "   (original)" : "",
                                  p == d.firmware ? "   <- loaded" : "");
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
            if (ImGui::InputText("Path", custom, sizeof custom,
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                want.firmware = custom;
                stage(want);
            }
            ImGui::EndDisabled();
        }

        if (want.mode != d.mode)
            ImGui::TextColored(kOrange, "Pending restart: %s",
                               want.mode == lle::Mode::Lle
                                   ? "original firmware" : "software substitute");
        if (want.firmware != d.firmwareForced)
            ImGui::TextColored(kOrange, "-> dump: %s",
                               want.firmware.empty()
                                   ? "automatic (forced choice removed)"
                                   : want.firmware.c_str());

        ImGui::Unindent();
        ImGui::PopID();
        ImGui::Separator();
    }

    if (pending == 0 && !devs.empty()) {
        ImGui::TextDisabled("Selection matches the current session.");
    }

    ImGui::End();
}

} // namespace pom68k
