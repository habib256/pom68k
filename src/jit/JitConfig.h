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
// that is compiled in, usable on this host, AND valid for the guest CPU
// family (JitBackend.h § GuestFamily — a code generator written against one
// 68k family is wrong on another, not merely slower). It always falls back to
// `threaded`, which needs no code generation and is valid for every guest, so
// it works everywhere POM68K builds, WASM included.
//
// Naming a backend explicitly does NOT bypass the guest check: an invalid
// combination is refused with an explanation rather than honoured into a
// wedged machine. POM68K_JIT_UNSAFE_BACKEND=1 forces it anyway, which is for
// developing support for a family the backend does not claim yet.
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
// `dflt` is the ACTIVE backend's answer, not a constant: the threaded
// backend measured slower with blocks than with the fetch window alone
// (below), while a code generator has nothing at all to run without them.
inline bool blockCacheEnabled(bool dflt) {
    return detail::envBool("POM68K_JIT_BLOCKS", dflt);
}

// Straight-line instruction ceiling per block. A block also ends at the
// first control-flow, MMU-touching or capability-missing instruction.
inline int maxBlockInstrs() { return detail::envInt("POM68K_JIT_BLOCK_MAX", 64, 1, 256); }

// Visits before a recorded block is handed to the backend. Native generators
// default to immediate compilation: after incremental I-cache invalidation,
// AArch64 measures 3.73x faster at 1, while 512 leaves 94% of the fixed Q605
// workload interpreted and loses the entire gain. Portable replay retains
// the conservative threshold when its block path is explicitly enabled.
// POM68K_JIT_HOT overrides both defaults for experiments.
inline int hotThreshold(bool nativeCode) {
    return detail::envInt("POM68K_JIT_HOT", nativeCode ? 1 : 512, 1, 1 << 20);
}

// J2b — per-ACCESS fallback. When the inline data TLB cannot serve an
// address (an I/O register, most often), the emitted instruction can either
// hand the whole instruction back to Moira or call a thunk for the access
// alone and stay in host code for the rest. The second is much faster on
// hardware poll loops, which are all I/O; this switch exists so the two can
// be compared, and so the conservative path is one environment variable
// away if a machine ever disagrees.
// 0 = off (hand the whole instruction back), 1 = loads only, 2 = loads and
// stores. Split because a load that faults has committed nothing, while a
// store that succeeds already has.
inline int accessThunkMode() { return detail::envInt("POM68K_JIT_ACCESS_THUNK", 2, 0, 2); }

// Blocks kept in the cache before a full flush (the cache is a plain map;
// J1 does not do fine-grained eviction).
inline int maxBlocks() { return detail::envInt("POM68K_JIT_MAX_BLOCKS", 65536, 64, 1 << 20); }

// MEASUREMENT knob, not a tuning one: kill the code window every N retired
// instructions, on purpose. The engine's dominant residual cost is the
// window dying under ATC eviction — 794 M window-lost exits over 12.2 G
// instructions on the idle Finder (POM68K_JIT.md § 3) — and that figure was
// a rate with no price attached: nobody had measured what ONE exit costs.
// Forcing exits at a chosen rate over an otherwise identical workload makes
// the price the SLOPE of wall time against exit count. A window kill is
// architecturally invisible (the next arm re-derives the same window from
// the same translation), so a bench fingerprint must NOT move with N —
// which is what makes the regression a measurement rather than a story.
// 0 = off. Never set outside a bench: it slows the engine down on purpose.
inline int windowKillEvery() {
    return detail::envInt("POM68K_JIT_WINDOW_KILL", 0, 0, 1 << 24);
}

// ATTRIBUTION knob for the 68030 instruction-cache charge that generated
// code emits (docs/JIT_BRINGUP.md § B). The model is architecturally
// required — generated code fetches nothing, so without it the clock drifts
// from the interpreter's — but "required" and "correct" are different
// claims, and separating them needs a switch. Off, an 030 block charges the
// instruction cost alone and any residual divergence belongs to something
// else. Default ON; only a bring-up measurement should turn it off.
inline bool icacheEmitEnabled() {
    return detail::envBool("POM68K_JIT_ICACHE_EMIT", true);
}

// Chatter on stderr: backend selection, flushes, block statistics.
inline bool verbose() { return detail::envBool("POM68K_JIT_VERBOSE", false); }

// How many compiled blocks a code generator dumps under POM68K_JIT_VERBOSE.
// It used to be a hard-coded 40, which is the first 40 blocks of a boot —
// i.e. ROM reset code, and never the form you are actually chasing. The
// dump is the only place a block's per-instruction MEASURED cycle counts
// (`Instr::cycles`) are visible, and those are what the cost cross-check
// refuses on, so a refusal deep in a boot was undiagnosable without a
// rebuild. Raise it to find one; it is pure stderr volume.
inline int verboseBlocks() {
    return detail::envInt("POM68K_JIT_VERBOSE_BLOCKS", 40, 0, 1 << 24);
}

}  // namespace jit
