// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Small concrete runtime behind MachineSessionRuntime. It owns one session's
// host services, GUI shell and type-erased platform object arena; family-
// specific construction is delegated to PlatformComposers.

#include "GuiMachineRuntime.h"

#include "GuiHostServices.h"
#include "GuiSessionObjects.h"
#include "GuiSessionState.h"
#include "GuiShell.h"
#include "MachineSession.h"
#include "PlatformComposers.h"

#include <memory>

namespace pom68k::gui {
namespace {

class GuiMachineRuntime final : public app::MachineSessionRuntime {
public:
    int run(app::MachineSession& session) override {
        shell_ = std::make_unique<GuiShell>(state_, objects_, session.config());
        state_.relaunch.targetProfile = session.profile().snapshot;
        services_ = std::make_unique<GuiHostServices>(
            state_, objects_, *shell_, session.config());
        return PlatformComposers::run(session, *services_);
    }

private:
    GuiSessionState state_;
    std::unique_ptr<GuiShell> shell_;
    std::unique_ptr<GuiHostServices> services_;
    // Declared last so contexts, machines and cores are destroyed before the
    // callbacks and configuration they reference.
    GuiSessionObjects objects_;
};

} // namespace

std::unique_ptr<app::MachineSessionRuntime> makeGuiMachineRuntime() {
    return std::make_unique<GuiMachineRuntime>();
}

} // namespace pom68k::gui
