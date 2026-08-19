// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The environment-knob surface, checked against its documentation.
//
// 179 distinct `POM68K_*` names are read across `src/`, `tests/` and the
// Moira fork. No knob is individually wrong; the problem is that the surface
// grows silently, and `DEV.md` § 5 calls itself "the complete list" — a claim
// nothing verified. When this gate was first written it found **twelve** names
// the code reads that no document mentioned, and one documented name that no
// longer exists in the tree.
//
// Both directions, because both drift:
//   1. every knob the code reads is documented
//   2. every documented knob exists in the code, unless listed as Retired
//
// It understands the three notations § 5 already uses, so nothing had to be
// rewritten to become checkable:
//   `POM68K_FOO`                          a full name
//   `POM68K_PROBE*`, `POM68K_BENCH_`      a prefix — covers everything under it
//   `POM68K_CENTRIS_FPU` / `_BAREFPU`     a suffix continuation of the name
//                                         before it (→ POM68K_CENTRIS_BAREFPU)
// Modelling those was not a nicety: a first pass that ignored them reported
// 23 undocumented knobs, and half of that was the checker misreading prose.
//
// What this gate does NOT check yet is the expiry — whether each knob is a
// permanent product option or a chantier leftover. That is a decision per
// knob, not a mechanical one (`TODO.md` § 8).
//
// Source-tree gate: soft-skips when run somewhere without `src/`.

#include "AssetFingerprint.h"          // testasset::find — the two-base search

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

static int gFails = 0;
static void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) gFails++;
}

static std::string slurp(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
}

// Every "POM68K_..." string literal in the file.
static void harvestCode(const std::string& text, std::set<std::string>& out) {
    const std::string tag = "\"POM68K_";
    for (size_t i = text.find(tag); i != std::string::npos;
         i = text.find(tag, i + 1)) {
        size_t k = i + 1;
        std::string name;
        while (k < text.size() && (std::isupper(uint8_t(text[k])) ||
                                   std::isdigit(uint8_t(text[k])) ||
                                   text[k] == '_'))
            name += text[k++];
        if (k < text.size() && text[k] == '"' && name.size() > 7)
            out.insert(name);
    }
}

// Doc tokens: backticked names, wildcards and suffix continuations.
static void harvestDoc(const std::string& text, std::set<std::string>& full,
                       std::set<std::string>& prefixes) {
    std::string last;
    for (size_t i = text.find('`'); i != std::string::npos;
         i = text.find('`', i + 1)) {
        size_t e = text.find('`', i + 1);
        if (e == std::string::npos) break;
        std::string tok = text.substr(i + 1, e - i - 1);
        i = e;
        if (tok.empty() || tok.size() > 48) continue;
        bool wild = tok.back() == '*';
        if (wild) tok.pop_back();
        const bool wellFormed = std::all_of(tok.begin(), tok.end(), [](char c) {
            return std::isupper(uint8_t(c)) || std::isdigit(uint8_t(c)) || c == '_';
        });
        if (!wellFormed || tok.empty()) continue;

        if (tok.rfind("POM68K_", 0) == 0) {
            if (wild || tok.back() == '_') prefixes.insert(tok);
            else full.insert(tok);
            last = tok;
        } else if (tok[0] == '_' && !last.empty()) {
            // `_BAREFPU` after `POM68K_CENTRIS_FPU` → POM68K_CENTRIS_BAREFPU
            const size_t cut = last.rfind('_');
            if (cut == std::string::npos) continue;
            const std::string joined = last.substr(0, cut) + tok;
            if (wild) prefixes.insert(joined); else full.insert(joined);
        }
    }
}

// The section of `text` from `heading` to the next "\n## ".
static std::string section(const std::string& text, const std::string& heading) {
    size_t s = text.find(heading);
    if (s == std::string::npos) return {};
    size_t e = text.find("\n## ", s + heading.size());
    return text.substr(s, e == std::string::npos ? std::string::npos : e - s);
}

int main() {
    namespace fs = std::filesystem;
    const std::string devPath = testasset::find("DEV.md");
    const std::string jitPath = testasset::find("src/jit/POM68K_JIT.md");
    std::string root;
    for (const char* base : { "", "../" })
        if (fs::is_directory(std::string(base) + "src")) { root = base; break; }
    if (root.empty() && !fs::is_directory("src")) {
        std::printf("SKIP: no src/ next to the working directory\n");
        return 0;
    }
    if (devPath.empty()) {
        std::printf("SKIP: DEV.md not found\n");
        return 0;
    }

    // ── What the code reads ───────────────────────────────────────────────
    std::set<std::string> code;
    for (const char* dir : { "src", "tests", "extern/moira/Moira" }) {
        const fs::path d = root + dir;
        if (!fs::is_directory(d)) continue;
        for (const auto& ent : fs::recursive_directory_iterator(d)) {
            if (!ent.is_regular_file()) continue;
            const std::string ext = ent.path().extension().string();
            if (ext != ".h" && ext != ".cpp" && ext != ".hpp") continue;
            harvestCode(slurp(ent.path()), code);
        }
    }
    check(code.size() > 50,
          "harvested the knob surface from the tree (" +
              std::to_string(code.size()) + " names)");

    // ── What the docs declare ─────────────────────────────────────────────
    const std::string dev = slurp(devPath);
    const std::string knobSec = section(dev, "## 5. Environment knobs");
    check(!knobSec.empty(), "DEV.md § 5 found");

    std::set<std::string> full, prefixes;
    harvestDoc(knobSec, full, prefixes);
    if (!jitPath.empty()) harvestDoc(slurp(jitPath), full, prefixes);
    check(full.size() > 50,
          "DEV.md § 5 + POM68K_JIT.md declare " + std::to_string(full.size()) +
              " names and " + std::to_string(prefixes.size()) + " prefixes");

    // Names under the "**Retired**" heading are expected to be ABSENT.
    std::set<std::string> retired;
    {
        const size_t r = knobSec.find("**Retired**");
        if (r != std::string::npos) {
            const size_t end = knobSec.find("\n### ", r);
            std::set<std::string> p;
            harvestDoc(knobSec.substr(r, end == std::string::npos
                                             ? std::string::npos : end - r),
                       retired, p);
        }
    }

    auto covered = [&](const std::string& k) {
        if (full.count(k)) return true;
        for (const std::string& p : prefixes)
            if (!p.empty() && k.rfind(p, 0) == 0) return true;
        return false;
    };

    // ── 1. Every knob the code reads is documented ────────────────────────
    std::vector<std::string> undocumented;
    for (const std::string& k : code)
        if (!covered(k)) undocumented.push_back(k);
    for (const std::string& k : undocumented)
        check(false, "the code reads `" + k +
                         "` and no document mentions it (DEV.md § 5)");
    check(undocumented.empty(),
          "every knob the code reads is documented (" +
              std::to_string(code.size()) + " checked)");

    // ── 2. …and every documented knob exists, unless retired ──────────────
    std::vector<std::string> ghosts;
    for (const std::string& k : full)
        if (!code.count(k) && !retired.count(k)) ghosts.push_back(k);
    for (const std::string& k : ghosts)
        check(false, "DEV.md documents `" + k +
                         "` but nothing in the tree reads it — remove it, or "
                         "list it under **Retired** with the reason");
    check(ghosts.empty(),
          "every documented knob exists in the code (" +
              std::to_string(full.size()) + " checked, " +
              std::to_string(retired.size()) + " retired)");

    // A retired knob that came back is the failure this list exists to catch.
    for (const std::string& k : retired)
        check(!code.count(k),
              "retired knob `" + k + "` is absent from the tree");

    std::printf("%s\n", gFails ? "FAILED" : "PASS");
    return gFails ? 1 : 0;
}
