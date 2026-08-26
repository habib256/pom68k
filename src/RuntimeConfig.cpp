// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Public startup composition: typed domain decoders feed the immutable model;
// this unit owns only application arguments and relaunch serialization.

#include "RuntimeConfig.h"
#include "RuntimeConfigParsers.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace pom68k::app {
namespace {

std::string_view firmwareTargetSlug(FirmwareTarget target) {
    switch (target) {
    case FirmwareTarget::Adb: return "adb";
    case FirmwareTarget::Egret: return "egret";
    case FirmwareTarget::Cuda: return "cuda";
    }
    return {};
}

std::optional<FirmwareTarget> firmwareTarget(std::string_view slug) {
    if (slug == "adb") return FirmwareTarget::Adb;
    if (slug == "egret") return FirmwareTarget::Egret;
    if (slug == "cuda") return FirmwareTarget::Cuda;
    return std::nullopt;
}

std::optional<FirmwareOverride> parseFirmwareOverride(
    std::string_view argument) {
    if (!argument.starts_with(kFirmwareOverrideOption)) return std::nullopt;
    const std::string_view value =
        argument.substr(kFirmwareOverrideOption.size());
    const std::size_t targetEnd = value.find(':');
    if (targetEnd == std::string_view::npos) return std::nullopt;
    const std::size_t modeEnd = value.find(':', targetEnd + 1);
    if (modeEnd == std::string_view::npos) return std::nullopt;
    const auto target = firmwareTarget(value.substr(0, targetEnd));
    const std::string_view mode =
        value.substr(targetEnd + 1, modeEnd - targetEnd - 1);
    if (!target || (mode != "lle" && mode != "hle")) return std::nullopt;
    const std::string_view path = value.substr(modeEnd + 1);
    return FirmwareOverride{*target, mode == "lle",
                            path.empty()
                                ? std::optional<std::string>()
                                : std::optional<std::string>(path)};
}

void applyFirmwareOverride(pom68k::CoreFirmwareConfig& firmware,
                           const FirmwareOverride& policy) {
    switch (policy.target) {
    case FirmwareTarget::Adb:
        firmware.adbLle = policy.lle;
        firmware.adbPath = policy.path;
        break;
    case FirmwareTarget::Egret:
        firmware.egretLle = policy.lle;
        firmware.egretPath = policy.path;
        break;
    case FirmwareTarget::Cuda:
        firmware.cudaLle = policy.lle;
        firmware.cudaPath = policy.path;
        break;
    }
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

RuntimeConfig RuntimeConfig::parse(
    int argc, char* const argv[], const StartupSnapshot& startup) {
    RuntimeConfig config;
    auto product = detail::parseProductStartup(startup);
    config.cpu_ = std::move(product.cpu);
    config.jit_ = std::move(product.jit);
    config.network_ = std::move(product.network);
    config.devices_ = std::move(product.devices);
    config.diagnostics_ = std::move(product.diagnostics);
    config.core_ = detail::parseCoreStartup(startup);
    config.machineSelection_ =
        detail::parseMachineSelectionStartup(startup);
    config.fullLleAarch64_ = product.fullLleAarch64;
    config.fullLleCheckOnly_ = product.fullLleCheckOnly;
    if (config.fullLleAarch64_) {
        config.jit_.resolved.engineExplicit = true;
        config.jit_.resolved.engine = jit::EngineKind::Jit;
        config.jit_.resolved.backend = "a64";
    }

    config.executable_ = argc > 0 && argv[0] ? argv[0] : "POM68K";
    std::optional<SnapMachine> commandLineProfile;
    std::vector<FirmwareOverride> firmwareOverrides;
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i] ? argv[i] : "";
        config.launchArguments_.emplace_back(arg);
        if (std::strcmp(arg, "--version") == 0) {
            config.showVersion_ = true;
            continue;
        }
        if (std::strcmp(arg, "--lle-aarch64") == 0 ||
            std::strcmp(arg, "--lle-aarch64-check") == 0) {
            config.fullLleAarch64_ = true;
            config.fullLleCheckOnly_ =
                config.fullLleCheckOnly_ ||
                std::strcmp(arg, "--lle-aarch64-check") == 0;
            config.jit_.resolved.engineExplicit = true;
            config.jit_.resolved.engine = jit::EngineKind::Jit;
            config.jit_.resolved.backend = "a64";
            continue;
        }

        constexpr std::string_view smokePrefix = "--gui-smoke=";
        const std::string_view argument(arg);
        if (argument.starts_with(kMachineProfileOption)) {
            const std::string_view slug =
                argument.substr(kMachineProfileOption.size());
            if (const MachineProfile* profile = machineProfile(slug))
                commandLineProfile = profile->snapshot;
            continue;
        }
        if (argument.starts_with(kFirmwareOverrideOption)) {
            if (const auto policy = parseFirmwareOverride(argument))
                firmwareOverrides.push_back(*policy);
            continue;
        }
        if (argument.starts_with(smokePrefix)) {
            const std::string_view report = argument.substr(smokePrefix.size());
            if (!report.empty()) {
                config.diagnostics_.smokeReport = std::string(report);
                // The gate exercises GUI lifecycle, not host devices.
                config.network_.appleTalk = false;
                config.network_.ltoUdp = false;
                config.devices_.audio = false;
                config.devices_.driveSounds = false;
                config.devices_.floppyWriteBack = false;
            }
            continue;
        }
        if (!config.romPath_) config.romPath_ = arg;
        else config.mediaArguments_.emplace_back(arg);
    }
    if (commandLineProfile)
        detail::applyMachineProfile(config.machineSelection_, config.cpu_,
                                    config.core_, *commandLineProfile);
    for (const FirmwareOverride& policy : firmwareOverrides)
        applyFirmwareOverride(config.core_.firmware, policy);
    return config;
}

std::optional<std::string_view> RuntimeConfig::romPath() const noexcept {
    if (!romPath_) return std::nullopt;
    return *romPath_;
}

} // namespace pom68k::app
