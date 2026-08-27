// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Typed startup configuration. Product routing and the emulation core consume
// this immutable value; process capture belongs to ProcessEnvironment.

#pragma once

#include "CoreConfig.h"
#include "FirmwareConfig.h"
#include "MachineCatalog.h"
#include "StartupSnapshot.h"
#include "jit/JitConfig.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pom68k::app {

// Startup policy is separated by responsibility. RuntimeConfig parses only
// explicit inputs; consumers receive these immutable value objects by
// reference.
struct CpuConfig {
    bool fpu = true;
    bool q605Fpu = true;
};

struct JitConfig {
    jit::ResolvedConfig resolved;
};

struct NetworkConfig {
    bool appleTalk = true;
    bool appleTalkWasSpecified = false;
    bool ltoUdp = false;
    int appleTalkWireBoost = 8;
    std::string shareDirectory;
};

struct DeviceConfig {
    bool audio = true;
    bool driveSounds = true;
    bool floppyWriteBack = true;
    std::optional<std::string> startupFloppy;
    std::optional<int> monitorWidth;
};

struct DiagnosticConfig {
    std::optional<std::string> fpuLog;
    bool keyTrace = false;
    bool freezeProbe = false;
    bool speedLog = false;
    int speedLogSkip = 0;
    int speedLogCount = 0;

    // Internal behavioural GUI gate. A non-empty report path requests a
    // hidden window and a deterministic open/render/engine/save/close/relaunch
    // scenario. It is command-line only so a normal process environment can
    // never enable test behaviour accidentally.
    std::optional<std::string> smokeReport;
};

// One normalized selection per ROM-sharing machine family. Raw environment
// spellings cease to exist after RuntimeConfig::parse returns.
struct MachineSelectionConfig {
    SnapMachine macIi = SnapMachine::IIx;
    SnapMachine sonora = SnapMachine::Lc3;
    SnapMachine aio = SnapMachine::Lc520;
    SnapMachine vasp = SnapMachine::IIvx;
    SnapMachine memcJr = SnapMachine::Lc475;
    SnapMachine djMemc = SnapMachine::Centris650;
    SnapMachine spike = SnapMachine::Q700;
    SnapMachine f108 = SnapMachine::Q630;
};

inline constexpr std::string_view kMachineProfileOption =
    "--machine-profile=";
std::string machineProfileArgument(SnapMachine profile);
std::vector<std::string> machineProfileArguments(
    std::vector<std::string> arguments,
    std::optional<SnapMachine> profile);

inline constexpr std::string_view kFirmwareOverrideOption =
    "--firmware-override=";
std::string firmwareOverrideArgument(const FirmwareOverride& override);
std::vector<std::string> firmwareOverrideArguments(
    std::vector<std::string> arguments,
    const std::vector<FirmwareOverride>& overrides);

class RuntimeConfig {
public:
    static RuntimeConfig parse(int argc, char* const argv[],
                               const StartupSnapshot& startup);

    bool showVersion() const noexcept { return showVersion_; }
    bool fullLleAarch64() const noexcept { return fullLleAarch64_; }
    bool fullLleCheckOnly() const noexcept { return fullLleCheckOnly_; }
    const CpuConfig& cpu() const noexcept { return cpu_; }
    const JitConfig& jit() const noexcept { return jit_; }
    const NetworkConfig& network() const noexcept { return network_; }
    const DeviceConfig& devices() const noexcept { return devices_; }
    const DiagnosticConfig& diagnostics() const noexcept {
        return diagnostics_;
    }
    const pom68k::CoreConfig& core() const noexcept { return core_; }
    // Mutable ONLY for the composition root, which binds the session's
    // LLE registry into the policy the devices will receive. Everything
    // downstream takes the const view above.
    pom68k::CoreConfig& mutableCore() noexcept { return core_; }
    const MachineSelectionConfig& machineSelection() const noexcept {
        return machineSelection_;
    }

    const std::string& executable() const noexcept { return executable_; }
    const std::vector<std::string>& launchArguments() const noexcept {
        return launchArguments_;
    }

    // Typed positional startup contract. The first non-application argument
    // is the ROM; remaining values are machine-specific boot/removable media.
    std::optional<std::string_view> romPath() const noexcept;
    const std::vector<std::string>& mediaArguments() const noexcept {
        return mediaArguments_;
    }

private:
    std::string executable_;
    std::optional<std::string> romPath_;
    std::vector<std::string> mediaArguments_;
    std::vector<std::string> launchArguments_;
    CpuConfig cpu_;
    JitConfig jit_;
    NetworkConfig network_;
    DeviceConfig devices_;
    DiagnosticConfig diagnostics_;
    pom68k::CoreConfig core_;
    MachineSelectionConfig machineSelection_;
    bool showVersion_ = false;
    bool fullLleAarch64_ = false;
    bool fullLleCheckOnly_ = false;
};

} // namespace pom68k::app
