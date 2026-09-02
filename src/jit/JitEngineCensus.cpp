// POM68K — JIT census bookkeeping outside the execution-engine hot unit
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "JitEngine.h"

#include <algorithm>
#include <cstdio>

namespace jit {

void Engine::recordRuntimeAddress(uint32_t reason, uint32_t opcode,
                                  uint32_t address, uint32_t bytes,
                                  uint32_t write, uint32_t codeMask) {
    RuntimeAddressKey key{reason, opcode, address, bytes, write, codeMask};
    if (lastRuntimeAddressCount_ && key == lastRuntimeAddress_) {
        ++*lastRuntimeAddressCount_;
        return;
    }
    auto [it, inserted] = runtimeAddressHisto_.try_emplace(key, 0);
    (void)inserted;
    lastRuntimeAddress_ = key;
    lastRuntimeAddressCount_ = &it->second;
    ++it->second;
}

// One tally per COMPILED slot, not per execution: the question it answers
// is "what shape is the code", and the static census supplies the weight.
void Engine::recordIndexForms(const BlockIr& ir) {
    if (indexFormSites_.empty()) return;
    for (const Instr& in : ir.instrs) {
        for (uint8_t e = 0; e < in.effectiveAddressCount; e++) {
            const DecodedEffectiveAddress& ea = in.effectiveAddresses[e];
            switch (ea.kind) {
                case EffectiveAddressKind::BriefIndex:
                case EffectiveAddressKind::PcBriefIndex:
                    indexFormSites_[in.opcode][0]++;
                    break;
                case EffectiveAddressKind::FullIndex:
                case EffectiveAddressKind::PcFullIndex:
                    indexFormSites_[in.opcode][1]++;
                    break;
                default: break;
            }
        }
    }
}

void Engine::censusPhase(const char* label) {
    if (histo_.empty()) return;
    std::fprintf(stderr, "\n[jit] ════ census phase '%s' ════\n", label);
    std::fprintf(stderr,
                 "[jit] dispatch cache: %llu hits, %llu gen-miss, %llu miss\n",
                 (unsigned long long)dispatchCache_.hits(),
                 (unsigned long long)dispatchCache_.genMiss(),
                 (unsigned long long)dispatchCache_.miss());
    dispatchCache_.resetPhase();
    std::fprintf(stderr, "[jit] flushes:");
    for (int f = 0; f < int(Flush::Count); f++)
        std::fprintf(stderr, " %s=%llu", flushName(Flush(f)),
                     (unsigned long long)stats_.flushCauses[f].load(
                         std::memory_order_relaxed));
    std::fprintf(stderr, "  blocks live=%zu, cold-evicted=%llu\n",
                 blocks_.size(), (unsigned long long)coldEvictions_);
    coldEvictions_ = 0;
    dumpHisto();
    std::fill(histo_.begin(), histo_.end(), 0);
    std::fill(slowStaticHisto_.begin(), slowStaticHisto_.end(), 0);
    std::fill(slowRuntimeHisto_.begin(), slowRuntimeHisto_.end(), 0);
    std::fill(slowRuntimeReasonHisto_.begin(), slowRuntimeReasonHisto_.end(), 0);
    shiftVersions_.resetPhase();
    // lastRuntimeAddressCount_ points INTO this map; clearing without
    // resetting it would leave the fast-path cache writing through a
    // dangling pointer on the next runtime fallback.
    runtimeAddressHisto_.clear();
    lastRuntimeAddress_ = {};
    lastRuntimeAddressCount_ = nullptr;
}

} // namespace jit
