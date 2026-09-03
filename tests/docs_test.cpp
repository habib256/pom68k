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
//  11. startup follows ProcessEnvironment -> RuntimeConfig -> Factory ->
//      Session -> GUI runtime, whose shared lifecycles stay outside main.cpp
//  12. GUI and gate media lookup share immutable-reference preference
//  13. TODO.md stays an open-only backlog and delegates registry facts
//  14. CMake registrations, dev tools and registry policy stay modular
//  15. leaf devices consume subsystem config views, never the whole core bag
//  16. STATUS.md — the registry as a GENERATED artifact (tools/status_md.py)
//      — re-derives against the same roster/manifest this gate reads
//
// Check 4 is here because it caught a live one the day it was written: four
// gates — the three IIfx ones and `duo230_boot_etalon` — were registered
// AFTER the old inline label-derivation block and so carried no
// label at all. Two whole platforms were invisible to every `ctest -L` tier
// while the docs advertised the tiers as complete.
//
// The roster comes from `pom68k_gates.tsv`, written by CMake at configure
// time: a documentation gate that cannot see the registry can only check the
// docs against themselves.

#include "AssetFingerprint.h"          // testasset::find — shared asset search
#include "MachineFactory.h"
#include "MachineCatalog.h"
#include "MachineSession.h"
#include "ProcessEnvironment.h"
#include "RuntimeConfig.h"
#include "StartupOptions.h"
#include "StartupSnapshot.h"
#include "StartupValueDecoding.h"
#include "GuiSessionObjects.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
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

static int countOccurrences(const std::string& text,
                            const std::string& needle) {
    int count = 0;
    for (size_t pos = text.find(needle); pos != std::string::npos;
         pos = text.find(needle, pos + needle.size()))
        count++;
    return count;
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

// Every integer that immediately FOLLOWS `needle`, past the same markdown
// emphasis. The registry's two DERIVED per-architecture totals are written
// that way ("configure sees **234**"), so the backward scan above cannot
// see them -- and for four months neither could anything else.
static std::vector<int> numbersAfter(const std::string& text,
                                     const std::string& needle) {
    std::vector<int> out;
    for (size_t i = text.find(needle); i != std::string::npos;
         i = text.find(needle, i + 1)) {
        size_t k = i + needle.size();
        while (k < text.size() && (text[k] == ' ' || text[k] == '*' ||
                                   text[k] == '`' || text[k] == '\n')) k++;
        const size_t begin = k;
        while (k < text.size() && std::isdigit(uint8_t(text[k]))) k++;
        if (k == begin || k - begin > 5) continue;
        out.push_back(std::stoi(text.substr(begin, k - begin)));
    }
    return out;
}

int main() {
    const std::string mainCpp = testasset::find("src/main.cpp");
    const std::string guiRuntimeCpp =
        testasset::find("src/GuiMachineRuntime.cpp");
    const std::string guiShell = testasset::find("src/GuiShell.h");
    const std::string guiShellCommon =
        testasset::find("src/GuiShellCommon.h");
    const std::array<std::string, 6> guiRunners = {
        testasset::find("src/GuiRunnerCompact.h"),
        testasset::find("src/GuiRunnerDafb.h"),
        testasset::find("src/GuiRunnerSonora.h"),
        testasset::find("src/GuiRunnerToby.h"),
        testasset::find("src/GuiRunnerV8.h"),
        testasset::find("src/GuiRunnerDuo.h")};
    const std::string guiShellCpp = testasset::find("src/GuiShell.cpp");
    const std::string guiHostServices =
        testasset::find("src/GuiHostServices.h");
    const std::string guiHostServicesCpp =
        testasset::find("src/GuiHostServices.cpp");
    const std::string platformComposers =
        testasset::find("src/PlatformComposers.cpp");
    const std::array<std::string, 6> platformFamilies = {
        testasset::find("src/PlatformCompact.cpp"),
        testasset::find("src/PlatformToby.cpp"),
        testasset::find("src/PlatformV8.cpp"),
        testasset::find("src/PlatformSonora.cpp"),
        testasset::find("src/PlatformDafb.cpp"),
        testasset::find("src/PlatformDuo.cpp")};
    const std::string platformSupport =
        testasset::find("src/PlatformCompositionSupport.h");
    const std::string guiSessionState =
        testasset::find("src/GuiSessionState.h");
    const std::string peripheralWindowHeader =
        testasset::find("src/PeripheralWindow.h");
    const std::string peripheralWindowCpp =
        testasset::find("src/PeripheralWindow.cpp");
    const std::string lleSessionHeader =
        testasset::find("src/LleSession.h");
    const std::string guiWindowSession =
        testasset::find("src/GuiWindowSession.h");
    const std::string runtimeConfig = testasset::find("src/RuntimeConfig.h");
    const std::string runtimeConfigSource =
        testasset::find("src/RuntimeConfig.cpp");
    const std::string runtimeConfigParsers =
        testasset::find("src/RuntimeConfigParsers.h");
    const std::string startupOptions =
        testasset::find("src/StartupOptions.h");
    const std::string startupSnapshot =
        testasset::find("src/StartupSnapshot.h");
    const std::string startupValuePolicy =
        testasset::find("src/StartupValuePolicy.h");
    const std::string startupValueDecoding =
        testasset::find("src/StartupValueDecoding.h");
    const std::string startupDomainView =
        testasset::find("src/StartupDomainView.h");
    const std::string runtimeConfigProduct =
        testasset::find("src/RuntimeConfigProduct.cpp");
    const std::string runtimeConfigCore =
        testasset::find("src/RuntimeConfigCore.cpp");
    const std::string runtimeConfigMachine =
        testasset::find("src/RuntimeConfigMachine.cpp");
    const std::string processEnvironment =
        testasset::find("src/ProcessEnvironment.cpp");
    const std::string jitConfigHeader =
        testasset::find("src/jit/JitConfig.h");
    const std::string jitTestConfig = testasset::find("tests/JitTestConfig.h");
    const std::array<std::string, 12> jitConfigConsumers = {
        testasset::find("src/Cpu68k.h"),
        testasset::find("src/IIfxCpu.h"),
        testasset::find("src/Cpu020.h"),
        testasset::find("src/Cpu030.h"),
        testasset::find("src/Cpu040.h"),
        testasset::find("src/MscCpu.h"),
        testasset::find("src/CentrisCpu.h"),
        testasset::find("src/Q630Cpu.h"),
        testasset::find("src/Q700Cpu.h"),
        testasset::find("src/SonoraCpu.h"),
        testasset::find("src/VaspCpu.h"),
        testasset::find("src/RbvCpu.h")};
    const std::array<std::string, 13> coreConfigLeaves = {
        testasset::find("src/AdbVia.h"),
        testasset::find("src/CudaLle.h"),
        testasset::find("src/PgePmu.h"),
        testasset::find("src/Cpu020.h"),
        testasset::find("src/Cpu030.h"),
        testasset::find("src/Cpu040.h"),
        testasset::find("src/MscCpu.h"),
        testasset::find("src/CentrisCpu.h"),
        testasset::find("src/Q630Cpu.h"),
        testasset::find("src/Q700Cpu.h"),
        testasset::find("src/SonoraCpu.h"),
        testasset::find("src/VaspCpu.h"),
        testasset::find("src/RbvCpu.h")};
    const std::array<std::string, 12> coreConfigRoots = {
        testasset::find("src/MacMemory.h"),
        testasset::find("src/MacIIMemory.h"),
        testasset::find("src/IIfxMemory.h"),
        testasset::find("src/V8Memory.h"),
        testasset::find("src/SonoraMemory.h"),
        testasset::find("src/VaspMemory.h"),
        testasset::find("src/RbvMemory.h"),
        testasset::find("src/MscMemory.h"),
        testasset::find("src/CentrisMemory.h"),
        testasset::find("src/Q605Memory.h"),
        testasset::find("src/Q630Memory.h"),
        testasset::find("src/Q700Memory.h")};
    const std::array<std::string, 10> coreCpuConsumers = {
        testasset::find("src/Cpu020.h"),
        testasset::find("src/Cpu030.h"),
        testasset::find("src/Cpu040.h"),
        testasset::find("src/MscCpu.h"),
        testasset::find("src/CentrisCpu.h"),
        testasset::find("src/Q630Cpu.h"),
        testasset::find("src/Q700Cpu.h"),
        testasset::find("src/SonoraCpu.h"),
        testasset::find("src/VaspCpu.h"),
        testasset::find("src/RbvCpu.h")};
    const std::string coreConfigHeader =
        testasset::find("src/CoreConfig.h");
    const std::string machineFactory = testasset::find("src/MachineFactory.cpp");
    const std::string machineSession = testasset::find("src/MachineSession.h");
    const std::string fixtureStore = testasset::find("src/FixtureStore.h");
    const std::string assetFingerprint =
        testasset::find("tests/AssetFingerprint.h");
    const std::string catalog = testasset::find("src/MachineCatalog.h");
    const std::string claude  = testasset::find("CLAUDE.md");
    const std::string cmakeRoot = testasset::find("CMakeLists.txt");
    const std::array<std::string, 5> cmakeModules = {
        testasset::find("cmake/Pom68kComponentGates.cmake"),
        testasset::find("cmake/Pom68kMachineGates.cmake"),
        testasset::find("cmake/Pom68kJitGates.cmake"),
        testasset::find("cmake/Pom68kDevTools.cmake"),
        testasset::find("cmake/Pom68kGatePolicy.cmake")};
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
    const std::string mainSource = slurp(mainCpp);
    const std::string guiRuntimeSource = slurp(guiRuntimeCpp);
    std::string shellHeaderSource = slurp(guiShellCommon);
    bool guiRunnersPresent = !guiShellCommon.empty();
    for (const std::string& runner : guiRunners) {
        guiRunnersPresent = guiRunnersPresent && !runner.empty();
        shellHeaderSource += slurp(runner);
    }
    const std::string shellSource = slurp(guiShellCpp);
    const std::string hostServicesSource =
        slurp(guiHostServices) + slurp(guiHostServicesCpp);
    const std::string peripheralWindowSource =
        slurp(peripheralWindowHeader) + slurp(peripheralWindowCpp);
    const std::string compactComposerSource = slurp(platformFamilies[0]);
    const std::string platformSupportSource = slurp(platformSupport);
    std::string composersSource = slurp(platformComposers) +
                                  platformSupportSource;
    bool platformFamiliesPresent = !platformSupport.empty();
    for (const std::string& path : platformFamilies) {
        platformFamiliesPresent = platformFamiliesPresent && !path.empty();
        composersSource += slurp(path);
    }
    const std::string cmakeRootSource = slurp(cmakeRoot);
    std::array<std::string, 5> cmakeModuleSources;
    bool cmakeModulesPresent = !cmakeRoot.empty();
    for (std::size_t i = 0; i < cmakeModules.size(); i++) {
        cmakeModulesPresent = cmakeModulesPresent && !cmakeModules[i].empty();
        cmakeModuleSources[i] = slurp(cmakeModules[i]);
    }
    check(shellSource.find("kMachineProfiles") !=
              std::string::npos,
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

    // ── 11. Application composition + shared GUI extraction contract ─────
    // main.cpp is the composition root, not the configuration/ROM dispatcher:
    // ProcessEnvironment -> RuntimeConfig -> MachineFactory ->
    // MachineSession -> concrete runtime.
    check(!processEnvironment.empty() && !runtimeConfig.empty() &&
              !runtimeConfigSource.empty() && !runtimeConfigParsers.empty() &&
              !startupOptions.empty() && !startupSnapshot.empty() &&
              !jitConfigHeader.empty() &&
              !startupDomainView.empty() &&
              !runtimeConfigProduct.empty() && !runtimeConfigCore.empty() &&
              !runtimeConfigMachine.empty() && !machineFactory.empty() &&
              !machineSession.empty(),
          "headless application architecture files are present");
    bool narrowCoreConfigLeaves = true;
    for (const std::string& leaf : coreConfigLeaves) {
        narrowCoreConfigLeaves = narrowCoreConfigLeaves && !leaf.empty() &&
            slurp(leaf).find("const pom68k::CoreConfig&") == std::string::npos &&
            slurp(leaf).find("defaultCoreConfig()") == std::string::npos;
    }
    check(narrowCoreConfigLeaves,
          "leaf devices consume subsystem views instead of the CoreConfig bag");
    bool requiredCoreConfigRoots = !coreConfigHeader.empty();
    for (const std::string& rootPath : coreConfigRoots) {
        const std::string source = slurp(rootPath);
        requiredCoreConfigRoots = requiredCoreConfigRoots && !rootPath.empty() &&
            source.find("const pom68k::CoreConfig& coreConfig") !=
                std::string::npos &&
            source.find("coreConfig =") == std::string::npos;
    }
    check(requiredCoreConfigRoots,
          "board composition roots require explicit CoreConfig injection");

    // ── LLE qualification state is owned, not global (2026-08-27) ────────
    // The bug this would have caught: it was five loose namespace-scope
    // globals, so a second machine in one process shared one verdict and one
    // device list. Three lines pin the direction it now runs in — the state
    // is a TYPE, a session OWNS one, and devices are HANDED one through the
    // core policy. Losing any of the three puts it back in the process.
    {
        const std::string lleHeader = slurp(testasset::find("src/LleSession.h"));
        const std::string sessionHeader =
            slurp(testasset::find("src/MachineSession.h"));
        const std::string coreHeader = slurp(testasset::find("src/CoreConfig.h"));
        check(lleHeader.find("class Registry") != std::string::npos,
              "LLE qualification state is a type, not loose globals");
        check(sessionHeader.find("std::unique_ptr<lle::Registry>") !=
                  std::string::npos,
              "a MachineSession owns its LLE registry");
        check(coreHeader.find("lle::Registry* registry") != std::string::npos,
              "devices receive their LLE registry through the core policy");
    }
    bool requiredCoreCpuViews = true;
    for (const std::string& cpuPath : coreCpuConsumers) {
        const std::string source = slurp(cpuPath);
        requiredCoreCpuViews = requiredCoreCpuViews && !cpuPath.empty() &&
            source.find("const pom68k::CoreCpuConfig& cpuConfig") !=
                std::string::npos &&
            source.find("cpuConfig =") == std::string::npos;
    }
    check(requiredCoreCpuViews,
          "configured CPU leaves require explicit CoreCpuConfig injection");
    const std::string cpu040Header = slurp(testasset::find("src/Cpu040.h"));
    const std::string pgeHeader = slurp(testasset::find("src/PgePmu.h"));
    const std::string coreHeaderSource = slurp(coreConfigHeader);
    check(cpu040Header.find("const pom68k::CoreDiagnosticConfig& diagnostics") !=
              std::string::npos &&
              cpu040Header.find("diagnostics =") == std::string::npos &&
              pgeHeader.find("const pom68k::CorePeripheralConfig& peripherals") !=
                  std::string::npos &&
              pgeHeader.find("peripherals =") == std::string::npos &&
              countOccurrences(coreHeaderSource,
                  "inline const CoreConfig& defaultCoreConfig()") == 1 &&
              coreHeaderSource.find("defaultCoreCpuConfig") == std::string::npos &&
              coreHeaderSource.find("defaultCorePeripheralConfig") ==
                  std::string::npos &&
              coreHeaderSource.find("defaultCoreDiagnosticConfig") ==
                  std::string::npos,
          "core policy has one explicit fixture default and no section fallbacks");
    const std::string factorySource = slurp(machineFactory);
    const std::size_t environmentStep =
        mainSource.find("captureRuntimeEnvironment");
    const std::size_t configStep = mainSource.find("RuntimeConfig::parse");
    const std::size_t factoryStep = mainSource.find("MachineFactory::create");
    const std::size_t sessionStep = mainSource.find("session.run()");
    check(environmentStep != std::string::npos &&
              configStep > environmentStep && factoryStep > configStep &&
              sessionStep > factoryStep,
          "main injects ProcessEnvironment into RuntimeConfig before composition");
    const std::string runtimeComposition = slurp(runtimeConfigSource);
    const std::string snapshotSource = slurp(startupSnapshot);
    const std::string startupViewHeader = slurp(startupDomainView);
    const std::string productDecoder = slurp(runtimeConfigProduct);
    const std::string coreDecoder = slurp(runtimeConfigCore);
    const std::string machineDecoder = slurp(runtimeConfigMachine);
    const std::string startupOptionSchema = slurp(startupOptions);
    const std::string startupPolicySchema = slurp(startupValuePolicy);
    const std::string startupValueDecoder = slurp(startupValueDecoding);
    const std::string jitDecoder = slurp(jitConfigHeader);
    check(runtimeComposition.find("parseProductStartup") !=
                  std::string::npos &&
              runtimeComposition.find("parseCoreStartup") !=
                  std::string::npos &&
              runtimeComposition.find("parseMachineSelectionStartup") !=
                  std::string::npos &&
              runtimeComposition.find("POM68K_") == std::string::npos,
          "RuntimeConfig composes typed domains without legacy knob spellings");
    check(snapshotSource.find("class StartupSnapshot") !=
                  std::string::npos &&
              snapshotSource.find("std::map<std::string, std::string") !=
                  std::string::npos &&
              startupViewHeader.find("const StartupSnapshot& values_") !=
                  std::string::npos &&
              productDecoder.find("parseProductStartup") !=
                  std::string::npos &&
              coreDecoder.find("parseCoreStartup") != std::string::npos &&
              machineDecoder.find("parseMachineSelectionStartup") !=
                  std::string::npos &&
              machineDecoder.find("applyMachineProfile") != std::string::npos,
          "startup decoding has separate compatibility, product, core and machine owners");
    check(startupOptionSchema.find("struct StartupOption") !=
                  std::string::npos &&
              startupOptionSchema.find("STARTUP_OPTION_SCHEMA") !=
                  std::string::npos &&
              startupOptionSchema.find("static_assert(validSchema()") !=
                  std::string::npos &&
              startupOptionSchema.find("concept StartupOptionFor") !=
                  std::string::npos &&
              startupPolicySchema.find("struct StartupValuePolicy") !=
                  std::string::npos &&
              startupValueDecoder.find("BooleanStartupOption") !=
                  std::string::npos &&
              startupValueDecoder.find("IntegerStartupOption") !=
                  std::string::npos &&
              startupViewHeader.find(
                  "requires StartupOptionFor<Option, Domain>") !=
                  std::string::npos &&
              startupOptionSchema.find("!StartupOptionFor<") !=
                  std::string::npos,
          "startup options have one unique typed schema consumed by decoders");
    std::array<std::size_t, 17> valuePolicyCounts{};
    for (const pom68k::StartupOptionSpec option :
         pom68k::startup_option::kAll)
        ++valuePolicyCounts[std::size_t(option.value.kind)];
    check(std::all_of(valuePolicyCounts.begin(), valuePolicyCounts.end(),
                      [](std::size_t count) { return count != 0; }) &&
              valuePolicyCounts[std::size_t(
                  pom68k::StartupValueKind::Custom)] == 5,
          "all startup options declare a value policy; only five stay custom");
    check(pom68k::startup_value::boolean(
              pom68k::startup_option::AppleTalk, std::string_view{}, false) &&
              !pom68k::startup_value::boolean(
                  pom68k::startup_option::SpeedLog, std::string_view{}, true) &&
              pom68k::startup_value::boolean(
                  pom68k::startup_option::DriveSounds,
                  std::string_view{}, true) &&
              !pom68k::startup_value::boolean(
                  pom68k::startup_option::JitFetch,
                  std::string_view("false"), true),
          "typed boolean policies preserve all legacy empty and false forms");
    check(pom68k::startup_value::integer(
              pom68k::startup_option::CacheBoost,
              std::string_view("0x10")) == std::optional<int>(16) &&
              !pom68k::startup_value::integer(
                  pom68k::startup_option::CacheBoost,
                  std::string_view("65")) &&
              pom68k::startup_value::integer(
                  pom68k::startup_option::JitBlockMax,
                  std::string_view("256")) == std::optional<int>(256) &&
              !pom68k::startup_value::integer(
                  pom68k::startup_option::JitBlockMax,
                  std::string_view("257")) &&
              pom68k::startup_value::integer(
                  pom68k::startup_option::ICacheMiss,
                  std::string_view{}) == std::optional<int>(0) &&
              !pom68k::startup_value::integer(
                  pom68k::startup_option::JitWindowKill,
                  std::string_view{}),
          "typed integer policies own radix and bounds");
    bool unknownStartupRejected = false;
    try {
        const pom68k::StartupSnapshot invalid{{"UNKNOWN_STARTUP_OPTION", "1"}};
        (void)invalid;
    } catch (const std::invalid_argument&) {
        unknownStartupRejected = true;
    }
    const pom68k::StartupSnapshot duplicateStartup{
        {"POM68K_AUDIO", "0"}, {"POM68K_AUDIO", "1"}};
    check(unknownStartupRejected && duplicateStartup.size() == 1 &&
              duplicateStartup.boolean(
                  pom68k::startup_option::Audio, false),
          "StartupSnapshot rejects unknown names and publishes immutable typed values");
    std::size_t jitOptionCount = 0;
    for (const pom68k::StartupOptionSpec option :
         pom68k::startup_option::kAll)
        if (pom68k::startupDomainIncludes(option.domains,
                                          pom68k::StartupDomain::Jit))
            ++jitOptionCount;
    check(sizeof(pom68k::startup_option::kAll) /
                  sizeof(pom68k::startup_option::kAll[0]) == 132 &&
              jitOptionCount == 40 &&
              jitDecoder.find("option::JitProfile") != std::string::npos &&
              jitDecoder.find("kConfigurationKeys") == std::string::npos &&
              jitDecoder.find("\"POM68K_") == std::string::npos,
          "the unified startup schema owns all 40 typed JIT options");
    check(productDecoder.find("ProductStartupView values") !=
                  std::string::npos &&
              coreDecoder.find("CoreStartupView values") !=
                  std::string::npos &&
              machineDecoder.find("MachineStartupView values") !=
                  std::string::npos &&
              (productDecoder + coreDecoder + machineDecoder).find(
                  ".numericSwitch(") == std::string::npos &&
              (productDecoder + coreDecoder + machineDecoder).find(
                  ".bounded(") == std::string::npos &&
              jitDecoder.find("values.integer") !=
                  std::string::npos &&
              jitDecoder.find("values.boolean") !=
                  std::string::npos &&
              jitDecoder.find("const pom68k::StartupSnapshot& values") !=
                  std::string::npos,
          "each startup decoder is compile-time restricted to its option domain");
    check(mainSource.find("makeGuiMachineRuntime()") != std::string::npos &&
              mainSource.find("GuiShell.h") == std::string::npos &&
              mainSource.find("MachineHost.h") == std::string::npos &&
              mainSource.find("GLFW") == std::string::npos &&
              mainSource.find("ImGui") == std::string::npos,
          "main is a cold composition root with no concrete GUI dependency");
    check(guiRuntimeSource.find("class GuiMachineRuntime") !=
              std::string::npos &&
              guiRuntimeSource.find("MachineSessionRuntime") !=
                  std::string::npos,
          "MachineSession type-erases the concrete core/host/UI runtime");
    const std::string sessionHeader = slurp(machineSession);
    check(sessionHeader.find(
              "std::unique_ptr<MachineSessionRuntime> runtime_") !=
              std::string::npos &&
              mainSource.find("makeGuiMachineRuntime()") !=
              std::string::npos,
          "MachineSession owns the concrete runtime with RAII");
    const std::string guiStateHeader = slurp(guiSessionState);
    check(guiRuntimeSource.find("GuiSessionState state_") !=
              std::string::npos &&
              guiStateHeader.find("AtalkHub atalk") != std::string::npos &&
              guiStateHeader.find("FloppySound floppySfx") !=
                  std::string::npos &&
              guiStateHeader.find("PeripheralHost peripherals") !=
                  std::string::npos &&
              guiStateHeader.find("struct GuiNetworkState") !=
                  std::string::npos &&
              guiStateHeader.find("struct GuiAudioState") !=
                  std::string::npos &&
              guiStateHeader.find("struct GuiRelaunchState") !=
                  std::string::npos &&
              guiStateHeader.find("struct GuiCpuPanelState") !=
                  std::string::npos &&
              guiStateHeader.find("struct GuiDiagnosticState") !=
                  std::string::npos &&
              guiRuntimeSource.find("static AtalkHub") == std::string::npos &&
              guiRuntimeSource.find("static FloppySound") ==
                  std::string::npos,
          "GUI runtime owns decomposed host/UI process state with RAII");
    const std::string guiWindowHeader = slurp(guiWindowSession);
    check(guiRuntimeSource.find("gGuiSessionState") == std::string::npos &&
              composersSource.find("runQuantumWithWire(GuiHostServices&") !=
                  std::string::npos &&
              composersSource.find("GuiSessionState& gui") ==
                  std::string::npos &&
              composersSource.find("hostServices->") == std::string::npos &&
              shellHeaderSource.find("services.sessionState()") ==
                  std::string::npos,
          "machines receive host services directly without a reverse pointer");
    check(guiWindowHeader.find("~GuiWindowSession() { close(); }") !=
              std::string::npos &&
              guiWindowHeader.find("void close() noexcept") !=
                  std::string::npos &&
              shellHeaderSource.find("glfwCreateWindow") == std::string::npos &&
              shellSource.find("glfwCreateWindow") == std::string::npos,
          "GUI window, ImGui backend and texture handles have one RAII owner");
    check(factorySource.find("MachineFactory::selectProfile") !=
              std::string::npos &&
              mainSource.find("0xECD99DC0") == std::string::npos &&
              composersSource.find("0xECD99DC0") == std::string::npos,
          "ROM identity dispatch belongs to MachineFactory, not main.cpp");
    const std::string runtimeConfigHeaderSource = slurp(runtimeConfig);
    check(runtimeConfigHeaderSource.find("struct MachineSelectionConfig") !=
              std::string::npos &&
              runtimeConfigHeaderSource.find("VariantMap") ==
                  std::string::npos &&
              runtimeConfigHeaderSource.find("EnvironmentMap") ==
                  std::string::npos &&
              runtimeConfigHeaderSource.find("const StartupSnapshot& startup") !=
                  std::string::npos &&
              runtimeConfigHeaderSource.find("variant(std::string_view") ==
                  std::string::npos &&
              factorySource.find("config.machineSelection()") !=
                  std::string::npos &&
              factorySource.find("config.variant") == std::string::npos,
          "profile routing consumes typed selections and retains no raw map");
    check(shellSource.find("profile.snapshot == current") !=
              std::string::npos &&
              shellSource.find("config_.variant") == std::string::npos,
          "Machine menu marks the actual typed snapshot as current");
    check(slurp(catalog).find("variantKey") == std::string::npos &&
              slurp(catalog).find("variantValue") == std::string::npos &&
              shellSource.find("setenv(") == std::string::npos &&
              shellSource.find("unsetenv(") == std::string::npos &&
              shellSource.find("relaunch.targetProfile = profile.snapshot") !=
                  std::string::npos &&
              hostServicesSource.find("machineProfileArguments(") !=
                  std::string::npos,
          "Machine menu relaunch is typed and never mutates process policy");
    check(peripheralWindowSource.find("setenv(") == std::string::npos &&
              peripheralWindowSource.find("unsetenv(") == std::string::npos &&
              peripheralWindowSource.find("firmwareOverridesForSelection") !=
                  std::string::npos &&
              hostServicesSource.find("firmwareOverrideArguments(") !=
                  std::string::npos &&
              guiStateHeader.find("FirmwareOverride> firmwareOverrides") !=
                  std::string::npos &&
              slurp(lleSessionHeader).find("EnvAssignment") ==
                  std::string::npos,
          "Peripheral relaunch carries typed firmware policy without env mutation");

    // RuntimeConfig strips application options, exposes typed ROM/media
    // inputs and preserves the exact relaunch argument list.
    {
        char a0[] = "POM68K";
        char a1[] = "--lle-aarch64-check";
        char a2[] = "machine.rom";
        char a3[] = "boot.vhd";
        char* av[] = {a0, a1, a2, a3};
        auto parsed = pom68k::app::RuntimeConfig::parse(4, av, {});
        check(parsed.fullLleAarch64() && parsed.fullLleCheckOnly() &&
                  parsed.romPath() && *parsed.romPath() == "machine.rom" &&
                  parsed.mediaArguments().size() == 1 &&
                  parsed.mediaArguments()[0] == "boot.vhd" &&
                  parsed.launchArguments().size() == 3,
              "RuntimeConfig parses typed ROM/media and exact relaunch inputs");

        pom68k::StartupSnapshot guiValues{
            {"POM68K_APPLETALK", "0"},
            {"POM68K_LTOUDP", "0"},
            {"POM68K_ATALK_WIRE_BOOST", "3"},
            {"POM68K_AUDIO", "0"},
            {"POM68K_DRIVE_SFX", "0"},
            {"POM68K_FLOPPY_RO", "1"},
            {"POM68K_FLOPPY", "boot.dsk"},
            {"POM68K_MONITOR", "512"},
            {"POM68K_NOFPU", "0"},
            {"POM68K_Q605_NOFPU", "1"},
            {"POM68K_FPU_LOG", "fpu.log"},
            {"POM68K_KEY_TRACE", ""},
            {"POM68K_FREEZE_PROBE", "1"},
            {"POM68K_SPEED_LOG", "1"},
            {"POM68K_SPEED_LOG_SKIP", "-2"},
            {"POM68K_SPEED_LOG_COUNT", "7"},
            {"POM68K_CPU_ENGINE", "interp"},
            {"POM68K_JIT_BACKEND", "threaded"},
            {"POM68K_JIT_HOT", "7"},
            {"POM68K_JIT_REQUIRE_NATIVE", ""},
            {"POM68K_CACHE_BOOST", "8"},
            {"POM68K_Q605_ID", "A55A222E"},
            {"POM68K_FLUX_JITTER", "12"},
            {"POM68K_ADB_LLE", "0"},
            {"POM68K_ATALK_DEBUG", "1"},
            {"POM68K_PGE_PCCOUNT", "12A,34B"},
        };
        auto injected = pom68k::app::RuntimeConfig::parse(
            1, av, guiValues);
        const auto& network = injected.network();
        const auto& devices = injected.devices();
        const auto& cpu = injected.cpu();
        const auto& diagnostics = injected.diagnostics();
        const auto& jitConfig = injected.jit().resolved;
        const auto& core = injected.core();
        check(!network.appleTalk && network.appleTalkWasSpecified &&
                  network.ltoUdp && network.appleTalkWireBoost == 3 &&
                  !devices.audio && !devices.driveSounds &&
                  !devices.floppyWriteBack && devices.startupFloppy &&
                  *devices.startupFloppy == "boot.dsk" &&
                  devices.monitorWidth && *devices.monitorWidth == 512 &&
                  !cpu.fpu && !cpu.q605Fpu && diagnostics.fpuLog &&
                  *diagnostics.fpuLog == "fpu.log" && diagnostics.keyTrace &&
                  diagnostics.freezeProbe && diagnostics.speedLog &&
                  diagnostics.speedLogSkip == 0 &&
                  diagnostics.speedLogCount == 7 && jitConfig.engineExplicit &&
                  jitConfig.engine == jit::EngineKind::Interp &&
                  jitConfig.backend == "threaded" && jitConfig.hotExplicit &&
                  jitConfig.hot == 7 && jitConfig.requireNative &&
                  core.cpu.cacheBoost && *core.cpu.cacheBoost == 8 &&
                  core.bus.q605MachineId &&
                  *core.bus.q605MachineId == 0xA55A222Eu &&
                  core.storage.fluxJitterPercent == 12 &&
                  !core.firmware.adbLle &&
                  core.diagnostics.appleTalkTrace &&
                  core.peripherals.pgePcCount.size() == 2 &&
                  core.peripherals.pgePcCount[0] == 0x12A &&
                  injected.machineSelection().memcJr ==
                      pom68k::SnapMachine::Lc575,
              "RuntimeConfig injects product, JIT and core policy aggregates");

        pom68k::StartupSnapshot emptyValues{
            {"POM68K_APPLETALK", ""}, {"POM68K_AUDIO", ""},
            {"POM68K_DRIVE_SFX", ""}};
        auto emptySpelling = pom68k::app::RuntimeConfig::parse(
            1, av, emptyValues);
        check(emptySpelling.network().appleTalk &&
                  emptySpelling.devices().audio &&
                  emptySpelling.devices().driveSounds,
              "typed configuration preserves empty-value default-on semantics");

        const auto savedEnvironment = [](const char* key) {
            const char* value = std::getenv(key);
            return value ? std::optional<std::string>(value) : std::nullopt;
        };
        const auto setEnvironment = [](const char* key, const char* value) {
#ifdef _WIN32
            _putenv_s(key, value ? value : "");
#else
            if (value) setenv(key, value, 1);
            else unsetenv(key);
#endif
        };
        const auto savedFull = savedEnvironment("POM68K_LLE_AARCH64_FULL");
        const auto savedCheck =
            savedEnvironment("POM68K_LLE_AARCH64_CHECK_ONLY");
        setEnvironment("POM68K_LLE_AARCH64_FULL", "0");
        setEnvironment("POM68K_LLE_AARCH64_CHECK_ONLY", "0");
        auto disabledEnv = pom68k::app::RuntimeConfig::parse(
            1, av, pom68k::app::captureRuntimeEnvironment());
        setEnvironment("POM68K_LLE_AARCH64_FULL", "1");
        auto enabledEnv = pom68k::app::RuntimeConfig::parse(
            1, av, pom68k::app::captureRuntimeEnvironment());
        setEnvironment("POM68K_LLE_AARCH64_FULL",
                       savedFull ? savedFull->c_str() : nullptr);
        setEnvironment("POM68K_LLE_AARCH64_CHECK_ONLY",
                       savedCheck ? savedCheck->c_str() : nullptr);
        check(!disabledEnv.fullLleAarch64() &&
                  !disabledEnv.fullLleCheckOnly() &&
                  enabledEnv.fullLleAarch64() &&
                  !enabledEnv.fullLleCheckOnly(),
              "RuntimeConfig preserves 0/1 environment semantics for strict mode");

        char profileArg[] = "--machine-profile=lc575";
        char profileRom[] = "quadra605.rom";
        char* profileArgv[] = {a0, profileArg, profileRom};
        pom68k::StartupSnapshot inheritedProfile{
            {"POM68K_Q605_ID", "A55A2225"},
            {"POM68K_Q605_NOFPU", "2"}};
        auto commandLineProfile = pom68k::app::RuntimeConfig::parse(
            3, profileArgv, inheritedProfile);
        check(commandLineProfile.machineSelection().memcJr ==
                  pom68k::SnapMachine::Lc575 &&
                  !commandLineProfile.cpu().q605Fpu &&
                  commandLineProfile.core().cpu.q605Fpu ==
                      pom68k::Q605FpuMode::Soft68882 &&
                  commandLineProfile.romPath() &&
                  *commandLineProfile.romPath() == "quadra605.rom",
              "typed profile option overrides inherited identity and FPU policy");

        char q605Arg[] = "--machine-profile=q605";
        char* q605Argv[] = {a0, q605Arg};
        pom68k::StartupSnapshot inheritedLc040{
            {"POM68K_Q605_NOFPU", "2"}};
        auto q605Profile = pom68k::app::RuntimeConfig::parse(
            2, q605Argv, inheritedLc040);
        check(q605Profile.machineSelection().memcJr ==
                  pom68k::SnapMachine::Q605 &&
                  q605Profile.cpu().q605Fpu &&
                  q605Profile.core().cpu.q605Fpu ==
                      pom68k::Q605FpuMode::Integrated,
              "typed Q605 relaunch restores integrated 68040 FPU policy");

        auto normalizedRelaunch = pom68k::app::machineProfileArguments(
            {"--machine-profile=iix", "machine.rom", "boot.vhd"},
            pom68k::SnapMachine::Lc3Plus);
        check(normalizedRelaunch.size() == 3 &&
                  normalizedRelaunch[0] == "--machine-profile=lc3plus" &&
                  normalizedRelaunch[1] == "machine.rom" &&
                  normalizedRelaunch[2] == "boot.vhd",
              "typed relaunch replaces stale profile arguments without moving media");

        char adbPolicyArg[] = "--firmware-override=adb:lle:";
        char firmwareRom[] = "machine.rom";
        char* adbPolicyArgv[] = {a0, adbPolicyArg, firmwareRom};
        pom68k::StartupSnapshot inheritedAdbPolicy{
            {"POM68K_ADB_LLE", "0"},
            {"POM68K_ADB_FW", "legacy-adb.bin"}};
        auto adbPolicy = pom68k::app::RuntimeConfig::parse(
            3, adbPolicyArgv, inheritedAdbPolicy);
        check(adbPolicy.core().firmware.adbLle &&
                  !adbPolicy.core().firmware.adbPath &&
                  adbPolicy.romPath() && *adbPolicy.romPath() == "machine.rom",
              "typed ADB policy overrides inherited mode and clears its path");

        char egretPolicyArg[] =
            "--firmware-override=egret:hle:custom:egret.bin";
        char* egretPolicyArgv[] = {a0, egretPolicyArg};
        pom68k::StartupSnapshot inheritedEgretPolicy{
            {"POM68K_EGRET_LLE", "1"},
            {"POM68K_CUDA_LLE", "0"},
            {"POM68K_CUDA_FW", "legacy-cuda.bin"}};
        auto egretPolicy = pom68k::app::RuntimeConfig::parse(
            2, egretPolicyArgv, inheritedEgretPolicy);
        check(!egretPolicy.core().firmware.egretLle &&
                  !egretPolicy.core().firmware.cudaLle &&
                  egretPolicy.core().firmware.egretPath &&
                  *egretPolicy.core().firmware.egretPath ==
                      "custom:egret.bin" &&
                  egretPolicy.core().firmware.cudaPath &&
                  *egretPolicy.core().firmware.cudaPath == "legacy-cuda.bin",
              "typed Egret mode stays distinct and preserves colons in paths");

        char cudaPolicyArg[] = "--firmware-override=cuda:lle:";
        char* cudaPolicyArgv[] = {a0, cudaPolicyArg};
        pom68k::StartupSnapshot inheritedCudaPolicy{
            {"POM68K_CUDA_LLE", "0"},
            {"POM68K_CUDA_FW", "legacy-cuda.bin"}};
        auto cudaPolicy = pom68k::app::RuntimeConfig::parse(
            2, cudaPolicyArgv, inheritedCudaPolicy);
        check(cudaPolicy.core().firmware.cudaLle &&
                  !cudaPolicy.core().firmware.cudaPath,
              "typed Cuda policy overrides inherited HLE and path independently");

        auto normalizedFirmware = pom68k::app::firmwareOverrideArguments(
            {"--firmware-override=adb:hle:old.bin",
             "--machine-profile=lc3plus",
             "--firmware-override=cuda:lle:old.bin",
             "machine.rom", "boot.vhd"},
            {{pom68k::FirmwareTarget::Adb, true, std::nullopt},
             {pom68k::FirmwareTarget::Cuda, false,
              std::string("C:\\dump:2.bin")}});
        check(normalizedFirmware.size() == 5 &&
                  normalizedFirmware[0] == "--firmware-override=adb:lle:" &&
                  normalizedFirmware[1] ==
                      "--firmware-override=cuda:hle:C:\\dump:2.bin" &&
                  normalizedFirmware[2] == "--machine-profile=lc3plus" &&
                  normalizedFirmware[3] == "machine.rom" &&
                  normalizedFirmware[4] == "boot.vhd",
              "typed firmware relaunch replaces stale policy without moving media");

        const std::vector<std::string> retainedFirmware{
            "--firmware-override=egret:lle:current.bin", "machine.rom"};
        check(pom68k::app::firmwareOverrideArguments(
                  retainedFirmware, {}) == retainedFirmware,
              "unrelated relaunch retains the current typed firmware policy");

        auto syntheticRom = [](std::size_t size, std::uint32_t checksum) {
            std::vector<std::uint8_t> rom(size);
            rom[0] = std::uint8_t(checksum >> 24);
            rom[1] = std::uint8_t(checksum >> 16);
            rom[2] = std::uint8_t(checksum >> 8);
            rom[3] = std::uint8_t(checksum);
            return rom;
        };
        struct RouteCase {
            std::size_t size;
            std::uint32_t checksum;
            pom68k::SnapMachine expected;
        };
        const RouteCase routes[] = {
            {128u << 10, 0,          pom68k::SnapMachine::Plus},
            {256u << 10, 0xB2E362A8, pom68k::SnapMachine::SE},
            {256u << 10, 0xB306E171, pom68k::SnapMachine::SEFDHD},
            {512u << 10, 0xA49F9914, pom68k::SnapMachine::Classic},
            {256u << 10, 0x9779D2C4, pom68k::SnapMachine::MacII},
            {256u << 10, 0x97221136, pom68k::SnapMachine::IIx},
            {512u << 10, 0x4147DD77, pom68k::SnapMachine::IIfx},
            {512u << 10, 0x368CADFE, pom68k::SnapMachine::IIci},
            {512u << 10, 0x36B7FB6C, pom68k::SnapMachine::IIsi},
            {512u << 10, 0x350EACF0, pom68k::SnapMachine::Lc},
            {512u << 10, 0x35C28F5F, pom68k::SnapMachine::LcII},
            {512u << 10, 0x3193670E, pom68k::SnapMachine::ClassicII},
            {1u << 20,   0xECD99DC0, pom68k::SnapMachine::ColorClassic},
            {1u << 20,   0xEAF1678D, pom68k::SnapMachine::MacTv},
            {1u << 20,   0xECBBC41C, pom68k::SnapMachine::Lc3},
            {1u << 20,   0xEDE66CBD, pom68k::SnapMachine::Lc520},
            {1u << 20,   0x4957EB49, pom68k::SnapMachine::IIvx},
            {1u << 20,   0xFF7439EE, pom68k::SnapMachine::Lc475},
            {1u << 20,   0xF1A6F343, pom68k::SnapMachine::Centris650},
            {1u << 20,   0x420DBFF3, pom68k::SnapMachine::Q700},
            {1u << 20,   0x3DC27823, pom68k::SnapMachine::Quadra950},
            {1u << 20,   0x06684214, pom68k::SnapMachine::Q630},
            {1u << 20,   0xECFA989B, pom68k::SnapMachine::Duo230},
        };
        bool allRoutes = true;
        std::set<pom68k::SnapMachine> routedProfiles;
        for (const RouteCase& route : routes) {
            const auto actual = pom68k::app::MachineFactory::selectProfile(
                parsed, syntheticRom(route.size, route.checksum)).snapshot;
            allRoutes = allRoutes &&
                actual == route.expected;
            routedProfiles.insert(actual);
        }
        check(allRoutes,
              "MachineFactory maps every platform's default ROM identity");

        struct ProfileRoute {
            const char* key;
            const char* value;
            std::size_t size;
            std::uint32_t checksum;
            pom68k::SnapMachine expected;
        };
        const ProfileRoute profileRoutes[] = {
            {"POM68K_MACII_MODEL", "iicx", 256u << 10, 0x97221136,
             pom68k::SnapMachine::IIcx},
            {"POM68K_MACII_MODEL", "se30", 256u << 10, 0x97221136,
             pom68k::SnapMachine::SE30},
            {"POM68K_MACII_MODEL", "fdhd", 256u << 10, 0x97221136,
             pom68k::SnapMachine::MacII},
            {"POM68K_LC3_PLUS", "1", 1u << 20, 0xECBBC41C,
             pom68k::SnapMachine::Lc3Plus},
            {"POM68K_AIO_ID", "A55A0101", 1u << 20, 0xEDE66CBD,
             pom68k::SnapMachine::Lc550},
            {"POM68K_AIO_ID", "CC2", 1u << 20, 0xEDE66CBD,
             pom68k::SnapMachine::CClassic2},
            {"POM68K_IIVI", "1", 1u << 20, 0x4957EB49,
             pom68k::SnapMachine::IIvi},
            {"POM68K_Q605_ID", "A55A2225", 1u << 20, 0xFF7439EE,
             pom68k::SnapMachine::Q605},
            {"POM68K_Q605_ID", "A55A222E", 1u << 20, 0xFF7439EE,
             pom68k::SnapMachine::Lc575},
            {"POM68K_CENTRIS_MODEL", "c610", 1u << 20, 0xF1A6F343,
             pom68k::SnapMachine::Centris610},
            {"POM68K_CENTRIS610", "1", 1u << 20, 0xF1A6F343,
             pom68k::SnapMachine::Centris610},
            {"POM68K_CENTRIS_MODEL", "q610", 1u << 20, 0xF1A6F343,
             pom68k::SnapMachine::Quadra610},
            {"POM68K_CENTRIS_MODEL", "q650", 1u << 20, 0xF1A6F343,
             pom68k::SnapMachine::Quadra650},
            {"POM68K_CENTRIS_MODEL", "q800", 1u << 20, 0xF1A6F343,
             pom68k::SnapMachine::Quadra800},
            {"POM68K_Q700_MODEL", "q900", 1u << 20, 0x420DBFF3,
             pom68k::SnapMachine::Quadra900},
            {"POM68K_Q630_ID", "A55A225A", 1u << 20, 0x06684214,
             pom68k::SnapMachine::Lc580},
        };
        char* baseArgv[] = {a0};
        for (const ProfileRoute& route : profileRoutes) {
            pom68k::StartupSnapshot one{
                {route.key, route.value}};
            auto selectionConfig =
                pom68k::app::RuntimeConfig::parse(
                    1, baseArgv, one);
            const auto actual = pom68k::app::MachineFactory::selectProfile(
                selectionConfig,
                syntheticRom(route.size, route.checksum)).snapshot;
            allRoutes = allRoutes && actual == route.expected;
            routedProfiles.insert(actual);
        }
        bool typedRelaunchRoutes = true;
        for (const ProfileRoute& route : profileRoutes) {
            std::string option = pom68k::app::machineProfileArgument(
                route.expected);
            char* typedArgv[] = {a0, option.data()};
            auto selectionConfig = pom68k::app::RuntimeConfig::parse(
                2, typedArgv, {});
            typedRelaunchRoutes = typedRelaunchRoutes &&
                pom68k::app::MachineFactory::selectProfile(
                    selectionConfig,
                    syntheticRom(route.size, route.checksum)).snapshot ==
                    route.expected;
        }
        check(allRoutes &&
                  routedProfiles.size() == pom68k::kMachineProfileCount,
              "MachineFactory routes every catalogue profile, including variants");
        check(typedRelaunchRoutes,
              "typed relaunch option round-trips every shared-ROM profile");

        bool strictRomSet = true;
        for (const std::uint32_t checksum : {
                 0xFF7439EEu, 0xF1A6F343u, 0xF1ACAD13u, 0x420DBFF3u,
                 0x3DC27823u, 0x06684214u, 0x064DC91Du}) {
            strictRomSet = strictRomSet &&
                pom68k::app::MachineFactory::qualifiesFullLleAarch64(
                    syntheticRom(1u << 20, checksum));
        }
        check(strictRomSet &&
                  !pom68k::app::MachineFactory::qualifiesFullLleAarch64(
                      syntheticRom(1u << 20, 0xDEADBEEF)) &&
                  !pom68k::app::MachineFactory::qualifiesFullLleAarch64(
                      syntheticRom(512u << 10, 0xFF7439EE)),
              "strict AArch64 mode accepts only qualified 1 MB 68040 ROMs");

        pom68k::SnapMachine observed = pom68k::SnapMachine::Plus;
        bool runtimeDestroyed = false;
        struct ProbeRuntime final : pom68k::app::MachineSessionRuntime {
            ProbeRuntime(pom68k::SnapMachine& value, bool& destroyedFlag)
                : observed(value), destroyed(destroyedFlag) {}
            ~ProbeRuntime() override { destroyed = true; }
            int run(pom68k::app::MachineSession& session) override {
                observed = session.profile().snapshot;
                return 73;
            }
            pom68k::SnapMachine& observed;
            bool& destroyed;
        };
        const auto iifx = pom68k::app::MachineFactory::selectProfile(
            parsed, syntheticRom(512u << 10, 0x4147DD77));
        {
            pom68k::app::MachineSession selectedSession(
                std::move(parsed), iifx,
                syntheticRom(512u << 10, 0x4147DD77), "iifx.rom",
                std::make_unique<ProbeRuntime>(observed, runtimeDestroyed));
            check(selectedSession.run() == 73 &&
                      observed == pom68k::SnapMachine::IIfx &&
                      !runtimeDestroyed,
                  "MachineSession retains and delegates once to its runtime");
        }
        check(runtimeDestroyed,
              "MachineSession destroys its owned runtime at session teardown");

        std::vector<int> destructionOrder;
        struct OwnedProbe {
            OwnedProbe(std::vector<int>& order, int identity)
                : order(order), identity(identity) {}
            ~OwnedProbe() { order.push_back(identity); }
            std::vector<int>& order;
            int identity;
        };
        {
            GuiSessionObjects objects;
            objects.make<OwnedProbe>(destructionOrder, 1);
            objects.make<OwnedProbe>(destructionOrder, 2);
        }
        check(destructionOrder == std::vector<int>({2, 1}),
              "GUI session objects are destroyed in reverse dependency order");
    }

    // GUI architecture responsibilities are physically separated and wired
    // through the session-owned runtime.
    check(!guiShell.empty() && !guiShellCpp.empty() && guiRunnersPresent &&
              !guiHostServices.empty() && !platformComposers.empty() &&
              platformFamiliesPresent,
          "GuiShell, GuiHostServices and PlatformComposers are present");
    check(guiRuntimeSource.find("PlatformComposers::run") !=
              std::string::npos &&
              guiRuntimeSource.find("std::make_unique<GuiShell>") !=
                  std::string::npos &&
              guiRuntimeSource.find("std::make_unique<GuiHostServices>") !=
                  std::string::npos &&
              guiRuntimeSource.find("runDafbGui<") == std::string::npos &&
              guiRuntimeSource.find("glfw") == std::string::npos,
          "GUI runtime only owns and connects the three responsibilities");
    check(hostServicesSource.find("class GuiHostServices") !=
              std::string::npos &&
              hostServicesSource.find("wireNetwork(") != std::string::npos &&
              hostServicesSource.find("prepareDriveSounds(") !=
                  std::string::npos &&
              hostServicesSource.find("requestRelaunch(") !=
                  std::string::npos &&
              hostServicesSource.find("qualify(") != std::string::npos &&
              hostServicesSource.find("openWindow(") == std::string::npos &&
              hostServicesSource.find("drawMachineMenu(") ==
                  std::string::npos,
          "GuiHostServices owns network, audio, relaunch and diagnostics only");
    check(shellHeaderSource.find("class GuiShell") != std::string::npos &&
              shellHeaderSource.find("int runCompactGui") !=
                  std::string::npos &&
              shellHeaderSource.find("int runDafbGui") != std::string::npos &&
              shellHeaderSource.find("int runSonoraGui") !=
                  std::string::npos &&
              shellHeaderSource.find("int runTobyGui") != std::string::npos &&
              shellHeaderSource.find("int runV8Gui") != std::string::npos &&
              shellHeaderSource.find("int runDuoGui") != std::string::npos &&
              shellSource.find("GuiShell::openWindow") != std::string::npos &&
              shellSource.find("GuiShell::drawMachineMenuImpl") !=
                  std::string::npos,
          "GuiShell owns windows, menus and six family render-loop units");
    check(composersSource.find("PlatformComposers::run") !=
              std::string::npos &&
              countOccurrences(composersSource, "runCompactGui(") == 1 &&
              countOccurrences(composersSource, "runDafbGui<") == 4 &&
              countOccurrences(composersSource, "runSonoraGui<") == 3 &&
              countOccurrences(composersSource, "runTobyGui<") == 2 &&
              countOccurrences(composersSource, "runV8Gui<") == 1 &&
              countOccurrences(composersSource, "runDuoGui<") == 1 &&
              composersSource.find("glfwCreateWindow") == std::string::npos &&
              composersSource.find("glfwPollEvents") == std::string::npos &&
              composersSource.find("ImGui::") == std::string::npos &&
              platformSupportSource.find("GuiShell") == std::string::npos &&
              platformSupportSource.find("imgui") == std::string::npos &&
              compactComposerSource.find("runCompactGui(") !=
                  std::string::npos,
          "PlatformComposers performs typed family construction without rendering");
    check(shellHeaderSource.find("int argc, char** argv") ==
              std::string::npos &&
              shellHeaderSource.find("static MachineT") ==
                  std::string::npos &&
              shellHeaderSource.find("static Ctx") == std::string::npos &&
              composersSource.find("legacyArgv") == std::string::npos,
          "GUI shell loops use typed media and session-owned mutable state");
    check(shellHeaderSource.find("struct ScreenInput") != std::string::npos &&
              shellHeaderSource.find("class AdbKeyboard") !=
                  std::string::npos,
          "GuiShell owns shared screen and ADB input mechanics");
    check(hostServicesSource.find("getenv(") == std::string::npos &&
              shellHeaderSource.find("getenv(") == std::string::npos &&
              shellSource.find("getenv(") == std::string::npos &&
              composersSource.find("getenv(") == std::string::npos &&
              guiRuntimeSource.find("getenv(") == std::string::npos,
          "the GUI runtime consumes injected configuration without getenv");
    {
        std::vector<std::string> policyLeaks;
        const std::filesystem::path sourceRoot =
            std::filesystem::path(runtimeConfig).parent_path();
        for (const auto& entry :
             std::filesystem::recursive_directory_iterator(sourceRoot)) {
            if (!entry.is_regular_file()) continue;
            const auto extension = entry.path().extension().string();
            if (extension != ".cpp" && extension != ".h") continue;
            const std::string relative =
                std::filesystem::relative(entry.path(), sourceRoot).string();
            if (relative == "ProcessEnvironment.cpp") continue;
            if (slurp(entry.path().string()).find("getenv(") !=
                std::string::npos)
                policyLeaks.push_back(relative);
        }
        check(policyLeaks.empty(),
              "only ProcessEnvironment may read process-global configuration in src");
        for (const std::string& relative : policyLeaks)
            check(false, relative + " contains process-global configuration");
        const std::string processCapture =
            slurp((sourceRoot / "ProcessEnvironment.cpp").string());
        check(countOccurrences(processCapture, "std::getenv(") == 1 &&
                  processCapture.find("startup_option::kAll") !=
                      std::string::npos &&
                  processCapture.find("StartupSnapshot(std::move(entries))") !=
                      std::string::npos &&
                  processCapture.find("kConfigurationKeys") ==
                      std::string::npos &&
                  processCapture.find("\"POM68K_") == std::string::npos,
              "ProcessEnvironment derives the sole process capture from option schemas");
        const std::string domainDecoders =
            productDecoder + coreDecoder + machineDecoder;
        check(domainDecoders.find("\"POM68K_") == std::string::npos,
              "domain decoders use typed options instead of raw key strings");
        const std::string typedParsers = runtimeComposition + snapshotSource +
            productDecoder + coreDecoder + machineDecoder;
        check(typedParsers.find("getenv(") == std::string::npos &&
                  runtimeComposition.find("config.environment_") ==
                      std::string::npos &&
                  slurp(runtimeConfig).find(
                      "static RuntimeConfig parse(int argc, char* const argv[]);") ==
                      std::string::npos,
              "RuntimeConfig decoders have explicit inputs and no process-reading API");
        const std::string testCapture = slurp(jitTestConfig);
        check(!jitTestConfig.empty() &&
                  countOccurrences(testCapture, "std::getenv(") == 1 &&
                  testCapture.find("StartupSnapshot") != std::string::npos &&
                  testCapture.find("resolveConfigFrom") == std::string::npos,
              "standalone JIT gates inject their own test-boundary snapshot");
    }
    check(shellHeaderSource.find("h.hasFloppyDrive = false") !=
              std::string::npos &&
              shellHeaderSource.find("h.supportsEmptyCdDrive = false") !=
                  std::string::npos,
          "Duo shell preserves no-floppy and staged-CD capabilities");

    // The build graph follows the same one-responsibility rule as the GUI.
    const std::size_t componentInclude =
        cmakeRootSource.find("include(cmake/Pom68kComponentGates.cmake)");
    const std::size_t machineInclude =
        cmakeRootSource.find("include(cmake/Pom68kMachineGates.cmake)");
    const std::size_t jitInclude =
        cmakeRootSource.find("include(cmake/Pom68kJitGates.cmake)");
    const std::size_t devInclude =
        cmakeRootSource.find("include(cmake/Pom68kDevTools.cmake)");
    const std::size_t policyInclude =
        cmakeRootSource.find("include(cmake/Pom68kGatePolicy.cmake)");
    check(cmakeModulesPresent && componentInclude < machineInclude &&
              machineInclude < jitInclude && jitInclude < devInclude &&
              devInclude < policyInclude &&
              cmakeRootSource.find("add_test(") == std::string::npos,
          "CMake root composes five ordered test responsibilities");
    check(cmakeModuleSources[0].find("gui_smoke_test") != std::string::npos &&
              cmakeModuleSources[1].find("duo230_boot_etalon") !=
                  std::string::npos &&
              cmakeModuleSources[2].find("jit_asset_free_lockstep_test") !=
                  std::string::npos &&
              cmakeModuleSources[3].find("EXCLUDE_FROM_ALL") !=
                  std::string::npos &&
              cmakeModuleSources[3].find("add_test(") == std::string::npos &&
              cmakeModuleSources[4].find("pom68k_gate_manifest") !=
                  std::string::npos,
          "CMake gates, dev tools and registry policy stay separated");

    // ── 12. One reference-fixture locator for GUI and gates ─────────────
    const std::string fixtureSource = slurp(fixtureStore);
    const std::string assetSource = slurp(assetFingerprint);
    check(factorySource.find("preferReferenceFixture(base + relative)") !=
              std::string::npos &&
          assetSource.find("preferReferenceFixture(base + rel)") !=
              std::string::npos,
          "GUI and gate locators both prefer hdv/ref fixtures");
    check(fixtureSource.find("parent / \"ref\" / src.filename()") !=
              std::string::npos &&
          fixtureSource.find("part == \"ref\"") != std::string::npos &&
          fixtureSource.find("dst /= \"work\"") != std::string::npos,
          "reference lookup and writable ref-to-work routing stay paired");

    int localAssetResolvers = 0;
    std::vector<std::string> bypassingResolvers;
    const std::filesystem::path testsDir =
        std::filesystem::path(mainCpp).parent_path().parent_path() / "tests";
    std::error_code testsEc;
    for (const auto& entry : std::filesystem::directory_iterator(testsDir, testsEc)) {
        if (testsEc || entry.path().extension() != ".cpp") continue;
        const std::string source = slurp(entry.path().string());
        if (source.find("#include \"AssetFingerprint.h\"") ==
            std::string::npos) continue;
        for (const char* signature : {"std::string find(",
                                      "std::string findAsset("}) {
            const size_t begin = source.find(signature);
            if (begin == std::string::npos) continue;
            ++localAssetResolvers;
            const size_t end = source.find("\n}", begin);
            const std::string body = source.substr(begin, end - begin);
            if (body.find("testasset::find") == std::string::npos)
                bypassingResolvers.push_back(entry.path().filename().string());
        }
    }
    check(localAssetResolvers > 0 && bypassingResolvers.empty(),
          bypassingResolvers.empty()
              ? "all gate-local asset resolvers delegate to the ref-aware helper"
              : "gate-local resolvers bypass ref preference: " + [&] {
                    std::string out;
                    for (const auto& name : bypassingResolvers) out += name + " ";
                    return out;
                }());

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
                cells[0] == "plus_interp" || cells[0] == "macii_interp" ||
                cells[0] == "host_wallclock";
            // `host_wallclock` is guest-independent (family `any`): the
            // noise floor prices the HOST, docs/MEASURING.md § R2.
            const bool knownFamily = cells[1] == "68000" ||
                cells[1] == "68020" || cells[1] == "68030" ||
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
    for (const std::string host : {"aarch64", "x86_64", "any"}) {
        for (const std::string metric : {"min_blocks_compiled", "min_blocks_run",
                                          "min_native_share_permille",
                                          "max_slow_instrs"})
            requiredBudgets.insert("synthetic_68040_lockstep/68040/" + host +
                                   "/" + metric);
        for (const std::string metric : {"max_slow_instrs",
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
    requiredBudgets.insert(
        "plus_interp/68000/apple_m4/min_realtime_permille");
    requiredBudgets.insert(
        "macii_interp/68020/apple_m4/min_realtime_permille");
    // The measured wall-clock noise floor per bench host, plus the
    // conservative `any` fallback an unmeasured host inherits
    // (docs/MEASURING.md § R2 owns the numbers and their provenance).
    requiredBudgets.insert(
        "host_wallclock/any/x86_64/noise_floor_permille");
    requiredBudgets.insert(
        "host_wallclock/any/aarch64/noise_floor_permille");
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
    // experimental lockstep only on x86-64 (Pom68kJitGates.cmake guards
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
                    "the documented union totals are held to account\n",
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

    // TODO.md is the active backlog, not a second registry or a historical
    // journal. STATUS.md is generated from the manifests and owns the gate
    // figures checked in section 16 below. Keep this file to unchecked work:
    // a completed or partially completed checkbox belongs in CHANGELOG.md.
    {
        const std::string todoPath = testasset::find("TODO.md");
        check(!todoPath.empty(), "TODO.md active backlog located");
        const std::string todo = slurp(todoPath);
        check(todo.find("uniquement du travail ouvert") != std::string::npos &&
                  todo.find("`STATUS.md` est généré") != std::string::npos &&
                  todo.find("source de vérité") != std::string::npos,
              "TODO.md delegates generated gate facts to STATUS.md");
        int openItems = 0;
        std::stringstream todoLines(todo);
        for (std::string line; std::getline(todoLines, line); ) {
            if (line.rfind("- [", 0) != 0) continue;
            check(line.rfind("- [ ] ", 0) == 0,
                  "TODO.md contains only unchecked task markers");
            if (line.rfind("- [ ] ", 0) == 0) openItems++;
        }
        check(openItems > 0, "TODO.md contains actionable open work");
    }

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
    for (const std::string& g : named) {
        const bool known = gates.count(g) > 0 || absent.count(g) > 0;
        check(known,
              "CLAUDE.md names `" + g +
                  "` — registered here or declared host-conditional");
    }

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

    // ── 5bis. …and the two totals the docs DERIVE from that union ────────
    // The union is checked on every host by check 5. The two
    // per-architecture numbers the SAME sentence derives from it were plain
    // prose arithmetic, checked by nothing, and on 2026-08-28 all four
    // documents carried a wrong one: CLAUDE.md said 232/233, TODO.md
    // 231/232, README.md and DEV.md 227/228 — where that registry revision
    // registered 234 on x86-64 and 235 on AArch64. The per-label figures printed
    // in the same breath were right, which is exactly why nobody re-derived
    // the totals.
    //
    // A host can only prove its own half, and that is enough: `gates` is
    // what CTest registered HERE, so hold the number written for THIS
    // architecture to it and let the other host hold the other. Same
    // division of labour as the absent roster above — and the same rule if
    // a third architecture ever appears: give it its phrase here, or it
    // goes unchecked and says so.
    {
#if defined(__aarch64__) || defined(_M_ARM64)
        const char* hostPhrase = "AArch64 configure sees";
        const char* hostName   = "AArch64";
#elif defined(__x86_64__) || defined(_M_X64)
        const char* hostPhrase = "x86-64 configure sees";
        const char* hostName   = "x86-64";
#else
        const char* hostPhrase = nullptr;
        const char* hostName   = "this architecture";
#endif
        check(hostPhrase != nullptr,
              std::string("the docs state a registry total for ") + hostName);
        if (hostPhrase) {
            const int here = int(gates.size());
            for (const char* file : { "CLAUDE.md", "TODO.md", "README.md",
                                      "DEV.md" }) {
                const std::string path = testasset::find(file);
                if (path.empty()) continue;
                const std::string text = slurp(path);
                // DEV.md words it "an x86-64 tree reads"; accept either, but
                // demand that the file which mentions the OTHER host's total
                // also states this one — a document cannot answer half.
                std::vector<int> stated = numbersAfter(text, hostPhrase);
                if (stated.empty())
                    stated = numbersAfter(text, "x86-64 tree reads");
                const bool mentionsSplit =
                    text.find("host-conditional") != std::string::npos ||
                    text.find("AArch64 one") != std::string::npos;
                check(!mentionsSplit || !stated.empty(),
                      std::string(file) +
                          " splits the registry by host and states the " +
                          hostName + " total");
                for (int n : stated)
                    check(n == here,
                          std::string(file) + " says a " + hostName +
                              " configure registers " + std::to_string(n) +
                              "; ctest registered " + std::to_string(here));
            }
        }
    }

    // ── 6. …including the ones inside a fenced code block or a table ─────
    // `ctest -L unit   # 79 gates` is the same claim as the prose, and it is
    // where the count was stalest: no backticks, so check 5 walked straight
    // past it.
    //
    // TWO forms escaped this check until 2026-08-28, and both were found
    // carrying a stale registry:
    //   * `# 112 legacy non-etalon gates` — the digits are not adjacent to
    //     " gates", so the backward scan gave up and the line passed. That
    //     is how README.md kept saying 111 while CTest registered 112.
    //   * DEV.md § 6's tier TABLE — ``| `ctest -L etalon` | 124 | …`` — which
    //     has no '#' at all and so was read by nothing. It was the stalest
    //     document of the four: 230 total, 121 `etalon`, 40 `jit`.
    // So: the count is now the FIRST integer of whatever trails the command,
    // and the trailer is either a '#' comment mentioning " gates" or the
    // second cell of a markdown row that BEGINS with the command. The row
    // must begin with it — `| Which `ctest` tier to run | §6 … |` is prose
    // about ctest, not a claim about a count.
    for (const char* file : { "CLAUDE.md", "README.md", "DEV.md" }) {
        const std::string p = testasset::find(file);
        if (p.empty()) continue;
        const std::string text = slurp(p);
        size_t bol = 0;
        while (bol < text.size()) {
            const size_t eol = std::min(text.find('\n', bol), text.size());
            const std::string line = text.substr(bol, eol - bol);
            bol = eol + 1;
            const size_t i = line.find("ctest");
            if (i == std::string::npos) continue;

            size_t tail = std::string::npos;          // where the count lives
            const size_t hash = line.find('#', i);
            if (hash != std::string::npos) {
                if (line.find(" gates", hash) == std::string::npos) continue;
                tail = hash + 1;
            } else if (line.compare(0, 8, "| `ctest") == 0) {
                const size_t cell = line.find('|', 1);
                if (cell == std::string::npos) continue;
                tail = cell + 1;
            }
            if (tail == std::string::npos) continue;

            size_t k = tail;
            while (k < line.size() && !std::isdigit(uint8_t(line[k]))) k++;
            const size_t begin = k;
            while (k < line.size() && std::isdigit(uint8_t(line[k]))) k++;
            if (k == begin) continue;
            const int stated_n = std::stoi(line.substr(begin, k - begin));

            // Which label? `-L <label>` before the trailer, else the total.
            const size_t lpos = line.find("-L ", i);
            int want = totalGates;
            std::string which = "total";
            if (lpos != std::string::npos && lpos < tail) {
                which.clear();
                for (size_t c = lpos + 3;
                     c < tail && !std::isspace(uint8_t(line[c])) &&
                     line[c] != '`' && line[c] != '|'; c++)
                    which += line[c];
                want = countLabel(which);
            }
            check(stated_n == want,
                  std::string(file) + ": `" + line.substr(0, tail - 1) +
                      "` says " + std::to_string(stated_n) + " gates; " + which +
                      " has " + std::to_string(want));
        }
    }

    // ── 16. STATUS.md is the registry's generated artifact ───────────────
    // CHANGELOG 2026-08-29 (ninth): the totals once existed in prose because it
    // came first; STATUS.md is written by `tools/status_md.py` from the same
    // configure-time files this gate reads, so here the whole artifact is
    // re-derived and compared. Division of labour as in check 5bis: the
    // union section must be right on EVERY host, the `## Registered on`
    // section only on the host that owns it — a placeholder section for a
    // host that never ran the tool is absence, not drift, and passes with a
    // printed note.
    {
        const std::string statusPath = testasset::find("STATUS.md");
        check(!statusPath.empty(), "STATUS.md generated registry located");
        const std::string status = statusPath.empty() ? "" : slurp(statusPath);
        check(status.find("GENERATED FILE") != std::string::npos,
              "STATUS.md declares itself generated");

        // Union heading: "## Union across hosts — N gates".
        const char* unionHead = "## Union across hosts — ";
        size_t u = status.find(unionHead);
        check(u != std::string::npos, "STATUS.md carries the union section");
        if (u != std::string::npos) {
            size_t k = u + std::strlen(unionHead);
            std::string digits;
            while (k < status.size() && std::isdigit(uint8_t(status[k])))
                digits += status[k++];
            check(!digits.empty() && std::stoi(digits) == totalGates,
                  "STATUS.md union total " + digits + " == configured union " +
                      std::to_string(totalGates));
        }

        // The union table: every non-policy label of the union roster, one
        // row each, counted with `-L` regex semantics. Parsed and expected
        // sets must match EXACTLY — a stale row is drift, a missing row is a
        // tier the artifact hides.
        std::map<std::string, int> expectedUnion;
        auto collectBase = [&](const std::string& labels) {
            std::stringstream ss(labels);
            std::string one;
            while (std::getline(ss, one, ',')) {
                if (one.empty() || one.rfind("asset-", 0) == 0 ||
                    one.rfind("host-", 0) == 0 || one.rfind("scope-", 0) == 0 ||
                    one.rfind("tier-", 0) == 0)
                    continue;
                expectedUnion[one] = 0;
            }
        };
        for (const auto& [name, labels] : gates) { (void)name; collectBase(labels); }
        for (const auto& [name, labels] : absent) { (void)name; collectBase(labels); }
        for (auto& [label, n] : expectedUnion) n = countLabel(label);

        std::map<std::string, int> statedUnion;
        const size_t unionEnd = status.find("\n## ", u == std::string::npos ? 0 : u);
        const std::string unionSec = u == std::string::npos ? "" :
            status.substr(u, unionEnd == std::string::npos
                                 ? std::string::npos : unionEnd - u);
        {
            std::stringstream ss(unionSec);
            std::string line;
            while (std::getline(ss, line)) {
                // "| `label` | N |"
                if (line.rfind("| `", 0) != 0) continue;
                const size_t e = line.find('`', 3);
                if (e == std::string::npos) continue;
                const std::string label = line.substr(3, e - 3);
                if (label == "ctest -L") continue;
                size_t d = line.find_first_of("0123456789", e);
                if (d == std::string::npos) continue;
                statedUnion[label] = std::atoi(line.c_str() + d);
            }
        }
        check(statedUnion == expectedUnion, [&] {
            if (statedUnion == expectedUnion)
                return std::string("STATUS.md union label table matches the roster");
            std::string s = "STATUS.md union label table drifted:";
            for (const auto& [label, n] : expectedUnion)
                if (!statedUnion.count(label) || statedUnion[label] != n)
                    s += " " + label + "=" + std::to_string(n) + " expected";
            for (const auto& [label, n] : statedUnion)
                if (!expectedUnion.count(label))
                    s += " " + label + "=" + std::to_string(n) + " stale";
            return s;
        }());

        // This host's own section. Same host vocabulary as status_md.py
        // (which mirrors Pom68kJitGates.cmake): aarch64 / x86_64 / other.
#if defined(__aarch64__) || defined(_M_ARM64)
        const char* statusHost = "aarch64";
#elif defined(__x86_64__) || defined(_M_X64)
        const char* statusHost = "x86_64";
#else
        const char* statusHost = "other";
#endif
        const std::string head = std::string("## Registered on ") + statusHost;
        const size_t h = status.find(head);
        check(h != std::string::npos,
              "STATUS.md carries a section for " + std::string(statusHost));
        const size_t hEnd = status.find("\n## ", h == std::string::npos ? 0 : h);
        const std::string hostSec = h == std::string::npos ? "" :
            status.substr(h, hEnd == std::string::npos ? std::string::npos
                                                       : hEnd - h);
        if (hostSec.find("_Not yet generated") != std::string::npos) {
            std::printf("note: STATUS.md has no %s section yet — run "
                        "tools/status_md.py on this host\n", statusHost);
        } else if (h != std::string::npos) {
            const auto registered = numbersBefore(hostSec, " gates registered");
            check(registered.size() == 1 &&
                      registered.front() == int(gates.size()),
                  "STATUS.md says " +
                      (registered.empty() ? std::string("?")
                                          : std::to_string(registered.front())) +
                      " gates registered on " + statusHost + "; ctest has " +
                      std::to_string(gates.size()));
            const auto missing = numbersBefore(hostSec, " union gates");
            check(missing.size() == 1 && missing.front() == int(absent.size()),
                  "STATUS.md absent-here count matches the absent roster");

            // Dimension rows, "| dim | value | N |", against the manifest.
            std::map<std::string, int> expectedDims, statedDims;
            for (const auto& [name, cells] : gateManifest) {
                (void)name;
                expectedDims["assets|" + cells[0]]++;
                expectedDims["host|" + cells[1]]++;
                expectedDims["scope|" + cells[2]]++;
                expectedDims["tier|" + cells[3]]++;
                expectedDims["slots_src|" + cells[5]]++;
            }
            std::stringstream ss(hostSec);
            std::string line;
            while (std::getline(ss, line)) {
                if (line.rfind("| ", 0) != 0 || line.find("| dimension") == 0)
                    continue;
                std::vector<std::string> cells;
                std::stringstream row(line);
                std::string cell;
                while (std::getline(row, cell, '|')) {
                    while (!cell.empty() && cell.front() == ' ') cell.erase(0, 1);
                    while (!cell.empty() && cell.back() == ' ') cell.pop_back();
                    if (!cell.empty()) cells.push_back(cell);
                }
                if (cells.size() != 3 || cells[0] == "dimension" ||
                    cells[0].find_first_not_of("- ") == std::string::npos)
                    continue;
                statedDims[cells[0] + "|" + cells[1]] = std::atoi(cells[2].c_str());
            }
            check(statedDims == expectedDims,
                  statedDims == expectedDims
                      ? "STATUS.md " + std::string(statusHost) +
                            " dimension table matches the manifest"
                      : "STATUS.md " + std::string(statusHost) +
                            " dimension table drifted from the manifest — "
                            "rerun tools/status_md.py");
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
        // Since the 2026-08-28 extraction (CHANGELOG third) the EA
        // admission wrapper and the 68k cycle-cost model live in
        // JitEaPlan.h / JitCost.h. A backend consumes them; the bug these
        // lines would catch is the one the D1F0 prototype nearly shipped —
        // a 68k predicate or cost constant re-transcribed into one backend,
        // creating its hand-ported twin in the other.
        check(source.find("decodeEaPlan(") != std::string::npos,
              std::string(relative) + " consumes the shared EA plan");
        check(source.find("findEffectiveAddress(") == std::string::npos,
              std::string(relative) + " does not re-decode effective addresses");
        check(source.find(".control") != std::string::npos,
              std::string(relative) + " consumes Instr control-flow plans");
        check(source.find("branchDisplacement(") == std::string::npos,
              std::string(relative) + " does not decode branch extensions");
        check(source.find("kEaRead[") != std::string::npos &&
              source.find("int8_t kEaRead") == std::string::npos,
              std::string(relative) + " reads the shared cost columns and defines none");
        check(source.find("fullFormatReadExtra(") != std::string::npos &&
              source.find("baseDisplacementWords != 2") == std::string::npos,
              std::string(relative) + " reads the shared full-format cost, not a local twin");
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

    // The host-invariant halves the backends consume (2026-08-28
    // extraction). The bug these catch: the shared decode or the D1F0
    // sub-form predicate quietly moving back into one backend, which is
    // exactly how the two generators drifted into two projects the first
    // time (TODO.md § 10, finding 2).
    {
        const std::string plan = slurp(testasset::find("src/jit/JitEaPlan.h"));
        check(plan.find("findEffectiveAddress(") != std::string::npos,
              "JitEaPlan.h owns the shared EA admission wrapper");
        check(plan.find("decoded->fullFormat") != std::string::npos,
              "JitEaPlan.h keeps full-format admission opt-in per caller");
        const std::string cost = slurp(testasset::find("src/jit/JitCost.h"));
        check(cost.find("int8_t kEaRead") != std::string::npos,
              "JitCost.h owns the 68020 cycle columns");
        check(cost.find("baseDisplacementWords != 2") != std::string::npos,
              "JitCost.h owns the D1F0 full-format sub-form predicate");
        check(cost.find("kCmpaExtraCycles") != std::string::npos,
              "JitCost.h owns the CMPA surcharge");
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
    // `iwm.cpp` is theirs.  The lookup must compare the final component's
    // spelling itself: std::filesystem::is_regular_file("src/iwm.cpp") also
    // finds src/Iwm.cpp on the default case-insensitive macOS filesystem.
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
        auto exactRegularFile = [](const std::string& path) {
            const std::filesystem::path wanted(path);
            const std::filesystem::path parent = wanted.parent_path().empty()
                ? std::filesystem::path(".") : wanted.parent_path();
            std::error_code ec;
            for (const auto& entry :
                 std::filesystem::directory_iterator(parent, ec)) {
                if (ec) return false;
                if (entry.path().filename() == wanted.filename())
                    return entry.is_regular_file(ec) && !ec;
            }
            return false;
        };
        check(exactRegularFile(root + "src/Iwm.cpp") &&
              !exactRegularFile(root + "src/iwm.cpp"),
              "citation lookup preserves filename case on every host");

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
                    if (exactRegularFile(candidate)) {
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

    const std::string engineHeaderPath =
        testasset::find("src/jit/JitEngine.h");
    const std::string engineHeader = slurp(engineHeaderPath);
    const std::string jitConfigPath = testasset::find("src/jit/JitConfig.h");
    const std::string jitConfigSource = slurp(jitConfigPath);
    const std::string enginePath = testasset::find("src/jit/JitEngine.cpp");
    const std::string engineSource = slurp(enginePath);
    check(!engineHeaderPath.empty() && !jitConfigPath.empty() &&
              !enginePath.empty() &&
              engineHeader.find("const ResolvedConfig& config);") !=
                  std::string::npos &&
              engineHeader.find("ResolvedConfig* config") == std::string::npos &&
              engineSource.find("config_(config)") != std::string::npos &&
              engineSource.find("resolveConfig()") == std::string::npos &&
              engineSource.find("getenv(") == std::string::npos &&
              jitConfigSource.find("defaultResolvedConfig()") !=
                  std::string::npos &&
              jitConfigSource.find("ResolvedConfig resolveConfig()") ==
                  std::string::npos,
          "each JIT Engine requires one explicitly injected snapshot");
    for (const std::string& path : jitConfigConsumers) {
        const std::string source = slurp(path);
        check(!path.empty() &&
                  source.find("const jit::ResolvedConfig& jitConfig") !=
                      std::string::npos &&
                  source.find("ResolvedConfig* jitConfig") ==
                      std::string::npos &&
                  source.find("jitConfig =") == std::string::npos,
              path + " requires non-null JIT configuration injection");
    }

    std::printf("%s\n", gFails ? "FAILED" : "PASS");
    return gFails ? 1 : 0;
}
