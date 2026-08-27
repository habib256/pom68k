// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "MachineSession.h"

#include <cstdio>
#include <utility>

namespace pom68k::app {

MachineSession::MachineSession(RuntimeConfig config,
                               const MachineProfile& profile,
                               std::vector<std::uint8_t> rom,
                               std::string romName,
                               std::unique_ptr<MachineSessionRuntime> runtime)
    : config_(std::move(config)), profile_(&profile), rom_(std::move(rom)),
      romName_(std::move(romName)), runtime_(std::move(runtime)) {
    // Bind the session's registry into the policy every device receives, and
    // start its session: from here on, "which devices did this machine build,
    // LLE or HLE" is a property of THIS session, not of the process.
    config_.mutableCore().firmware.registry = lle_.get();
    lle_->beginSession(config_.fullLleAarch64());
}

MachineSession::MachineSession(RuntimeConfig config)
    : config_(std::move(config)) {}

MachineSession MachineSession::rejected(
    RuntimeConfig config, std::unique_ptr<MachineSessionRuntime> runtime,
    int exitCode, std::string diagnostic) {
    MachineSession session(std::move(config));
    session.runtime_ = std::move(runtime);
    session.exitCode_ = exitCode;
    session.diagnostic_ = std::move(diagnostic);
    return session;
}

int MachineSession::run() {
    if (ran_) {
        std::fprintf(stderr, "MachineSession: a session can only be run once\n");
        return 1;
    }
    ran_ = true;
    if (!diagnostic_.empty()) {
        std::fprintf(stderr, "%s\n", diagnostic_.c_str());
        return exitCode_;
    }
    if (!runtime_) {
        std::fprintf(stderr, "MachineSession: no runtime installed\n");
        return 1;
    }
    return runtime_->run(*this);
}

} // namespace pom68k::app
