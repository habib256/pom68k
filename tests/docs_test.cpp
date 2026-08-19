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
//   6. the permanent Moira fork's pom*/POM68K boundary matches its inventory
//   7. native JIT backends consume, but never recreate, memory semantics
//   8. A64/x64 dispatch, extensions, EAs and control come from Instr
//   9. performance policy is keyed by workload, guest family and host
//  10. every `file:line` citation that resolves in-tree lands inside the file
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
#include "MachineCatalog.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
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
    const std::string catalog = testasset::find("src/MachineCatalog.h");
    const std::string claude  = testasset::find("CLAUDE.md");
    // The roster path is baked in at configure time. It used to be searched
    // for relative to the working directory, and run from the source tree the
    // search failed and the gate returned 0 after only the first two checks —
    // a green run that had skipped everything that mattered.
    std::string roster;
    std::string manifest;
    std::string performanceBudgets;
#ifdef POM68K_GATE_ROSTER
    if (std::ifstream(POM68K_GATE_ROSTER)) roster = POM68K_GATE_ROSTER;
#endif
    if (roster.empty()) roster = testasset::find("pom68k_gates.tsv");
#ifdef POM68K_GATE_MANIFEST
    if (std::ifstream(POM68K_GATE_MANIFEST)) manifest = POM68K_GATE_MANIFEST;
#endif
    if (manifest.empty()) manifest = testasset::find("pom68k_gate_manifest.tsv");
#ifdef POM68K_PERF_BUDGET_FILE
    if (std::ifstream(POM68K_PERF_BUDGET_FILE))
        performanceBudgets = POM68K_PERF_BUDGET_FILE;
#endif
    if (performanceBudgets.empty())
        performanceBudgets = testasset::find("performance_budgets.tsv");
    if (mainCpp.empty() || catalog.empty() || claude.empty()) {
        std::printf("SKIP: run from the build or source tree (needs src/ + CLAUDE.md)\n");
        return 0;
    }

    // ── 1. One compiled catalogue drives menu and snapshot identity ───────
    const int rows = int(pom68k::kMachineProfileCount);
    check(slurp(mainCpp).find("pom68k::kMachineProfiles") != std::string::npos,
          "Machine menu consumes the compiled profile catalogue");
    check(slurp(catalog).find("enum class SnapMachine") != std::string::npos,
          "snapshot ids live beside the profile catalogue");
    check(rows > 0, "compiled machine catalogue is non-empty");
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

    // Every gate has an explicit, orthogonal execution contract. This catches
    // the old false identity `unit == asset-free`, host-only gates hidden by a
    // generic tier, and future registrations omitted from daily policy.
    std::map<std::string, std::vector<std::string>> gateManifest;
    {
        std::ifstream f(manifest);
        std::string line;
        std::getline(f, line); // header
        // Seven columns since 2026-08-17: `slots` and `slots_src` carry what
        // a gate COSTS to run, so `ctest -j` can schedule the suite instead
        // of it being sequential because nobody knows.
        check(line == "name\tassets\thost\tscope\ttier\tslots\tslots_src",
              "gate manifest has the versioned seven-column schema");
        while (std::getline(f, line)) {
            std::vector<std::string> cells;
            std::stringstream ss(line);
            std::string cell;
            while (std::getline(ss, cell, '\t')) cells.push_back(cell);
            if (cells.size() == 7)
                gateManifest[cells[0]] = {cells[1], cells[2], cells[3],
                                          cells[4], cells[5], cells[6]};
        }
    }
    check(gateManifest.size() == gates.size(),
          "gate manifest covers every registered gate exactly once");
    const std::set<std::string> assetKinds{"none", "optional", "required"};
    const std::set<std::string> hostKinds{"any", "native", "a64", "x64"};
    const std::set<std::string> scopeKinds{"component", "engine", "profile", "repository"};
    const std::set<std::string> tierKinds{"daily", "platform", "full"};
    // `assumed` means nobody has measured this gate on this host, and it is
    // scheduled as one slot. It must therefore NEVER carry more than one:
    // a multi-slot claim that came from nowhere would quietly serialize the
    // suite it is meant to parallelize.
    const std::set<std::string> slotSources{"measured", "assumed"};
    std::vector<std::string> invalidManifest;
    for (const auto& [name, cells] : gateManifest) {
        if (!gates.count(name) || cells.size() != 6 ||
            !assetKinds.count(cells[0]) || !hostKinds.count(cells[1]) ||
            !scopeKinds.count(cells[2]) || !tierKinds.count(cells[3]) ||
            !slotSources.count(cells[5])) {
            invalidManifest.push_back(name);
            continue;
        }
        const int slots = std::atoi(cells[4].c_str());
        if (slots < 1 || (cells[5] == "assumed" && slots != 1))
            invalidManifest.push_back(name);
    }
    check(invalidManifest.empty(),
          invalidManifest.empty()
              ? "every gate manifest row has valid assets/host/scope/tier/slots"
              : "invalid gate manifest rows: " + [&] {
                    std::string out;
                    for (const auto& name : invalidManifest) out += name + " ";
                    return out;
                }());

    // Budgets are data, not literals hidden inside benchmarks. CMake consumes
    // these exact rows into the named gates; this side catches schema drift,
    // duplicate policy and a budget attached to an unregistered gate.
    std::set<std::string> budgetKeys;
    bool budgetSchema = !performanceBudgets.empty();
    {
        std::ifstream f(performanceBudgets);
        for (std::string line; std::getline(f, line); ) {
            if (line.empty() || line[0] == '#') continue;
            std::vector<std::string> cells;
            std::stringstream ss(line);
            for (std::string cell; std::getline(ss, cell, '\t'); )
                cells.push_back(cell);
            if (cells.size() != 6) {
                budgetSchema = false;
                continue;
            }
            const bool knownWorkload = cells[0] == "synthetic_68040_lockstep" ||
                cells[0] == "synthetic_68040_copyback" ||
                cells[0] == "q605_jit" || cells[0] == "lcii_threaded" ||
                cells[0] == "host_wallclock";
            // `host_wallclock` is guest-independent (family `any`): the
            // noise floor prices the HOST, docs/MEASURING.md § R2.
            const bool knownFamily = cells[1] == "68030" ||
                cells[1] == "68040" || cells[1] == "any";
            const bool knownHost = cells[2] == "aarch64" ||
                cells[2] == "x86_64" || cells[2] == "any" ||
                cells[2] == "reference_x86_64" ||
                cells[2] == "apple_m4";
            if (!knownWorkload || !knownFamily || !knownHost ||
                cells[3].empty() || cells[4].empty() || cells[5].empty() ||
                !std::all_of(cells[4].begin(), cells[4].end(),
                             [](unsigned char c) { return std::isdigit(c); }))
                budgetSchema = false;
            if (!budgetKeys.insert(cells[0] + "/" + cells[1] + "/" +
                                   cells[2] + "/" + cells[3]).second)
                budgetSchema = false;
        }
    }
    std::set<std::string> requiredBudgets;
    for (const std::string& host : {"aarch64", "x86_64", "any"}) {
        for (const std::string& metric : {"min_blocks_compiled", "min_blocks_run",
                                          "min_native_share_permille",
                                          "max_slow_instrs"})
            requiredBudgets.insert("synthetic_68040_lockstep/68040/" + host +
                                   "/" + metric);
        for (const std::string& metric : {"max_slow_instrs",
                                          "max_native_ratio_permille",
                                          "native_slack_microseconds"})
            requiredBudgets.insert("synthetic_68040_copyback/68040/" + host +
                                   "/" + metric);
    }
    requiredBudgets.insert(
        "q605_jit/68040/reference_x86_64/min_realtime_permille");
    requiredBudgets.insert(
        "lcii_threaded/68030/reference_x86_64/min_realtime_permille");
    requiredBudgets.insert(
        "q605_jit/68040/apple_m4/min_realtime_permille");
    requiredBudgets.insert(
        "lcii_threaded/68030/apple_m4/min_realtime_permille");
    // The measured wall-clock noise floor per bench host, plus the
    // conservative `any` fallback an unmeasured host inherits
    // (docs/MEASURING.md § R2 owns the numbers and their provenance).
    requiredBudgets.insert(
        "host_wallclock/any/x86_64/noise_floor_permille");
    requiredBudgets.insert(
        "host_wallclock/any/any/noise_floor_permille");
    check(budgetSchema,
          "performance budget manifest has valid workload/family/host rows");
    check(budgetKeys == requiredBudgets,
          "performance policy separates daily hosts and pins measured Macintosh baselines");

    // Both native runners must publish the same schema and pass it through
    // the same budget checker. Architecture-specific filenames are metadata,
    // not two reporting implementations.
    const std::string metricsHeader = slurp(
        testasset::find("src/jit/JitMetrics.h"));
    const std::string linuxCi = slurp(
        testasset::find(".github/workflows/ci.yml"));
    const std::string macCi = slurp(
        testasset::find(".github/workflows/macos.yml"));
    check(metricsHeader.find("pom68k.jit.metrics.v1") != std::string::npos,
          "JIT metrics schema is explicit and versioned");
    for (const auto& [name, workflow] :
         std::array<std::pair<const char*, const std::string*>, 2>{{
             {"x86-64 CI", &linuxCi}, {"AArch64 CI", &macCi}}}) {
        check(workflow->find("POM68K_JIT_METRICS_FILE") != std::string::npos &&
              workflow->find("tools/check_jit_performance.py") != std::string::npos &&
              workflow->find("actions/upload-artifact@v4") != std::string::npos,
              std::string(name) + " validates and archives structured JIT metrics");
    }

    // The registry has MORE THAN ONE size: the AArch64 lockstep pair is
    // registered only on an AArch64 host, and since 2026-08-18 the x64 030
    // experimental lockstep only on x86-64 (CMakeLists.txt, the guards that
    // write this file) — so a document cannot state one total and be true
    // everywhere. The docs state the UNION numbers; CMake tells us here
    // which of them this host is missing, and checks 5 and 6 add them back
    // before comparing. No host has an empty file any more.
    //
    // The alternative — declaring one architecture canonical and skipping the
    // gate elsewhere — was rejected: it would leave the counts unchecked on
    // the CI host that runs them most often. If a THIRD conditional gate ever
    // appears, register it in that same else() branch or these checks start
    // lying by exactly one. (2026-08-12)
    std::map<std::string, std::string> absent;         // name → would-be labels
    {
        std::string p;
#ifdef POM68K_GATE_ROSTER_ABSENT
        if (std::ifstream(POM68K_GATE_ROSTER_ABSENT)) p = POM68K_GATE_ROSTER_ABSENT;
#endif
        if (p.empty()) p = testasset::find("pom68k_gates_absent.tsv");
        std::ifstream f(p);
        std::string line;
        while (std::getline(f, line)) {
            size_t tab = line.find('\t');
            if (tab == std::string::npos) continue;
            absent[line.substr(0, tab)] = line.substr(tab + 1);
        }
    }
    if (!absent.empty())
        std::printf("note: %zu host-conditional gate(s) not registered here; "
                    "the documented AArch64 totals are held to account\n",
                    absent.size());

    // How many gates `ctest -L <label>` selects, counting the ones this host
    // cannot register. -L is a REGEX over each label, not an equality test:
    // `jit` therefore also selects `jit-fast`. The old whole-field compare
    // made the documentation gate certify an exact-label count while CTest
    // actually ran that count plus the docs/config `jit-fast` gates.
    auto countLabel = [&](const std::string& label) {
        int n = 0;
        auto scan = [&](const std::string& labels) {
            std::stringstream ss(labels);
            std::string one;
            while (std::getline(ss, one, ','))
                if (one.find(label) != std::string::npos) { n++; return; }
        };
        for (const auto& [name, labels] : gates) { (void)name; scan(labels); }
        for (const auto& [name, labels] : absent) { (void)name; scan(labels); }
        return n;
    };
    const int totalGates = int(gates.size() + absent.size());

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
        check(n == totalGates,
              "CLAUDE.md says " + std::to_string(n) + " CTest gates; ctest has " +
              std::to_string(totalGates));

    for (const char* label : { "unit", "smoke", "jit", "m040", "etalon" }) {
        const int have = countLabel(label);
        for (int n : numbersBefore(doc, std::string("`") + label + "`"))
            check(n == have,
                  std::string("CLAUDE.md says ") + std::to_string(n) + " `" +
                      label + "` gates; ctest has " + std::to_string(have));
    }

    // ── 6. …including the ones inside a fenced code block ────────────────
    // `ctest -L unit   # 79 gates` is the same claim as the prose, and it is
    // where the count was stalest: no backticks, so check 5 walked straight
    // past it. Matched in CLAUDE.md and README.md alike.
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
            int want = totalGates;
            std::string which = "total";
            if (lpos != std::string::npos && lpos < hash) {
                which.clear();
                for (size_t c = lpos + 3; c < hash && !std::isspace(uint8_t(line[c])); c++)
                    which += line[c];
                want = countLabel(which);
            }
            check(stated_n == want,
                  std::string(file) + ": `" + line.substr(0, hash - 1) +
                      "` says " + std::to_string(stated_n) + " gates; " + which +
                      " has " + std::to_string(want));
        }
    }

    {
        // ── 7. The permanent Moira fork has an explicit boundary ─────────
        // Physical indirection is intentionally NOT the boundary: the fork's
        // design record documents an ~11% loss when a hot i-cache seam was
        // virtualised. The auditable boundary is lexical instead: local APIs
        // use `pom*`, changed source files carry `POM68K`, and the inventory
        // names every such file. Recompute all four headline numbers so the
        // vendor document cannot silently fossilise again.
        namespace fs = std::filesystem;
        const std::string vendorPath =
            testasset::find("extern/moira/POM68K_VENDOR.md");
        check(!vendorPath.empty(), "Moira fork design record located");
        if (!vendorPath.empty()) {
            const std::string vendor = slurp(vendorPath);
            const fs::path moiraDir = fs::path(vendorPath).parent_path() / "Moira";
            int sourceFiles = 0, markedFiles = 0, markedLines = 0;
            std::set<std::string> pomIds;
            std::vector<std::string> boundaryLeaks;
            std::vector<std::string> markedNames;
            const auto word = [](char c) {
                return std::isalnum(uint8_t(c)) || c == '_';
            };
            std::error_code ec;
            for (fs::recursive_directory_iterator it(moiraDir, ec), end;
                 !ec && it != end; it.increment(ec)) {
                if (!it->is_regular_file()) continue;
                const std::string ext = it->path().extension().string();
                if (ext != ".h" && ext != ".cpp") continue;
                sourceFiles++;
                const std::string source = slurp(it->path().string());
                bool marked = false, hasPom = false;
                std::stringstream lines(source);
                for (std::string line; std::getline(lines, line); ) {
                    if (line.find("POM68K") != std::string::npos) {
                        marked = true;
                        markedLines++;
                    }
                }
                for (size_t i = source.find("pom"); i != std::string::npos;
                     i = source.find("pom", i + 3)) {
                    if (i && word(source[i - 1])) continue;
                    size_t e = i + 3;
                    while (e < source.size() && word(source[e])) e++;
                    if (e == i + 3) continue;
                    hasPom = true;
                    pomIds.insert(source.substr(i, e - i));
                }
                const std::string name = it->path().filename().string();
                if (marked) {
                    markedFiles++;
                    markedNames.push_back(name);
                }
                if (hasPom && !marked) boundaryLeaks.push_back(name);
            }
            check(!ec, "Moira source inventory traversed");
            check(boundaryLeaks.empty(),
                  "every source file exposing a pom* extension carries a POM68K marker");

            const size_t tableStart = vendor.find("## Inventory of local patches");
            const size_t tableEnd = vendor.find("\n## ", tableStart + 3);
            const std::string inventory = tableStart == std::string::npos
                ? std::string() : vendor.substr(tableStart, tableEnd - tableStart);
            int patchGroups = 0;
            std::stringstream rowsIn(inventory);
            for (std::string line; std::getline(rowsIn, line); ) {
                if (line.size() > 3 && line[0] == '|' && line[1] == ' ' &&
                    std::isdigit(uint8_t(line[2]))) patchGroups++;
            }
            check(patchGroups > 0, "Moira local-patch inventory table parsed");
            for (const std::string& name : markedNames)
                check(inventory.find('`' + name + '`') != std::string::npos,
                      "Moira inventory names marked source `" + name + "`");

            int jitIds = 0;
            for (const std::string& id : pomIds)
                if (id.rfind("pomJit", 0) == 0) jitIds++;
            const std::string idClaim = "**" + std::to_string(pomIds.size()) +
                "** (" + std::to_string(jitIds) + " of them `pomJit*`)";
            const std::string lineClaim = "| **" + std::to_string(markedLines) + "** |";
            const std::string fileClaim = "**" + std::to_string(markedFiles) +
                " of " + std::to_string(sourceFiles) + "**";
            const std::string groupClaim = "| **" + std::to_string(patchGroups) + "** |";
            check(vendor.find(idClaim) != std::string::npos,
                  "Moira document states the computed pom* identifier count");
            check(vendor.find(lineClaim) != std::string::npos,
                  "Moira document states the computed POM68K-marked line count");
            check(vendor.find(fileClaim) != std::string::npos,
                  "Moira document states the computed marked/source file count");
            check(vendor.find(groupClaim) != std::string::npos,
                  "Moira document states the computed patch-group count");
        }
    }

    // A generated index that silently stops covering new entries is worse
    // than none: it looks complete. Regenerate with
    // `python3 tools/changelog_index.py`.
    {
        // ── 8. CHANGELOG_INDEX.md covers every dated entry ───────────────
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

    // ── 9. Native backends do not grow a second memory decoder ───────────
    // The compiler enforces token-bearing access-helper signatures; this
    // lexical boundary catches the two tempting ways to bypass that seam.
    for (const char* relative : {
             "src/jit/backends/JitBackendA64.cpp",
             "src/jit/backends/JitBackendX64.cpp" }) {
        const std::string path = testasset::find(relative);
        check(!path.empty(), std::string(relative) + " located");
        if (path.empty()) continue;
        const std::string source = slurp(path);
        check(source.find("instructionMemoryPlan(") != std::string::npos,
              std::string(relative) + " consumes Instr::memory plans");
        check(source.find("describeMemory(") == std::string::npos,
              std::string(relative) + " does not decode memory semantics");
        check(source.find("soleAccess") == std::string::npos,
              std::string(relative) + " has no opcode-local sole-access guess");
        check(source.find("exactTstRead030") == std::string::npos,
              std::string(relative) + " has no opcode-local exact-access exception");
        check(source.find(".semantics") != std::string::npos,
              std::string(relative) + " consumes Instr::semantics");
        check(source.find("findEffectiveAddress(") != std::string::npos,
              std::string(relative) + " consumes Instr effective-address plans");
        check(source.find(".control") != std::string::npos,
              std::string(relative) + " consumes Instr control-flow plans");
        check(source.find("branchDisplacement(") == std::string::npos,
              std::string(relative) + " does not decode branch extensions");
        check(source.find("decoded->fullFormat") != std::string::npos,
              std::string(relative) + " rejects unproved full-index lowering safely");
        check(source.find(".word(") == std::string::npos,
              std::string(relative) + " never re-decodes BlockIr extension words");
        check(source.find("describeInstruction(op)") != std::string::npos,
              std::string(relative) + " shares raw-opcode census semantics");
        check(source.find("switch (op & 0xF000)") == std::string::npos,
              std::string(relative) + " has no opcode-line dispatch decoder");
        check(source.find("aluDirectionA64") == std::string::npos &&
              source.find("memoryAluDirection(") == std::string::npos &&
              source.find("enum class AluOp") == std::string::npos,
              std::string(relative) + " has no private ALU semantic decoder");
        check(source.find("detail::env") == std::string::npos &&
              source.find("getenv(") == std::string::npos,
              std::string(relative) + " has no private live environment policy");
    }

    // ── 10. Every `file:line` citation the docs make lands inside the file ─
    // The house rule is "cite file + line range in comments", and the docs
    // follow it several hundred times over. Line numbers are the one kind of
    // reference that rots with every unrelated edit above them, silently:
    // nothing in the build reads them, so a citation can point twenty lines
    // past the end of its file and every gate stays green. The 2026-08-19
    // pass found three that had — `SaveStateMachines.h:96-155` on a
    // 113-line header among them.
    //
    // This is deliberately the WEAK half of the claim: it proves the range
    // exists, not that it says what the sentence says. A wrong-but-in-range
    // citation still needs a reader. Mechanising the sound half is cheap
    // insurance against the failure that cannot be read at all.
    //
    // Only citations that resolve inside this tree are judged. A doc naming
    // `mac128.cpp:1317` is quoting MAME, which is not vendored here; those
    // resolve to nothing and are skipped rather than guessed at. The
    // POM68K/MAME naming split makes that safe — `Iwm.cpp` is ours,
    // `iwm.cpp` is theirs.
    {
        const std::string root =
            claude.substr(0, claude.size() - std::string("CLAUDE.md").size());

        std::vector<std::string> docs = {
            "CLAUDE.md", "README.md", "DEV.md", "TODO.md",
            "src/jit/POM68K_JIT.md", "extern/moira/POM68K_VENDOR.md",
        };
        {   // …and every research note under docs/, including tomorrow's.
            std::error_code ec;
            std::vector<std::string> notes;
            for (const auto& entry :
                 std::filesystem::directory_iterator(root + "docs", ec))
                if (!ec && entry.is_regular_file() &&
                    entry.path().extension() == ".md")
                    notes.push_back("docs/" + entry.path().filename().string());
            std::sort(notes.begin(), notes.end());
            docs.insert(docs.end(), notes.begin(), notes.end());
        }

        // First hit wins, so a bare `CMakeLists.txt` is the repo's own.
        static const char* const kRoots[] = {
            "", "src/", "src/jit/", "src/jit/backends/", "tests/", "tools/",
            "oracle/", "extern/moira/Moira/", ".github/workflows/",
        };
        auto lineCount = [](const std::string& path) {
            std::ifstream file(path);
            int lines = 0;
            for (std::string ignored; std::getline(file, ignored); ) lines++;
            return lines;
        };

        int cited = 0;
        for (const std::string& doc : docs) {
            const std::string text = slurp(root + doc);
            if (text.empty()) continue;
            std::vector<std::string> stale;
            for (size_t i = text.find('`'); i != std::string::npos;
                 i = text.find('`', i + 1)) {
                const size_t end = text.find('`', i + 1);
                if (end == std::string::npos) break;
                const std::string token = text.substr(i + 1, end - i - 1);
                i = end;
                // `<path>.<ext>:<first>[-<last>]`, nothing else.
                const size_t colon = token.rfind(':');
                if (colon == std::string::npos || colon == 0) continue;
                const std::string path = token.substr(0, colon);
                const std::string span = token.substr(colon + 1);
                if (path.find('.') == std::string::npos ||
                    path.find(' ') != std::string::npos) continue;
                int first = 0, last = 0;
                {
                    size_t k = 0;
                    while (k < span.size() && std::isdigit(uint8_t(span[k])))
                        first = first * 10 + (span[k++] - '0');
                    if (k == 0) continue;
                    last = first;
                    if (k < span.size() && span[k] == '-') {
                        last = 0;
                        size_t d = ++k;
                        while (k < span.size() && std::isdigit(uint8_t(span[k])))
                            last = last * 10 + (span[k++] - '0');
                        if (k == d) continue;
                    }
                    if (k != span.size()) continue;      // not a pure range
                }
                std::string resolved;
                for (const char* base : kRoots) {
                    const std::string candidate = root + base + path;
                    if (std::filesystem::is_regular_file(candidate)) {
                        resolved = candidate;
                        break;
                    }
                }
                if (resolved.empty()) continue;          // not ours to judge
                cited++;
                const int lines = lineCount(resolved);
                if (std::max(first, last) > lines)
                    stale.push_back(token + " (" + path + " has " +
                                    std::to_string(lines) + " lines)");
            }
            for (const std::string& one : stale)
                check(false, doc + " cites `" + one + "`");
            check(stale.empty(), doc + ": every in-tree file:line citation "
                                       "lands inside its file");
        }
        check(cited > 100,
              "in-tree file:line citations judged (" + std::to_string(cited) + ")");
    }

    const std::string enginePath = testasset::find("src/jit/JitEngine.cpp");
    check(!enginePath.empty() &&
          slurp(enginePath).find("config_(resolveConfig())") != std::string::npos,
          "each JIT Engine resolves its configuration exactly at construction");

    std::printf("%s\n", gFails ? "FAILED" : "PASS");
    return gFails ? 1 : 0;
}
