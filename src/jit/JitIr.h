// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── JIT intermediate representation (host-neutral) ──
// A compiled block is described here and NOWHERE else in host terms. The
// threaded backend, the x86-64 backend and any future backend all consume
// this same structure; if a backend ever needs a field that only makes
// sense on its own architecture, that is a design error (invariant 6 of
// src/jit/POM68K_JIT.md).
//
// Three contracts are deliberately written into the IR rather than left to
// the backends:
//
//  1. CONDITION CODES ARE EXPLICIT. An entry declares which 68k CCR bits it
//     reads and writes. No backend may "just keep the host flags": x86
//     EFLAGS and AArch64 NZCV disagree on the carry of a subtraction, and
//     the 68k X bit exists on neither.
//  2. ENDIANNESS IS EXPLICIT. The 68k is big-endian, every host POM68K
//     targets is little-endian. Memory accesses carry their size so a
//     backend byte-swaps deliberately instead of by accident.
//  3. NO HOST POINTERS. The IR holds guest addresses only. Host pointers
//     live in the code window (Moira::PomJitWindow) and in the backend's
//     own compiled artefact, both of which are invalidated independently.

#pragma once
#include <cstdint>
#include <vector>

namespace jit {

// Coarse instruction classes. The block builder uses `Unsafe` to decide
// where a block must stop; a code-generating backend uses the rest to
// decide what it can emit natively.
enum class Kind : uint8_t {
    Move,        // MOVE / MOVEA / MOVEQ
    Alu,         // ADD/SUB/AND/OR/EOR/CMP/NEG/NOT/CLR/TST/…
    Shift,       // ASL/LSR/ROL/ROXR/bitfield
    BitOp,       // BTST/BCHG/BCLR/BSET
    Muldiv,      // MULU/MULS/DIVU/DIVS (may trap)
    AddrCalc,    // LEA / PEA / EXT / SWAP / LINK / UNLK
    Multi,       // MOVEM / MOVEP
    Cond,        // Scc (sets a byte, does not branch)
    Unsafe,      // must end a block BEFORE it: control flow, SR/MMU/cache
    Count
};

// Per-entry properties a backend must honour.
//
// WHAT THE TRACER FILLS: `FlagMayTrap` only. Everything else is the IR
// CONTRACT — the set of facts a code-generating backend needs and must
// therefore compute in its own decode pass before it may claim the
// corresponding BackendCaps bit. They are declared here, rather than in a
// backend, precisely so that two backends cannot disagree about what
// "carry" or "byte order" mean (see src/jit/backends/JitBackendA64.md).
// The threaded backend needs none of them: it delegates to Moira.
enum Flag : uint8_t {
    FlagNone      = 0,
    FlagReadsMem  = 1 << 0,
    FlagWritesMem = 1 << 1,
    FlagMayTrap   = 1 << 2,   // divide-by-zero, CHK, BKPT… — set by the tracer
    FlagSetsCcr   = 1 << 3,
    FlagUsesCcr   = 1 << 4,
    FlagSetsX     = 1 << 5,   // the 68k extend bit — no host has one
};

// One guest instruction inside a block. `words` is the FULL instruction
// length (opcode + extension words) as observed while the trace was
// recorded, so a code generator can read the extension words back out of
// the code window without re-implementing 68k instruction lengths.
struct Instr {
    uint32_t pc      = 0;      // logical address of the opcode word
    uint16_t opcode  = 0;      // the word Moira dispatched on (ird)
    uint16_t words   = 1;      // instruction length in 16-bit words
    Kind     kind    = Kind::Alu;
    uint8_t  flags   = FlagNone;
};

// Why a block stopped growing. Recorded for the gauges: the mix tells us
// which classifier rule is costing us block length.
enum class EndReason : uint8_t {
    ControlFlow,   // the next instruction branches / returns / traps
    Unsafe,        // the next instruction can change SR, the MMU or a cache
    LengthLimit,   // POM68K_JIT_BLOCK_MAX reached
    WindowEdge,    // the next instruction leaves the validated code window
    Discontinuity, // execution did not land where the trace expected
    Faulted,       // the trace ended on an MMU/bus fault
    Count
};

struct BlockIr {
    uint32_t entryPc  = 0;     // logical pc the block is keyed on
    bool     super    = false; // sr.s the block was recorded under
    uint32_t codeBase = 0;     // logical footprint, for the window check
    uint32_t codeLen  = 0;
    uint32_t physBase = 0;     // physical footprint, for the write guard
    uint32_t physLen  = 0;
    EndReason endReason = EndReason::LengthLimit;
    std::vector<Instr> instrs;

    uint32_t exitPc() const {
        return instrs.empty() ? entryPc
                              : instrs.back().pc + uint32_t(instrs.back().words) * 2;
    }
};

// ── Classifier ───────────────────────────────────────────────────────────
// Runs at block-BUILD time only (never per executed instruction), so it can
// afford to be explicit. Conservative by construction: anything not proven
// safe is `Unsafe` and ends the block before it.
//
// "Safe" here means exactly two things, because everything else is already
// caught at replay time by the per-instruction pc and flags checks:
//   (a) it cannot change the MMU translation, the ATC, the caches or the
//       supervisor bit — those would silently stale the code window;
//   (b) it cannot be an unconditional transfer of control, which would make
//       the recorded straight line meaningless.
inline Kind classify(uint16_t op) {
    const uint16_t hi = uint16_t(op & 0xF000);

    switch (hi) {

        case 0x0000: {
            // Immediate group, plus the awkward tenants: MOVES, CAS/CAS2,
            // CMP2/CHK2 and the "immediate to CCR/SR" encodings.
            const uint16_t lo = uint16_t(op & 0x00FF);
            if (lo == 0x003C || lo == 0x007C) return Kind::Unsafe;   // …to CCR/SR
            if ((op & 0x0F00) == 0x0E00) return Kind::Unsafe;        // MOVES, CAS.l, CAS2.l
            if ((op & 0xFFC0) == 0x0AC0 || (op & 0xFFC0) == 0x0CC0) return Kind::Unsafe;  // CAS.b/.w
            if (op == 0x0CFC) return Kind::Unsafe;                   // CAS2.w
            if ((op & 0xFFC0) == 0x00C0 || (op & 0xFFC0) == 0x02C0 ||
                (op & 0xFFC0) == 0x04C0) return Kind::Unsafe;        // CMP2/CHK2 (trap)
            if ((op & 0x0F00) == 0x0800) return Kind::BitOp;         // static bit ops
            if ((op & 0x0138) == 0x0108) return Kind::Multi;         // MOVEP
            return Kind::Alu;
        }

        case 0x1000: case 0x2000: case 0x3000:
            return Kind::Move;

        case 0x4000: {
            if ((op & 0xFF00) == 0x4E00) {
                // $4Exx: TRAP/LINK/UNLK/MOVE USP/RESET/NOP/STOP/RTE/RTD/
                // RTS/TRAPV/RTR/MOVEC/JSR/JMP. LINK/UNLK/NOP are harmless
                // but rare enough that keeping this whole group out costs
                // nothing and keeps the rule readable.
                return Kind::Unsafe;
            }
            if ((op & 0xFFC0) == 0x40C0 || (op & 0xFFC0) == 0x42C0 ||
                (op & 0xFFC0) == 0x44C0 || (op & 0xFFC0) == 0x46C0) {
                return Kind::Unsafe;                                 // MOVE from/to SR/CCR
            }
            // TAS is a LOCKED read-modify-write: it sets mmu040Lrmw, which
            // makes its read translate with WRITE semantics. Out.
            if ((op & 0xFFC0) == 0x4AC0) return Kind::Unsafe;
            if ((op & 0xFFF8) == 0x4848) return Kind::Unsafe;        // BKPT (traps)
            if ((op & 0xFB80) == 0x4880) return Kind::Multi;         // MOVEM
            if ((op & 0xFF80) == 0x4C00) return Kind::Muldiv;        // MULL / DIVL (020+)
            if ((op & 0xF1C0) == 0x41C0) return Kind::AddrCalc;      // LEA
            if ((op & 0xFFC0) == 0x4840) return Kind::AddrCalc;      // SWAP / PEA
            if ((op & 0xF1C0) == 0x4180 || (op & 0xF1C0) == 0x4100)
                return Kind::Muldiv;                                 // CHK (may trap)
            return Kind::Alu;                                        // CLR/NEG/NOT/TST/EXT/NBCD
        }

        case 0x5000: {
            if ((op & 0xF0F8) == 0x50C8) return Kind::Unsafe;        // DBcc
            if ((op & 0xF0F8) == 0x50F8) return Kind::Unsafe;        // TRAPcc
            if ((op & 0x00C0) == 0x00C0) return Kind::Cond;          // Scc
            return Kind::Alu;                                        // ADDQ / SUBQ
        }

        case 0x6000:                                                 // Bcc / BRA / BSR
            return Kind::Unsafe;

        case 0x7000:
            return Kind::Move;                                       // MOVEQ

        case 0x8000: case 0xC000:                                    // OR/DIV/SBCD, AND/MUL/ABCD/EXG
            if ((op & 0x01C0) == 0x00C0 || (op & 0x01C0) == 0x01C0) return Kind::Muldiv;
            return Kind::Alu;

        case 0x9000: case 0xB000: case 0xD000:                       // SUB/CMP/ADD families
            return Kind::Alu;

        case 0xA000:                                                 // A-line trap
            return Kind::Unsafe;

        case 0xE000:
            return Kind::Shift;                                      // shifts / rotates / bitfield

        case 0xF000:
        default:
            // F-line: FPU, MMU (PFLUSH/PTEST/PLPA), CINV/CPUSH, MOVE16.
            // The MMU and cache members of this group are exactly what the
            // code window cannot survive, so the whole line stays out.
            return Kind::Unsafe;
    }
}

inline bool endsBlock(Kind k) { return k == Kind::Unsafe; }

// The only flag the tracer can honestly know without a decoder. Kind::Muldiv
// collects everything that can take an exception while looking like ordinary
// straight-line code (MULU/MULS/DIVU/DIVS, MULL/DIVL, CHK); the replay's
// pc-continuity check turns such a trap into a clean block exit, and a code
// generator must treat it as a bail-out point.
inline uint8_t instrFlags(uint16_t /*op*/, Kind k) {
    return k == Kind::Muldiv ? uint8_t(FlagMayTrap) : uint8_t(FlagNone);
}

inline const char* endReasonName(EndReason r) {
    switch (r) {
        case EndReason::ControlFlow:   return "control flow";
        case EndReason::Unsafe:        return "unsafe opcode";
        case EndReason::LengthLimit:   return "length limit";
        case EndReason::WindowEdge:    return "window edge";
        case EndReason::Discontinuity: return "discontinuity";
        case EndReason::Faulted:       return "faulted";
        default:                       return "?";
    }
}

}  // namespace jit
