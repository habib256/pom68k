// Typed firmware overrides shared by startup and relaunch.
#include "RuntimeConfigParsers.h"

namespace pom68k::app::detail {
std::optional<FirmwareTarget> firmwareTarget(std::string_view slug) {
    if (slug == "adb") return FirmwareTarget::Adb;
    if (slug == "egret") return FirmwareTarget::Egret;
    if (slug == "cuda") return FirmwareTarget::Cuda;
    if (slug == "toby") return FirmwareTarget::TobyDecl;
    if (slug == "pbvideo") return FirmwareTarget::ColorVideo;
    if (slug == "pmu") return FirmwareTarget::Pmu;
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
    case FirmwareTarget::TobyDecl:
        firmware.tobyDeclLle = policy.lle;
        firmware.tobyDeclPath = policy.path;
        break;
    case FirmwareTarget::ColorVideo:
        firmware.colorVideoLle=policy.lle; firmware.colorVideoPath=policy.path;
        break;
    case FirmwareTarget::Pmu:
        firmware.pmuLle = policy.lle; firmware.pmuPath = policy.path;
        break;
    }
}
} // namespace pom68k::app::detail

