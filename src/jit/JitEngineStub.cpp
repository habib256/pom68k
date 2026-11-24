// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── jit::Engine with the engine compiled out (POM68K_JIT=OFF) ──
// Every CPU wrapper owns a jit::Engine member and calls executeUntil(),
// flushAll() and the diagnostics accessors on it. With the engine compiled
// out, this file gives that member the interpreter's behaviour: the engine
// never enables, executeUntil() is Moira's own, and every cache operation is
// a no-op because nothing is ever cached. No backend exists, so no code is
// generated and no fetch window is armed.
//
// This is how the Macintosh layer is brought up on Dirk Hoffmann's Moira,
// whose core carries none of the fork's pomJit* seam (docs/MOIRA_CONVERGENCE.md).

#include "JitEngine.h"
#include "Moira.h"

#include <cstdio>

namespace jit {

Engine::Engine(moira::Moira& cpu, const MemoryHooks& mem, uint32_t guestFamily,
               const ResolvedConfig& config)
    : cpu_(cpu), mem_(mem), guestFamily_(guestFamily), config_(config) {}

Engine::~Engine() = default;

void Engine::setEnabled(bool on) {
    if (on && !refusalReported_) {
        refusalReported_ = true;
        std::fprintf(stderr, "[jit] this build has no JIT (POM68K_JIT=OFF): "
                             "the interpreter runs\n");
    }
    enabled_ = false;
}

const char* Engine::backendName() const { return "none"; }
const char* Engine::backendDescription() const {
    return "no JIT (POM68K_JIT=OFF): interpreter only";
}
bool Engine::nativeBackend() const { return false; }
uint64_t Engine::retired() const { return 0; }
void Engine::flushAll(Flush) {}
void Engine::censusPhase(const char*) {}
void Engine::executeUntil(int64_t clockTarget) { cpu_.executeUntil(clockTarget); }

const char* const* backendNames(int& count) { count = 0; return nullptr; }
const char* const* backendKeys(int& count) { count = 0; return nullptr; }

}  // namespace jit
