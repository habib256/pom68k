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

// `TobyDecl` is the Toby video card's declaration ROM (342-0008-a): not an
// MCU, but the same choice — the real dump or a non-conformant substitute —
// reported and overridden through the same typed path.
enum class FirmwareTarget { Adb, Egret, Cuda, TobyDecl };

// Presence of this value means both fields are authoritative. A missing path
// explicitly requests automatic candidate selection; it does not mean
// "inherit the environment".
struct FirmwareOverride {
    FirmwareTarget target = FirmwareTarget::Cuda;
    bool lle = true;
    std::optional<std::string> path;
};

} // namespace pom68k
