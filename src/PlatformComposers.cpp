// POM68K — typed platform-family dispatch
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "PlatformComposers.h"

#include "GuiHostServices.h"
#include "MachineSession.h"
#include "PlatformFamilyComposers.h"

#include <cstdio>
#include <utility>

namespace pom68k::gui {

int PlatformComposers::run(app::MachineSession& session,
                           GuiHostServices& services) {
    const MachineProfile& profile = session.profile();
    PlatformLaunch launch{
        session.takeRom(), session.romName(),
        session.config().mediaArguments(), profile.platform, profile.snapshot};

    switch (profile.platform) {
    case PlatformKind::Compact:
        return composeCompact(std::move(launch), services);
    case PlatformKind::Glue:
    case PlatformKind::Oss:
        return composeToby(std::move(launch), services);
    case PlatformKind::V8:
        return composeV8(std::move(launch), services);
    case PlatformKind::Rbv:
    case PlatformKind::Sonora:
    case PlatformKind::Vasp:
        return composeSonora(std::move(launch), services);
    case PlatformKind::MemcJr:
    case PlatformKind::DjMemc:
    case PlatformKind::Spike:
    case PlatformKind::F108:
        return composeDafb(std::move(launch), services);
    case PlatformKind::Msc:
        return composeDuo(std::move(launch), services);
    }
    std::fprintf(stderr, "MachineSession: unsupported platform\n");
    return 1;
}

} // namespace pom68k::gui
