// POM68K — startup compatibility decoding for machine selection.

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
        else if (*model == "fdhd") options.macIi = SnapMachine::MacIIFDHD;
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


} // namespace pom68k::app::detail
