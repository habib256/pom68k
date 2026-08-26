// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ROM discovery and product-profile selection.  This is the only place where
// ROM identity is translated into a MachineCatalog row.

#pragma once

#include "MachineSession.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pom68k::app {

class MachineFactory {
public:
    static MachineSession create(
        RuntimeConfig config, std::unique_ptr<MachineSessionRuntime> runtime);

    static const MachineProfile& selectProfile(
        const RuntimeConfig& config, const std::vector<std::uint8_t>& rom);
    static bool qualifiesFullLleAarch64(
        const std::vector<std::uint8_t>& rom) noexcept;

    // Shared startup/host resource policy.  GUI services and the factory use
    // the same CWD/executable/parent search instead of maintaining two copies.
    static std::vector<std::uint8_t> readFile(const std::string& path);
    static std::vector<std::uint8_t> findResource(
        const std::string& relative, std::string& matched);
    static std::string findPath(const std::string& relative);
    static std::string findRomBySignature(const std::string& signature);
    static std::string executableDirectory();
};

} // namespace pom68k::app
