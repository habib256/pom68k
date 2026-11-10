// Compile-time host identity for backend diagnostics and metrics.
#pragma once

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

} // namespace jit
