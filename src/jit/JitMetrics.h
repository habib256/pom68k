// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Stable, host-neutral JIT metrics emitted by the asset-free CI proof and by
// the fixed-cycle Macintosh benches. The JSON schema is deliberately flat:
// CI can archive/compare it without a project-specific parser.

#pragma once

#include <cstdint>
#include <cstdio>

namespace jit {

inline const char* metricsHostArchitecture() {
#if defined(__aarch64__) || defined(_M_ARM64)
    return "aarch64";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#else
    return "other";
#endif
}

inline const char* metricsHostProfile(const char* configured = nullptr) {
    const char* profile = configured;
    return profile && *profile ? profile : metricsHostArchitecture();
}

struct MetricsRecord {
    const char* gate = "";
    const char* workload = "";
    const char* cpuFamily = "";
    const char* backend = "";
    const char* engine = "";
    const char* status = "pass";
    uint64_t machineCycles = 0;
    uint64_t coreCycles = 0;
    uint64_t wallNs = 0;
    uint64_t fingerprint = 0;
    uint64_t blocksCompiled = 0;
    uint64_t blocksRun = 0;
    uint64_t jitInstrs = 0;
    uint64_t nativeInstrs = 0;
    uint64_t interpInstrs = 0;
    uint64_t slowInstrs = 0;
    uint64_t windowInstrs = 0;
    uint64_t nativeSharePermille = 0;
    uint64_t realtimePermille = 0;
};

inline void writeMetricsJson(FILE* out, const MetricsRecord& r,
                             const char* hostProfile = nullptr) {
    // All string values are identifiers selected by the program, not user
    // text. Keeping this writer closed over that vocabulary avoids pulling a
    // JSON dependency into the emulator core and its smallest gates.
    std::fprintf(out,
        "{\"schema\":\"pom68k.jit.metrics.v1\","
        "\"gate\":\"%s\",\"workload\":\"%s\","
        "\"cpu_family\":\"%s\",\"host_arch\":\"%s\","
        "\"host_profile\":\"%s\","
        "\"backend\":\"%s\",\"engine\":\"%s\","
        "\"status\":\"%s\",\"machine_cycles\":%llu,"
        "\"core_cycles\":%llu,\"wall_ns\":%llu,"
        "\"fingerprint\":\"%016llx\","
        "\"blocks_compiled\":%llu,\"blocks_run\":%llu,"
        "\"jit_instrs\":%llu,\"native_instrs\":%llu,"
        "\"interp_instrs\":%llu,\"slow_instrs\":%llu,"
        "\"window_instrs\":%llu,\"native_share_permille\":%llu,"
        "\"realtime_permille\":%llu}",
        r.gate, r.workload, r.cpuFamily, metricsHostArchitecture(),
        metricsHostProfile(hostProfile),
        r.backend, r.engine, r.status,
        (unsigned long long)r.machineCycles,
        (unsigned long long)r.coreCycles,
        (unsigned long long)r.wallNs,
        (unsigned long long)r.fingerprint,
        (unsigned long long)r.blocksCompiled,
        (unsigned long long)r.blocksRun,
        (unsigned long long)r.jitInstrs,
        (unsigned long long)r.nativeInstrs,
        (unsigned long long)r.interpInstrs,
        (unsigned long long)r.slowInstrs,
        (unsigned long long)r.windowInstrs,
        (unsigned long long)r.nativeSharePermille,
        (unsigned long long)r.realtimePermille);
}

// Always publish one machine-readable line in the test log. The caller may
// inject an artifact path and host profile captured at its process boundary;
// failure to create a requested artifact is reported.
inline bool emitMetrics(const MetricsRecord& r, const char* path = nullptr,
                        const char* hostProfile = nullptr) {
    std::fputs("POM68K_JIT_METRICS ", stdout);
    writeMetricsJson(stdout, r, hostProfile);
    std::fputc('\n', stdout);

    if (!path || !*path) return true;
    FILE* out = std::fopen(path, "wb");
    if (!out) return false;
    writeMetricsJson(out, r, hostProfile);
    std::fputc('\n', out);
    return std::fclose(out) == 0;
}

} // namespace jit
