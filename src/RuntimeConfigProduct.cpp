// POM68K — product/host startup policy decoder.

#include "RuntimeConfigParsers.h"

namespace pom68k::app::detail {

ProductStartupConfig parseProductStartup(
    const StartupSnapshot& startup) {
    const ProductStartupView values(startup);
    ProductStartupConfig config;

    config.cpu.fpu = !values.present(startup_option::NoFpu);
    config.cpu.q605Fpu = !values.present(startup_option::Q605NoFpu);
    config.jit.resolved = jit::resolveConfig(startup);

    config.network.appleTalkWasSpecified =
        values.present(startup_option::AppleTalk);
    config.network.appleTalk =
        values.boolean(startup_option::AppleTalk, true);
    // Presence historically requested the optional cable, including `=0`.
    config.network.ltoUdp = values.present(startup_option::LtoUdp);
    const int boost =
        values.integer(startup_option::AppleTalkWireBoost).value_or(8);
    config.network.appleTalkWireBoost = boost >= 1 ? boost : 8;
    if (const auto value = values.text(startup_option::ShareDirectory))
        config.network.shareDirectory = *value;

    config.devices.audio = values.boolean(startup_option::Audio, true);
    config.devices.driveSounds =
        values.boolean(startup_option::DriveSounds, true);
    config.devices.floppyWriteBack = !values.present(startup_option::FloppyReadOnly);
    config.devices.startupFloppy = values.text(startup_option::Floppy);
    if (const auto value = values.integer(startup_option::Monitor))
        config.devices.monitorWidth = *value;

    config.diagnostics.fpuLog = values.text(startup_option::FpuLog);
    config.diagnostics.inputRecord = values.text(startup_option::InputRecord);
    config.diagnostics.keyTrace = values.present(startup_option::KeyTrace);
    config.diagnostics.freezeProbe = values.present(startup_option::FreezeProbe);
    config.diagnostics.speedLog = values.boolean(startup_option::SpeedLog);
    config.diagnostics.speedLogSkip =
        values.integer(startup_option::SpeedLogSkip).value_or(0);
    config.diagnostics.speedLogCount =
        values.integer(startup_option::SpeedLogCount).value_or(0);

    config.fullLleAarch64 = values.boolean(startup_option::LleAarch64Full);
    config.fullLleCheckOnly =
        values.boolean(startup_option::LleAarch64CheckOnly);
    return config;
}

} // namespace pom68k::app::detail
