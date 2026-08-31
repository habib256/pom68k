// POM68K — bounded count-version cache for measured dynamic shifts
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace jit {

// Dynamic shifts cannot share one PC-keyed compiled block when their live
// count changes: the old pre-state guard would merely replay every other
// value. This helper owns the finite count-key domain and its exact inverse
// bookkeeping; Engine still owns blocks, code and physical invalidation.
class ShiftVersionCache {
public:
    static constexpr uint64_t kFlag = uint64_t(1) << 33;
    static constexpr unsigned kMaxPerSite = 32;

    struct Admission {
        uint64_t baseKey = 0;
        uint64_t cacheKey = 0;
        uint8_t version = 0xFF;
        bool versioned = false;
        bool accepted = true;
    };

    static uint64_t versionKey(uint64_t baseKey, unsigned count) {
        return baseKey | kFlag | (uint64_t(count) << 34);
    }
    static bool isVersionKey(uint64_t blockKey) {
        return (blockKey & kFlag) != 0;
    }
    static uint64_t baseKey(uint64_t blockKey) {
        return blockKey & (kFlag - 1);
    }
    static unsigned versionFromKey(uint64_t blockKey) {
        return unsigned((blockKey >> 34) & 31u);
    }

    bool knows(uint64_t baseKey) const { return sites_.contains(baseKey); }
    void remember(uint64_t baseKey) { sites_.insert(baseKey); }

    uint64_t select(uint64_t baseKey, uint16_t opcode, unsigned count,
                    bool histogram);
    Admission admit(uint64_t baseKey, uint16_t opcode, int count,
                    bool enabled, bool histogram);
    void commit(const Admission& admission);
    void forget(uint64_t blockKey);
    void prune(uint64_t blockKey, bool baseBlockAlive);
    void clear();
    void resetPhase();

    uint32_t mask(uint64_t baseKey) const;
    const std::unordered_map<uint64_t, uint32_t>& masks() const {
        return masks_;
    }

    void dump(const std::vector<uint64_t>& slowStatic,
              const std::vector<uint64_t>& slowRuntime) const;

private:
    std::unordered_map<uint64_t, uint32_t> masks_;
    std::unordered_set<uint64_t> sites_;
    std::array<uint64_t, 4> capHisto_{};
    std::array<uint64_t, 4> wideHisto_{};
};

} // namespace jit
