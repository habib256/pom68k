// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Process-facing state owned by one GUI machine session. Free callbacks keep
// only a non-owning pointer to this aggregate; ownership stays in the concrete
// MachineSessionRuntime and therefore follows normal RAII teardown.

#pragma once

#include "AtalkHub.h"
#include "FloppySound.h"
#include "FirmwareConfig.h"
#include "GuiSpeedGauge.h"
#include "LtoUdp.h"
#include "MachineCatalog.h"
#include "PeripheralWindow.h"
#include "jit/JitStats.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct GuiNetworkState {
    bool appleTalkEnabled = true;
    bool appleTalkWasSpecified = false;
    bool ltoUdpEnabled = false;
    int appleTalkWireBoost = 8;
    std::string shareDirectory;
    LtoUdp ltoudp;
    AtalkHub atalk;
    bool showWindow = false;
    bool configured = false;
};

struct GuiAudioState {
    FloppySound floppySfx;
    FloppySound hddSfx;
    bool initialized = false;
};

struct GuiRelaunchState {
    std::vector<std::string> switchArguments;
    std::vector<std::string> launchArguments;
    std::optional<pom68k::SnapMachine> targetProfile;
    std::vector<pom68k::FirmwareOverride> firmwareOverrides;
    bool showWindow = false;
};

struct GuiCpuPanelState {
    bool showJit = false;
    std::function<void(int)> setCpuEngine;
    std::function<int()> getCpuEngine;
    std::function<jit::Stats::Snapshot()> jitStats;
    std::function<std::pair<long long, long long>()> speedSample;
    const char* jitBackend = nullptr;
    bool speedMeasurementDone = false;
    pom68k::GuiSpeedGauge speedGauge;

    std::uint64_t jitLastTotal = 0;
    double jitRate = 0.0;
    std::chrono::steady_clock::time_point jitLastAt{};
};

struct GuiDiagnosticState {
    bool keyTraceEnabled = false;
    bool freezeProbeEnabled = false;
};

struct GuiSessionState {
    GuiNetworkState network;
    GuiAudioState audio;
    GuiRelaunchState relaunch;
    GuiCpuPanelState cpu;
    GuiDiagnosticState diagnostics;
    pom68k::PeripheralHost peripherals;
};
