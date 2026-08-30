// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── JIT configuration (injected policy surface) ──
// One place for every knob of the second execution engine. The fastest
// conformant engine is the default on validated 68040 and 68030 guests; every
// other family keeps the Moira interpreter. The interpreter remains an
// explicit, continuously tested reference — see src/jit/POM68K_JIT.md.

#pragma once
#include "StartupSnapshot.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

namespace jit {

namespace option = pom68k::startup_option;

// Which execution engine drives a CPU wrapper. `Interp` calls
// Moira::executeUntil() exactly like before this subsystem existed.
enum class EngineKind { Interp, Jit };

// Stabilised operating profiles. Fine-grained variables remain supported as
// explicit overrides for attribution, but ordinary runs should select one
// coherent policy instead of assembling a dozen booleans by hand.
enum class OperatingProfile { Production, Conservative, Instrumented };

namespace detail {
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
    bool accessThunkExplicit = false;
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

    // 68030 memory bitfields through (An)/d16(An). Read-only forms preflight
    // an optional fifth-byte tail before the first load; tailless writes use
    // the shared read4/write4 RMW proof. ON after the cross-backend directed
    // 030 oracle, four real locksteps and the Speedometer E9D4 census
    // (2026-08-31). The explicit knob remains as an attribution/veto arm.
    bool memBitfield030 = true;

    // A64 pacing control is captured here too: a backend must never retain
    // a private getenv-based policy surface.
    bool a64Pacing = true;

    // Engine-level gate and bisection diagnostics. They used to be sampled
    // independently inside JitEngine.cpp; keeping them in this snapshot makes
    // a constructed Engine independent from later process-environment edits.
    bool requireNative = false;
    bool dispatchRing = false;
    uint32_t denyFrom = 0;
    uint32_t denyTo = 0;
    // POM68K_JIT_TRACE_BLOCK (hex pc): print a block's recorded IR — the
    // instrument that settled the 2026-08-29 ±2 (a traced field, not an
    // emitter, carried the answer). 0 = off.
    uint32_t traceBlockPc = 0;

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

inline ResolvedConfig resolveConfig(const pom68k::StartupSnapshot& values) {
    const auto present = [&](auto key) {
        return values.present(key);
    };
    const auto envText = [&](auto key) -> const char* {
        const auto value = values.text(key);
        return value ? value->data() : nullptr;
    };
    const auto envBool = [&](auto key, bool fallback) {
        return values.boolean(key, fallback);
    };
    const auto envInt = [&](auto key, int fallback) {
        return values.integer(key).value_or(fallback);
    };
    const auto envHex = [&](auto key) {
        return values.hexadecimal(key);
    };

    ResolvedConfig c;
    if (const char* v = envText(option::JitProfile)) {
        if (!std::strcmp(v, "conservative") || !std::strcmp(v, "safe") ||
            !std::strcmp(v, "proof"))
            c.profile = OperatingProfile::Conservative;
        else if (!std::strcmp(v, "instrumented") || !std::strcmp(v, "census"))
            c.profile = OperatingProfile::Instrumented;
    }
    if (const char* v = envText(option::JitBackend)) c.backend = v;
    if (const char* v = envText(option::CpuEngine)) {
        c.engineExplicit = true;
        c.engine = (!std::strncmp(v, "jit", 3) || v[0] == '1') ?
            EngineKind::Jit : EngineKind::Interp;
    }
    c.unsafeBackend = envBool(option::JitUnsafeBackend, false);
    c.fetchWindow = envBool(option::JitFetch, true);
    c.blockCacheExplicit = present(option::JitBlocks);
    c.blockCache = envBool(option::JitBlocks, false);
    c.maxBlockInstrs = envInt(option::JitBlockMax, 64);
    c.hotExplicit = present(option::JitHot);
    c.hot = envInt(option::JitHot, 512);

    const bool conservative = c.profile == OperatingProfile::Conservative;
    const bool instrumented = c.profile == OperatingProfile::Instrumented;
    const bool production = c.profile == OperatingProfile::Production;
    c.accessThunk = envInt(option::JitAccessThunk, conservative ? 0 : 2);
    c.accessThunkExplicit = present(option::JitAccessThunk);
    c.cache040LineReads = envBool(option::Jit040LineRead, !conservative);
    c.cache040LineWrites = envBool(option::Jit040LineWrite, !conservative);
    c.cache040LinePairs = envBool(option::Jit040LinePair, !conservative);
    c.cache040LineReadStats = envBool(option::Jit040LineStats, instrumented);
    c.links = envBool(option::JitLinks, production);
    c.packedCcr = envBool(option::JitPackedCcr, false);
    c.regCache = envBool(option::JitRegisterCache, false);
    c.edgeCells = envBool(option::JitEdgeCells, false);
    c.dynamicBitfield = envBool(option::JitDynamicBitfield, true);
    c.paranoid = envBool(option::JitParanoid, !production);
    c.histogram = envBool(option::JitHistogram, instrumented);
    c.maxBlocks = envInt(option::JitMaxBlocks, 65536);
    c.windowKill = envInt(option::JitWindowKill, 0);
    c.icacheEmit = envBool(option::JitICacheEmit, true);
    c.verbose = envBool(option::JitVerbose, false);
    c.verboseBlocks = envInt(option::JitVerboseBlocks, 40);
    c.watchOpcodes = detail::parseWatchOpcodes(
        envText(option::JitWatchOpcode), c.watchOpcode);
    c.minNativePercent = envInt(option::JitMinNative, 50);
    c.profitScoreExplicit = present(option::JitProfitScore);
    c.profitScore = envInt(option::JitProfitScore, 0);
    c.armBackoff = envInt(option::JitArmBackoff, 32);
    c.dataWindow = envBool(option::DataWindow, false);
    c.restartBaseExplicit = present(option::JitRestartBase);
    c.restartBaseAdmission = envBool(option::JitRestartBase, false);
    c.bsrwExplicit = present(option::JitBsrWide);
    c.bsrWideAdmission = envBool(option::JitBsrWide, false);
    c.memBitfield030 = envBool(option::JitMemBitfield030, true);
    c.a64Pacing = envBool(option::JitA64Pacing, true);
    c.requireNative = present(option::JitRequireNative);
    c.dispatchRing = envBool(option::JitDispatchRing, false);
    if (const auto value = envHex(option::JitDenyFrom)) c.denyFrom = *value;
    if (const auto value = envHex(option::JitDenyTo)) c.denyTo = *value;
    if (const auto value = envHex(option::JitTraceBlock))
        c.traceBlockPc = *value;
    return c;
}

// Explicit deterministic policy for unit-sized component construction.
// Product startup and process-driven test harnesses inject their own resolved
// snapshot. Tests which deliberately want the defaults must name this object;
// Engine construction itself has no hidden fallback.
inline const ResolvedConfig& defaultResolvedConfig() {
    static const ResolvedConfig config;
    return config;
}

namespace detail {
inline thread_local const ResolvedConfig* activeConfig = nullptr;
}  // namespace detail

// Backends enter this scope while compiling. It preserves the legacy leaf
// accessors below, but redirects them to the owning Engine's snapshot rather
// than process-global lookup, including when two Engines compile concurrently.
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
    return detail::activeConfig ? detail::activeConfig->profile
                                : OperatingProfile::Production;
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
// is the evidence-backed per-family policy supplied by Engine: true for the
// validated 68040 and 68030 paths. The GUI can still switch live either way.
inline EngineKind defaultEngine(bool jitByDefault) {
    if (detail::activeConfig)
        return detail::activeConfig->engineForGuest(jitByDefault);
    return jitByDefault ? EngineKind::Jit : EngineKind::Interp;
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
    return detail::activeConfig ? detail::activeConfig->backend.c_str() : "auto";
}

// J1a — instruction-fetch window. The measured win of the whole J1 stage;
// kept separately switchable so its contribution can be attributed.
inline bool fetchWindowEnabled() {
    return detail::activeConfig ? detail::activeConfig->fetchWindow : true;
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
    return dflt;
}

// Straight-line instruction ceiling per block. A block also ends at the
// first control-flow, MMU-touching or capability-missing instruction.
inline int maxBlockInstrs() {
    return detail::activeConfig ? detail::activeConfig->maxBlockInstrs : 64;
}

// Visits before a recorded block is handed to the backend. Native generators
// default to immediate compilation: after incremental I-cache invalidation,
// AArch64 measures 3.73x faster at 1, while 512 leaves 94% of the fixed Q605
// workload interpreted and loses the entire gain. Portable replay retains
// the conservative threshold when its block path is explicitly enabled.
// POM68K_JIT_HOT overrides both defaults for experiments.
inline int hotThreshold(bool nativeCode) {
    if (detail::activeConfig) return detail::activeConfig->hot;
    return nativeCode ? 1 : 512;
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
    return detail::activeConfig ? detail::activeConfig->accessThunk : 2;
}

// JIT_BRINGUP § C.4nonies admission: base-cost admission of the 030
// restartable-write family. Default is per-backend (the backend's
// accessClockBias declaration — ON under x64 and a64 since 2026-08-22,
// both emitters consulting it for the MOVE-to-memory admission); the env
// fallback below only serves code running outside an Engine's scope.
inline bool restartBaseAdmission() {
    return detail::activeConfig ? detail::activeConfig->restartBaseAdmission : false;
}

// Same § and same resolution: BSR.W into the armed-charge exemption.
inline bool bsrWideAdmission() {
    return detail::activeConfig ? detail::activeConfig->bsrWideAdmission : false;
}

// POM68K_JIT_030_MEMBF — 68030 memory-bitfield admission. Production is ON;
// an explicit 0 remains the exact attribution/veto arm.
inline bool memBitfield030Admission() {
    return detail::activeConfig ? detail::activeConfig->memBitfield030 : true;
}

// J4 — resident 68040 D-cache line reads. Kept independently switchable so
// the cache-aware JIT can be compared against the exact-access control path
// without changing the architectural cache model itself.
inline bool cache040LineReadsEnabled() {
    return detail::activeConfig ? detail::activeConfig->cache040LineReads : true;
}
inline bool cache040LineWritesEnabled() {
    return detail::activeConfig ? detail::activeConfig->cache040LineWrites : true;
}
inline bool cache040LinePairsEnabled() {
    return detail::activeConfig ? detail::activeConfig->cache040LinePairs : true;
}
inline bool cache040LineReadStatsEnabled() {
    return detail::activeConfig ? detail::activeConfig->cache040LineReadStats : false;
}

inline bool linksEnabled() {
    return detail::activeConfig ? detail::activeConfig->links : true;
}

// Generated-code CCR cache.  The representation is deliberately the 68k
// CCR bit layout (C=0,V=1,Z=2,N=3,X=4), so spilling never translates a
// backend-private condition format.  It is conformant, but remains
// independently switchable for lockstep attribution and conservative-mode
// comparison.
inline bool packedCcrEnabled() {
    return detail::activeConfig ? detail::activeConfig->packedCcr : false;
}

inline bool registerCacheEnabled() {
    return detail::activeConfig ? detail::activeConfig->regCache : false;
}

inline bool edgeLinkCellsEnabled() {
    return detail::activeConfig ? detail::activeConfig->edgeCells : false;
}

inline bool dynamicRegisterBitfieldEnabled() {
    return detail::activeConfig ? detail::activeConfig->dynamicBitfield : true;
}

inline bool paranoidEnabled() {
    return detail::activeConfig ? detail::activeConfig->paranoid : false;
}

inline bool histogramEnabled() {
    return detail::activeConfig ? detail::activeConfig->histogram : false;
}

// Blocks kept in the cache before a full flush (the cache is a plain map;
// J1 does not do fine-grained eviction).
inline int maxBlocks() {
    return detail::activeConfig ? detail::activeConfig->maxBlocks : 65536;
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
    return detail::activeConfig ? detail::activeConfig->windowKill : 0;
}

// ATTRIBUTION knob for the 68030 instruction-cache charge that generated
// code emits (docs/JIT_BRINGUP.md § B). The model is architecturally
// required — generated code fetches nothing, so without it the clock drifts
// from the interpreter's — but "required" and "correct" are different
// claims, and separating them needs a switch. Off, an 030 block charges the
// instruction cost alone and any residual divergence belongs to something
// else. Default ON; only a bring-up measurement should turn it off.
inline bool icacheEmitEnabled() {
    return detail::activeConfig ? detail::activeConfig->icacheEmit : true;
}

// Chatter on stderr: backend selection, flushes, block statistics.
inline bool verbose() {
    return detail::activeConfig ? detail::activeConfig->verbose : false;
}

// How many compiled blocks a code generator dumps under POM68K_JIT_VERBOSE.
// It used to be a hard-coded 40, which is the first 40 blocks of a boot —
// i.e. ROM reset code, and never the form you are actually chasing. The
// dump is the only place a block's per-instruction MEASURED cycle counts
// (`Instr::cycles`) are visible, and those are what the cost cross-check
// refuses on, so a refusal deep in a boot was undiagnosable without a
// rebuild. Raise it to find one; it is pure stderr volume.
inline int verboseBlocks() {
    return detail::activeConfig ? detail::activeConfig->verboseBlocks : 40;
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
    return detail::activeConfig ? detail::activeConfig->minNativePercent : 50;
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
    return detail::activeConfig ? detail::activeConfig->armBackoff : 32;
}

inline bool a64PacingEnabled() {
    return detail::activeConfig ? detail::activeConfig->a64Pacing : true;
}

}  // namespace jit
