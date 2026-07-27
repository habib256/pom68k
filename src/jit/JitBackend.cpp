// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Backend registry. The ORDER of kEntries is the policy: most capable
// first, `threaded` last and unconditional. `auto` walks the list and takes
// the first entry that is both compiled in and usable() on this machine, so
// a host with no code generator — or a hardened kernel that refuses
// executable pages — still gets a working JIT instead of an error.

#include "JitBackend.h"
#include "JitConfig.h"
#include "backends/JitBackendThreaded.h"

#include <cstdio>
#include <cstring>

#if defined(POM68K_JIT_BACKEND_X64)
#include "backends/JitBackendX64.h"
#endif
#if defined(POM68K_JIT_BACKEND_A64)
#include "backends/JitBackendA64.h"
#endif

namespace jit {

namespace {

struct Entry {
    const char* key;
    Backend* (*get)();
};

const Entry kEntries[] = {
#if defined(POM68K_JIT_BACKEND_X64)
    { "x64", x64Backend },
#endif
#if defined(POM68K_JIT_BACKEND_A64)
    { "a64", a64Backend },
#endif
    { "threaded", threadedBackend },   // always present, always usable
};

constexpr int kCount = int(sizeof(kEntries) / sizeof(kEntries[0]));

const char* kNames[kCount] = {};

}  // namespace

Backend* selectBackend(const char* pref) {
    if (!pref || !*pref) pref = "auto";

    if (std::strcmp(pref, "auto") != 0) {
        for (const Entry& e : kEntries) {
            if (std::strcmp(e.key, pref) != 0) continue;
            Backend* b = e.get();
            if (b->usable()) return b;
            std::fprintf(stderr,
                         "[jit] backend '%s' is compiled in but not usable here — "
                         "falling back to 'threaded'\n", pref);
            return threadedBackend();
        }
        std::fprintf(stderr,
                     "[jit] unknown backend '%s' — falling back to 'auto'\n", pref);
    }

    for (const Entry& e : kEntries) {
        Backend* b = e.get();
        if (b->usable()) return b;
    }
    return threadedBackend();          // unreachable: threaded is always usable
}

const char* const* backendNames(int& count) {
    for (int i = 0; i < kCount; i++) kNames[i] = kEntries[i].get()->name();
    count = kCount;
    return kNames;
}

}  // namespace jit
