// POM68K — the « Débogueur » window
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// See GuiDebuggerWindow.h. Same rendering rule as the dashboard: what is
// shown follows the machine's published state, never the click.

#include "GuiDebuggerWindow.h"

#include "imgui.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace pom68k::gui {

const char* kDebuggerWindowTitle = "Débogueur";

namespace {

using pom68k::dbg::ByteState;
using pom68k::dbg::Command;
using pom68k::dbg::Snapshot;
using pom68k::dbg::Space;
using pom68k::dbg::StopReason;

constexpr std::uint32_t kMemoryWindowBytes = 256;

void post(GuiDebuggerState& state, Command::Kind kind, std::uint32_t addr = 0) {
    Command c;
    c.kind = kind;
    c.addr = addr;
    state.session->post(c);
}

const char* reasonText(StopReason r) {
    switch (r) {
    case StopReason::Pause:      return "pause";
    case StopReason::Step:       return "pas à pas";
    case StopReason::Breakpoint: return "point d'arrêt";
    case StopReason::None:       break;
    }
    return "";
}

void drawStatus(const Snapshot& s) {
    if (!s.available)
        ImGui::TextDisabled("Arrêts indisponibles dans cette version");
    if (s.stopped) {
        ImGui::Text("Arrêté (%s) à $%08X", reasonText(s.reason), s.regs.pc);
        if (s.inQuantum)
            ImGui::TextDisabled("Dans une tranche : redémarrage, états et "
                                "moteur attendent la reprise");
    } else {
        ImGui::TextUnformatted("En cours");
        ImGui::TextDisabled("Registres et mémoire : relevés à la dernière commande");
    }
    ImGui::Text("CPU %s  horloge %lld  %s", s.model.c_str(),
                static_cast<long long>(s.machineClock),
                s.supervisor ? "superviseur" : "utilisateur");
    const char* engines[] = {"interpréteur", "accéléré"};
    if (s.effectiveEngine == s.requestedEngine)
        ImGui::Text("Moteur : %s", engines[s.effectiveEngine & 1]);
    else
        ImGui::Text("Moteur : %s (demandé : %s, arrêts armés)",
                    engines[s.effectiveEngine & 1],
                    engines[s.requestedEngine & 1]);
}

void drawRegisters(const Snapshot& s) {
    const auto& r = s.regs;
    if (!ImGui::BeginTable("##regs", 4, ImGuiTableFlags_SizingFixedFit))
        return;
    for (int n = 0; n < 8; ++n) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::Text("D%d", n);
        ImGui::TableNextColumn(); ImGui::Text("%08X", r.d[n]);
        ImGui::TableNextColumn(); ImGui::Text("A%d", n);
        ImGui::TableNextColumn(); ImGui::Text("%08X", r.a[n]);
    }
    ImGui::EndTable();
    ImGui::Text("PC %08X  SR %04X  VBR %08X", r.pc, r.sr, r.vbr);
    ImGui::Text("USP %08X  ISP %08X  MSP %08X", r.usp, r.isp, r.msp);
    if (s.model == "68030")
        ImGui::Text("TC %08X  TT0 %08X  TT1 %08X  %s", r.tc, r.tt0, r.tt1,
                    s.mmuEnabled ? "MMU active" : "MMU inactive");
    else if (s.model == "68040" || s.model == "68LC040")
        ImGui::Text("TC %04X  DTT0 %08X  DTT1 %08X  %s", r.tc040, r.dtt0,
                    r.dtt1, s.mmuEnabled ? "MMU active" : "MMU inactive");
}

void drawDisassembly(GuiDebuggerState& state, const Snapshot& s) {
    ImGui::TextDisabled("Cliquer une ligne pose ou retire un point d'arrêt");
    for (const auto& line : s.disasm) {
        char label[160];
        std::snprintf(label, sizeof label, "%s%s %08X  %s",
                      line.breakpoint ? "*" : " ",
                      line.addr == s.regs.pc ? ">" : " ",
                      line.addr, line.text.c_str());
        ImGui::PushID(static_cast<int>(line.addr));
        if (ImGui::Selectable(label, line.addr == s.regs.pc && s.stopped))
            post(state, line.breakpoint ? Command::Kind::RemoveBreakpoint
                                        : Command::Kind::AddBreakpoint,
                 line.addr);
        ImGui::PopID();
    }
}

void drawBreakpoints(GuiDebuggerState& state, const Snapshot& s) {
    ImGui::SetNextItemWidth(110);
    const bool enter = ImGui::InputText("##bp", state.breakpointText.data(),
                                        state.breakpointText.size(),
                                        ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Ajouter") || enter) {
        if (auto a = parseGuestAddress(state.breakpointText.data())) {
            post(state, Command::Kind::AddBreakpoint, *a);
            state.inputError.clear();
        } else {
            state.inputError = "Adresse hexadécimale attendue";
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Tout retirer"))
        post(state, Command::Kind::ClearBreakpoints);
    for (std::uint32_t a : s.breakpoints) {
        ImGui::PushID(static_cast<int>(a));
        ImGui::Text("$%08X", a);
        ImGui::SameLine();
        if (ImGui::SmallButton("Retirer"))
            post(state, Command::Kind::RemoveBreakpoint, a);
        ImGui::PopID();
    }
}

void drawMemory(GuiDebuggerState& state, const Snapshot& s) {
    ImGui::SetNextItemWidth(110);
    const bool enter = ImGui::InputText("##mem", state.memoryText.data(),
                                        state.memoryText.size(),
                                        ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    ImGui::RadioButton("Logique", &state.memorySpace, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Physique", &state.memorySpace, 1);
    ImGui::SameLine();
    if (ImGui::Button("Afficher") || enter) {
        if (auto a = parseGuestAddress(state.memoryText.data())) {
            Command c;
            c.kind = Command::Kind::ViewMemory;
            c.addr = *a;
            c.length = kMemoryWindowBytes;
            c.space = state.memorySpace ? Space::Physical : Space::Logical;
            state.session->post(c);
            state.inputError.clear();
        } else {
            state.inputError = "Adresse hexadécimale attendue";
        }
    }
    ImGui::TextDisabled("-- : registre ou zone non mémoire (jamais lu)");
    ImGui::TextDisabled(".. : adresse logique non traduite");
    const auto& m = s.memory;
    for (std::size_t row = 0; row < m.bytes.size(); row += 16) {
        char line[128];
        int n = std::snprintf(line, sizeof line, "%08X ",
                              m.addr + static_cast<std::uint32_t>(row));
        for (std::size_t i = row; i < row + 16 && i < m.bytes.size(); ++i) {
            const char* fmt = m.state[i] == ByteState::Ok ? " %02X"
                : m.state[i] == ByteState::Untranslated ? " .." : " --";
            n += std::snprintf(line + n, sizeof line - std::size_t(n), fmt,
                               m.bytes[i]);
        }
        ImGui::TextUnformatted(line);
    }
}

} // namespace

std::optional<std::uint32_t> parseGuestAddress(const char* text) {
    if (!text) return std::nullopt;
    while (std::isspace(static_cast<unsigned char>(*text))) ++text;
    if (*text == '$') ++text;
    else if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) text += 2;
    if (!std::isxdigit(static_cast<unsigned char>(*text))) return std::nullopt;
    char* end = nullptr;
    const unsigned long long v = std::strtoull(text, &end, 16);
    while (end && std::isspace(static_cast<unsigned char>(*end))) ++end;
    if (!end || *end || v > 0xFFFFFFFFull) return std::nullopt;
    return static_cast<std::uint32_t>(v);
}

void drawDebuggerWindow(GuiDebuggerState& state) {
    if (!state.bound() || !state.showWindow) return;
    ImGui::SetNextWindowSize(ImVec2(520, 640), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(kDebuggerWindowTitle, &state.showWindow)) {
        ImGui::End();
        return;
    }
    const auto snap = state.session->snapshot();
    const Snapshot& s = *snap;
    drawStatus(s);
    if (s.stopped) {
        if (ImGui::Button("Continuer")) post(state, Command::Kind::Continue);
        ImGui::SameLine();
        if (ImGui::Button("Pas à pas")) post(state, Command::Kind::Step);
    } else if (ImGui::Button("Pause")) {
        post(state, Command::Kind::Pause);
    }
    if (!s.message.empty()) ImGui::TextWrapped("%s", s.message.c_str());
    if (!state.inputError.empty())
        ImGui::TextWrapped("%s", state.inputError.c_str());
    if (ImGui::CollapsingHeader("Registres", ImGuiTreeNodeFlags_DefaultOpen))
        drawRegisters(s);
    if (ImGui::CollapsingHeader("Points d'arrêt", ImGuiTreeNodeFlags_DefaultOpen))
        drawBreakpoints(state, s);
    if (ImGui::CollapsingHeader("Désassemblage", ImGuiTreeNodeFlags_DefaultOpen)) {
        // A bounded pane, so the memory section stays reachable.
        ImGui::BeginChild("##disasm",
                          ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 13));
        drawDisassembly(state, s);
        ImGui::EndChild();
    }
    if (ImGui::CollapsingHeader("Mémoire")) drawMemory(state, s);
    ImGui::End();
}

} // namespace pom68k::gui
