// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// "Did the guest create a folder?" — the observable the beyond-boot persist
// gates are built on, once.
//
// It was written three times (`lcii_beyond_etalon` twice, `q605_beyond_etalon`
// once) and all three copies carried the same two defects, found on 2026-08-09
// when the pattern was ported to the IIvx:
//
//   1. **Case-sensitive**, spelled `dossier sans titre`. The French System 7.5
//      Finder writes `Dossier sans titre`.
//   2. **It reported the most FREQUENT candidate.** On the French volume
//      `Nouveau dossier` occurs 51 times as a constant localization resource
//      and never moves, while the created folder goes 10 → 12. The maximum
//      picked the resource string every run, and the gate printed
//      `×51 → ×51` — which reads exactly like "the machine never created the
//      folder" while a screen dump showed it plainly there.
//
// The signal is not the biggest count. It is **the count that changes**. The
// two older gates were green on their English volumes only because
// `untitled folder` happens to be both there; the first localized image would
// have made them lie the same way.
//
// HFS stores a directory name twice — the catalog folder record and its thread
// record — so a created folder moves its candidate by 2, not 1. Only the
// direction is asserted.

#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace folderprobe {

// Whatever the volume's Finder writes for a new folder. Full phrases only:
// the bare "untitled" substring occurs hundreds of times in a stock volume and
// would drown the signal.
inline constexpr const char* kNames[] = { "untitled folder",
                                          "Nouveau dossier",
                                          "Dossier sans titre" };
inline constexpr size_t kCount = sizeof(kNames) / sizeof(kNames[0]);

// Case-insensitive on ASCII: the catalog name is MacRoman and the candidates
// are ASCII, so folding A-Z is enough.
//
// A 256-entry fold table plus a first-byte reject, rather than folding both
// sides inside the inner loop: **1.10 s → 0.49 s** for the three candidates
// over 314 MB (856 → 1942 MB/s, random corpus).
//
// Worth having, and worth stating what it is NOT: the naive version was first
// justified here as having "measurably slowed the gates", which was an
// inference from long-feeling runs and not a measurement. Measured, it costs
// ~3.3 s per gate run against a 550-second gate — 0.6 %, invisible. The
// rewrite stands on being strictly better and gated, not on a regression it
// never caused.
inline long count(const std::vector<uint8_t>& hay, const char* needle) {
    const size_t n = std::strlen(needle);
    if (!n || hay.size() < n) return 0;

    static const auto kFold = [] {
        std::array<uint8_t, 256> t{};
        for (int i = 0; i < 256; i++)
            t[size_t(i)] = uint8_t(i >= 'A' && i <= 'Z' ? i + 32 : i);
        return t;
    }();

    uint8_t pat[64];
    const size_t m = n < sizeof(pat) ? n : sizeof(pat);
    for (size_t i = 0; i < m; i++) pat[i] = kFold[uint8_t(needle[i])];

    const uint8_t* p = hay.data();
    const size_t last = hay.size() - m;
    long c = 0;
    for (size_t i = 0; i <= last; i++) {
        if (kFold[p[i]] != pat[0]) continue;
        size_t k = 1;
        while (k < m && kFold[p[i + k]] == pat[k]) k++;
        if (k == m) c++;
    }
    return c;
}

// Every candidate, printed. Printing only the winner is what turned a working
// machine into a wrong diagnosis; `when` labels the sampling point.
inline void sample(const std::vector<uint8_t>& img, long out[kCount],
                   const char* when) {
    for (size_t i = 0; i < kCount; i++) {
        out[i] = count(img, kNames[i]);
        std::printf("  [%s] '%s' x%ld\n", when, kNames[i], out[i]);
    }
}

// Index of the candidate that grew, or kCount if none did.
inline size_t grew(const long before[kCount], const long after[kCount]) {
    for (size_t i = 0; i < kCount; i++)
        if (after[i] > before[i]) return i;
    return kCount;
}

}  // namespace folderprobe
