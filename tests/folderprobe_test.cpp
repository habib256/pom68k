// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Gate for tests/FolderProbe.h — the observable the three beyond-boot persist
// gates are built on.
//
// It exists because validating a string matcher through three six-minute
// machine runs is the wrong loop, and because the thing it replaced was wrong
// in two ways at once (case-sensitive, and reporting the most frequent
// candidate instead of the one that moves). A shared helper that three gates
// judge PASS/FAIL on deserves its own checks.
//
// The fast path is also the risky one: `count()` uses a 256-entry fold table
// and a first-byte reject (1.10 s → 0.49 s over 314 MB). Same answers, or the
// speed is worthless — which is what the reference implementation below is
// for.

#include "FolderProbe.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int gFails = 0;
static void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) gFails++;
}

static std::vector<uint8_t> bytes(const std::string& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

// The obvious, slow, obviously-correct implementation. The fast one has to
// agree with it on every input below.
static long refCount(const std::vector<uint8_t>& hay, const char* needle) {
    const size_t n = std::strlen(needle);
    if (!n || hay.size() < n) return 0;
    auto low = [](uint8_t c) { return uint8_t(c >= 'A' && c <= 'Z' ? c + 32 : c); };
    long c = 0;
    for (size_t i = 0; i + n <= hay.size(); i++) {
        size_t k = 0;
        while (k < n && low(hay[i + k]) == low(uint8_t(needle[k]))) k++;
        if (k == n) c++;
    }
    return c;
}

int main() {
    using namespace folderprobe;

    // ── count(): the matching itself ──────────────────────────────────────
    check(count(bytes("untitled folder"), "untitled folder") == 1, "exact match");
    check(count(bytes("Untitled Folder"), "untitled folder") == 1,
          "case-insensitive: the Finder's capitalisation does not matter");
    check(count(bytes("dossier sans titre"), "Dossier sans titre") == 1,
          "case-insensitive both ways — the defect that made a green machine "
          "look dead");
    check(count(bytes("xxuntitled folderyyuntitled folder"), "untitled folder") == 2,
          "counts every occurrence");
    check(count(bytes("untitled folde"), "untitled folder") == 0,
          "a truncated tail is not a match");
    check(count({}, "untitled folder") == 0, "empty haystack");
    check(count(bytes("anything"), "") == 0, "empty needle counts nothing");
    check(count(bytes("aa"), "aaa") == 0, "needle longer than the haystack");

    // Overlapping runs: the first-byte reject must not skip a valid start.
    check(count(bytes("aaaa"), "aa") == 3, "overlapping matches are all counted");
    check(count(bytes("aAaA"), "aa") == 3, "…and folding does not change that");

    // The catalog stores MacRoman; bytes ≥ 0x80 must pass through unfolded.
    {
        std::vector<uint8_t> hay = bytes("dossier ");
        hay.push_back(0x8E);                       // MacRoman 'é'
        std::vector<uint8_t> pat = hay;
        check(count(hay, "dossier ") == 1, "high-bit bytes do not break the scan");
        check(count(hay, "DOSSIER ") == 1, "…and folding still applies to ASCII");
    }

    // Agreement with the reference implementation on adversarial shapes.
    {
        const char* needles[] = { "untitled folder", "Nouveau dossier",
                                  "Dossier sans titre", "aa", "a" };
        std::string corpus;
        for (int i = 0; i < 400; i++) {
            corpus += (i % 7 == 0) ? "Untitled Folder" : "untitled fold";
            corpus += char('A' + (i % 26));
            corpus += (i % 5 == 0) ? "NOUVEAU DOSSIER" : "nouveau doss";
            corpus += "\x80\xFF\x00 ";
        }
        const std::vector<uint8_t> hay = bytes(corpus);
        bool agree = true;
        for (const char* n : needles)
            if (count(hay, n) != refCount(hay, n)) agree = false;
        check(agree, "the fold-table scan agrees with the naive one on a "
                     "40 KB adversarial corpus");
    }

    // ── grew(): the criterion ─────────────────────────────────────────────
    // The whole point. A candidate that is huge and constant must never win
    // over one that moved by 2 — that inversion is what reported a created
    // folder as "never created".
    {
        long before[kCount] = { 10, 51, 10 };
        long after[kCount]  = { 10, 51, 12 };
        check(grew(before, after) == 2,
              "the candidate that MOVED wins over the one that is largest");
        check(std::strcmp(kNames[grew(before, after)], "Dossier sans titre") == 0,
              "…and it is the name the French Finder actually writes");
    }
    {
        long before[kCount] = { 14, 51, 10 };
        long after[kCount]  = { 16, 51, 10 };
        check(grew(before, after) == 0,
              "the English volume's candidate is picked the same way");
    }
    {
        long before[kCount] = { 14, 51, 10 };
        long after[kCount]  = { 14, 51, 10 };
        check(grew(before, after) == kCount,
              "nothing moved reports NO candidate, not a false winner");
    }
    {
        // A candidate that SHRANK is not evidence of creation.
        long before[kCount] = { 14, 51, 10 };
        long after[kCount]  = { 12, 51, 10 };
        check(grew(before, after) == kCount, "a shrinking candidate is not a match");
    }

    check(kCount == 3, "three candidate names are compiled in");

    std::printf("%s\n", gFails ? "FAILED" : "PASS");
    return gFails ? 1 : 0;
}
