// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── JIT configuration (environment surface) ──
// One place for every knob of the second execution engine. The fastest
// conformant engine is the default on 68040 guests; every other family keeps
// the Moira interpreter. The interpreter remains an explicit, continuously
// tested reference — see src/jit/POM68K_JIT.md.

#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

namespace jit {

// Which execution engine drives a CPU wrapper. `Interp` calls
// Moira::executeUntil() exactly like before this subsystem existed.
enum class EngineKind { Interp, Jit };

// Stabilised operating profiles. Fine-grained variables remain supported as
// explicit overrides for attribution, but ordinary runs should select one
// coherent policy instead of assembling a dozen booleans by hand.
enum class OperatingProfile { Production, Conservative, Instrumented };

namespace detail {

inline const char* env(const char* key) {
    const char* v = std::getenv(key);
    return (v && *v) ? v : nullptr;
}

// "<hex>[,<hex>…]" → up to four opcodes; returns how many were parsed.
inline int parseWatchOpcodes(const char* v, uint16_t out[4]) {
    int n = 0;
    while (v && *v && n < 4) {
        char* end = nullptr;
        const unsigned long x = std::strtoul(v, &end, 16);
        if (end == v) break;
        out[n++] = uint16_t(x);
        v = *end == ',' ? end + 1 : end;
    }
    return n;
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

// One immutable policy snapshot per Engine. Environment variables are an
// input format, not live process-wide configuration: changing one after an
// Engine was constructed must not alter a later block compiled by that
// Engine. `applyBackendDefaults` resolves the defaults that depend on the
// selected backend — block cache, hot threshold, and the two § C.4nonies
// admissions — before the snapshot is published through Context.
struct ResolvedConfig {
    OperatingProfile profile = OperatingProfile::Production;
    std::string backend = "auto";
    EngineKind engine = EngineKind::Interp;
    bool engineExplicit = false;
    bool unsafeBackend = false;

    bool fetchWindow = true;
    bool blockCache = false;
    bool blockCacheExplicit = false;
    int maxBlockInstrs = 64;
    int hot = 512;
    bool hotExplicit = false;
    int accessThunk = 2;
    bool cache040LineReads = true;
    bool cache040LineWrites = true;
    bool cache040LinePairs = true;
    bool cache040LineReadStats = false;
    bool links = true;
    // Keep XNZVC in the low five bits of the generated backend's retired
    // counter register.  Architectural flag bytes are materialised only at
    // helper/exit boundaries; conservative mode keeps the original eager
    // stores as a continuously available oracle.
    bool packedCcr = false;
    bool regCache = false;
    bool edgeCells = false;
    // A64 register bitfields whose offset and/or width come from Dn. This is
    // a production admission with a negative attribution switch: Rogue's
    // measured mask loop is dominated by BFEXTU/BFINS in exactly this form.
    bool dynamicBitfield = true;
    bool paranoid = false;
    bool histogram = false;
    int maxBlocks = 65536;
    int windowKill = 0;
    bool icacheEmit = true;
    bool verbose = false;
    int verboseBlocks = 40;
    // Diagnosis: up to four opcodes whose compile-time refusal the a64
    // backend reports with its admission inputs (POM68K_JIT_WATCH_OPCODE,
    // hex, comma-separated). Parsed here so the backend reads no env.
    uint16_t watchOpcode[4] = {};
    int watchOpcodes = 0;
    int minNativePercent = 50;
    int profitScore = 0;
    bool profitScoreExplicit = false;
    int armBackoff = 32;
    bool dataWindow = false;
    // The § C.4nonies admissions: the 030 restartable-write family on the
    // split BASE cost, and BSR.W ($6100) into the armed-charge exemption.
    // Their default is PER-BACKEND, resolved by applyBackendDefaults from
    // the backend's `accessClockBias` declaration — ON where the access
    // thunks carry the peripheral-phase clock bias (x64 since 2026-08-22,
    // measured −4.3 % / −2.3 % alone and −8.0 % together at 6000 frames,
    // fp identical; a64 since the same day — its emitter consults both
    // knobs since the evening, and the total-cost rule it had hard-wired
    // was refusing every push traced on an i-cache miss). An explicit
    // env wins in either direction; jit_lockstep_030_{x64,a64}_alignment_test
    // pin both ON at 120k.
    bool restartBaseAdmission = false;
    bool restartBaseExplicit = false;
    bool bsrWideAdmission = false;
    bool bsrwExplicit = false;

    // A64 pacing control is captured here too: a backend must never retain
    // a private getenv-based policy surface.
    bool a64Pacing = true;

    void applyBackendDefaults(bool nativeCode, bool accessClockBias) {
        if (!blockCacheExplicit) blockCache = nativeCode;
        if (!hotExplicit) hot = nativeCode ? 1 : 512;
        // The admissions follow the backend's alignment declaration — see
        // the field comment above. A backend that never emits (threaded)
        // declares false and the value is then inert anyway.
        if (!restartBaseExplicit) restartBaseAdmission = accessClockBias;
        if (!bsrwExplicit) bsrWideAdmission = accessClockBias;
    }

    EngineKind engineForGuest(bool jitByDefault) const {
        return engineExplicit ? engine :
            (jitByDefault ? EngineKind::Jit : EngineKind::Interp);
    }

    const char* profileName() const {
        switch (profile) {
            case OperatingProfile::Conservative: return "conservative";
            case OperatingProfile::Instrumented: return "instrumented";
            default: return "production";
        }
    }
};

inline ResolvedConfig resolveConfig() {
    ResolvedConfig c;
    if (const char* v = detail::env("POM68K_JIT_PROFILE")) {
        if (!std::strcmp(v, "conservative") || !std::strcmp(v, "safe") ||
            !std::strcmp(v, "proof"))
            c.profile = OperatingProfile::Conservative;
        else if (!std::strcmp(v, "instrumented") || !std::strcmp(v, "census"))
            c.profile = OperatingProfile::Instrumented;
    }
    if (const char* v = detail::env("POM68K_JIT_BACKEND")) c.backend = v;
    if (const char* v = detail::env("POM68K_CPU_ENGINE")) {
        c.engineExplicit = true;
        c.engine = (!std::strncmp(v, "jit", 3) || v[0] == '1') ?
            EngineKind::Jit : EngineKind::Interp;
    }
    c.unsafeBackend = detail::envBool("POM68K_JIT_UNSAFE_BACKEND", false);
    c.fetchWindow = detail::envBool("POM68K_JIT_FETCH", true);
    c.blockCacheExplicit = detail::env("POM68K_JIT_BLOCKS") != nullptr;
    c.blockCache = detail::envBool("POM68K_JIT_BLOCKS", false);
    c.maxBlockInstrs = detail::envInt("POM68K_JIT_BLOCK_MAX", 64, 1, 256);
    c.hotExplicit = detail::env("POM68K_JIT_HOT") != nullptr;
    c.hot = detail::envInt("POM68K_JIT_HOT", 512, 1, 1 << 20);

    const bool conservative = c.profile == OperatingProfile::Conservative;
    const bool instrumented = c.profile == OperatingProfile::Instrumented;
    const bool production = c.profile == OperatingProfile::Production;
    c.accessThunk = detail::envInt("POM68K_JIT_ACCESS_THUNK",
                                   conservative ? 0 : 2, 0, 2);
    c.cache040LineReads = detail::envBool("POM68K_JIT_040_LINE_READ", !conservative);
    c.cache040LineWrites = detail::envBool("POM68K_JIT_040_LINE_WRITE", !conservative);
    c.cache040LinePairs = detail::envBool("POM68K_JIT_040_LINE_PAIR", !conservative);
    c.cache040LineReadStats = detail::envBool("POM68K_JIT_040_LINE_STATS", instrumented);
    c.links = detail::envBool("POM68K_JIT_LINKS", production);
    c.packedCcr = detail::envBool("POM68K_JIT_PACKED_CCR", false);
    c.regCache = detail::envBool("POM68K_JIT_REG_CACHE", false);
    c.edgeCells = detail::envBool("POM68K_JIT_EDGE_CELLS", false);
    c.dynamicBitfield = detail::envBool("POM68K_JIT_DYNAMIC_BITFIELD", true);
    c.paranoid = detail::envBool("POM68K_JIT_PARANOID", !production);
    c.histogram = detail::envBool("POM68K_JIT_HISTO", instrumented);
    c.maxBlocks = detail::envInt("POM68K_JIT_MAX_BLOCKS", 65536, 64, 1 << 20);
    c.windowKill = detail::envInt("POM68K_JIT_WINDOW_KILL", 0, 0, 1 << 24);
    c.icacheEmit = detail::envBool("POM68K_JIT_ICACHE_EMIT", true);
    c.verbose = detail::envBool("POM68K_JIT_VERBOSE", false);
    c.verboseBlocks = detail::envInt("POM68K_JIT_VERBOSE_BLOCKS", 40, 0, 1 << 24);
    c.watchOpcodes = detail::parseWatchOpcodes(
        detail::env("POM68K_JIT_WATCH_OPCODE"), c.watchOpcode);
    c.minNativePercent = detail::envInt("POM68K_JIT_MIN_NATIVE", 50, 0, 100);
    c.profitScoreExplicit = detail::env("POM68K_JIT_PROFIT_SCORE") != nullptr;
    c.profitScore = detail::envInt("POM68K_JIT_PROFIT_SCORE", 0, 0, 1 << 30);
    c.armBackoff = detail::envInt("POM68K_JIT_ARM_BACKOFF", 32, 0, 4096);
    c.dataWindow = detail::envBool("POM68K_DATA_WINDOW", false);
    c.restartBaseExplicit = detail::env("POM68K_JIT_RESTART_BASE") != nullptr;
    c.restartBaseAdmission =
        detail::envBool("POM68K_JIT_RESTART_BASE", false);
    c.bsrwExplicit = detail::env("POM68K_JIT_BSRW") != nullptr;
    c.bsrWideAdmission = detail::envBool("POM68K_JIT_BSRW", false);
    c.a64Pacing = detail::envBool("POM68K_JIT_A64_PACING", true);
    return c;
}

namespace detail {
inline thread_local const ResolvedConfig* activeConfig = nullptr;
}  // namespace detail

// Backends enter this scope while compiling. It preserves the legacy leaf
// accessors below, but redirects them to the owning Engine's snapshot rather
// than getenv(), including when two Engines compile concurrently.
class ScopedResolvedConfig {
public:
    explicit ScopedResolvedConfig(const ResolvedConfig* config)
        : previous_(detail::activeConfig) { detail::activeConfig = config; }
    ~ScopedResolvedConfig() { detail::activeConfig = previous_; }
    ScopedResolvedConfig(const ScopedResolvedConfig&) = delete;
    ScopedResolvedConfig& operator=(const ScopedResolvedConfig&) = delete;
private:
    const ResolvedConfig* previous_;
};

inline OperatingProfile operatingProfile() {
    if (detail::activeConfig) return detail::activeConfig->profile;
    const char* v = detail::env("POM68K_JIT_PROFILE");
    if (!v) return OperatingProfile::Production;
    if (!std::strcmp(v, "conservative") || !std::strcmp(v, "safe") ||
        !std::strcmp(v, "proof"))
        return OperatingProfile::Conservative;
    if (!std::strcmp(v, "instrumented") || !std::strcmp(v, "census"))
        return OperatingProfile::Instrumented;
    return OperatingProfile::Production;
}

inline const char* operatingProfileName() {
    if (detail::activeConfig) return detail::activeConfig->profileName();
    switch (operatingProfile()) {
        case OperatingProfile::Conservative: return "conservative";
        case OperatingProfile::Instrumented: return "instrumented";
        default: return "production";
    }
}

// An explicit POM68K_CPU_ENGINE always wins. With it unset, `jitByDefault`
// is the evidence-backed per-family policy supplied by Engine: true only for
// the 68040 today. The GUI menu can still switch live in either direction.
inline EngineKind defaultEngine(bool jitByDefault) {
    if (detail::activeConfig)
        return detail::activeConfig->engineForGuest(jitByDefault);
    const char* v = detail::env("POM68K_CPU_ENGINE");
    if (!v) return jitByDefault ? EngineKind::Jit : EngineKind::Interp;
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
    if (detail::activeConfig) return detail::activeConfig->backend.c_str();
    const char* v = detail::env("POM68K_JIT_BACKEND");
    return v ? v : "auto";
}

// J1a — instruction-fetch window. The measured win of the whole J1 stage;
// kept separately switchable so its contribution can be attributed.
inline bool fetchWindowEnabled() {
    return detail::activeConfig ? detail::activeConfig->fetchWindow :
        detail::envBool("POM68K_JIT_FETCH", true);
}

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
    if (detail::activeConfig) return detail::activeConfig->blockCache;
    return detail::envBool("POM68K_JIT_BLOCKS", dflt);
}

// Straight-line instruction ceiling per block. A block also ends at the
// first control-flow, MMU-touching or capability-missing instruction.
inline int maxBlockInstrs() {
    return detail::activeConfig ? detail::activeConfig->maxBlockInstrs :
        detail::envInt("POM68K_JIT_BLOCK_MAX", 64, 1, 256);
}

// Visits before a recorded block is handed to the backend. Native generators
// default to immediate compilation: after incremental I-cache invalidation,
// AArch64 measures 3.73x faster at 1, while 512 leaves 94% of the fixed Q605
// workload interpreted and loses the entire gain. Portable replay retains
// the conservative threshold when its block path is explicitly enabled.
// POM68K_JIT_HOT overrides both defaults for experiments.
inline int hotThreshold(bool nativeCode) {
    if (detail::activeConfig) return detail::activeConfig->hot;
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
inline int accessThunkMode() {
    if (detail::activeConfig) return detail::activeConfig->accessThunk;
    const int dflt = operatingProfile() == OperatingProfile::Conservative ? 0 : 2;
    return detail::envInt("POM68K_JIT_ACCESS_THUNK", dflt, 0, 2);
}

// JIT_BRINGUP § C.4nonies admission: base-cost admission of the 030
// restartable-write family. Default is per-backend (the backend's
// accessClockBias declaration — ON under x64 and a64 since 2026-08-22,
// both emitters consulting it for the MOVE-to-memory admission); the env
// fallback below only serves code running outside an Engine's scope.
inline bool restartBaseAdmission() {
    if (detail::activeConfig) return detail::activeConfig->restartBaseAdmission;
    return detail::envBool("POM68K_JIT_RESTART_BASE", false);
}

// Same § and same resolution: BSR.W into the armed-charge exemption.
inline bool bsrWideAdmission() {
    if (detail::activeConfig) return detail::activeConfig->bsrWideAdmission;
    return detail::envBool("POM68K_JIT_BSRW", false);
}

// J4 — resident 68040 D-cache line reads. Kept independently switchable so
// the cache-aware JIT can be compared against the exact-access control path
// without changing the architectural cache model itself.
inline bool cache040LineReadsEnabled() {
    if (detail::activeConfig) return detail::activeConfig->cache040LineReads;
    return detail::envBool("POM68K_JIT_040_LINE_READ",
        operatingProfile() != OperatingProfile::Conservative);
}
inline bool cache040LineWritesEnabled() {
    if (detail::activeConfig) return detail::activeConfig->cache040LineWrites;
    return detail::envBool("POM68K_JIT_040_LINE_WRITE",
        operatingProfile() != OperatingProfile::Conservative);
}
inline bool cache040LinePairsEnabled() {
    if (detail::activeConfig) return detail::activeConfig->cache040LinePairs;
    return detail::envBool("POM68K_JIT_040_LINE_PAIR",
        operatingProfile() != OperatingProfile::Conservative);
}
inline bool cache040LineReadStatsEnabled() {
    if (detail::activeConfig) return detail::activeConfig->cache040LineReadStats;
    return detail::envBool("POM68K_JIT_040_LINE_STATS",
        operatingProfile() == OperatingProfile::Instrumented);
}

inline bool linksEnabled() {
    if (detail::activeConfig) return detail::activeConfig->links;
    return detail::envBool("POM68K_JIT_LINKS",
        operatingProfile() == OperatingProfile::Production);
}

// Generated-code CCR cache.  The representation is deliberately the 68k
// CCR bit layout (C=0,V=1,Z=2,N=3,X=4), so spilling never translates a
// backend-private condition format.  It is conformant, but remains
// independently switchable for lockstep attribution and conservative-mode
// comparison.
inline bool packedCcrEnabled() {
    if (detail::activeConfig) return detail::activeConfig->packedCcr;
    return detail::envBool("POM68K_JIT_PACKED_CCR", false);
}

inline bool registerCacheEnabled() {
    if (detail::activeConfig) return detail::activeConfig->regCache;
    return detail::envBool("POM68K_JIT_REG_CACHE", false);
}

inline bool edgeLinkCellsEnabled() {
    if (detail::activeConfig) return detail::activeConfig->edgeCells;
    return detail::envBool("POM68K_JIT_EDGE_CELLS", false);
}

inline bool dynamicRegisterBitfieldEnabled() {
    if (detail::activeConfig) return detail::activeConfig->dynamicBitfield;
    return detail::envBool("POM68K_JIT_DYNAMIC_BITFIELD", true);
}

inline bool paranoidEnabled() {
    if (detail::activeConfig) return detail::activeConfig->paranoid;
    return detail::envBool("POM68K_JIT_PARANOID",
        operatingProfile() != OperatingProfile::Production);
}

inline bool histogramEnabled() {
    if (detail::activeConfig) return detail::activeConfig->histogram;
    return detail::envBool("POM68K_JIT_HISTO",
        operatingProfile() == OperatingProfile::Instrumented);
}

// Blocks kept in the cache before a full flush (the cache is a plain map;
// J1 does not do fine-grained eviction).
inline int maxBlocks() {
    return detail::activeConfig ? detail::activeConfig->maxBlocks :
        detail::envInt("POM68K_JIT_MAX_BLOCKS", 65536, 64, 1 << 20);
}

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
    if (detail::activeConfig) return detail::activeConfig->windowKill;
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
    if (detail::activeConfig) return detail::activeConfig->icacheEmit;
    return detail::envBool("POM68K_JIT_ICACHE_EMIT", true);
}

// Chatter on stderr: backend selection, flushes, block statistics.
inline bool verbose() {
    return detail::activeConfig ? detail::activeConfig->verbose :
        detail::envBool("POM68K_JIT_VERBOSE", false);
}

// How many compiled blocks a code generator dumps under POM68K_JIT_VERBOSE.
// It used to be a hard-coded 40, which is the first 40 blocks of a boot —
// i.e. ROM reset code, and never the form you are actually chasing. The
// dump is the only place a block's per-instruction MEASURED cycle counts
// (`Instr::cycles`) are visible, and those are what the cost cross-check
// refuses on, so a refusal deep in a boot was undiagnosable without a
// rebuild. Raise it to find one; it is pure stderr volume.
inline int verboseBlocks() {
    if (detail::activeConfig) return detail::activeConfig->verboseBlocks;
    return detail::envInt("POM68K_JIT_VERBOSE_BLOCKS", 40, 0, 1 << 24);
}

// POM68K_JIT_WATCH_OPCODE — is `op` one of the watched opcodes? Consulted
// by the a64 compile loop only on the fallback path, so the cost of the
// instrument when unset is one load of a zero count.
inline bool watchOpcodeWanted(uint16_t op) {
    if (detail::activeConfig) {
        const ResolvedConfig& c = *detail::activeConfig;
        for (int i = 0; i < c.watchOpcodes; i++)
            if (c.watchOpcode[i] == op) return true;
        return false;
    }
    uint16_t list[4]; const int n = detail::parseWatchOpcodes(
        detail::env("POM68K_JIT_WATCH_OPCODE"), list);
    for (int i = 0; i < n; i++) if (list[i] == op) return true;
    return false;
}

// The share of a block's instructions a code generator must emit natively
// before the block is worth keeping. Below it the block is refused and the
// engine runs the fetch window instead — same interpreter work without a
// call and a frame, which is the right trade for a block that is ALL
// fallbacks. The 50 % it started at is an inherited 68040 number and never
// bites there (98.5 % coverage); on a 68030 it refused 202 848 of 277 002
// compile attempts — 73 % — and with them two thirds of all execution
// (docs/JIT_BRINGUP.md § C.4bis). A knob so the trade can be measured on
// each guest instead of assumed from one.
inline int minNativePercent() {
    if (detail::activeConfig) return detail::activeConfig->minNativePercent;
    return detail::envInt("POM68K_JIT_MIN_NATIVE", 50, 0, 100);
}

// Instructions single-stepped through the interpreter after `armWindow`
// refuses a pc, before the engine probes again. Probing on EVERY
// instruction is what once made the 030 machines slower under the JIT than
// interpreted, hence a backoff — but `armWindow`'s own comment says the
// interpreter's fetch fills the ATC and "the next attempt usually
// succeeds", which argues the other way — and the backoff does carry 13.5 %
// of all retired instructions on the LC II (998 655 refusals x 32).
//
// **It looked like a 2.9 % win and it is not one.** A single-run sweep read
//   32 -> 16.04 s   8 -> 15.65 s   2 -> 15.57 s   0 -> 15.64 s
// which is a tidy curve with a minimum at 2. Re-measured INTERLEAVED, three
// repeats per arm on one binary (docs/MEASURING.md § R1), it inverts:
// 32 medians 15.61 s (15.36-15.75), 2 medians 15.95 s (15.47-16.01) — both
// spreads larger than the gap. The whole effect was the host's noise floor,
// and the first sweep found a minimum in it because a sweep of single runs
// always finds one somewhere. **Default stays 32**; the knob stays because
// pricing this again needs it, not because 32 is proven optimal.
inline int armBackoffSteps() {
    if (detail::activeConfig) return detail::activeConfig->armBackoff;
    return detail::envInt("POM68K_JIT_ARM_BACKOFF", 32, 0, 4096);
}

inline bool a64PacingEnabled() {
    return detail::activeConfig ? detail::activeConfig->a64Pacing :
        detail::envBool("POM68K_JIT_A64_PACING", true);
}

}  // namespace jit
