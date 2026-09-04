// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Immutable policy consumed by the emulation core.  Environment variables
// are only one startup syntax; core objects receive these typed values from
// their composer and never inspect process-global state.

#pragma once

#include "LleSession.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pom68k {

enum class Q605FpuMode { Integrated, Soft68882, None };

struct CoreCpuConfig {
    std::optional<int> cacheBoost;
    std::optional<int> icacheMiss;
    std::optional<bool> floppyBoostGate;
    std::optional<bool> cacr030Flush;
    bool macIiEventDriven = false;
    bool duoEventDriven = false;
    bool mmu040Walk = false;

    Q605FpuMode q605Fpu = Q605FpuMode::Integrated;
    std::optional<int> q605CacheBoost;
    std::optional<int> q605IcacheMiss;

    bool centrisFull040 = false;
    bool centrisBareFpu = false;
    std::optional<int> centrisCacheBoost;

    bool q630Lc040 = false;
    bool q630BareFpu = false;
    std::optional<int> q630CacheBoost;

    bool q700Lc040 = false;
    bool q700BareFpu = false;
    std::optional<int> q700CacheBoost;
};

struct CoreBusConfig {
    std::optional<int> scsiLatency;
    bool q605SccEventDriven = true;
    bool q605ScsiEventDriven = true;
    std::optional<std::uint32_t> q605MachineId;
    std::optional<std::uint32_t> q630MachineId;
    std::optional<int> daynaPortId;

    long v8IoHoleTraceLimit = 0;
    std::uint8_t v8IoHoleValue = 0xFF;
    bool q900IopBreak = false;
    std::optional<int> q900IopWatch;
    long q900IopTraceLimit = 0;
};

struct CoreStorageConfig {
    bool cdBay = true;
    int fluxJitterPercent = 0;
    std::optional<std::string> ddmTemplate;
    bool scsiTrace = false;
    bool cdTrace = false;
    bool ownScsiInquiry = false;
};

struct CoreFirmwareConfig {
    // Where a device REPORTS what it built (LLE or HLE, which dump, which
    // paths it searched). Injected rather than global since 2026-08-27: a
    // second machine in one process, an in-process parallel gate or an
    // embeddable library needs a registry it OWNS. Never null — the
    // composition root supplies the session's, fixtures get the process one
    // through defaultCoreConfig().
    lle::Registry* registry = &lle::processRegistry();
    bool adbLle = true;
    bool egretLle = true;
    bool cudaLle = true;
    std::optional<std::string> adbPath;
    std::optional<std::string> egretPath;
    std::optional<std::string> cudaPath;
    std::optional<std::string> root;
};

struct CorePeripheralConfig {
    // AppleTalk is seeded in PRAM only when the user explicitly requested
    // it.  Product networking may be enabled by default without changing a
    // cold machine's historical $22 (printer-port async) setting.
    bool appleTalkPram = false;
    std::uint8_t adbKeyboardHandlerId = 1;

    bool adbLleTrace = false;
    bool adbPicTrace = false;
    bool seViaTrace = false;
    bool rtcTrace = false;
    bool sccTrace = false;
    bool egretCommandTrace = false;
    bool dafbClockTrace = false;
    bool iifxIoTrace = false;
    bool iifxAdbTrace = false;
    bool iifxScsiTrace = false;

    bool pgeTrace = false;
    bool pgeHandshakeTrace = false;
    bool pgeAdbTrace = false;
    bool pgeSpiBytes = false;
    bool pgeTrackballTrace = false;
    int pgeSpinUs = 80;
    std::optional<std::uint8_t> pgeTrapByte;
    std::vector<std::uint16_t> pgePcCount;
    std::vector<std::pair<std::int64_t, std::int64_t>> pgePcWindows;
    std::optional<std::pair<std::int64_t, std::int64_t>> pgePcHistogram;
};

struct CoreDiagnosticConfig {
    bool appleTalkTrace = false;
    bool macIpTrace = false;
    bool peripheralStats = false;
};

struct CoreConfig {
    CoreCpuConfig cpu;
    CoreBusConfig bus;
    CoreStorageConfig storage;
    CoreFirmwareConfig firmware;
    CorePeripheralConfig peripherals;
    CoreDiagnosticConfig diagnostics;
};

// Stable deterministic policy for unit-sized component construction. Product
// composition always supplies RuntimeConfig::core(); fixtures must name this
// value explicitly because no configured constructor has a hidden fallback.
inline const CoreConfig& defaultCoreConfig() noexcept {
    static const CoreConfig config;
    return config;
}

} // namespace pom68k
