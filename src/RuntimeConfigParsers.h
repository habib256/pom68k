// POM68K — internal typed decoders behind RuntimeConfig.
// Internal header: keeps legacy syntax out of immutable RuntimeConfig and
// gives each policy domain one parser; it is not product API.

#pragma once
#include "RuntimeConfig.h"
#include "StartupDomainView.h"

namespace pom68k::app::detail {

struct ProductStartupConfig {
    CpuConfig cpu;
    JitConfig jit;
    NetworkConfig network;
    DeviceConfig devices;
    DiagnosticConfig diagnostics;
    bool fullLleAarch64 = false;
    bool fullLleCheckOnly = false;
};

ProductStartupConfig parseProductStartup(const StartupSnapshot& startup);
SerialPortConfig parseSerialPort(const std::optional<std::string>& value);
pom68k::CoreConfig parseCoreStartup(
    const StartupSnapshot& startup);
MachineSelectionConfig parseMachineSelectionStartup(
    const StartupSnapshot& startup);

void applyMachineProfile(MachineSelectionConfig& selection, CpuConfig& cpu,
                         pom68k::CoreConfig& core, SnapMachine profile);

// POM68K_DAYNAPORT and --daynaport= share this reading: empty, absent or a
// leading '0' = no card; 2-6 taken literally; anything else (1, out of
// range, non-numeric) = the default ID 3. Gate: daynaport_test.
std::optional<int> decodeDaynaPortId(std::optional<std::string_view> value);

} // namespace pom68k::app::detail
