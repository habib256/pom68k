// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The documentation's citable invariants, checked against the code.
//
// 186 000 words of documentation for 55 000 lines of code is the project's
// distinctive asset and its largest maintenance item, and at that ratio prose
// drifts exactly like code does. It already has: `CLAUDE.md`'s *Subsystems*
// table said the Duo 230 was "not a GUI profile: no kProfiles row, no save
// states" — false since 2026-08-06 — while the *Status* section of the same
// file said the opposite, correctly.
//
// So this gate covers the claims a reader would act on, and only those. Not
// prose, not intent: numbers that exist twice and names that must resolve.
//
//   1. profile count agreement — kProfiles rows == SnapMachine tags
//   2. every profile count CLAUDE.md states equals that number
//   3. every gate CLAUDE.md names by its full name is registered in CTest
//   4. every registered gate carries at least one label
//   5. the gate totals CLAUDE.md states match the registry
//
// Check 4 is here because it caught a live one the day it was written: four
// gates — the three IIfx ones and `duo230_boot_etalon` — were registered
// AFTER the label-derivation block in `CMakeLists.txt` and so carried no
// label at all. Two whole platforms were invisible to every `ctest -L` tier
// while the docs advertised the tiers as complete.
//
// The roster comes from `pom68k_gates.tsv`, written by CMake at configure
// time: a documentation gate that cannot see the registry can only check the
// docs against themselves.

#include "AssetFingerprint.h"          // testasset::find — the two-base search

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

static int gFails = 0;
static void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) gFails++;
}

static std::string slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
}

// The braced initialiser list of `const Profile kProfiles[] = { ... }`: one
// row per profile, each opening with "{ " at the start of its line.
static int countProfileRows(const std::string& src) {
    const std::string anchor = "const Profile kProfiles[] = {";
    size_t i = src.find(anchor);
    if (i == std::string::npos) return -1;
    size_t j = i + anchor.size() - 1;             // the opening brace
    int depth = 0, rows = 0;
    bool atLineStart = false;
    for (size_t k = j; k < src.size(); k++) {
        if (src[k] == '\n') { atLineStart = true; continue; }
        if (atLineStart && (src[k] == ' ' || src[k] == '\t')) continue;
        if (src[k] == '{') {
            depth++;
            if (depth == 2 && atLineStart) rows++;   // a row, not the array
        } else if (src[k] == '}') {
            if (--depth == 0) break;
        }
        atLineStart = false;
    }
    return rows;
}

// `enum class SnapMachine : std::uint32_t { LcII = 1, ... };` — every
// enumerator carries an explicit value because the values ARE the file
// format, which is what makes them countable.
static int countSnapTags(const std::string& src) {
    size_t i = src.find("enum class SnapMachine");
    if (i == std::string::npos) return -1;
    size_t e = src.find("};", i);
    if (e == std::string::npos) return -1;
    int n = 0;
    for (size_t k = i; k + 1 < e; k++) {
        if (src[k] != '=') continue;
        size_t v = k + 1;
        while (v < e && std::isspace(uint8_t(src[v]))) v++;
        if (v < e && std::isdigit(uint8_t(src[v]))) n++;
    }
    return n;
}

// Every integer that immediately precedes `needle` in `text`, ignoring the
// markdown emphasis and backticks the docs wrap numbers in.
static std::vector<int> numbersBefore(const std::string& text,
                                      const std::string& needle) {
    std::vector<int> out;
    for (size_t i = text.find(needle); i != std::string::npos;
         i = text.find(needle, i + 1)) {
        size_t k = i;
        // step back over the separator: spaces, '*', '`'
        while (k > 0 && (text[k - 1] == ' ' || text[k - 1] == '*' ||
                         text[k - 1] == '`' || text[k - 1] == '\n')) k--;
        size_t end = k;
        while (k > 0 && std::isdigit(uint8_t(text[k - 1]))) k--;
        if (end == k) continue;
        std::string digits;
        for (size_t d = k; d < end; d++)
            if (std::isdigit(uint8_t(text[d]))) digits += text[d];
        if (!digits.empty() && digits.size() <= 5) out.push_back(std::stoi(digits));
    }
    return out;
}

int main() {
    const std::string mainCpp = testasset::find("src/main.cpp");
    const std::string snapH   = testasset::find("src/SaveStateMachines.h");
    const std::string claude  = testasset::find("CLAUDE.md");
    // The roster path is baked in at configure time. It used to be searched
    // for relative to the working directory, and run from the source tree the
    // search failed and the gate returned 0 after only the first two checks —
    // a green run that had skipped everything that mattered.
    std::string roster;
#ifdef POM68K_GATE_ROSTER
    if (std::ifstream(POM68K_GATE_ROSTER)) roster = POM68K_GATE_ROSTER;
#endif
    if (roster.empty()) roster = testasset::find("pom68k_gates.tsv");
    if (mainCpp.empty() || snapH.empty() || claude.empty()) {
        std::printf("SKIP: run from the build or source tree (needs src/ + CLAUDE.md)\n");
        return 0;
    }

    // ── 1. The two places a profile is declared must agree ────────────────
    const int rows = countProfileRows(slurp(mainCpp));
    const int tags = countSnapTags(slurp(snapH));
    check(rows > 0, "kProfiles table found in src/main.cpp");
    check(tags > 0, "SnapMachine enum found in src/SaveStateMachines.h");
    check(rows == tags,
          "kProfiles rows (" + std::to_string(rows) + ") == SnapMachine tags (" +
          std::to_string(tags) + ")");

    // ── 2. …and CLAUDE.md must state that number, everywhere it states one ─
    const std::string doc = slurp(claude);
    std::vector<int> stated;
    for (const char* form : { " machine profiles", " profiles", " `SnapMachine` tags" })
        for (int n : numbersBefore(doc, form)) stated.push_back(n);
    // "| **Platform** | Profiles (37) |" — the machine table's own header.
    for (size_t i = doc.find("Profiles ("); i != std::string::npos;
         i = doc.find("Profiles (", i + 1)) {
        std::string digits;
        for (size_t k = i + 10; k < doc.size() && std::isdigit(uint8_t(doc[k])); k++)
            digits += doc[k];
        if (!digits.empty()) stated.push_back(std::stoi(digits));
    }
    check(!stated.empty(), "CLAUDE.md states a profile count somewhere");
    for (int n : stated)
        check(n == rows,
              "CLAUDE.md profile count " + std::to_string(n) + " == " +
              std::to_string(rows) + " in the code");

    // Not a soft skip: the roster is generated by our own CMake, so its
    // absence means the build is wrong, not that an asset is missing.
    check(!roster.empty(),
          "pom68k_gates.tsv located (CMake writes it at configure time)");
    if (roster.empty()) {
        std::printf("FAILED\n");
        return 1;
    }

    // ── 3/4. The CTest registry ───────────────────────────────────────────
    std::map<std::string, std::string> gates;          // name → labels
    {
        std::ifstream f(roster);
        std::string line;
        while (std::getline(f, line)) {
            size_t tab = line.find('\t');
            if (tab == std::string::npos) continue;
            gates[line.substr(0, tab)] = line.substr(tab + 1);
        }
    }
    check(!gates.empty(), "gate roster read from pom68k_gates.tsv");

    std::vector<std::string> unlabelled;
    for (const auto& [name, labels] : gates)
        if (labels.empty()) unlabelled.push_back(name);
    check(unlabelled.empty(),
          unlabelled.empty()
              ? "every registered gate carries at least one label"
              : "gates with NO label (invisible to every `ctest -L` tier): " +
                    [&] {
                        std::string s;
                        for (const auto& n : unlabelled) s += n + " ";
                        return s;
                    }());

    // Gate names CLAUDE.md spells out in full, inside backticks. The
    // compressed forms it also uses (`q605_soak/persist_etalon`,
    // `rom_/disk_/system_boot_etalon`) carry a '/' and are deliberately NOT
    // checked — expanding them would be guessing at prose.
    std::set<std::string> named;
    for (size_t i = doc.find('`'); i != std::string::npos; i = doc.find('`', i + 1)) {
        size_t e = doc.find('`', i + 1);
        if (e == std::string::npos) break;
        std::string tok = doc.substr(i + 1, e - i - 1);
        i = e;
        if (tok.size() < 5 || tok.size() > 64) continue;
        const bool ok = std::all_of(tok.begin(), tok.end(), [](char c) {
            return std::islower(uint8_t(c)) || std::isdigit(uint8_t(c)) || c == '_';
        });
        if (!ok) continue;
        auto endsWith = [&](const char* suf) {
            const size_t n = std::strlen(suf);
            return tok.size() > n && tok.compare(tok.size() - n, n, suf) == 0;
        };
        if (endsWith("_test") || endsWith("_etalon")) named.insert(tok);
    }
    check(!named.empty(), "CLAUDE.md names gates by their full name");
    for (const std::string& g : named)
        check(gates.count(g) > 0,
              "CLAUDE.md names `" + g + "` — registered in CTest");

    // ── 5. The totals CLAUDE.md quotes ───────────────────────────────────
    for (int n : numbersBefore(doc, " CTest gates"))
        check(n == int(gates.size()),
              "CLAUDE.md says " + std::to_string(n) + " CTest gates; ctest has " +
              std::to_string(gates.size()));

    for (const char* label : { "unit", "smoke", "jit", "m040", "etalon" }) {
        int have = 0;
        for (const auto& [name, labels] : gates) {
            (void)name;
            // Labels are comma-separated; `etalon` must not match
            // `etalon-core`, which is why this compares whole fields.
            std::stringstream ss(labels);
            std::string one;
            while (std::getline(ss, one, ','))
                if (one == label) { have++; break; }
        }
        for (int n : numbersBefore(doc, std::string("`") + label + "`"))
            check(n == have,
                  std::string("CLAUDE.md says ") + std::to_string(n) + " `" +
                      label + "` gates; ctest has " + std::to_string(have));
    }

    // ── 6. …including the ones inside a fenced code block ────────────────
    // `ctest -L unit   # 79 gates` is the same claim as the prose, and it is
    // where the count was stalest: no backticks, so check 5 walked straight
    // past it. Matched in CLAUDE.md and README.md alike.
    auto labelCount = [&](const std::string& label) {
        int n = 0;
        for (const auto& [name, labels] : gates) {
            (void)name;
            std::stringstream ss(labels);
            std::string one;
            while (std::getline(ss, one, ','))
                if (one == label) { n++; break; }
        }
        return n;
    };
    for (const char* file : { "CLAUDE.md", "README.md" }) {
        const std::string p = testasset::find(file);
        if (p.empty()) continue;
        const std::string text = slurp(p);
        const std::string cmd = "ctest";
        for (size_t i = text.find(cmd); i != std::string::npos;
             i = text.find(cmd, i + 1)) {
            const size_t eol = text.find('\n', i);
            const std::string line = text.substr(i, eol - i);
            const size_t hash = line.find('#');
            if (hash == std::string::npos) continue;
            // "<n> gates" in the trailing comment
            const size_t g = line.find(" gates", hash);
            if (g == std::string::npos) continue;
            size_t k = g, end;
            while (k > hash && line[k - 1] == ' ') k--;
            end = k;
            while (k > hash && std::isdigit(uint8_t(line[k - 1]))) k--;
            if (k == end) continue;
            const int stated_n = std::stoi(line.substr(k, end - k));
            // Which label? `-L <label>` on the same line, else the total.
            const size_t lpos = line.find("-L ");
            int want = int(gates.size());
            std::string which = "total";
            if (lpos != std::string::npos && lpos < hash) {
                which.clear();
                for (size_t c = lpos + 3; c < hash && !std::isspace(uint8_t(line[c])); c++)
                    which += line[c];
                want = labelCount(which);
            }
            check(stated_n == want,
                  std::string(file) + ": `" + line.substr(0, hash - 1) +
                      "` says " + std::to_string(stated_n) + " gates; " + which +
                      " has " + std::to_string(want));
        }
    }

    // ── 7. CHANGELOG_INDEX.md covers every dated entry ───────────────────
    // A generated index that silently stops covering new entries is worse
    // than none: it looks complete. Regenerate with
    // `python3 tools/changelog_index.py`.
    {
        const std::string clPath = testasset::find("CHANGELOG.md");
        const std::string ixPath = testasset::find("CHANGELOG_INDEX.md");
        if (!clPath.empty() && !ixPath.empty()) {
            const std::string cl = slurp(clPath), ix = slurp(ixPath);
            int dated = 0;
            for (size_t i = 0; i + 5 < cl.size(); i++)
                if ((i == 0 || cl[i - 1] == '\n') && cl.compare(i, 5, "## 20") == 0)
                    dated++;
            int listed = 0;
            for (size_t i = 0; i + 4 < ix.size(); i++)
                if ((i == 0 || ix[i - 1] == '\n') && ix.compare(i, 4, "- **") == 0)
                    listed++;
            check(dated == listed,
                  "CHANGELOG_INDEX.md lists " + std::to_string(listed) +
                      " of the " + std::to_string(dated) +
                      " dated entries — regenerate with tools/changelog_index.py");
        }
    }

    std::printf("%s\n", gFails ? "FAILED" : "PASS");
    return gFails ? 1 : 0;
}
