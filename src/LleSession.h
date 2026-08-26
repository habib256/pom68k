// POM68K — process-wide LLE product qualification state.
//
// Devices report every command-level fallback here.  Product mode then has
// one authoritative answer for the whole session, save states included,
// instead of inferring purity from the one device a machine happens to query.
//
// Since 2026-08-14 the same report carries the WHOLE outcome, not just the
// failures: which LLE-capable device a machine actually built, which side it
// landed on, why, and which dump it loaded.  That is what the "Périphériques"
// window renders (src/PeripheralWindow.*).  The rule it exists to enforce:
// **the GUI displays what the device reported, it never re-derives the
// decision** — a second copy of the "is there a dump / is the knob set"
// logic is exactly the drift this tree has paid for before (CLAUDE.md's
// per-file notes are full of it). Devices are built once from injected
// configuration, so a changed selection is applied by a typed relaunch.

#pragma once

#include "FirmwareConfig.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace pom68k::lle {

enum Module : std::uint32_t {
    HleEgretCuda = 1u << 0,
    HleAdbModem  = 1u << 1,
};

inline std::atomic<std::uint32_t> gHleModules{0};
inline std::atomic<bool> gQualified{false};
inline std::atomic<bool> gRequested{false};

inline bool requested() noexcept {
    return gRequested.load(std::memory_order_acquire);
}

inline void clearDevices();          // defined with the registry below

inline void beginSession(bool strict = false) noexcept {
    gHleModules.store(0, std::memory_order_release);
    gQualified.store(false, std::memory_order_release);
    gRequested.store(strict, std::memory_order_release);
    clearDevices();
}

inline void activateHle(Module module) noexcept {
    gHleModules.fetch_or(static_cast<std::uint32_t>(module),
                         std::memory_order_acq_rel);
    gQualified.store(false, std::memory_order_release);
}

inline std::uint32_t activeHleModules() noexcept {
    return gHleModules.load(std::memory_order_acquire);
}

inline void setQualified(bool on) noexcept {
    gQualified.store(on && activeHleModules() == 0,
                     std::memory_order_release);
}

inline bool qualified() noexcept {
    return gQualified.load(std::memory_order_acquire) &&
           activeHleModules() == 0;
}

// Once a strict session is qualified its execution engine is part of the
// product promise.  Direct callers and the GUI command queue share this gate.
inline bool engineChangeAllowed(int engine) noexcept {
    return !requested() || !qualified() || engine == 1;
}

// ── Peripheral registry — what the "Périphériques" window renders ───────
//
// One entry per LLE-CAPABLE device the running machine actually built. A
// machine that has no such device (the compacts' M0110 keyboard, a Centris
// with no Egret) contributes nothing, which is why the window lists what
// this machine has rather than a fixed table of every device in the tree.

enum class Mode { Lle, Hle };

enum class Why {
    LleFirmware,   // real firmware executing on its real MCU core
    HleNoDump,     // no dump found under any candidate path — the fallback
    HleForced,     // the knob says 0, dumps notwithstanding
};

struct Device {
    Module      module = HleEgretCuda;
    FirmwareTarget target = FirmwareTarget::Cuda;
    std::string name;                       // shown as the row title
    std::string knob;                       // legacy source label for diagnostics
    Mode        mode = Mode::Hle;
    Why         why  = Why::HleNoDump;
    std::string firmware;                   // dump loaded (LLE), else empty
    std::vector<std::string> candidates;    // paths the device searched
    // Per-device dump override. `pathKnob` names the env variable
    // ("POM68K_CUDA_FW"); `firmwareForced` is the value that was in force
    // when this device was built — empty means "automatic, first candidate
    // found". The two are separate from `firmware` on purpose: `firmware`
    // says what LOADED, `firmwareForced` says what was ASKED FOR, and a
    // window comparing a staged choice against the loaded path would report
    // a pending change every time automatic mode picked a file.
    std::string pathKnob;
    std::string firmwareForced;
};

inline std::mutex gDeviceMutex;
inline std::vector<Device> gDevices;

// A device reports its outcome exactly once, at construction. Reporting an
// HLE outcome IS the old activateHle() call — product mode keeps behaving
// identically, and no device can now mark itself HLE without also saying
// which knob and which dumps were in play.
inline void report(const Device& d) {
    {
        std::lock_guard<std::mutex> lock(gDeviceMutex);
        bool replaced = false;
        for (Device& e : gDevices)
            if (e.module == d.module) { e = d; replaced = true; break; }
        if (!replaced) gDevices.push_back(d);
    }
    if (d.mode == Mode::Hle) activateHle(d.module);
}

inline std::vector<Device> devices() {
    std::lock_guard<std::mutex> lock(gDeviceMutex);
    return gDevices;
}

inline void clearDevices() {
    std::lock_guard<std::mutex> lock(gDeviceMutex);
    gDevices.clear();
}

// Could this device run LLE at all? Tested on the very paths the device
// itself searched, so the answer cannot disagree with what a relaunch would
// find. A device already running firmware needs no probe.
inline bool dumpAvailable(const Device& d) {
    if (d.mode == Mode::Lle) return true;
    for (const std::string& p : d.candidates)
        if (std::ifstream(p, std::ios::binary)) return true;
    return false;
}

struct Choice {
    Module      module = HleEgretCuda;
    Mode        mode   = Mode::Lle;
    std::string firmware;                   // "" = automatic (first found)
};

// Typed policy implied by a staged selection, ready for serialization at the
// process boundary. Every selected device emits a complete override. In
// particular, a missing path means "automatic" and explicitly defeats an
// inherited legacy path variable.
inline std::vector<FirmwareOverride>
firmwareOverridesForSelection(const std::vector<Device>& live,
                              const std::vector<Choice>& want) {
    std::vector<FirmwareOverride> out;
    if (want.empty()) return out;
    for (const Device& d : live) {
        Choice selected{d.module, d.mode, d.firmwareForced};
        for (const Choice& staged : want)
            if (staged.module == d.module) { selected = staged; break; }
        out.push_back({d.target, selected.mode == Mode::Lle,
                       selected.firmware.empty()
                           ? std::optional<std::string>()
                           : std::optional<std::string>(selected.firmware)});
    }
    return out;
}

// How many staged choices differ from what this session is running — the
// window's "anything to apply?" test. A dump pick counts on its own:
// switching an already-LLE device from one factory revision to another
// changes nothing about the mode and everything about the machine.
inline int pendingCount(const std::vector<Device>& live,
                        const std::vector<Choice>& want) {
    int n = 0;
    for (const Device& d : live)
        for (const Choice& c : want) {
            if (c.module != d.module) continue;
            if (c.mode != d.mode || c.firmware != d.firmwareForced) n++;
            break;
        }
    return n;
}

enum SnapshotFlag : std::uint32_t {
    SnapshotStrict    = 1u << 0,
    SnapshotQualified = 1u << 1,
    SnapshotHleShift  = 8,
};

inline std::uint32_t snapshotFlags() noexcept {
    return (requested() ? SnapshotStrict : 0u) |
           (qualified() ? SnapshotQualified : 0u) |
           (activeHleModules() << SnapshotHleShift);
}

inline std::uint32_t snapshotHleModules(std::uint32_t flags) noexcept {
    return flags >> SnapshotHleShift;
}

} // namespace pom68k::lle
