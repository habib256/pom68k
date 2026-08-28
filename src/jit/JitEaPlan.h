// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── shared effective-address plan for native code generators ─────────────
// The DECODING itself already lives in the common IR (`findEffectiveAddress`
// over `DecodedEffectiveAddress`); what each backend used to duplicate was
// the flattened per-instruction view of it — its own `struct Ea` and its own
// admission wrapper, drifting field by field (a64 carried memory
// indirection, x64 did not). Both objects model the 68k, not the host
// (TODO.md § 10, finding 2), so they are written once here. Backends keep
// only emission: how an admitted plan turns into host instructions.

#pragma once

#include "JitCost.h"
#include "JitIr.h"

#include <cstdint>

namespace jit {

struct EaPlan {
    int mode = 0, reg = 0;
    int idx = -1;                   // EaCostIndex row (JitCost.h)
    int32_t value = 0;              // displacement, absolute address or imm
    int32_t baseDisplacement = 0;
    uint32_t base = 0;              // PC-relative/indexed base
    int ixReg = 0, ixShift = 0;
    int ext = 0;                    // extension words consumed
    uint8_t baseDisplacementWords = 0;
    bool ixLong = false;
    bool fullFormat = false;
    bool baseSuppressed = false;
    bool indexSuppressed = false;
    bool memory = false;            // needs a real guest access
    IndexIndirect indirect = IndexIndirect::None;
    int32_t outerDisplacement = 0;
    uint8_t outerDisplacementWords = 0;
};

// Admission wrapper over the IR's decoded EA. Full 68020 index plans are
// valid shared IR, not brief extensions; they are refused by DEFAULT and
// each caller opts in per form — direct and memory-indirect separately —
// so an unfinished lowering cannot admit a plan it does not emit.
inline bool decodeEaPlan(const Instr& in, int mode, int reg,
                         int szIdx, int extAt, EaPlan& ea,
                         bool allowFullDirect = false,
                         bool allowFullIndirect = false) {
    ea.mode = mode; ea.reg = reg;
    ea.idx = eaCostIndex(mode, reg);
    if (ea.idx < 0) return false;
    const DecodedEffectiveAddress* decoded = findEffectiveAddress(
        in, uint8_t(mode), uint8_t(reg), uint8_t(szIdx), uint8_t(extAt));
    if (!decoded || !decoded->valid) return false;
    if (decoded->fullFormat) {
        const bool direct = decoded->indirect == IndexIndirect::None;
        if ((direct && !allowFullDirect) || (!direct && !allowFullIndirect))
            return false;
    }
    ea.memory = decoded->memory();
    ea.value = decoded->value;
    ea.baseDisplacement = decoded->baseDisplacement;
    ea.base = decoded->extensionAddress;
    ea.ixReg = decoded->indexRegister;
    ea.ixLong = decoded->indexLong;
    ea.ixShift = decoded->indexShift;
    ea.baseDisplacementWords = decoded->baseDisplacementWords;
    ea.fullFormat = decoded->fullFormat;
    ea.baseSuppressed = decoded->baseSuppressed;
    ea.indexSuppressed = decoded->indexSuppressed;
    ea.indirect = decoded->indirect;
    ea.outerDisplacement = decoded->outerDisplacement;
    ea.outerDisplacementWords = decoded->outerDisplacementWords;
    ea.ext = decoded->extensionWords;
    return true;
}

// Cost-model conveniences over an admitted plan (JitCost.h holds the model).
inline int fullIndexPenalty(const EaPlan& ea) {
    return fullIndexPenalty(ea.baseDisplacementWords, ea.indirect,
                            ea.outerDisplacementWords);
}
inline int fullFormatReadExtra(const EaPlan& ea) {
    return fullFormatReadExtra(ea.indirect, ea.baseSuppressed,
                               ea.indexSuppressed, ea.baseDisplacementWords);
}

}  // namespace jit
