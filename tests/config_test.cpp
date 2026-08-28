// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The environment-knob surface, checked against its documentation and exact
// lifecycle registry.
//
// 181 distinct `POM68K_*` names are currently read across `src/`, `tests/`
// and the Moira fork. `DEV.md` remains the human explanation; the exact
// `config_knobs.tsv` row is the machine contract that prevents a family
// wildcard from hiding an unclassified addition.
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
// Registry contracts:
//   product     gate:<configured gate>[,<configured gate>...]
//   diagnostic  owner:<source path containing the literal>
//   test        owner:<test path containing the literal>, absent from product
//   chantier    expiry:<document>#<topic>, with the knob named in the document
//
// Source-tree gate: soft-skips when run somewhere without `src/`.

#include "AssetFingerprint.h"          // testasset::find — shared asset search

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifndef POM68K_GATE_ROSTER
#define POM68K_GATE_ROSTER ""
#endif
// The registry has more than one size (`cmake/Pom68kJitGates.cmake` writes
// this file with what THIS host cannot register), and a product knob is
// allowed to cite a host-conditional gate as its evidence: on the other
// host that citation is not stale, it is simply absent. Reading only the
// registered roster made POM68K_JIT_030_MEMBF -- whose evidence is the
// AArch64 alignment lockstep -- a red on every x86-64 tree from the day it
// landed. Same division of labour as docs_test: the union is what a
// document may name, the host says which half it can run.
#ifndef POM68K_GATE_ROSTER_ABSENT
#define POM68K_GATE_ROSTER_ABSENT ""
#endif

static int gFails = 0;
static void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) gFails++;
}

static void require(bool ok, const std::string& what) {
    if (!ok) check(false, what);
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

struct KnobEntry {
    std::string kind;
    std::string contract;
    int line = 0;
};

static std::vector<std::string> split(const std::string& text, char sep) {
    std::vector<std::string> out;
    std::istringstream in(text);
    for (std::string part; std::getline(in, part, sep);) out.push_back(part);
    return out;
}

static bool knobName(const std::string& name) {
    if (name.rfind("POM68K_", 0) != 0 || name.size() <= 7) return false;
    return std::all_of(name.begin(), name.end(), [](char c) {
        return std::isupper(uint8_t(c)) || std::isdigit(uint8_t(c)) || c == '_';
    });
}

static std::map<std::string, KnobEntry> loadRegistry(const std::string& path) {
    std::map<std::string, KnobEntry> out;
    std::ifstream f(path);
    std::string line;
    int lineNo = 0;
    while (std::getline(f, line)) {
        lineNo++;
        if (line.empty() || line[0] == '#') continue;
        const std::vector<std::string> fields = split(line, '\t');
        if (fields.size() != 3) {
            require(false, path + ":" + std::to_string(lineNo) +
                               " must contain exactly 3 tab-separated fields");
            continue;
        }
        require(knobName(fields[0]), path + ":" + std::to_string(lineNo) +
                                        " has a valid exact knob name");
        const bool kindOk = fields[1] == "product" || fields[1] == "diagnostic" ||
                            fields[1] == "test" || fields[1] == "chantier";
        require(kindOk, path + ":" + std::to_string(lineNo) +
                            " has a valid lifecycle class");
        if (!knobName(fields[0]) || !kindOk) continue;
        const bool fresh = out.emplace(fields[0], KnobEntry{fields[1], fields[2], lineNo}).second;
        require(fresh, path + ":" + std::to_string(lineNo) +
                           " duplicates `" + fields[0] + "`");
    }
    return out;
}

static std::set<std::string> loadGates(const std::string& path) {
    std::set<std::string> out;
    std::ifstream f(path);
    for (std::string line; std::getline(f, line);) {
        const size_t tab = line.find('\t');
        if (tab != std::string::npos && tab) out.insert(line.substr(0, tab));
    }
    return out;
}

int main() {
    namespace fs = std::filesystem;
    const std::string devPath = testasset::find("DEV.md");
    const std::string jitPath = testasset::find("src/jit/POM68K_JIT.md");
    const std::string registryPath = testasset::find("config_knobs.tsv");
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
    if (registryPath.empty()) {
        std::printf("FAIL config_knobs.tsv not found\n");
        return 1;
    }

    // ── What the code reads ───────────────────────────────────────────────
    std::set<std::string> code, productCode;
    for (const char* dir : { "src", "tests", "extern/moira/Moira" }) {
        const fs::path d = root + dir;
        if (!fs::is_directory(d)) continue;
        for (const auto& ent : fs::recursive_directory_iterator(d)) {
            if (!ent.is_regular_file()) continue;
            const std::string ext = ent.path().extension().string();
            if (ext != ".h" && ext != ".cpp" && ext != ".hpp") continue;
            const std::string body = slurp(ent.path());
            harvestCode(body, code);
            if (std::string(dir) != "tests") harvestCode(body, productCode);
        }
    }
    check(code.size() > 50,
          "harvested the knob surface from the tree (" +
              std::to_string(code.size()) + " names)");

    // ── Exact lifecycle classification ──────────────────────────────────
    const auto registry = loadRegistry(registryPath);
    check(registry.size() == code.size(),
          "config_knobs.tsv has one exact row per harvested knob (" +
              std::to_string(registry.size()) + " rows)");
    for (const std::string& k : code)
        require(registry.count(k), "harvested knob `" + k + "` is not classified");
    for (const auto& [k, entry] : registry)
        require(code.count(k), "classified knob `" + k + "` does not exist in the tree");

    std::set<std::string> gates = loadGates(POM68K_GATE_ROSTER);
    check(!gates.empty(), "configured gate roster is available to config_test");
    const std::set<std::string> elsewhere = loadGates(POM68K_GATE_ROSTER_ABSENT);
    if (!elsewhere.empty()) {
        std::printf("note: %zu gate(s) exist only on the other host; a knob "
                    "may cite them\n", elsewhere.size());
        gates.insert(elsewhere.begin(), elsewhere.end());
    }
    std::map<std::string, int> classCounts;
    for (const auto& [k, entry] : registry) {
        classCounts[entry.kind]++;
        const std::string where = registryPath + ":" + std::to_string(entry.line);
        if (entry.kind == "product") {
            const std::string prefix = "gate:";
            require(entry.contract.rfind(prefix, 0) == 0,
                    where + " product contract for `" + k + "` must name a gate");
            const auto named = split(entry.contract.substr(prefix.size()), ',');
            require(!named.empty() && !named.front().empty(),
                    where + " product contract for `" + k + "` is empty");
            for (const std::string& gate : named)
                require(gates.count(gate), "product knob `" + k +
                                               "` cites missing gate `" + gate + "`");
        } else if (entry.kind == "diagnostic" || entry.kind == "test") {
            const std::string prefix = "owner:";
            require(entry.contract.rfind(prefix, 0) == 0,
                    where + " `" + k + "` must name an owner");
            const std::string owner = entry.contract.substr(prefix.size());
            const fs::path ownerPath = root + owner;
            require(fs::is_regular_file(ownerPath), "owner `" + owner + "` does not exist for `" + k + "`");
            if (fs::is_regular_file(ownerPath))
                require(slurp(ownerPath).find("\"" + k + "\"") != std::string::npos,
                        "owner `" + owner + "` does not contain `" + k + "`");
            if (entry.kind == "test")
                require(!productCode.count(k), "test knob `" + k + "` leaked into product sources");
        } else if (entry.kind == "chantier") {
            const std::string prefix = "expiry:";
            require(entry.contract.rfind(prefix, 0) == 0,
                    where + " chantier `" + k + "` needs an expiry reference");
            const std::string ref = entry.contract.substr(prefix.size());
            const size_t hash = ref.find('#');
            require(hash != std::string::npos && hash > 0 && hash + 1 < ref.size(),
                    where + " chantier `" + k + "` must name document and topic");
            if (hash != std::string::npos) {
                const std::string doc = ref.substr(0, hash);
                const fs::path docPath = root + doc;
                require(fs::is_regular_file(docPath), "expiry document `" + doc + "` does not exist for `" + k + "`");
                if (fs::is_regular_file(docPath))
                    require(slurp(docPath).find(k) != std::string::npos,
                            "expiry document `" + doc + "` does not name `" + k + "`");
            }
        }
    }
    check(classCounts["product"] && classCounts["diagnostic"] &&
              classCounts["test"] && classCounts["chantier"],
          "all four lifecycle classes are represented (product=" +
              std::to_string(classCounts["product"]) + ", diagnostic=" +
              std::to_string(classCounts["diagnostic"]) + ", test=" +
              std::to_string(classCounts["test"]) + ", chantier=" +
              std::to_string(classCounts["chantier"]) + ")");

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
