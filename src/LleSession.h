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
// per-file notes are full of it).  Devices are built once, from getenv, so a
// changed selection is applied by relaunching the process with a new
// environment — the Machine menu's mechanism, reused verbatim.

#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>
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

inline bool requested() noexcept {
    const char* e = std::getenv("POM68K_LLE_AARCH64_FULL");
    return e && e[0] && e[0] != '0';
}

inline void clearDevices();          // defined with the registry below

inline void beginSession() noexcept {
    gHleModules.store(0, std::memory_order_release);
    gQualified.store(false, std::memory_order_release);
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
    std::string name;                       // shown as the row title
    std::string knob;                       // "POM68K_EGRET_LLE"
    Mode        mode = Mode::Hle;
    Why         why  = Why::HleNoDump;
    std::string firmware;                   // dump loaded (LLE), else empty
    std::vector<std::string> candidates;    // paths the device searched
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

// The shape seven platforms share verbatim: an MCU that runs a factory dump
// when one is found and the knob allows it, and a command-level substitute
// otherwise. One helper so the seven cannot drift — `lle` is what the device
// achieved, `wanted` is what the knob asked for, and the two together are
// the whole reason.
inline void reportFirmwareDevice(Module module, std::string name,
                                 std::string knob, bool lle, bool wanted,
                                 std::string firmware,
                                 std::vector<std::string> candidates) {
    Device d;
    d.module = module;
    d.name = std::move(name);
    d.knob = std::move(knob);
    d.mode = lle ? Mode::Lle : Mode::Hle;
    d.why = lle      ? Why::LleFirmware
            : wanted ? Why::HleNoDump
                     : Why::HleForced;
    d.firmware = std::move(firmware);
    d.candidates = std::move(candidates);
    report(d);
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
    Module module = HleEgretCuda;
    Mode   mode   = Mode::Lle;
};

// The environment a staged selection implies, as (knob, value) pairs for
// setenv() before the relaunch.
//
// Every selected device is emitted, INCLUDING one whose choice matches what
// is running: the relaunch inherits this process's environment, so a knob
// left over from an earlier relaunch would otherwise survive and silently
// win. "Undo my earlier HLE forcing" is precisely an explicit `=1`.
inline std::vector<std::pair<std::string, std::string>>
envForSelection(const std::vector<Device>& live,
                const std::vector<Choice>& want) {
    std::vector<std::pair<std::string, std::string>> out;
    for (const Device& d : live) {
        if (d.knob.empty()) continue;
        for (const Choice& c : want)
            if (c.module == d.module) {
                out.emplace_back(d.knob, c.mode == Mode::Lle ? "1" : "0");
                break;
            }
    }
    return out;
}

// How many staged choices differ from what this session is running — the
// window's "anything to apply?" test.
inline int pendingCount(const std::vector<Device>& live,
                        const std::vector<Choice>& want) {
    int n = 0;
    for (const Device& d : live)
        for (const Choice& c : want)
            if (c.module == d.module && c.mode != d.mode) { n++; break; }
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
