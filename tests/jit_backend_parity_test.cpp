// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// JIT gate: backend acceptance parity, asset-free (TODO.md § 10 wave 2).
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
// exception table below, with its reason. A new divergence — a lowering
// added on one side only — fails RED here instead of becoming next month's
// "port the deltas" debt.

#include "jit/JitBackend.h"
#include "jit/JitIr.h"
#include "jit/backends/JitBackendA64.h"
#include "jit/backends/JitBackendX64.h"

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
    }
    return "?";
}

// One tolerated divergence group: every opcode of this SemanticOp where one
// backend accepts and the other refuses, in the stated direction. Each row
// is dated and carries the reason it is allowed to exist. Closing a row
// means landing the missing lowering, then DELETING it here — the gate goes
// red if a listed group disappears, so a stale exception cannot linger.
struct AllowedDivergence {
    SemanticOp op;
    bool a64Only;       // true: a64 accepts, x64 refuses; false: the reverse
    const char* why;
};

const AllowedDivergence kAllowed[] = {
    // ── real coverage gaps: lowerings a64 has and x64 lacks ──────────────
    // (the three the 2026-08-28 review named, plus what this gate's first
    // sweep added the same day; closing one = landing the x64 lowering)
    // (2026-08-30: the Bitfield row is retired — x64 admits the family with
    // a64:1100's exact rule and lowers register plus TAILLESS memory forms.
    // Possible five-byte reads still refuse at EMISSION on x64, which this
    // gate cannot see and the directed residency oracle can.)
    { SemanticOp::MoveSrToReg, true,
      "2026-08-28: a64 lowers MOVE SR,Dn; x64 has no emitter" },
    { SemanticOp::Bit, true,
      "2026-08-28: a64 lowers the modifying bit ops (BCHG/BCLR/BSET, "
      "action != 0); x64's rule admits BTST only" },
    { SemanticOp::Move, true,
      "2026-08-28: a64 admits indexed destinations x64's kMoveDst row "
      "refuses — the 'port the a64 030 deltas' debt made visible" },
    // ── x64 canEmit over-claims: forms its EMITTER refuses ───────────────
    // canEmit answers yes and compile() then rejects, which only costs an
    // invisible refusal — but the drift is real and stays documented until
    // the x64 predicate is tightened on a host that can run its gates.
    // Most rows are illegal 68k encodings (An with byte size, immediate or
    // PC-relative destinations); Test also covers TST #imm, a legal 020
    // form only x64 claims, and Bit covers BTST Dn,#imm likewise.
    { SemanticOp::Move, false,
      "2026-08-28: x64's rule omits a64's byte-size-An exclusion — "
      "MOVE.B An,<ea> / MOVE.B <ea>,An are illegal encodings" },
    { SemanticOp::ImmediateAlu, false,
      "2026-08-28: x64 admits An/immediate/d16(PC) destinations a64 "
      "refuses; only CMPI d16(PC) among them is a legal 020 form" },
    { SemanticOp::Bit, false,
      "2026-08-28: x64 admits An/#imm static-bit operands a64 refuses "
      "(BTST Dn,#imm is the one legal form)" },
    { SemanticOp::AddSubQuick, false,
      "2026-08-28: x64 admits the illegal d16(PC) destination" },
    { SemanticOp::AluRegToEa, false,
      "2026-08-28: x64 admits the illegal immediate destination" },
    { SemanticOp::Test, false,
      "2026-08-28: TST #imm — legal on the 020+, only x64 claims it" },
    { SemanticOp::Clear, false,
      "2026-08-28: x64 admits the illegal immediate destination" },
    { SemanticOp::Negate, false,
      "2026-08-28: x64 admits the illegal immediate destination" },
    { SemanticOp::Complement, false,
      "2026-08-28: x64 admits the illegal immediate destination" },
    { SemanticOp::Pea, false,
      "2026-08-28: x64 admits (An)+/-(An)/An/#imm — illegal PEA "
      "encodings its own kPea row refuses at emit time" },
};

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
