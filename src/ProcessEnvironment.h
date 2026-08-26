// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Process-global configuration boundary. The composition root captures this
// snapshot once, then injects it into RuntimeConfig's pure parser.

#pragma once

#include "StartupSnapshot.h"

namespace pom68k::app {

StartupSnapshot captureRuntimeEnvironment();

} // namespace pom68k::app
