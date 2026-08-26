// POM68K — shared-ROM identity decoding and typed profile application.

#include "RuntimeConfigParsers.h"

namespace pom68k::app::detail {
namespace {

bool contains(const std::optional<std::string>& value, std::string_view part) {
    return value && value->find(part) != std::string::npos;
}

} // namespace

MachineSelectionConfig parseMachineSelectionStartup(
    const StartupSnapshot& startup) {
    const MachineStartupView values(startup);
    MachineSelectionConfig options;

    if (const auto model = values.text(startup_option::MacIiModel)) {
        if (*model == "iicx") options.macIi = SnapMachine::IIcx;
        else if (*model == "se30") options.macIi = SnapMachine::SE30;
        else if (*model == "fdhd") options.macIi = SnapMachine::MacII;
    }
    options.sonora = values.boolean(startup_option::Lc3Plus, false)
        ? SnapMachine::Lc3Plus : SnapMachine::Lc3;
    options.vasp = values.boolean(startup_option::IIvi, false)
        ? SnapMachine::IIvi : SnapMachine::IIvx;

    const auto aio = values.text(startup_option::AioId);
    if (contains(aio, "CC2")) options.aio = SnapMachine::CClassic2;
    else if (contains(aio, "0101")) options.aio = SnapMachine::Lc550;

    const auto q605 = values.encodedText(startup_option::Q605Id);
    if (contains(q605, "2225")) options.memcJr = SnapMachine::Q605;
    else if (contains(q605, "222E") || contains(q605, "222e"))
        options.memcJr = SnapMachine::Lc575;

    if (const auto model = values.text(startup_option::CentrisModel)) {
        if (*model == "c610") options.djMemc = SnapMachine::Centris610;
        else if (*model == "q610") options.djMemc = SnapMachine::Quadra610;
        else if (*model == "q650") options.djMemc = SnapMachine::Quadra650;
        else if (*model == "q800") options.djMemc = SnapMachine::Quadra800;
    } else if (values.present(startup_option::Centris610)) {
        options.djMemc = SnapMachine::Centris610;
    }

    if (values.text(startup_option::Q700Model) ==
        std::optional<std::string>("q900"))
        options.spike = SnapMachine::Quadra900;

    const auto q630 = values.encodedText(startup_option::Q630Id);
    if (contains(q630, "225A") || contains(q630, "225a"))
        options.f108 = SnapMachine::Lc580;
    return options;
}

void applyMachineProfile(MachineSelectionConfig& selection, CpuConfig& cpu,
                         pom68k::CoreConfig& core, SnapMachine profile) {
    switch (profile) {
    case SnapMachine::MacII:
    case SnapMachine::IIx:
    case SnapMachine::IIcx:
    case SnapMachine::SE30:
        selection.macIi = profile;
        break;
    case SnapMachine::Lc3:
    case SnapMachine::Lc3Plus:
        selection.sonora = profile;
        break;
    case SnapMachine::Lc520:
    case SnapMachine::Lc550:
    case SnapMachine::CClassic2:
        selection.aio = profile;
        break;
    case SnapMachine::IIvx:
    case SnapMachine::IIvi:
        selection.vasp = profile;
        break;
    case SnapMachine::Lc475:
    case SnapMachine::Lc575:
        selection.memcJr = profile;
        cpu.q605Fpu = false;
        core.cpu.q605Fpu = pom68k::Q605FpuMode::Soft68882;
        break;
    case SnapMachine::Q605:
        selection.memcJr = profile;
        cpu.q605Fpu = true;
        core.cpu.q605Fpu = pom68k::Q605FpuMode::Integrated;
        break;
    case SnapMachine::Centris610:
    case SnapMachine::Centris650:
    case SnapMachine::Quadra610:
    case SnapMachine::Quadra650:
    case SnapMachine::Quadra800:
        selection.djMemc = profile;
        break;
    case SnapMachine::Q700:
    case SnapMachine::Quadra900:
        selection.spike = profile;
        break;
    case SnapMachine::Q630:
    case SnapMachine::Lc580:
        selection.f108 = profile;
        break;
    default:
        break;
    }
}

} // namespace pom68k::app::detail
