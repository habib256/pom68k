// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Process-facing state owned by one GUI machine session. Free callbacks keep
// only a non-owning pointer to this aggregate; ownership stays in the concrete
// MachineSessionRuntime and therefore follows normal RAII teardown.

#pragma once

#include "AtalkHub.h"
#include "CdAudioSource.h"
#include "FloppySound.h"
#include "FirmwareConfig.h"
#include "GuiDisplay.h"
#include "GuiMachineControls.h"
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
    bool ethernetEnabled = false;       // DaynaPort uplink, independent of LocalTalk
    int ethernetScsiId = -1;            // where the card sits; -1 = no card
    std::uint8_t scsiOccupied = 0;      // bit n: a disk (not the card) holds SCSI ID n
    // Staged + relaunch, the card being probed at boot only: nullopt = no card.
    std::function<void(std::optional<int>)> relaunchWithDaynaPort;
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
    // The CD-audio lead: a playing disc never passes through the ASC on a
    // real Macintosh, so it is mixed beside the mechanisms (CdAudioSource.h).
    CdAudioSource cdAudio;
    bool initialized = false;
};

struct GuiRelaunchState {
    std::vector<std::string> switchArguments;
    std::vector<std::string> launchArguments;
    std::optional<pom68k::SnapMachine> targetProfile;
    std::vector<pom68k::FirmwareOverride> firmwareOverrides;
    // The card the relaunched machine gets: the session's own unless the
    // AppleTalk window staged another. Always serialized (`--daynaport=`),
    // so a relaunch reads the same whether the card came from the
    // environment, the command line or the window.
    std::optional<int> daynaPortId;
    bool showWindow = false;
    // The frame asked the window to close (a staged relaunch, a machine
    // switch, the speed measurement done, a kiosk quit chord); the shell
    // carries it to GLFW. GuiShellMenu.cpp sets it, GuiShell.cpp clears it.
    bool closeWindow = false;
    // Relaunch on the session's own command line: the machine comes back
    // identical apart from what the caller staged just before.
    void stageOwnCommandLine() {
        switchArguments = launchArguments;
        if (switchArguments.empty()) switchArguments = {std::string()};
        showWindow = true;
    }
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
    pom68k::gui::GuiDisplayState display;
    GuiNetworkState network;
    GuiAudioState audio;
    GuiRelaunchState relaunch;
    GuiCpuPanelState cpu;
    GuiDiagnosticState diagnostics;
    pom68k::PeripheralHost peripherals;
    pom68k::gui::GuiMachineControls machine;
};
