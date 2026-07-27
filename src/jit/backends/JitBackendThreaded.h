// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Threaded backend — the portable floor of the JIT ──
// Generates no host code at all, and therefore builds and runs everywhere
// POM68K builds: x86-64, AArch64, anything else, Emscripten included. It is
// what `POM68K_JIT_BACKEND=auto` falls back to when no native code
// generator is available or allowed, which is why the JIT is a portable
// feature rather than an x86-only one.
//
// What it actually does: replays a recorded block through Moira's own
// instruction handlers with the code-window armed, verifying at every step
// that execution is still where the trace said it would be. The speed comes
// from the window (two or three translated fetches per instruction turn
// into two or three bounds-checked loads), not from the replay — the replay
// exists so that block discovery, invalidation and the fallback contract are
// proven by a backend that cannot possibly be wrong about semantics, before
// a code generator gets to be wrong about them.

#pragma once
#include "../JitBackend.h"

namespace jit {

Backend* threadedBackend();

}  // namespace jit
