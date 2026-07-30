// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Backend registry. Two separate questions live here, and conflating them
// is how a JIT ends up slower than the interpreter it was meant to beat:
//
//   * WHAT IS AVAILABLE — the order of kEntries, most capable first. This
//     is what `POM68K_JIT_BACKEND=<name>` selects from and what the GUI
//     lists.
//   * WHAT `auto` PICKS — the first entry that is usable on this host and
//     VALID FOR THE GUEST. Validity comes before speed: a backend is only
//     a candidate for the CPU families it declares in
//     caps().guestFamilies (see JitBackend.h § GuestFamily), because a code
//     generator that bakes in one family's instruction-boundary conventions
//     is wrong on another rather than slow. Among the valid ones, kEntries
//     order (native generators first, portable replay last) is the ranking.

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
const char* kKeys[kCount] = {};

}  // namespace

namespace {
// A backend is only a candidate for a guest whose family it declares.
bool validForGuest(Backend* b, uint32_t guestFamily) {
    return (b->caps().guestFamilies & guestFamily) != 0;
}

const char* familyName(uint32_t f) {
    switch (f) {
        case kGuest68000: return "68000/68010";
        case kGuest68020: return "68020";
        case kGuest68030: return "68030";
        case kGuest68040: return "68040";
        default:          return "unknown";
    }
}
}  // namespace

Backend* selectBackend(const char* pref, uint32_t guestFamily) {
    if (!pref || !*pref) pref = "auto";

    if (std::strcmp(pref, "auto") != 0) {
        for (const Entry& e : kEntries) {
            if (std::strcmp(e.key, pref) != 0) continue;
            Backend* b = e.get();
            if (!b->usable()) {
                std::fprintf(stderr,
                             "[jit] backend '%s' is compiled in but not usable here — "
                             "falling back to 'threaded'\n", pref);
                return threadedBackend();
            }
            // An EXPLICIT request for a backend that is not valid for this
            // guest is refused rather than honoured: honouring it produces a
            // wedged machine with no explanation, which is how an hour of
            // etalon timeout gets blamed on unrelated work. The override
            // exists so the family can still be worked on deliberately.
            if (!validForGuest(b, guestFamily)) {
                if (detail::envBool("POM68K_JIT_UNSAFE_BACKEND", false)) {
                    std::fprintf(stderr,
                                 "[jit] WARNING: backend '%s' is not valid for a %s "
                                 "guest and POM68K_JIT_UNSAFE_BACKEND is set — "
                                 "expect divergence\n", pref, familyName(guestFamily));
                    return b;
                }
                std::fprintf(stderr,
                             "[jit] backend '%s' is not valid for a %s guest "
                             "(it was written against another family) — falling back "
                             "to 'threaded'. POM68K_JIT_UNSAFE_BACKEND=1 forces it.\n",
                             pref, familyName(guestFamily));
                return threadedBackend();
            }
            return b;
        }
        std::fprintf(stderr,
                     "[jit] unknown backend '%s' — falling back to 'auto'\n", pref);
    }

    // `kEntries` is ordered best-first (native code generators, then the
    // portable replay), so trying it in order IS "pick the best backend
    // compiled in, usable on this host, and valid for this guest". The
    // original loop filtered on `dflt`, which only `threaded` carries — so
    // `auto` could never reach x64 however capable the host was. Measured
    // cost of that on the Q605 boot (2026-07-29): interpreter ~62 s,
    // threaded 32.6 s, x64 26.1 s — every JIT user was losing 20 %, and
    // because the `jit_*_boot_etalon` gates run whatever `auto` picks, the
    // x86-64 code generator had no end-to-end boot coverage at all.
    //
    // The guest-validity test is the other half of that fix, and it is why
    // this is not simply "drop the dflt filter": lifting the filter alone
    // also handed x64 the 68030 and 68020 machines it was never written for
    // (2026-07-30 — jit_lcii_boot_etalon wedged in the ROM's Egret handshake
    // and timed out at an hour). `threaded` is always usable and valid for
    // every guest, so the loop always terminates on it at worst; `dflt` now
    // only documents that.
    for (const Entry& e : kEntries) {
        Backend* b = e.get();
        if (b->usable() && validForGuest(b, guestFamily)) return b;
    }
    return threadedBackend();          // threaded is always usable and valid
}

const char* const* backendNames(int& count) {
    for (int i = 0; i < kCount; i++) kNames[i] = kEntries[i].get()->name();
    count = kCount;
    return kNames;
}

const char* const* backendKeys(int& count) {
    for (int i = 0; i < kCount; i++) kKeys[i] = kEntries[i].key;
    count = kCount;
    return kKeys;
}

}  // namespace jit
