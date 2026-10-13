// POM68K — the "Accelerated engine" statistics window
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "GuiEngineWindow.h"

#include "GuiSessionState.h"
#include "NetworkWindow.h"          // statusDot
#include "imgui.h"

#include <chrono>
#include <cstdint>

namespace pom68k::gui {

void drawEngineWindow(GuiCpuPanelState& state) {
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
    if (!ImGui::Begin("Accelerated engine", &state.showJit,
                      ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    ImGui::SeparatorText("Moteur");
    statusDot(engine == 1, engine == 1 ? "Accelerated engine actif"
                                      : "Moira interpreter (reference)");
    ImGui::Text("Backend : %s", state.jitBackend ? state.jitBackend : "-");
    if (engine == 1)
        ImGui::Text("Instructions/s : %.2f M", state.jitRate / 1e6);
    else
        ImGui::TextDisabled(
            "Instructions/s: - (accelerated engine counters, stopped)");

    ImGui::SeparatorText("Distribution");
    const double all = double(total ? total : 1);
    ImGui::Text("By engine: %llu  (%.1f %%)",
                static_cast<unsigned long long>(snapshot.instrs),
                100.0 * double(snapshot.instrs) / all);
    ImGui::Text("By interpreter: %llu  (%.1f %%)",
                static_cast<unsigned long long>(snapshot.interpInstrs),
                100.0 * double(snapshot.interpInstrs) / all);
    ImGui::SeparatorText("Blocs");
    ImGui::Text("Compiled %llu   ·   live %llu   ·   replayed %llu",
                static_cast<unsigned long long>(snapshot.blocksCompiled),
                static_cast<unsigned long long>(snapshot.blocksLive),
                static_cast<unsigned long long>(snapshot.blocksRun));
    const double reuse = snapshot.blocksCompiled
        ? double(snapshot.blocksRun) / double(snapshot.blocksCompiled)
        : 0.0;
    ImGui::Text("Reuse: %.1f replays per compiled block", reuse);
    ImGui::Text("Flushes %llu   ·   invalidations %llu",
                static_cast<unsigned long long>(snapshot.flushes),
                static_cast<unsigned long long>(snapshot.invalidations));
    ImGui::SeparatorText("Code window");
    ImGui::Text("Validations %llu   ·   rejected %llu  (%.1f %%)",
                static_cast<unsigned long long>(snapshot.windowArmed),
                static_cast<unsigned long long>(snapshot.windowFailed),
                snapshot.windowArmed
                    ? 100.0 * double(snapshot.windowFailed) /
                          double(snapshot.windowArmed)
                    : 0.0);
    ImGui::TextDisabled("Rejected = I/O, VRAM, active overlay, or page not "
                        "yet in the ATC.");
    ImGui::SeparatorText("Block exits (by cause)");
    for (int i = 0; i < int(jit::Exit::Count); ++i) {
        if (!snapshot.exits[i]) continue;
        ImGui::Text("%-16s %llu", jit::exitName(jit::Exit(i)),
                    static_cast<unsigned long long>(snapshot.exits[i]));
    }
    ImGui::TextDisabled("Every exit occurs at an instruction boundary, "
                        "with exact guest state.");
    ImGui::End();
}

} // namespace pom68k::gui
