// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// AbstractionCatalog -- what each subsystem is emulated AS, on POM68K's own
// classification scale, as data. No ImGui: the « Abstraction Levels » window
// (GuiAbstractionWindow.*) draws it, and `abstraction_catalog_test` holds it
// to the tree (every file named exists, every firmware entry and platform is
// covered) and to `docs/LLE_VS_HLE.md`, whose master table must list the
// same ids with the same classes. The document is the authority: it carries
// the evidence, the rows here carry only the verdict. Edit the two together.
//
// Live state is never re-derived. A firmware row's ACTIVE level comes from
// what the device reported to the session's `lle::Registry` (LleSession.h),
// the engine row from the CPU binding the shell already holds; the window
// only renders `resolve()`'s answer.

#pragma once

#include "LleSession.h"
#include "MachineCatalog.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pom68k::abstraction {

// The classification of docs/LLE_VS_HLE.md § Classification, in its order:
// from the silicon's own behaviour down to machinery with no guest effect.
enum class Level : std::uint8_t {
    PureLle,          // registers/protocol/timing from primary sources or ROM traces
    LleSimplified,    // functional, not exact; the gap is tracked in § 1
    HleReplacement,   // a whole device replaced at command level (fallback-only)
    HleHack,          // the emulator reaches into the guest (fallback-only)
    HostConvenience,  // no guest-visible hardware effect
};

const char* levelBadge(Level level);   // "LLE", "LLE~", "HLE", "HACK", "HOST"
const char* levelName(Level level);    // "Pure LLE", ... as the document spells it
bool levelIsLle(Level level);

using PlatformMask = std::uint32_t;
constexpr PlatformMask platformBit(PlatformKind kind) {
    return PlatformMask(1) << unsigned(kind);
}
inline constexpr PlatformMask kAllPlatforms =
    (platformBit(PlatformKind::Portable) << 1) - 1;

// Where a row's live state comes from.
enum class LiveSource : std::uint8_t {
    None,         // static: the level is the same in every session
    Firmware,     // the device reported to lle::Registry under `module`
    CpuEngine,    // interpreter or accelerated engine, from the CPU binding
    AdbFallback,  // engaged only while the ADB transceiver runs its HLE fallback
};

// Every member has a default so the designated-initializer rows in the .cpp
// may omit what they do not use without tripping -Wmissing-field-initializers.
struct Entry {
    const char* id = "";          // stable key: lower-case, '-' separated
    const char* group = "";       // section heading; one contiguous run each
    const char* subsystem = "";   // human name
    Level level = Level::PureLle; // the class of the default (conformant) path
    const char* modelled = "";    // what is modelled, one sentence
    const char* whyNot = "";      // why not lower, or why this is the floor
    const char* files = "";       // repository-relative paths, space separated
    PlatformMask platforms = 0;   // board implementations carrying it
    const char* docSection = "-"; // docs/LLE_VS_HLE.md section ("1.4", "3"), or "-"
    LiveSource live = LiveSource::None;
    lle::Module module = lle::HleEgretCuda;  // meaningful for Firmware/AdbFallback
    Level fallback = Level::PureLle;         // Firmware rows: the class without the dump
    bool hasFallback = false;                // false: firmware is required, no substitute
    // Profiles of a listed platform that do NOT carry the subsystem (the
    // 128K/512K/Plus have no ADB and the 128K/512K no SCSI).
    std::vector<SnapMachine> exceptProfiles{};
    // `firmware::kManifest` labels this row's dumps are verified against in
    // strict mode (FirmwareManifest.h).
    std::vector<std::string_view> manifest{};
};

const std::vector<Entry>& catalogue();
const Entry* find(std::string_view id);

// The profile a GUI session runs, or null for an unknown id.
const MachineProfile* profileFor(SnapMachine machine);

// Everything resolve() may consult about the running session. Built by the
// window from state it is handed; nothing here reads the process.
struct SessionView {
    std::optional<SnapMachine> machine;       // nullopt: no machine bound
    std::vector<lle::Device> devices;         // the session registry's reports
    bool strictRequested = false;
    bool strictQualified = false;
    int engine = -1;                          // -1 unbound, 0 interpreter, 1 accelerated
    std::string backend;                      // accelerated backend name, if any
};

struct LiveRow {
    bool present = true;       // carried by the running machine
    bool known = false;        // a live source answered (else static)
    Level active = Level::PureLle;
    bool degraded = false;     // running above (less faithful than) its class
    bool engaged = true;       // HleHack rows: false while dormant
    std::string state;         // short « Now » text
    std::string detail;        // one clause: why
    std::string hint;          // how to raise it back to its class, if degraded
};

bool presentOn(const Entry& entry, SnapMachine machine);
LiveRow resolve(const Entry& entry, const SessionView& view);

} // namespace pom68k::abstraction
