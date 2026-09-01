// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// JIT gate: backend acceptance parity, asset-free (CHANGELOG 2026-09-01).
//
// The 2026-08-28 architecture review found the two native code generators
// had drifted into two projects: `Op::Bitfield`, `Op::MoveSrToReg` and
// `Op::ShiftRegister` were emitted by a64 and absent from x64, and NO gate
// compared the two `canEmit`. This gate is the review's rule applied — it
// names the bug it would have caught. Both predicates are pure functions of
// the opcode, so the whole 65 536-entry space is swept on every host: both
// backend translation units compile on any ISA (they emit bytes into
// buffers; only EXECUTING generated code needs the matching host), which is
// what lets an AArch64 machine hold the x64 backend to its word.
//
// Any (SemanticOp, direction) divergence group must appear in the dated
// exception table below, with its reason. The table is empty now: a new
// lowering added on one side only fails RED here instead of becoming next
// month's "port the deltas" debt.

#include "jit/JitBackend.h"
#include "jit/JitIr.h"
#include "jit/backends/JitBackendA64.h"
#include "jit/backends/JitBackendX64.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <map>

namespace {

using jit::SemanticOp;

const char* opName(SemanticOp op) {
    switch (op) {
        case SemanticOp::Unknown:          return "Unknown";
        case SemanticOp::Nop:              return "Nop";
        case SemanticOp::Move:             return "Move";
        case SemanticOp::MoveQuick:        return "MoveQuick";
        case SemanticOp::MoveSrToReg:      return "MoveSrToReg";
        case SemanticOp::ImmediateAlu:     return "ImmediateAlu";
        case SemanticOp::Bit:              return "Bit";
        case SemanticOp::AddSubQuick:      return "AddSubQuick";
        case SemanticOp::AddSubExtend:     return "AddSubExtend";
        case SemanticOp::AluEaToReg:       return "AluEaToReg";
        case SemanticOp::AluRegToEa:       return "AluRegToEa";
        case SemanticOp::AddressAlu:       return "AddressAlu";
        case SemanticOp::Test:             return "Test";
        case SemanticOp::Clear:            return "Clear";
        case SemanticOp::Negate:           return "Negate";
        case SemanticOp::Complement:       return "Complement";
        case SemanticOp::Extend:           return "Extend";
        case SemanticOp::Swap:             return "Swap";
        case SemanticOp::Exchange:         return "Exchange";
        case SemanticOp::CompareMemory:    return "CompareMemory";
        case SemanticOp::Lea:              return "Lea";
        case SemanticOp::Pea:              return "Pea";
        case SemanticOp::Movem:            return "Movem";
        case SemanticOp::Link:             return "Link";
        case SemanticOp::Unlink:           return "Unlink";
        case SemanticOp::SetCondition:     return "SetCondition";
        case SemanticOp::Branch:           return "Branch";
        case SemanticOp::BranchSubroutine: return "BranchSubroutine";
        case SemanticOp::DecrementBranch:  return "DecrementBranch";
        case SemanticOp::JumpSubroutine:   return "JumpSubroutine";
        case SemanticOp::Jump:             return "Jump";
        case SemanticOp::ReturnSubroutine: return "ReturnSubroutine";
        case SemanticOp::ShiftRegister:    return "ShiftRegister";
        case SemanticOp::Bitfield:         return "Bitfield";
        case SemanticOp::MultiplyWord:     return "MultiplyWord";
        case SemanticOp::MultiplyLong:     return "MultiplyLong";
        case SemanticOp::DivideWord:       return "DivideWord";
        case SemanticOp::DivideLong:       return "DivideLong";
    }
    return "?";
}

// A tolerated divergence group is every opcode of one SemanticOp where one
// backend accepts and the other refuses, in the stated direction. Each row
// must be dated and carry the reason it is allowed to exist. The gate also
// rejects stale rows once their lowering lands.
struct AllowedDivergence {
    SemanticOp op;
    bool a64Only;       // true: a64 accepts, x64 refuses; false: the reverse
    const char* why;
};

constexpr std::array<AllowedDivergence, 0> kAllowed{};

int failures = 0;

}  // namespace

int main() {
    jit::Backend* a64 = jit::a64Backend();
    jit::Backend* x64 = jit::x64Backend();
    if (!a64 || !x64) { std::printf("FAIL: backend factory\n"); return 1; }

    // (semantic op, a64Only) -> count, with one sample opcode for the report.
    std::map<std::pair<int, bool>, std::pair<int, uint16_t>> groups;
    for (uint32_t op = 0; op < 0x10000; ++op) {
        const bool a = a64->canEmit(uint16_t(op));
        const bool x = x64->canEmit(uint16_t(op));
        if (a == x) continue;
        auto& g = groups[{int(jit::describeInstruction(uint16_t(op)).operation), a}];
        if (g.first++ == 0) g.second = uint16_t(op);
    }

    for (const auto& [key, val] : groups) {
        const auto op = SemanticOp(key.first);
        const bool a64Only = key.second;
        const char* why = nullptr;
        for (const AllowedDivergence& e : kAllowed)
            if (e.op == op && e.a64Only == a64Only) { why = e.why; break; }
        std::printf("  %-16s %s-only: %5d opcodes (e.g. $%04X)%s\n",
                    opName(op), a64Only ? "a64" : "x64",
                    val.first, val.second,
                    why ? "  [allowed]" : "  FAIL: undocumented divergence");
        if (!why) failures++;
    }

    // A listed exception that no longer diverges is stale: the lowering
    // landed (delete the row) or the sweep broke (find out which).
    for (const AllowedDivergence& e : kAllowed) {
        if (!groups.count({int(e.op), e.a64Only})) {
            std::printf("  FAIL: allowed divergence %s (%s-only) not observed"
                        " — delete its row or explain it\n",
                        opName(e.op), e.a64Only ? "a64" : "x64");
            failures++;
        }
    }

    std::printf("jit_backend_parity_test: %d divergence group(s), "
                "%d failure(s)\n", int(groups.size()), failures);
    return failures ? 1 : 0;
}
