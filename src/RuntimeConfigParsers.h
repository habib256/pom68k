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

ProductStartupConfig parseProductStartup(
    const StartupSnapshot& startup);
pom68k::CoreConfig parseCoreStartup(
    const StartupSnapshot& startup);
MachineSelectionConfig parseMachineSelectionStartup(
    const StartupSnapshot& startup);

void applyMachineProfile(MachineSelectionConfig& selection, CpuConfig& cpu,
                         pom68k::CoreConfig& core, SnapMachine profile);

} // namespace pom68k::app::detail
