// POM68K — Toby/IIfx status-panel view
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#pragma once

#include "imgui.h"

#include <cstdint>
#include <string>

namespace pom68k::gui {

struct TobyStatusView {
    std::uint32_t pc = 0;
    long long clock = 0;
    bool overlay = false;
    bool hmmu24 = false;
    int videoWidth = 0;
    int videoHeight = 0;
    bool showIopCycles = false;
    long long sccPicCycles = 0;
    long long swimPicCycles = 0;
};

inline void drawTobyStatus(const std::string& cpuLine,
                           const TobyStatusView& status) {
    ImGui::Text("%s  PC=%08X  clock=%lld", cpuLine.c_str(),
                status.pc, status.clock);
    if (status.showIopCycles) {
        ImGui::Text("overlay=%d  Toby=%dx%d", status.overlay ? 1 : 0,
                    status.videoWidth, status.videoHeight);
        ImGui::Text("IOP 65C02  SCC=%lld cyc   SWIM=%lld cyc",
                    status.sccPicCycles, status.swimPicCycles);
    } else {
        ImGui::Text("overlay=%d  HMMU24=%d  Toby=%dx%d",
                    status.overlay ? 1 : 0, status.hmmu24 ? 1 : 0,
                    status.videoWidth, status.videoHeight);
    }
}

} // namespace pom68k::gui
