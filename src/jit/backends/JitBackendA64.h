// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — MIT (see LICENSE-MIT)

#pragma once
#include "../JitBackend.h"

namespace jit {

// Native AArch64 generator for the 68040 and 68030 families. This is their
// automatic backend on arm64 hosts; `threaded` remains the portable fallback.
Backend* a64Backend();

}  // namespace jit
