// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The JIT store-inventory contract, pinned at the source level. A board
// may declare `kJitStoreInventoryComplete = true` — every store into its
// guest RAM passes CodeGuard::note() — and its CPU wrapper may then retire
// the CACR SMC flush (drop ALL generated code on the CI/CEI strobes) that
// stood in for the missing inventory. The claim is a property of the
// board's SOURCE: which lines assign into `ram_`, whether a bulk copy can
// bypass the guard, and where the raw buffer pointer escapes. So the gate
// reads the source, exactly as `docs_test` pins its cross-document
// contracts:
//
//   1. every `ram_[...] = ...` assignment in a claiming board's .cpp sits
//      within a few lines after a `->note(` call;
//   2. no memcpy/std::copy/std::fill/assign touches `ram_` there;
//   3. a non-const `ram_.data()` escapes only through `return` statements
//      (the codeSpan/dataSpan windows, which the engine's codeMask guards);
//   4. the board's wrapper actually consults the constant;
//   5. a board that has NOT declared the constant keeps flushing: its
//      wrapper's didChangeCACR still contains the unconditional flushAll.
//
// TODO § B.4 rule: do not extrapolate one board's proof to another. Each
// claiming pair below was audited individually (V8: 2026-08-19 §
// C.4quinquies; VASP: 2026-09-03 (ninth)).

#include "AssetFingerprint.h"

#include <cstdio>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) failures++;
}

std::string slurp(const std::string& path) {
    std::ifstream f(path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::vector<std::string> lines(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream ss(s);
    for (std::string l; std::getline(ss, l);) out.push_back(l);
    return out;
}

// Rule 1-3 over one claiming board's implementation file.
void checkInventory(const std::string& name, const std::string& cppPath) {
    const std::string src = slurp(cppPath);
    check(!src.empty(), name + ": implementation readable");
    const std::vector<std::string> ls = lines(src);

    // An assignment into the RAM vector: `ram_[i] = v` in any spelling,
    // excluding comparisons (== / != handled by requiring a single `=`).
    const std::regex store(R"(ram_\[[^\]]*\]\s*=[^=])");
    const std::regex noted(R"(->note\()");
    int stores = 0;
    for (size_t i = 0; i < ls.size(); i++) {
        if (!std::regex_search(ls[i], store)) continue;
        stores++;
        bool guarded = false;
        for (size_t j = i > 12 ? i - 12 : 0; j <= i; j++)
            if (std::regex_search(ls[j], noted)) { guarded = true; break; }
        check(guarded, name + ": ram_ store at " + cppPath + ":" +
                           std::to_string(i + 1) +
                           " sits within 12 lines of a CodeGuard note()");
    }
    check(stores > 0, name + ": the scanner still sees the ram_ store sites (" +
                          std::to_string(stores) + ")");

    for (size_t i = 0; i < ls.size(); i++) {
        const std::string& l = ls[i];
        const bool bulk = l.find("memcpy") != std::string::npos ||
                          l.find("std::copy") != std::string::npos ||
                          l.find("std::fill") != std::string::npos ||
                          l.find(".assign(") != std::string::npos;
        check(!(bulk && l.find("ram_") != std::string::npos),
              name + ": no bulk copy into ram_ at line " + std::to_string(i + 1));
        if (l.find("ram_.data()") != std::string::npos)
            check(l.find("return") != std::string::npos,
                  name + ": raw ram_.data() escapes only through the span "
                         "accessors (line " + std::to_string(i + 1) + ")");
    }
}

}  // namespace

int main() {
    // ── The claiming boards, each audited on its own ─────────────────────
    struct Board { const char* name, *header, *cpp, *wrapper; };
    const Board proven[] = {
        {"V8", "src/V8Memory.h", "src/V8Memory.cpp", "src/Cpu030.cpp"},
        {"VASP", "src/VaspMemory.h", "src/VaspMemory.cpp", "src/VaspCpu.cpp"},
    };
    for (const Board& b : proven) {
        const std::string header = slurp(testasset::find(b.header));
        check(header.find("kJitStoreInventoryComplete = true") !=
                  std::string::npos,
              std::string(b.name) + ": header declares the complete inventory");
        checkInventory(b.name, testasset::find(b.cpp));
        const std::string wrapper = slurp(testasset::find(b.wrapper));
        check(wrapper.find("kJitStoreInventoryComplete") != std::string::npos,
              std::string(b.name) + ": wrapper consults the constant");
    }

    // ── The boards that have NOT earned the claim keep the flush ─────────
    struct Unproven { const char* name, *header, *wrapper; };
    const Unproven pending[] = {
        {"RBV", "src/RbvMemory.h", "src/RbvCpu.cpp"},
        {"MSC", "src/MscMemory.h", "src/MscCpu.cpp"},
        {"Sonora", "src/SonoraMemory.h", "src/SonoraCpu.cpp"},
    };
    for (const Unproven& b : pending) {
        const std::string header = slurp(testasset::find(b.header));
        check(header.find("kJitStoreInventoryComplete") == std::string::npos,
              std::string(b.name) +
                  ": no inventory claim without its own audit and gate row");
        const std::string wrapper = slurp(testasset::find(b.wrapper));
        check(wrapper.find("flushAll") != std::string::npos,
              std::string(b.name) + ": wrapper still flushes on the strobes");
    }

    if (failures) {
        std::printf("\n%d store-inventory contract(s) violated\n", failures);
        return 1;
    }
    std::printf("\nstore inventory contracts hold\n");
    return 0;
}
