// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Typed identity for firmware policy crossing the product/UI boundary.
// Legacy POM68K_* names remain accepted startup syntax, but are never the
// representation carried by a running session or a GUI relaunch.

#pragma once

#include <optional>
#include <string>

namespace pom68k {

enum class FirmwareTarget { Adb, Egret, Cuda };

// Presence of this value means both fields are authoritative. A missing path
// explicitly requests automatic candidate selection; it does not mean
// "inherit the environment".
struct FirmwareOverride {
    FirmwareTarget target = FirmwareTarget::Cuda;
    bool lle = true;
    std::optional<std::string> path;
};

} // namespace pom68k
