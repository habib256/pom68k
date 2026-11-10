// POM68K — host startup adapter for the portable JIT configuration.
// Startup option names and decoding belong to the embedding application.
// The JIT itself consumes only jit::ResolvedConfig from jit/JitConfig.h.

#pragma once

#include "StartupSnapshot.h"
#include "jit/JitConfig.h"

#include <cstdlib>
#include <cstring>

namespace pom68k {

namespace jit_startup_detail {
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

}  // namespace jit_startup_detail

inline jit::ResolvedConfig resolveJitConfig(const StartupSnapshot& values) {
    namespace option = startup_option;
    using jit::EngineKind;
    using jit::OperatingProfile;
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

    jit::ResolvedConfig c;
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
    c.cache040Model = envBool(option::Cache040Model, false);
    c.cache040CmStats = present(option::Cache040CmStats);
    c.exceptionDiag = present(option::MoiraExceptionDiag);
    // Opt-in: the cache-active 040 late-IPL-poll admission. Conformant
    // (locksteps identical with it on) but measured −6.3 % wall on the
    // cache-on fixed-budget Q605 bench (2026-09-03): the admitted class is
    // the boot's hot poll loops, whose native line path loses to the fetch
    // window. Off until a workload shows the win the window-lengthening
    // argument predicts.
    c.cache040LatePoll = envBool(option::Jit040LatePoll, false);
    c.fpuMember = envBool(option::JitFpuMember, true);
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
    c.watchOpcodes = jit_startup_detail::parseWatchOpcodes(
        envText(option::JitWatchOpcode), c.watchOpcode);
    c.minNativePercent = envInt(option::JitMinNative, 50);
    c.profitScoreExplicit = present(option::JitProfitScore);
    c.profitScore = envInt(option::JitProfitScore, 0);
    c.armBackoff = envInt(option::JitArmBackoff, 32);
    c.dataWindowExplicit = present(option::DataWindow);
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

}  // namespace pom68k
