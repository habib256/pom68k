// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// See GuiAbstractionWindow.h. Strings drawn here stay ASCII: ImGui's default
// font has no em-dash or arrows (gui_windows_test scans this file).

#include "GuiAbstractionWindow.h"

#include "FirmwareManifest.h"
#include "GuiSessionState.h"
#include "imgui.h"

#include <cctype>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <string>

namespace pom68k::gui {

const char* kAbstractionWindowTitle = "Abstraction Levels (LLE / HLE)";

namespace {

using abstraction::Entry;
using abstraction::Level;
using abstraction::LiveRow;

// The Peripherals window's vocabulary, extended: green is the conformant
// path, orange a working substitute, red a hack; the two simplified/host
// classes are neither good nor bad news and get neutral hues.
ImVec4 levelColour(Level level) {
    switch (level) {
    case Level::PureLle: return {0.30f, 0.85f, 0.35f, 1.0f};
    case Level::LleSimplified: return {0.45f, 0.70f, 0.95f, 1.0f};
    case Level::HleReplacement: return {0.95f, 0.75f, 0.30f, 1.0f};
    case Level::HleHack: return {0.95f, 0.35f, 0.30f, 1.0f};
    case Level::HostConvenience: return {0.65f, 0.65f, 0.65f, 1.0f};
    }
    return {1, 1, 1, 1};
}

constexpr ImVec4 kOrange{0.95f, 0.75f, 0.30f, 1.0f};

// A filled chip: the class is the column the eye scans, and a chip separates
// from prose where a coloured word does not. ImGui has no badge primitive.
void chip(Level level) {
    const ImVec4 c = levelColour(level);
    const char* text = abstraction::levelBadge(level);
    const ImVec2 size = ImGui::CalcTextSize(text);
    const ImVec2 pad(ImGui::GetStyle().FramePadding.x * 0.75f, 1.0f);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + size.x + pad.x * 2, p0.y + size.y + pad.y * 2);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(p0, p1, ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, 0.20f)), 3.0f);
    draw->AddRect(p0, p1, ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, 0.60f)), 3.0f);
    draw->AddText(ImVec2(p0.x + pad.x, p0.y + pad.y), ImGui::GetColorU32(c), text);
    ImGui::Dummy(ImVec2(p1.x - p0.x, p1.y - p0.y));
}

const char* levelTip(Level level) {
    switch (level) {
    case Level::PureLle:
        return "Pure LLE: registers, protocol and timing from primary sources,\n"
               "ROM traces or real firmware running on a real core.";
    case Level::LleSimplified:
        return "LLE simplified: functional, not exact. Every gap and what\n"
               "would close it is listed in docs/LLE_VS_HLE.md sec. 1.";
    case Level::HleReplacement:
        return "HLE replacement: a whole device replaced at command level.\n"
               "Fallback-only: used when a firmware dump is missing or disabled.";
    case Level::HleHack:
        return "HLE hack: the emulator reaches into the guest. None runs on a\n"
               "default path; the last one lives inside an HLE fallback.";
    case Level::HostConvenience:
        return "Host convenience: no guest-visible hardware effect\n"
               "(media formats, host audio, bridges, save states).";
    }
    return "";
}

std::string platformsText(const Entry& e) {
    std::string out;
    const char* lastGroup = nullptr;
    for (const MachineProfile& p : kMachineProfiles) {
        if (!(e.platforms & abstraction::platformBit(p.platform))) continue;
        bool excepted = false;
        for (SnapMachine m : e.exceptProfiles) excepted |= m == p.snapshot;
        if (excepted) continue;
        if (lastGroup && std::strcmp(lastGroup, p.group) == 0) {
            out += ", ";
        } else {
            if (lastGroup) out += "\n";
            lastGroup = p.group;
        }
        out += p.label;
    }
    return out;
}

std::string manifestText(const Entry& e) {
    std::string out;
    for (std::string_view label : e.manifest)
        for (const firmware::Entry& m : firmware::kManifest)
            if (label == m.label) {
                if (!out.empty()) out += "\n";
                out += "  " + std::string(m.label) + ": " + m.path;
            }
    return out;
}

bool matches(const Entry& e, const char* needle) {
    if (!needle[0]) return true;
    auto lower = [](std::string s) {
        for (char& c : s) c = char(std::tolower((unsigned char)c));
        return s;
    };
    const std::string hay = lower(std::string(e.id) + " " + e.group + " " +
                                  e.subsystem + " " + e.modelled + " " + e.files);
    return hay.find(lower(needle)) != std::string::npos;
}

void entryTooltip(const Entry& e, const LiveRow& live) {
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
    ImGui::TextColored(levelColour(e.level), "%s - %s", abstraction::levelBadge(e.level),
                       abstraction::levelName(e.level));
    if (e.hasFallback)
        ImGui::TextColored(levelColour(e.fallback), "Without its dump: %s",
                           abstraction::levelName(e.fallback));
    else if (e.live == abstraction::LiveSource::Firmware)
        ImGui::TextDisabled("Firmware required: there is no substitute.");
    ImGui::Separator();
    ImGui::TextWrapped("%s", e.modelled);
    ImGui::Spacing();
    ImGui::TextWrapped("Why not lower: %s", e.whyNot);
    if (!live.detail.empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped("Now: %s", live.detail.c_str());
    }
    const std::string dumps = manifestText(e);
    if (!dumps.empty()) {
        ImGui::Spacing();
        ImGui::TextUnformatted("Dumps verified in strict LLE mode:");
        ImGui::TextUnformatted(dumps.c_str());
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Files: %s", e.files);
    ImGui::TextDisabled("docs/LLE_VS_HLE.md: %s",
                        std::strcmp(e.docSection, "-") ? e.docSection
                                                       : "master table only");
    ImGui::Separator();
    ImGui::TextDisabled("%s", platformsText(e).c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

} // namespace

void drawAbstractionWindow(GuiSessionState& state, SnapMachine current) {
    if (!state.abstraction.showWindow) return;
    abstraction::SessionView view;
    view.machine = current;
    const lle::Registry& registry = state.peripherals.registry
        ? *state.peripherals.registry : lle::processRegistry();
    view.devices = registry.devices();
    view.strictRequested = registry.requested();
    view.strictQualified = registry.qualified();
    // The CPU menu's own reading: no engine binding means the interpreter.
    const bool accelerated = bool(state.cpu.setCpuEngine);
    view.engine = accelerated && state.cpu.getCpuEngine ? state.cpu.getCpuEngine() : 0;
    if (state.cpu.jitBackend) view.backend = state.cpu.jitBackend;
    drawAbstractionWindow(state.abstraction, view);
}

void drawAbstractionWindow(GuiAbstractionState& state,
                           const abstraction::SessionView& view) {
    if (!state.showWindow) return;
    ImGui::SetNextWindowSize(ImVec2(760, 560), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(kAbstractionWindowTitle, &state.showWindow)) {
        ImGui::End();
        return;
    }

    // ── What machine, and the strict-mode verdict ───────────────────────
    const MachineProfile* profile =
        view.machine ? abstraction::profileFor(*view.machine) : nullptr;
    ImGui::Text("Machine: %s", profile ? profile->label : "(none bound)");
    if (view.strictRequested) {
        ImGui::SameLine();
        ImGui::TextColored(view.strictQualified ? levelColour(Level::PureLle)
                                                : levelColour(Level::HleHack),
                           view.strictQualified ? "  strict LLE: qualified"
                                                : "  strict LLE: NOT QUALIFIED");
    }

    // ── Legend: the five classes, the reasoning in tooltips ─────────────
    for (int i = 0; i <= int(Level::HostConvenience); ++i) {
        const Level level = Level(i);
        if (i) ImGui::SameLine(0, 10);
        chip(level);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", levelTip(level));
        ImGui::SameLine(0, 4);
        ImGui::TextDisabled("%s", abstraction::levelName(level));
    }

    // ── Filter ──────────────────────────────────────────────────────────
    if (ImGui::RadioButton("This machine", !state.allMachines)) state.allMachines = false;
    ImGui::SameLine();
    if (ImGui::RadioButton("All machines", state.allMachines)) state.allMachines = true;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##abstraction-search", "Filter...", state.search.data(),
                             state.search.size());

    int degraded = 0;
    int shown = 0;
    if (ImGui::BeginTable("##abstraction", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
                              ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_SizingStretchProp,
                          ImVec2(0, -ImGui::GetFrameHeightWithSpacing()))) {
        const float em = ImGui::GetFontSize();
        ImGui::TableSetupColumn("Class", ImGuiTableColumnFlags_WidthFixed, 3.5f * em);
        ImGui::TableSetupColumn("Subsystem", ImGuiTableColumnFlags_WidthStretch, 0.62f);
        ImGui::TableSetupColumn("Now", ImGuiTableColumnFlags_WidthStretch, 0.38f);
        ImGui::TableSetupColumn("Doc", ImGuiTableColumnFlags_WidthFixed, 2.5f * em);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        const char* group = nullptr;
        for (const Entry& e : abstraction::catalogue()) {
            if (!matches(e, state.search.data())) continue;
            const LiveRow live = abstraction::resolve(e, view);
            if (!live.present && !state.allMachines) continue;
            if (live.degraded) ++degraded;
            ++shown;

            if (!group || std::strcmp(group, e.group) != 0) {
                group = e.group;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(ImVec4(0.75f, 0.65f, 0.95f, 1.0f), "%s", group);
            }
            ImGui::PushID(e.id);
            ImGui::TableNextRow();
            // Rows this machine does not carry stay readable, dimmed.
            if (!live.present) ImGui::BeginDisabled();

            ImGui::TableSetColumnIndex(0);
            chip(e.level);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", levelTip(e.level));

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(e.subsystem);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                entryTooltip(e, live);
            if (!live.hint.empty()) {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextColored(kOrange, "%s", live.hint.c_str());
                ImGui::PopTextWrapPos();
            }

            ImGui::TableSetColumnIndex(2);
            if (live.known) {
                if (live.active != e.level || e.level == Level::HleHack) {
                    chip(live.active);
                    ImGui::SameLine(0, 6);
                }
                ImGui::TextColored(live.degraded ? kOrange : ImGui::GetStyleColorVec4(ImGuiCol_Text),
                                   "%s", live.state.c_str());
                if (!live.detail.empty() && ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", live.detail.c_str());
            } else if (!live.present) {
                ImGui::TextUnformatted(live.state.c_str());
            } else {
                ImGui::TextDisabled("-");   // static: the class column says it all
            }

            ImGui::TableSetColumnIndex(3);
            ImGui::TextDisabled("%s", e.docSection);

            if (!live.present) ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (degraded)
        ImGui::TextColored(kOrange, "%d running below their class on this machine "
                                    "(see Peripherals to change a firmware choice).",
                           degraded);
    else
        ImGui::TextDisabled("%d rows. Classification and evidence: docs/LLE_VS_HLE.md.",
                            shown);
    ImGui::End();
}

} // namespace pom68k::gui
