// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#pragma once
#include "../JitBackend.h"

namespace jit {

// Native AArch64 generator for the 68040 family. This is the automatic
// backend on arm64 hosts; `threaded` remains the portable fallback.
Backend* a64Backend();

}  // namespace jit
