// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── which blocks stay resident, and every duty an erase owes ─────────────
// The execution engine (JitEngine.cpp) decides what to RUN; this unit owns
// the other half of the block cache: capacity eviction, guard-driven
// eviction, and the physical-footprint index that makes an eviction exact.
// They are one concern because they share one contract — a block leaving
// blocks_ must also lose its published link, its generated code, its slice
// marks, its shift version and its dispatch-cache slot, or the engine keeps
// a reference to something that no longer exists (JitEngine.h documents the
// coherence duty on unmarkPages() and on DispatchCache).
//
// Gate: jit_asset_free_lockstep_test — "mark/unmark inverse stays exact
// across 384 one/two-slice evictions".

#include "JitEngine.h"

#include "Moira.h"

#include <algorithm>

namespace jit {

// Capacity eviction: erase every block not dispatched since the previous
// saturation, with ALL the duties an erase owes (the guard-service path
// is the model): retract its published link, release its code, unmark its
// pages, forget its shift version, drop its dispatch-cache slot, prune
// the version bookkeeping. Advances the epoch so the next saturation
// measures a fresh generation.
void Engine::evictColdBlocks() {
    size_t evicted = 0;
    for (auto it = blocks_.begin(); it != blocks_.end();) {
        Block& b = it->second;
        if (b.epoch == blockEpoch_) { ++it; continue; }
        const uint64_t blockKey = it->first;
        if (b.code && b.shiftVersion == 0xFF)
            retractLink(b.ir.entryPc, b.ir.super);
        if (b.code) backend_->release(b.code);
        uint32_t lo = 0, len = 0;
        blockSpan(b.ir, lo, len);
        unmarkPages(blockKey, lo, len);
        shiftVersions_.forget(blockKey);
        it = blocks_.erase(it);
        dispatchCache_.evict(blockKey);
        shiftVersions_.prune(
            blockKey, blocks_.contains(ShiftVersionCache::baseKey(blockKey)));
        evicted++;
    }
    blockEpoch_++;
    stats_.blocksLive.store(blocks_.size(), std::memory_order_relaxed);
    coldEvictions_ += evicted;
}

// A write landed in memory some cached block was translated from. J1 dropped
// the whole cache here, on the reasoning that writes into live code are rare
// and a recorded IR is cheap to rebuild. Both halves of that turned out to
// be wrong once the cache held GENERATED CODE: the writes are not rare (68k
// code and its data share memory closely), and what gets thrown away is no
// longer cheap. So evict precisely, and keep the flush for the two cases
// that genuinely leave nothing to salvage.
void Engine::serviceGuard() {
    if (!guard_.tripped()) return;
    stats_.add(stats_.invalidations);

    if (guard_.mustFlush() || blocks_.empty()) { flushAll(Flush::Guard); return; }

    for (int k = 0; k < guard_.nHits; k++) {
        const CodeGuard::Hit& h = guard_.hits[k];
        // note() only records a hit when the write covers a sub-slice some
        // block has bytes in, so nearly every trip names a real victim; the
        // per-block intersection below is what makes it exact. Walk a copy:
        // unmarkPages() edits the vector being walked.
        std::vector<uint64_t> keys;
        if (auto indexed = sliceIndex_.find(h.slice); indexed != sliceIndex_.end())
            keys = indexed->second;
        for (uint64_t blockKey : keys) {
            auto b = blocks_.find(blockKey);
            if (b == blocks_.end()) continue;
            const BlockIr& ir = b->second.ir;
            uint32_t lo = 0, len = 0;
            blockSpan(ir, lo, len);
            if (!len || h.hi < lo || h.lo > lo + len - 1) continue;
            if (b->second.code && b->second.shiftVersion == 0xFF) {
                retractLink(ir.entryPc, ir.super);
            }
            if (b->second.code) backend_->release(b->second.code);
            unmarkPages(blockKey, lo, len);
            shiftVersions_.forget(blockKey);
            blocks_.erase(b);
            dispatchCache_.evict(blockKey);
            shiftVersions_.prune(
                blockKey,
                blocks_.contains(ShiftVersionCache::baseKey(blockKey)));
            stats_.add(stats_.evictions);
        }
    }
    guard_.clear();
    stats_.blocksLive.store(blocks_.size(), std::memory_order_relaxed);
}

void Engine::recomputeSliceMark(uint32_t slice) {
    if (slice >= pageMap_.size()) return;
    uint8_t mask = 0;
    if (auto it = sliceIndex_.find(slice); it != sliceIndex_.end()) {
        const uint32_t sliceLo = slice << CodeGuard::kShift;
        const uint32_t sliceHi = sliceLo + CodeGuard::kUnit - 1;
        for (uint64_t key : it->second) {
            auto b = blocks_.find(key);
            if (b == blocks_.end()) continue;
            uint32_t lo = 0, len = 0;
            blockSpan(b->second.ir, lo, len);
            if (!len) continue;
            const uint32_t hi = lo + len - 1;
            if (hi < sliceLo || lo > sliceHi) continue;
            mask |= CodeGuard::subMask(lo > sliceLo ? lo : sliceLo,
                                       hi < sliceHi ? hi : sliceHi);
        }
    }
    pageMap_[slice] = mask;
    if (mask) return;

    // codePage_ answers the same question at the 4 KB granularity the
    // data TLB hands out, so it may only be cleared once no slice in the
    // page is marked any more.
    const uint32_t page = (slice << CodeGuard::kShift) >> 12;
    if (page >= codePage_.size() || !codePage_[page]) return;
    const uint32_t first = (page << 12) >> CodeGuard::kShift;
    const uint32_t count = 4096 >> CodeGuard::kShift;
    for (uint32_t i = first; i < first + count && i < pageMap_.size(); i++)
        if (pageMap_[i]) return;
    codePage_[page] = 0;
    cpu_.pomJitDtlbFlush();
}

void Engine::unmarkPages(uint64_t blockKey, uint32_t lo, uint32_t len) {
    if (pageMap_.empty() || !len) return;
    uint32_t p = lo >> CodeGuard::kShift;
    const uint32_t last = (lo + len - 1) >> CodeGuard::kShift;
    for (; p <= last && p < pageMap_.size(); p++) {
        auto it = sliceIndex_.find(p);
        if (it == sliceIndex_.end()) continue;
        std::vector<uint64_t>& keys = it->second;
        keys.erase(std::remove(keys.begin(), keys.end(), blockKey), keys.end());
        if (keys.empty()) sliceIndex_.erase(it);
        recomputeSliceMark(p);
    }
}

void Engine::markPages(uint64_t blockKey, uint32_t lo, uint32_t len) {
    if (pageMap_.empty() || !len) return;
    const uint32_t hi = lo + len - 1;
    uint32_t p = lo >> CodeGuard::kShift;
    const uint32_t last = hi >> CodeGuard::kShift;
    bool gained = false;
    for (; p <= last && p < pageMap_.size(); p++) {
        const uint32_t sliceLo = p << CodeGuard::kShift;
        const uint32_t sliceHi = sliceLo + CodeGuard::kUnit - 1;
        if (!pageMap_[p]) gained = true;
        pageMap_[p] |= CodeGuard::subMask(lo > sliceLo ? lo : sliceLo,
                                          hi < sliceHi ? hi : sliceHi);
        sliceIndex_[p].push_back(blockKey);
    }
    uint32_t q = lo >> 12;
    const uint32_t qlast = hi >> 12;
    for (; q <= qlast && q < codePage_.size(); q++) codePage_[q] = 1;

    // ── the INVALIDATION this function owes, and never paid ──────────────
    // A data-TLB write entry filled for a page BEFORE it held any translated
    // code points straight at the host bytes and carries an empty code mask.
    // A store through it bypasses the memory map, so the write guard never
    // sees it and the block the page now carries is never evicted:
    // self-modifying code, undetected. POM68K_JIT.md § 8's invalidation
    // table has claimed since it was written that "markPages() flushes when
    // it marks"; it did not — only the 1 -> 0 direction in serviceGuard()
    // ever flushed (found and fixed 2026-08-10).
    //
    // The flush is on the 0 -> 1 TRANSITION of a SLICE, which is both what
    // makes the mask sound and what keeps this cheap: the engine compiles
    // tens of thousands of blocks per boot but they come from far fewer
    // distinct slices, and the count falls to nothing once the working set
    // is translated. An UNCONDITIONAL flush here would be the same 8 KB
    // memset whose arm-time cousin cost -23 to -33 % of wall clock (§ 8,
    // "the arm-time flush that owned nothing"). Do not promote it to one.
    if (gained) cpu_.pomJitDtlbFlush();
}

}  // namespace jit
