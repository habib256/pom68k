// POM68K — emulation-core startup policy decoder.

#include "RuntimeConfigParsers.h"

#include <cstdlib>

namespace pom68k::app::detail {
namespace {

std::vector<std::uint16_t> commaHexValues(const std::string& text) {
    std::vector<std::uint16_t> values;
    const char* cursor = text.c_str();
    while (*cursor) {
        char* end = nullptr;
        const unsigned long value = std::strtoul(cursor, &end, 16);
        if (end == cursor) break;
        values.push_back(std::uint16_t(value));
        cursor = *end == ',' ? end + 1 : end;
    }
    return values;
}

std::vector<std::pair<std::int64_t, std::int64_t>>
commaDecimalRanges(const std::string& text) {
    std::vector<std::pair<std::int64_t, std::int64_t>> ranges;
    const char* cursor = text.c_str();
    while (*cursor) {
        char* end = nullptr;
        const long long lo = std::strtoll(cursor, &end, 10);
        if (end == cursor) break;
        long long hi = -1;
        if (*end == ',') {
            cursor = end + 1;
            hi = std::strtoll(cursor, &end, 10);
        }
        ranges.emplace_back(lo, hi);
        cursor = *end == ';' ? end + 1 : end;
    }
    return ranges;
}

} // namespace

pom68k::CoreConfig parseCoreStartup(
    const StartupSnapshot& startup) {
    const CoreStartupView values(startup);
    pom68k::CoreConfig options;

    options.cpu.cacheBoost = values.integer(startup_option::CacheBoost);
    options.cpu.icacheMiss = values.integer(startup_option::ICacheMiss);
    if (values.present(startup_option::FloppyBoostGate))
        options.cpu.floppyBoostGate =
            values.boolean(startup_option::FloppyBoostGate, false);
    if (values.present(startup_option::Jit030CacrFlush))
        options.cpu.cacr030Flush =
            values.boolean(startup_option::Jit030CacrFlush, false);
    options.cpu.macIiEventDriven =
        values.boolean(startup_option::MacIiEvent, false);
    options.cpu.duoEventDriven =
        values.boolean(startup_option::DuoEvent, false);
    options.cpu.mmu040Walk = values.present(startup_option::Mmu040Walk);

    if (const auto noFpu = values.text(startup_option::Q605NoFpu))
        options.cpu.q605Fpu = (!noFpu->empty() &&
                               ((*noFpu)[0] == '2' || (*noFpu)[0] == 'b'))
            ? pom68k::Q605FpuMode::None : pom68k::Q605FpuMode::Soft68882;
    options.cpu.q605CacheBoost =
        values.integer(startup_option::Q605CacheBoost);
    options.cpu.q605IcacheMiss =
        values.integer(startup_option::Q605ICacheMiss);
    options.cpu.centrisFull040 = values.present(startup_option::CentrisFpu);
    options.cpu.centrisBareFpu = values.present(startup_option::CentrisBareFpu);
    options.cpu.centrisCacheBoost =
        values.integer(startup_option::CentrisCacheBoost);
    options.cpu.q630Lc040 = values.present(startup_option::Q630Lc040);
    options.cpu.q630BareFpu = values.present(startup_option::Q630BareFpu);
    options.cpu.q630CacheBoost =
        values.integer(startup_option::Q630CacheBoost);
    options.cpu.q700Lc040 = values.present(startup_option::Q700Lc040);
    options.cpu.q700BareFpu = values.present(startup_option::Q700BareFpu);
    options.cpu.q700CacheBoost =
        values.integer(startup_option::Q700CacheBoost);

    if (const auto latency = values.integer(startup_option::ScsiLatency))
        options.bus.scsiLatency = *latency;
    options.bus.q605SccEventDriven =
        values.boolean(startup_option::Q605EventScc, true);
    options.bus.q605ScsiEventDriven =
        values.boolean(startup_option::Q605EventScsi, true);
    options.bus.q605MachineId = values.hexadecimal(startup_option::Q605Id);
    options.bus.q630MachineId = values.hexadecimal(startup_option::Q630Id);
    if (const auto dayna = values.text(startup_option::DaynaPort);
        dayna && !dayna->empty() && (*dayna)[0] != '0') {
        int id = std::atoi(dayna->c_str());
        if (id <= 1 || id > 6) id = 3;
        options.bus.daynaPortId = id;
    }
    options.bus.v8IoHoleTraceLimit =
        values.traceLimit(startup_option::V8IoHole, 200);
    if (const auto hole = values.hexadecimal(startup_option::V8HoleValue))
        options.bus.v8IoHoleValue = std::uint8_t(*hole);
    options.bus.q900IopBreak = values.present(startup_option::Q900IopBreak);
    if (const auto watch = values.hexadecimal(startup_option::Q900IopWatch))
        options.bus.q900IopWatch = int(*watch);
    options.bus.q900IopTraceLimit =
        values.traceLimit(startup_option::Q900IopTrace, 600);

    options.storage.cdBay = !values.present(startup_option::NoCdBay);
    options.storage.fluxJitterPercent =
        values.integer(startup_option::FluxJitter).value_or(0);
    options.storage.ddmTemplate =
        values.text(startup_option::ScsiDdmTemplate);
    options.storage.scsiTrace = values.present(startup_option::ScsiTrace);
    options.storage.cdTrace = values.present(startup_option::CdTrace);
    options.storage.ownScsiInquiry =
        values.text(startup_option::ScsiInquiry) ==
        std::optional<std::string>("pom68k");

    options.firmware.adbLle = values.boolean(startup_option::AdbLle, true);
    options.firmware.egretLle = values.boolean(startup_option::EgretLle, true);
    options.firmware.cudaLle = values.boolean(startup_option::CudaLle, true);
    options.firmware.adbPath = values.text(startup_option::AdbFirmware);
    options.firmware.egretPath = values.text(startup_option::CudaFirmware);
    options.firmware.cudaPath = options.firmware.egretPath;
    options.firmware.root = values.text(startup_option::FirmwareRoot);

    options.peripherals.appleTalkPram =
        values.present(startup_option::AppleTalk) &&
        values.boolean(startup_option::AppleTalk, false);
    if (const auto id = values.integer(startup_option::AdbKeyboardId))
        options.peripherals.adbKeyboardHandlerId = std::uint8_t(*id);
    options.peripherals.adbLleTrace = values.present(startup_option::AdbLleTrace);
    options.peripherals.adbPicTrace = values.present(startup_option::AdbPicTrace);
    options.peripherals.seViaTrace = values.present(startup_option::SeViaTrace);
    options.peripherals.rtcTrace = values.present(startup_option::RtcDebug);
    options.peripherals.sccTrace = values.present(startup_option::SccDebug);
    options.peripherals.egretCommandTrace =
        values.present(startup_option::EgretCommandLog);
    options.peripherals.dafbClockTrace =
        values.present(startup_option::DafbClockTrace);
    options.peripherals.iifxIoTrace = values.present(startup_option::IIfxIoTrace);
    options.peripherals.iifxAdbTrace =
        values.present(startup_option::IIfxAdbTrace);
    options.peripherals.iifxScsiTrace =
        values.present(startup_option::IIfxScsiTrace);
    options.peripherals.pgeTrace = values.present(startup_option::PgeTrace);
    options.peripherals.pgeHandshakeTrace =
        values.present(startup_option::PgeHandshake);
    options.peripherals.pgeAdbTrace =
        values.present(startup_option::PgeAdbTrace);
    options.peripherals.pgeSpiBytes =
        values.present(startup_option::PgeSpiBytes);
    options.peripherals.pgeTrackballTrace =
        values.present(startup_option::PgeTrackballTrace);
    if (const auto spin = values.integer(startup_option::PgeSpinUs))
        options.peripherals.pgeSpinUs = *spin;
    if (const auto trap = values.hexadecimal(startup_option::PgeTrap))
        options.peripherals.pgeTrapByte = std::uint8_t(*trap);
    if (const auto counts = values.text(startup_option::PgePcCount))
        options.peripherals.pgePcCount = commaHexValues(*counts);
    if (const auto windows = values.text(startup_option::PgePcWindow))
        options.peripherals.pgePcWindows = commaDecimalRanges(*windows);
    if (const auto histogram = values.text(startup_option::PgePcHistogram)) {
        const auto ranges = commaDecimalRanges(*histogram);
        if (!ranges.empty()) options.peripherals.pgePcHistogram = ranges.front();
    }

    options.diagnostics.appleTalkTrace =
        values.present(startup_option::AppleTalkDebug);
    options.diagnostics.macIpTrace = values.present(startup_option::MacIpDebug);
    options.diagnostics.peripheralStats = values.present(startup_option::PeripheralStats);
    return options;
}

} // namespace pom68k::app::detail
