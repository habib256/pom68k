// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── JIT configuration (environment surface) ──
// One place for every knob of the second execution engine. The engine is
// OFF by default everywhere (GUI, headless, CTest): the Moira interpreter
// stays the reference, the JIT is opt-in — see src/jit/POM68K_JIT.md.

#pragma once
#include <cstdlib>
#include <cstring>

namespace jit {

// Which execution engine drives a CPU wrapper. `Interp` calls
// Moira::executeUntil() exactly like before this subsystem existed.
enum class EngineKind { Interp, Jit };

namespace detail {

inline const char* env(const char* key) {
    const char* v = std::getenv(key);
    return (v && *v) ? v : nullptr;
}

inline bool envBool(const char* key, bool dflt) {
    const char* v = env(key);
    if (!v) return dflt;
    return !(v[0] == '0' || v[0] == 'n' || v[0] == 'N' || v[0] == 'f' || v[0] == 'F');
}

inline int envInt(const char* key, int dflt, int lo, int hi) {
    const char* v = env(key);
    if (!v) return dflt;
    int n = std::atoi(v);
    return (n >= lo && n <= hi) ? n : dflt;
}

}  // namespace detail

// POM68K_CPU_ENGINE=jit turns the JIT on at construction time. Anything else
// (unset, `interp`, `0`) keeps the interpreter — the GUI menu can still
// switch at runtime.
inline EngineKind defaultEngine() {
    const char* v = detail::env("POM68K_CPU_ENGINE");
    if (!v) return EngineKind::Interp;
    if (!std::strncmp(v, "jit", 3) || v[0] == '1') return EngineKind::Jit;
    return EngineKind::Interp;
}

// POM68K_JIT_BACKEND=auto|threaded|x64|a64. `auto` picks the best backend
// compiled in AND usable on this host, always falling back to `threaded`
// (which needs no code generation and therefore works everywhere POM68K
// builds, WASM included).
inline const char* backendPreference() {
    const char* v = detail::env("POM68K_JIT_BACKEND");
    return v ? v : "auto";
}

// J1a — instruction-fetch window. The measured win of the whole J1 stage;
// kept separately switchable so its contribution can be attributed.
inline bool fetchWindowEnabled() { return detail::envBool("POM68K_JIT_FETCH", true); }

// J1b — basic-block discovery + cached replay. OFF by default, and that is
// a MEASURED decision, not a hedge: on q605_boot_etalon the fetch window
// alone runs the boot in 27.3 s against the interpreter's 60.5 s, while
// window+blocks takes 42.4 s. Real 68k code is branch-dense (recorded
// blocks average ~1 instruction), so the per-block bookkeeping — a hash
// lookup and a trace attempt at every branch — costs more than the replay
// saves. The machinery stays because it is exactly what a code-generating
// backend needs (block boundaries, an IR, caps()-driven fallback); turn it
// on with POM68K_JIT_BLOCKS=1, which is what the block-path gate does.
inline bool blockCacheEnabled() { return detail::envBool("POM68K_JIT_BLOCKS", false); }

// Straight-line instruction ceiling per block. A block also ends at the
// first control-flow, MMU-touching or capability-missing instruction.
inline int maxBlockInstrs() { return detail::envInt("POM68K_JIT_BLOCK_MAX", 64, 1, 256); }

// Blocks kept in the cache before a full flush (the cache is a plain map;
// J1 does not do fine-grained eviction).
inline int maxBlocks() { return detail::envInt("POM68K_JIT_MAX_BLOCKS", 16384, 64, 1 << 20); }

// Chatter on stderr: backend selection, flushes, block statistics.
inline bool verbose() { return detail::envBool("POM68K_JIT_VERBOSE", false); }

}  // namespace jit
