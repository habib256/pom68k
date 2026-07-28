// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#pragma once
#include "../JitBackend.h"

namespace jit {

// The x86-64 code generator (J2). Registered ahead of `threaded` in
// JitBackend.cpp's candidate list, and selected by `auto` only when the
// host can actually hand out executable memory — see JitCodeBuffer.
Backend* x64Backend();

}  // namespace jit
