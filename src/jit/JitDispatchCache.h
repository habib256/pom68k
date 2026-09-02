// POM68K — direct-mapped dispatch cache in front of the JIT block map
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#pragma once

#include "JitShiftVersions.h"

#include <array>
#include <cstdint>

namespace jit {

// One entry per (pc, super) slot, consulted before the block hashtable. The
// 68040 time profile (CHANGELOG 2026-09-02 (eighth)) attributed ~34 % of a
// Rogue run to executeUntil + the block hashtable + dispatchBlockKey for
// 7.6 % of generated code: the cache-active 040's short windows make FINDING
// a block cost more than running it.
//
// Only plain base-keyed blocks enter — never a shift-versioned site, whose
// dispatch key depends on a live data register — and lookup() additionally
// requires the block's proved MMU generation, so a stale-generation block
// still takes the slow path that re-proves or evicts it.
//
// Coherence contract, owed by the CACHE'S USER: every path that erases from
// the block map calls evict() with the erased key (the same discipline
// Engine::unmarkPages already imposes), a full flush calls clear(), and a
// site the shift-version cache admits has its base slot evicted. Block
// pointers must be stable across rehash — Engine holds its blocks in a
// node-based map, which is what makes a raw pointer here safe.
//
// Templated on the block type only because that type is Engine-private;
// nothing here knows what a block IS beyond its `gen` stamp.
template <class BlockT>
class DispatchCache {
public:
    struct Entry { uint64_t key = 0; BlockT* block = nullptr; };

    // 65536 slots (1 MB): the Rogue gameplay working set is ~16k live
    // blocks and a 4096-slot table measured 3.3 % hits from pure
    // direct-map collision thrash (2026-09-02, the counters below).
    static constexpr uint32_t kSize = 65536;

    // The block filed under `plainKey`, or nullptr when the slot is cold or
    // holds a block whose proved MMU generation is no longer `gen`.
    BlockT* lookup(uint64_t plainKey, uint32_t gen) {
        const Entry& e = slot(plainKey);
        if (e.key != plainKey) { miss_++; return nullptr; }
        if (e.block->gen != gen) { genMiss_++; return nullptr; }
        hits_++;
        return e.block;
    }

    void fill(uint64_t plainKey, BlockT* block) {
        slot(plainKey) = { plainKey, block };
    }

    void evict(uint64_t blockKey) {
        if (ShiftVersionCache::isVersionKey(blockKey)) return;
        Entry& e = slot(blockKey);
        if (e.key == blockKey) e = {};
    }

    void clear() { entries_.fill({}); }

    // Census visibility: how often the fast slot answered, how often the
    // MMU generation forced the slow path anyway, how often the slot was
    // cold. Printed by Engine::censusPhase(); the 040 diagnosis depends on
    // the gen-miss column.
    uint64_t hits() const { return hits_; }
    uint64_t genMiss() const { return genMiss_; }
    uint64_t miss() const { return miss_; }
    void resetPhase() { hits_ = genMiss_ = miss_ = 0; }

private:
    static uint32_t index(uint32_t pc, bool super) {
        return ((pc >> 1) ^ (uint32_t(super) << 11)) & (kSize - 1);
    }
    Entry& slot(uint64_t plainKey) {
        return entries_[index(uint32_t(plainKey),
                              ((plainKey >> 32) & 1) != 0)];
    }
    std::array<Entry, kSize> entries_{};
    uint64_t hits_ = 0, genMiss_ = 0, miss_ = 0;
};

}  // namespace jit
