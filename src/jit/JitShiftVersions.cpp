// POM68K — bounded count-version cache for measured dynamic shifts
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "JitShiftVersions.h"

#include "JitCost.h"
#include "JitEngine.h"
#include "Moira.h"

#include <bit>
#include <cstdio>

namespace jit {

uint64_t Engine::dispatchBlockKey(uint32_t pc, bool super) {
    const uint64_t base = key(pc, super);
    if (!shiftVersions_.knows(base)) return base;

    // A stale hint after SMC must degrade to the ordinary key, never treat
    // unrelated replacement code as a shift. Physical invalidation owns the
    // old versions; this path only selects among still-live aliases.
    const uint16_t opcode = cpu_.pomJitPeek(pc);
    if (!isMultiVersionShiftOpcode(opcode)) return base;
    const InstructionSemantics sem = describeInstruction(opcode);
    if (sem.operation != SemanticOp::ShiftRegister || !sem.dynamic ||
        sem.registerIndex > 7)
        return base;
    const unsigned count = cpu_.getD(sem.registerIndex) & 63u;
    return shiftVersions_.select(base, opcode, count, !histo_.empty());
}

uint64_t ShiftVersionCache::select(uint64_t base, uint16_t opcode,
                                   unsigned count, bool histogram) {
    if (count > kMaxMultiVersionShiftCount) {
        if (histogram) wideHisto_[multiVersionShiftIndex(opcode)]++;
        return base;
    }
    return versionKey(base, count);
}

ShiftVersionCache::Admission ShiftVersionCache::admit(
    uint64_t base, uint16_t opcode, int count, bool enabled, bool histogram) {
    Admission result{base, base, 0xFF, enabled, true};
    if (!enabled) return result;
    sites_.insert(base);
    if (count < 0) return result;
    if (unsigned(count) > kMaxMultiVersionShiftCount) {
        if (histogram) wideHisto_[multiVersionShiftIndex(opcode)]++;
        return result;
    }

    const uint32_t bit = uint32_t(1) << unsigned(count);
    const auto found = masks_.find(base);
    const uint32_t mask = found == masks_.end() ? 0 : found->second;
    if (!(mask & bit) && unsigned(std::popcount(mask)) >= kMaxPerSite) {
        if (histogram) capHisto_[multiVersionShiftIndex(opcode)]++;
        result.accepted = false;
        return result;
    }
    result.version = uint8_t(count);
    result.cacheKey = versionKey(base, unsigned(count));
    return result;
}

void ShiftVersionCache::commit(const Admission& admission) {
    if (admission.version != 0xFF)
        masks_[admission.baseKey] |= uint32_t(1) << admission.version;
}

void ShiftVersionCache::forget(uint64_t blockKey) {
    if (!isVersionKey(blockKey)) return;
    const uint64_t base = baseKey(blockKey);
    auto it = masks_.find(base);
    if (it == masks_.end()) return;
    it->second &= ~(uint32_t(1) << versionFromKey(blockKey));
    if (!it->second) masks_.erase(it);
}

void ShiftVersionCache::prune(uint64_t blockKey, bool baseBlockAlive) {
    const uint64_t base = baseKey(blockKey);
    if (!masks_.contains(base) && !baseBlockAlive) sites_.erase(base);
}

void ShiftVersionCache::clear() {
    masks_.clear();
    sites_.clear();
}

void ShiftVersionCache::resetPhase() {
    capHisto_.fill(0);
    wideHisto_.fill(0);
}

uint32_t ShiftVersionCache::mask(uint64_t base) const {
    const auto it = masks_.find(base);
    return it == masks_.end() ? 0 : it->second;
}

void ShiftVersionCache::dump(const std::vector<uint64_t>& slowStatic,
                             const std::vector<uint64_t>& slowRuntime) const {
    static constexpr uint16_t opcodes[] = {
        0xE0A9, 0xE2AB, 0xE4A4, 0xE2AA
    };
    unsigned live = 0, saturated = 0;
    for (const auto& [site, mask] : masks_) {
        (void)site;
        live += unsigned(std::popcount(mask));
        saturated += unsigned(std::popcount(mask)) == kMaxPerSite;
    }
    std::fprintf(stderr,
                 "\n[jit] bounded shift versions — %zu sites, %u live, "
                 "%u saturated\n", masks_.size(), live, saturated);
    for (unsigned i = 0; i < std::size(opcodes); i++) {
        const uint16_t opcode = opcodes[i];
        std::fprintf(stderr,
                     "  %04X %10llu unsupported %10llu runtime  "
                     "%10llu cap-interp %10llu count>31\n", opcode,
                     (unsigned long long)slowStatic[opcode],
                     (unsigned long long)slowRuntime[opcode],
                     (unsigned long long)capHisto_[i],
                     (unsigned long long)wideHisto_[i]);
    }
}

} // namespace jit
