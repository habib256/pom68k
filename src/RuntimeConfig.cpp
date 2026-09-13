// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Public startup composition: typed domain decoders feed the immutable model;
// this unit owns only application arguments. Their relaunch serialization —
// the inverse of this parser — is RuntimeConfigRelaunch.cpp.

#include "RuntimeConfig.h"
#include "RuntimeConfigParsers.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace pom68k::app {
namespace {

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
    // Outer optional: was the option given at all. Inner: the card, or none.
    std::optional<std::optional<int>> commandLineDaynaPort;
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
        if (argument.starts_with(kDaynaPortOption)) {
            commandLineDaynaPort = detail::decodeDaynaPortId(
                argument.substr(kDaynaPortOption.size()));
            continue;
        }
        if (argument.starts_with(smokePrefix)) {
            const std::string_view report = argument.substr(smokePrefix.size());
            if (!report.empty()) {
                config.diagnostics_.smokeReport = std::string(report);
                // The gate exercises GUI lifecycle, not host devices.
                config.network_.appleTalk = false;
                config.network_.ltoUdp = false;
                config.devices_.audio = config.devices_.driveSounds = false;
                config.devices_.floppyWriteBack = false;
                config.devices_.serialPrinter = config.devices_.serialModem = {};
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
    if (commandLineDaynaPort)
        config.core_.bus.daynaPortId = *commandLineDaynaPort;
    return config;
}

std::optional<std::string_view> RuntimeConfig::romPath() const noexcept {
    if (!romPath_) return std::nullopt;
    return *romPath_;
}

} // namespace pom68k::app
