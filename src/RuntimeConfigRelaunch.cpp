// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Relaunch serialization: what the GUI stages (a machine profile, firmware
// policy, the DaynaPort card) becomes command-line arguments that
// RuntimeConfig::parse reads back, so a re-exec'd process comes up as the
// session that asked for it. Each serializer replaces its own option and
// leaves every other argument in place. Gates: daynaport_test (the card's
// round trip), docs_test (the profile and firmware forms).

#include "RuntimeConfig.h"

#include <algorithm>

namespace pom68k::app {
namespace {

std::string_view firmwareTargetSlug(FirmwareTarget target) {
    switch (target) {
    case FirmwareTarget::Adb: return "adb";
    case FirmwareTarget::Egret: return "egret";
    case FirmwareTarget::Cuda: return "cuda";
    case FirmwareTarget::TobyDecl: return "toby";
    }
    return {};
}

} // namespace

std::string machineProfileArgument(SnapMachine profile) {
    const MachineProfile* selected = machineProfile(profile);
    return selected
        ? std::string(kMachineProfileOption) + selected->slug
        : std::string();
}

std::vector<std::string> machineProfileArguments(
    std::vector<std::string> arguments,
    std::optional<SnapMachine> profile) {
    std::erase_if(arguments, [](const std::string& argument) {
        return argument.starts_with(kMachineProfileOption);
    });
    if (profile) {
        const std::string serialized = machineProfileArgument(*profile);
        if (!serialized.empty()) arguments.insert(
            arguments.begin(), serialized);
    }
    return arguments;
}

std::string firmwareOverrideArgument(const FirmwareOverride& policy) {
    return std::string(kFirmwareOverrideOption) +
        std::string(firmwareTargetSlug(policy.target)) + ':' +
        (policy.lle ? "lle:" : "hle:") + policy.path.value_or(std::string());
}

std::vector<std::string> firmwareOverrideArguments(
    std::vector<std::string> arguments,
    const std::vector<FirmwareOverride>& overrides) {
    if (overrides.empty()) return arguments;
    std::erase_if(arguments, [](const std::string& argument) {
        return argument.starts_with(kFirmwareOverrideOption);
    });
    std::vector<std::string> serialized;
    serialized.reserve(overrides.size());
    for (const FirmwareOverride& policy : overrides)
        serialized.push_back(firmwareOverrideArgument(policy));
    arguments.insert(arguments.begin(), serialized.begin(), serialized.end());
    return arguments;
}

std::string daynaPortArgument(std::optional<int> id) {
    return std::string(kDaynaPortOption) + std::to_string(id.value_or(0));
}

std::vector<std::string> daynaPortArguments(std::vector<std::string> arguments,
                                            std::optional<int> id) {
    std::erase_if(arguments, [](const std::string& argument) {
        return argument.starts_with(kDaynaPortOption);
    });
    arguments.insert(arguments.begin(), daynaPortArgument(id));
    return arguments;
}

} // namespace pom68k::app
