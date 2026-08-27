// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// A selected product session. It owns startup configuration, ROM bytes and
// the concrete core/host/UI runtime behind the small type-erased boundary.

#pragma once

#include "LleSession.h"
#include "MachineCatalog.h"
#include "RuntimeConfig.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pom68k::app {

class MachineSession;

class MachineSessionRuntime {
public:
    virtual ~MachineSessionRuntime() = default;
    virtual int run(MachineSession& session) = 0;
};

class MachineSession {
public:
    MachineSession(RuntimeConfig config, const MachineProfile& profile,
                   std::vector<std::uint8_t> rom, std::string romName,
                   std::unique_ptr<MachineSessionRuntime> runtime);

    static MachineSession rejected(
        RuntimeConfig config, std::unique_ptr<MachineSessionRuntime> runtime,
        int exitCode, std::string diagnostic);

    MachineSession(const MachineSession&) = delete;
    MachineSession& operator=(const MachineSession&) = delete;
    MachineSession(MachineSession&&) noexcept = default;
    MachineSession& operator=(MachineSession&&) noexcept = default;

    int run();

    RuntimeConfig& config() noexcept { return config_; }
    const RuntimeConfig& config() const noexcept { return config_; }
    const MachineProfile& profile() const noexcept { return *profile_; }
    const std::string& romName() const noexcept { return romName_; }
    std::vector<std::uint8_t> takeRom() { return std::move(rom_); }
    // The session's OWN peripheral-qualification registry. Devices report
    // into it through CoreConfig; the Périphériques window and the save
    // states read it back. unique_ptr because the registry holds a mutex
    // and atomics and the session is movable.
    lle::Registry& lleRegistry() noexcept { return *lle_; }

private:
    explicit MachineSession(RuntimeConfig config);

    std::unique_ptr<lle::Registry> lle_ = std::make_unique<lle::Registry>();
    RuntimeConfig config_;
    const MachineProfile* profile_ = nullptr;
    std::vector<std::uint8_t> rom_;
    std::string romName_;
    std::string diagnostic_;
    int exitCode_ = 0;
    bool ran_ = false;
    // Declared last so runtime-owned objects are torn down before session data.
    std::unique_ptr<MachineSessionRuntime> runtime_;
};

} // namespace pom68k::app
