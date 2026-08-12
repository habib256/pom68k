// POM68K — process-wide LLE product qualification state.
//
// Devices report every command-level fallback here.  Product mode then has
// one authoritative answer for the whole session, save states included,
// instead of inferring purity from the one device a machine happens to query.

#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>

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

inline void beginSession() noexcept {
    gHleModules.store(0, std::memory_order_release);
    gQualified.store(false, std::memory_order_release);
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
