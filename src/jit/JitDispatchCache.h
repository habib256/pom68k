// POM68K — direct-mapped dispatch cache in front of the JIT block map
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#pragma once

#include "JitShiftVersions.h"

#include <algorithm>
#include <cstdint>
#include <vector>

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

    // 16384 slots (256 KB, Entry = 16 B). It was 65536 (1 MB) until
    // 2026-09-17 on the strength of a 2026-09-02 note claiming "4096 slots =
    // 3.3 % hits"; measurement refuted it. On the Rogue gameplay census —
    // that note's own workload — a sweep over identical guest work
    // (40 571 024 lookups, identical fingerprint at every size) reads
    // 78.82 / 79.01 / 79.15 / 79.20 / 79.22 % hits at 4096 / 8192 / 16384 /
    // 32768 / 65536: sixteen times the memory buys 0.39 points. The ABBA
    // pass a size change owes then read |delta| 0.2 % against a 1.2 % floor
    // (5 repeats, identical fingerprints) — no measurable wall cost. 16384
    // takes the 4x memory cut while staying within 0.07 points of the
    // megabyte, keeping margin for working sets larger than Rogue's.
    // POM68K_JIT_DISPATCH_CACHE_SLOTS (a -D, power of two >= 4096 so the
    // super bit at 1<<11 still lands inside the table) sweeps that choice:
    // the hit rate is a deterministic counter over identical guest work, so
    // sizes compare without the ABBA timing protocol (docs/MEASURING.md).
#ifndef POM68K_JIT_DISPATCH_CACHE_SLOTS
#define POM68K_JIT_DISPATCH_CACHE_SLOTS 16384
#endif
    static constexpr uint32_t kDefaultSize = POM68K_JIT_DISPATCH_CACHE_SLOTS;
    static_assert(kDefaultSize >= 4096 && (kDefaultSize & (kDefaultSize - 1)) == 0,
                  "dispatch cache: power of two, at least 4096");

    // Heap, not an inline array: 65536 slots is a megabyte, and an Engine
    // carrying it inline is what forced /STACK:16777216 on MSVC and pushed
    // 15+154 fixtures onto the heap. A vector also makes the size a RUNTIME
    // choice, which is what lets two sizes be compared ABBA inside one
    // binary (docs/MEASURING.md forbids reading a cross-binary pair as a
    // timing claim) — jit_bench's POM68K_BENCH_DISPATCH_SLOTS arm.
    DispatchCache() { resize(kDefaultSize); }

    // Power of two, at least 4096 so the super bit at 1<<11 lands inside the
    // table; anything else is refused and the previous size kept.
    void resize(uint32_t slots) {
        if (slots < 4096 || (slots & (slots - 1)) != 0) return;
        entries_.assign(slots, Entry{});
        mask_ = slots - 1;
    }
    uint32_t size() const { return uint32_t(entries_.size()); }

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

    void clear() { std::fill(entries_.begin(), entries_.end(), Entry{}); }

    // Census visibility: how often the fast slot answered, how often the
    // MMU generation forced the slow path anyway, how often the slot was
    // cold. Printed by Engine::censusPhase(); the 040 diagnosis depends on
    // the gen-miss column.
    uint64_t hits() const { return hits_; }
    uint64_t genMiss() const { return genMiss_; }
    uint64_t miss() const { return miss_; }
    void resetPhase() { hits_ = genMiss_ = miss_ = 0; }

private:
    uint32_t index(uint32_t pc, bool super) const {
        return ((pc >> 1) ^ (uint32_t(super) << 11)) & mask_;
    }
    Entry& slot(uint64_t plainKey) {
        return entries_[index(uint32_t(plainKey),
                              ((plainKey >> 32) & 1) != 0)];
    }
    std::vector<Entry> entries_;
    uint32_t mask_ = 0;
    uint64_t hits_ = 0, genMiss_ = 0, miss_ = 0;
};

}  // namespace jit
