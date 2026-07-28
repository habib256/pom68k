// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Backend registry. Two separate questions live here, and conflating them
// is how a JIT ends up slower than the interpreter it was meant to beat:
//
//   * WHAT IS AVAILABLE — the order of kEntries, most capable first. This
//     is what `POM68K_JIT_BACKEND=<name>` selects from and what the GUI
//     lists.
//   * WHAT `auto` PICKS — `dflt`, which is a MEASURED choice, not a
//     capability ranking. See src/jit/POM68K_JIT.md § 7: on a full Mac OS
//     8.1 boot the x86-64 code generator is slower than the portable
//     backend, because Finder-era 68k code is branch-dense enough that
//     blocks average five instructions and per-entry cost dominates. It
//     wins on compute-bound guest code and it is bit-exact either way, so
//     it ships selectable rather than default until block linking lands.

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
    bool dflt;                         // may `auto` choose this one?
};

const Entry kEntries[] = {
#if defined(POM68K_JIT_BACKEND_X64)
    { "x64", x64Backend, false },
#endif
#if defined(POM68K_JIT_BACKEND_A64)
    { "a64", a64Backend, false },
#endif
    { "threaded", threadedBackend, true },   // always present, always usable
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
        if (!e.dflt) continue;
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
