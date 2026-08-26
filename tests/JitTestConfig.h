// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Process-environment adapter for standalone test and benchmark executables.
// Production captures the same keys in RuntimeConfig. Keeping this adapter in
// tests makes the core consume only an injected immutable snapshot.

#pragma once

#include "jit/JitConfig.h"

#include <cstdlib>
#include <utility>
#include <vector>

namespace testjit {

inline jit::ResolvedConfig resolveFromEnvironment() {
    std::vector<pom68k::StartupSnapshot::Entry> entries;
    for (const pom68k::StartupOptionSpec option :
         pom68k::startup_option::kAll) {
        if (!pom68k::startupDomainIncludes(
                option.domains, pom68k::StartupDomain::Jit))
            continue;
        const char* value = std::getenv(option.name.data());
        if (value && *value) entries.emplace_back(option.name, value);
    }
    return jit::resolveConfig(
        pom68k::StartupSnapshot(std::move(entries)));
}

}  // namespace testjit
