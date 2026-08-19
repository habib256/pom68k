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
// Six contracts are deliberately written into the IR rather than left to
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
//  4. THE TERMINAL PREFETCH STATE IS OBSERVED. On the mode-5 68030 there is
//     no tail refill: IRC may be the next word, an extension word, or a
//     displacement depending on the instruction path. The tracer records
//     PC/IRD/IRC separately so a backend never infers this from length.
//  5. DATA ACCESSES ARE A PROTOCOL, NOT A BOOLEAN. Their order, count, EA
//     commit point, fault phase and cache eligibility are recorded once.
//     Backends derive the same preflight/thunk/cache proof from that record;
//     an opcode-local `soleAccess` guess is no longer an architectural fact.
//  6. OPCODE MEANING IS SHARED. Family, ALU operation, width, direction,
//     operands, condition and sub-operation are decoded once into
//     `Instr::semantics`; a backend only chooses a host lowering.

#pragma once
#include <array>
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
    Branch,      // Bcc/BRA/DBcc — may end a block, never appear inside one
    Unsafe,      // must end a block BEFORE it: control flow, SR/MMU/cache
    Count
};

// Host-neutral meaning of the opcode subset understood by a native backend.
// `Kind` above answers whether tracing may cross an instruction; this contract
// answers what a proved native instruction does. Backends may still reject an
// EA their host lowering cannot encode, but they must not independently decide
// that (for example) $Dxxx means ADD, which operand is the EA, or which width
// is affected.
enum class SemanticOp : uint8_t {
    Unknown,
    Nop,
    Move,
    MoveQuick,
    MoveSrToReg,
    ImmediateAlu,
    Bit,
    AddSubQuick,
    AluEaToReg,
    AluRegToEa,
    AddressAlu,
    Test,
    Clear,
    Negate,
    Complement,
    Extend,
    Swap,
    Lea,
    Pea,
    Movem,
    Link,
    Unlink,
    SetCondition,
    Branch,
    BranchSubroutine,
    DecrementBranch,
    JumpSubroutine,
    Jump,
    ReturnSubroutine,
    ShiftRegister,
    Bitfield,
};

enum class AluOperation : uint8_t {
    None, Or, And, Eor, Add, Sub, Cmp
};

struct InstructionSemantics {
    static constexpr uint8_t NoSize = 0xFF;
    static constexpr uint8_t NoField = 0xFF;

    SemanticOp operation = SemanticOp::Unknown;
    AluOperation alu = AluOperation::None;
    uint8_t sizeIndex = NoSize;       // 0/1/2 = byte/word/long
    uint8_t eaMode = NoField;         // low opcode EA field
    uint8_t eaReg = NoField;
    uint8_t registerIndex = NoField;  // opcode bits 11..9
    uint8_t destinationMode = NoField;// MOVE's reordered destination EA
    uint8_t destinationReg = NoField;
    uint8_t condition = NoField;      // Bcc/DBcc/Scc
    uint8_t action = NoField;         // bit, shift or bitfield sub-operation
    bool dynamic = false;             // register bit/count rather than immediate
    bool left = false;                // shift direction
    bool toRegisters = false;         // MOVEM memory -> register list

    bool valid() const { return operation != SemanticOp::Unknown; }
    uint8_t bytes() const {
        return sizeIndex <= 2 ? uint8_t(1u << sizeIndex) : 0;
    }
};

// Host-neutral effective-address vocabulary. Extension words are copied into
// Instr and decoded here once; a backend maps this result to host registers
// and addressing instructions, but never parses the 68k extension format.
enum class EffectiveAddressKind : uint8_t {
    Invalid,
    DataRegister,
    AddressRegister,
    AddressIndirect,
    PostIncrement,
    PreDecrement,
    Displacement,
    BriefIndex,
    FullIndex,
    AbsoluteWord,
    AbsoluteLong,
    PcDisplacement,
    PcBriefIndex,
    PcFullIndex,
    Immediate,
};

enum class OperandRole : uint8_t {
    Source, Destination, Operand, Control
};

enum class IndexIndirect : uint8_t {
    None,
    Preindexed,
    Postindexed,
};

struct DecodedEffectiveAddress {
    EffectiveAddressKind kind = EffectiveAddressKind::Invalid;
    OperandRole role = OperandRole::Operand;
    uint8_t mode = 0xFF;
    uint8_t reg = 0xFF;
    uint8_t sizeIndex = InstructionSemantics::NoSize;
    uint8_t extensionOffset = 0;
    uint8_t extensionWords = 0;
    int32_t value = 0;             // displacement, absolute address or immediate
    uint32_t extensionAddress = 0; // PC-relative/index base
    uint8_t indexRegister = 0;
    uint8_t indexShift = 0;
    bool indexLong = false;
    // Full-format 68020 extension. `value` remains the value used by the
    // simple EA forms; the fields below never overload it, so a backend
    // cannot confuse a base displacement with an outer displacement.
    int32_t baseDisplacement = 0;
    int32_t outerDisplacement = 0;
    uint8_t baseDisplacementWords = 0;
    uint8_t outerDisplacementWords = 0;
    IndexIndirect indirect = IndexIndirect::None;
    bool fullFormat = false;
    bool baseSuppressed = false;
    bool indexSuppressed = false;
    bool valid = false;

    bool memory() const {
        return kind != EffectiveAddressKind::Invalid &&
               kind != EffectiveAddressKind::DataRegister &&
               kind != EffectiveAddressKind::AddressRegister &&
               kind != EffectiveAddressKind::Immediate;
    }
};

enum class ControlFlowKind : uint8_t {
    None,
    DirectBranch,
    ConditionalBranch,
    DecrementBranch,
    DirectCall,
    IndirectCall,
    DirectJump,
    IndirectJump,
    Return,
};

// The host-neutral boundary produced by a control-transfer instruction.
// Queue contents remain observed architectural state on Instr; this plan
// owns the addresses and decisions that used to be recomputed independently
// by A64 and x64.
struct ControlFlowPlan {
    ControlFlowKind kind = ControlFlowKind::None;
    uint32_t target = 0;
    uint32_t fallthrough = 0;
    uint32_t returnAddress = 0;
    uint8_t condition = InstructionSemantics::NoField;
    bool targetKnown = false;
    bool conditional = false;
    bool pushesReturnAddress = false;
    bool valid = false;
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

// ── Memory-access contract ──────────────────────────────────────────────
// This is intentionally about guest-visible semantics, never host pointers
// or registers. Two slots cover ordinary 68k instructions; MOVEM uses the
// variable-count marker and describes the repeated access in slot zero.
enum class MemoryDirection : uint8_t { Read, Write };
enum class MemoryOperand : uint8_t {
    Source, Destination, Operand, Stack, RegisterList
};
enum class MemoryOrder : uint8_t {
    None, Single, SourceThenDestination, ReadModifyWrite,
    RegisterAscending, RegisterDescending
};
enum class EaCommit : uint8_t {
    None, BeforeAccess, AfterAccess, PerElement
};
enum class FaultPhase : uint8_t {
    RestartInstruction, LastWrite, RestartableLastWrite
};
enum MemoryCache : uint8_t {
    MemoryCacheNone  = 0,
    MemoryCacheRead  = 1 << 0,
    MemoryCacheWrite = 1 << 1,
};

struct MemoryAccess {
    MemoryDirection direction = MemoryDirection::Read;
    MemoryOperand operand = MemoryOperand::Operand;
    uint8_t bytes = 0;          // 1, 2 or 4; 0 only for an unknown/dynamic span
    uint8_t eaMode = 0xFF;      // encoded 68k mode/register, or FF for implicit SP
    uint8_t eaReg = 0xFF;
    EaCommit eaCommit = EaCommit::None;
    FaultPhase fault = FaultPhase::RestartInstruction;
    uint8_t cache = MemoryCacheNone;
    bool exactRequired = false; // model/device timing requires the exact path
};

struct MemoryContract {
    static constexpr uint8_t VariableCount = 0xFF;
    static constexpr uint8_t NoLastWrite = 0xFF;
    static constexpr uint8_t VariableLastWrite = 0xFE;

    bool described = false;     // false means a backend must not infer a protocol
    uint8_t count = 0;
    MemoryOrder order = MemoryOrder::None;
    uint8_t lastWrite = NoLastWrite;
    // True only for memory-to-memory forms whose atomic two-line cache path
    // has its own fault/replay gate. Shape alone is not enough evidence.
    bool cachePairProved = false;
    MemoryAccess access[2] = {};
};

enum class MemoryProofProtocol : uint8_t {
    Interpreter, None, SingleRead, SingleWrite, PreflightAll,
    ReadModifyWrite, OrderedSpan, AtomicCachePair
};

struct MemoryProofOptions {
    bool exactReads = true;
    bool exactWrites = true;
    bool cacheReads = false;
    bool cacheWrites = false;
    bool cachePairs = false;
    bool restartableWriteRequired = false; // mode-5 68030 exact-thunk rule
};

// The backend-neutral result consumed by both native generators. Masks use
// bit N for MemoryContract::access[N]. `preflightMask` is the safety rule:
// those mappings must all be proved before access 0 becomes observable.
struct MemoryProofPlan {
    MemoryProofProtocol protocol = MemoryProofProtocol::Interpreter;
    uint8_t preflightMask = 0;
    uint8_t exactThunkMask = 0;
    uint8_t cacheMask = 0;
    uint8_t lastWrite = MemoryContract::NoLastWrite;
    bool lastWriteRestartable = false;

    bool valid() const { return protocol != MemoryProofProtocol::Interpreter; }
    bool single() const {
        return protocol == MemoryProofProtocol::SingleRead ||
               protocol == MemoryProofProtocol::SingleWrite;
    }
    bool atomicCachePair() const {
        return protocol == MemoryProofProtocol::AtomicCachePair;
    }
    bool restartableLastWrite() const {
        return lastWriteRestartable;
    }
};

// A capability for one concrete access, minted only by matching a decoded
// operand against Instr::memory. Native access helpers take this instead of
// a backend-local `soleAccess` boolean, so thunk/cache/EA-commit decisions
// cannot be reconstructed from the opcode a second time.
struct MemoryAccessPlan {
    MemoryProofProtocol protocol = MemoryProofProtocol::Interpreter;
    uint8_t index = 0xFF;
    MemoryDirection direction = MemoryDirection::Read;
    MemoryOperand operand = MemoryOperand::Operand;
    uint8_t bytes = 0;
    EaCommit eaCommit = EaCommit::None;
    FaultPhase fault = FaultPhase::RestartInstruction;
    bool preflight = false;
    bool exactThunk = false;
    bool exactRequired = false;
    bool cache = false;

    bool valid() const { return index < 2; }
    uint8_t mask() const { return valid() ? uint8_t(1u << index) : 0; }
    bool single() const {
        return protocol == MemoryProofProtocol::SingleRead ||
               protocol == MemoryProofProtocol::SingleWrite;
    }
};

inline bool memoryEa(uint8_t mode, uint8_t reg) {
    return mode >= 2 && !(mode == 7 && reg == 4); // #immediate is not memory
}

inline EaCommit memoryEaCommit(uint8_t mode, bool is030) {
    if (mode == 4) return EaCommit::BeforeAccess;       // -(An), every core
    if (mode == 3) return is030 ? EaCommit::BeforeAccess
                                : EaCommit::AfterAccess; // (An)+ differs
    return EaCommit::None;
}

inline MemoryAccess memoryAccess(MemoryDirection direction,
                                 MemoryOperand operand, uint8_t bytes,
                                 uint8_t mode, uint8_t reg, bool is030,
                                 FaultPhase fault) {
    MemoryAccess a;
    a.direction = direction;
    a.operand = operand;
    a.bytes = bytes;
    a.eaMode = mode;
    a.eaReg = reg;
    a.eaCommit = memoryEaCommit(mode, is030);
    a.fault = fault;
    a.cache = direction == MemoryDirection::Read ? MemoryCacheRead
                                                  : MemoryCacheWrite;
    return a;
}

// Common direction decoder for the $8/$9/$B/$C/$D ALU families.
// 0 = <ea> -> register, 1 = register -> <ea>, -1 = overlapping encoding.
inline int memoryAluDirection(uint16_t op) {
    const int hi = (op >> 12) & 0xF;
    const int opmode = (op >> 6) & 7;
    const int mode = (op >> 3) & 7;
    if (opmode == 3 || opmode == 7)
        return (hi == 0x9 || hi == 0xB || hi == 0xD) ? 0 : -1;
    if (opmode <= 2) return 0;
    if (mode <= 1) return (hi == 0xB && mode == 0) ? 1 : -1;
    return 1;
}

// Decode native-relevant instruction meaning once, when the trace becomes
// IR. This is deliberately broader than either backend: a backend selects
// from this vocabulary according to host mechanics, never by recreating an
// opcode-family decoder of its own.
inline InstructionSemantics describeInstruction(uint16_t op) {
    InstructionSemantics s;
    const uint16_t line = op & 0xF000;
    const uint8_t mode = uint8_t((op >> 3) & 7);
    const uint8_t reg = uint8_t(op & 7);
    s.eaMode = mode;
    s.eaReg = reg;
    s.registerIndex = uint8_t((op >> 9) & 7);

    const auto set = [&](SemanticOp operation,
                         uint8_t size = InstructionSemantics::NoSize) {
        s.operation = operation;
        s.sizeIndex = size;
    };
    const auto aluForImmediate = [](unsigned kind) {
        switch (kind) {
            case 0: return AluOperation::Or;
            case 1: return AluOperation::And;
            case 2: return AluOperation::Sub;
            case 3: return AluOperation::Add;
            case 5: return AluOperation::Eor;
            case 6: return AluOperation::Cmp;
            default: return AluOperation::None;
        }
    };

    if (line == 0x0000) {
        // MOVEP shares the dynamic-bit prefix but is a different operation;
        // leave it undescribed until it has its own common contract.
        if ((op & 0x0138) == 0x0108) return s;
        const bool dynamicBit = (op & 0xF100) == 0x0100;
        const bool staticBit = (op & 0xFF00) == 0x0800;
        if (dynamicBit || staticBit) {
            set(SemanticOp::Bit, mode == 0 ? 2 : 0);
            s.action = uint8_t((op >> 6) & 3);
            s.dynamic = dynamicBit;
            return s;
        }
        const uint8_t lo = uint8_t(op);
        if (lo == 0x3C || lo == 0x7C || (op & 0x0F00) == 0x0E00 ||
            (op & 0xFFC0) == 0x0AC0 || (op & 0xFFC0) == 0x0CC0 ||
            op == 0x0CFC || (op & 0xFFC0) == 0x00C0 ||
            (op & 0xFFC0) == 0x02C0 || (op & 0xFFC0) == 0x04C0)
            return s; // immediate to CCR/SR, MOVES/CAS family
        const uint8_t size = uint8_t((op >> 6) & 3);
        const AluOperation alu = aluForImmediate((op >> 9) & 7);
        if (size <= 2 && alu != AluOperation::None) {
            set(SemanticOp::ImmediateAlu, size);
            s.alu = alu;
        }
        return s;
    }

    if (line == 0x1000 || line == 0x2000 || line == 0x3000) {
        set(SemanticOp::Move, line == 0x1000 ? 0 : line == 0x3000 ? 1 : 2);
        s.destinationMode = uint8_t((op >> 6) & 7);
        s.destinationReg = uint8_t((op >> 9) & 7);
        return s;
    }

    if (line == 0x4000) {
        if (op == 0x4E71) set(SemanticOp::Nop);
        else if (op == 0x4E75) set(SemanticOp::ReturnSubroutine);
        else if ((op & 0xFFF8) == 0x4E50) set(SemanticOp::Link, 2);
        else if ((op & 0xFFF8) == 0x4E58) set(SemanticOp::Unlink, 2);
        else if ((op & 0xFFC0) == 0x4E80) set(SemanticOp::JumpSubroutine, 2);
        else if ((op & 0xFFC0) == 0x4EC0) set(SemanticOp::Jump, 2);
        else if ((op & 0xFFF8) == 0x40C0) set(SemanticOp::MoveSrToReg, 1);
        else if ((op & 0xF1C0) == 0x41C0) set(SemanticOp::Lea, 2);
        else if ((op & 0xFB80) == 0x4880 && mode >= 2) {
            set(SemanticOp::Movem, (op & 0x0040) ? 2 : 1);
            s.toRegisters = (op & 0x0400) != 0;
        }
        else if ((op & 0xFFB8) == 0x4880)
            set(SemanticOp::Extend, (op & 0x0040) ? 2 : 1);
        else if ((op & 0xFFF8) == 0x4840) set(SemanticOp::Swap, 2);
        else if ((op & 0xFFC0) == 0x4840 && mode >= 2)
            set(SemanticOp::Pea, 2);
        else {
            const uint8_t size = uint8_t((op >> 6) & 3);
            if (size <= 2) {
                switch (op & 0xFF00) {
                    case 0x4A00: set(SemanticOp::Test, size); break;
                    case 0x4200: set(SemanticOp::Clear, size); break;
                    case 0x4400: set(SemanticOp::Negate, size); break;
                    case 0x4600: set(SemanticOp::Complement, size); break;
                    default: break;
                }
            }
        }
        return s;
    }

    if (line == 0x5000) {
        s.condition = uint8_t((op >> 8) & 15);
        if ((op & 0xF0F8) == 0x50C8) {
            set(SemanticOp::DecrementBranch, 1);
        } else if ((op & 0xF0F8) == 0x50F8) {
            return InstructionSemantics{}; // TRAPcc
        } else if ((op & 0x00C0) == 0x00C0) {
            set(SemanticOp::SetCondition, 0);
        } else {
            const uint8_t size = uint8_t((op >> 6) & 3);
            if (size <= 2) {
                set(SemanticOp::AddSubQuick, size);
                s.alu = (op & 0x0100) ? AluOperation::Sub
                                      : AluOperation::Add;
            }
        }
        return s;
    }

    if (line == 0x6000) {
        s.condition = uint8_t((op >> 8) & 15);
        set(s.condition == 1 ? SemanticOp::BranchSubroutine
                             : SemanticOp::Branch);
        return s;
    }

    if (line == 0x7000) {
        if ((op & 0x0100) == 0) set(SemanticOp::MoveQuick, 2);
        return s;
    }

    if (line == 0x8000 || line == 0x9000 || line == 0xB000 ||
        line == 0xC000 || line == 0xD000) {
        const int direction = memoryAluDirection(op);
        if (direction < 0) return s;
        const uint8_t hi = uint8_t(op >> 12);
        const uint8_t opmode = uint8_t((op >> 6) & 7);
        if (opmode == 3 || opmode == 7) {
            if (hi != 0x9 && hi != 0xB && hi != 0xD) return s;
            set(SemanticOp::AddressAlu, opmode == 3 ? 1 : 2);
            s.alu = hi == 0x9 ? AluOperation::Sub
                  : hi == 0xB ? AluOperation::Cmp : AluOperation::Add;
            return s;
        }
        const int size = direction == 0 ? opmode : int(opmode) - 4;
        if (size < 0 || size > 2) return s;
        set(direction == 0 ? SemanticOp::AluEaToReg
                           : SemanticOp::AluRegToEa,
            uint8_t(size));
        switch (hi) {
            case 0x8: s.alu = AluOperation::Or; break;
            case 0x9: s.alu = AluOperation::Sub; break;
            case 0xB: s.alu = direction == 0 ? AluOperation::Cmp
                                             : AluOperation::Eor; break;
            case 0xC: s.alu = AluOperation::And; break;
            case 0xD: s.alu = AluOperation::Add; break;
            default: s.operation = SemanticOp::Unknown; break;
        }
        return s;
    }

    if (line == 0xE000) {
        if ((op & 0xF8C0) == 0xE8C0) {
            set(SemanticOp::Bitfield);
            s.action = uint8_t((op >> 8) & 7);
            return s;
        }
        const uint8_t size = uint8_t((op >> 6) & 3);
        if (size <= 2) {
            set(SemanticOp::ShiftRegister, size);
            s.action = uint8_t((op >> 3) & 3);
            s.dynamic = (op & 0x0020) != 0;
            s.left = (op & 0x0100) != 0;
        }
    }
    return s;
}

// Decode the data-access shape once, when the trace becomes IR. This is not
// an ISA-admission decoder: unsupported instructions may still receive an
// honest contract, and a backend remains free to reject their encoding.
inline MemoryContract describeMemory(uint16_t op, bool is030) {
    MemoryContract c;
    c.described = true;
    const uint16_t line = op & 0xF000;
    const uint8_t mode = uint8_t((op >> 3) & 7), reg = uint8_t(op & 7);
    auto setSingle = [&](MemoryAccess a) {
        c.count = 1;
        c.order = MemoryOrder::Single;
        c.access[0] = a;
        if (a.direction == MemoryDirection::Write) c.lastWrite = 0;
    };
    auto setRmw = [&](uint8_t bytes, MemoryOperand operand) {
        c.count = 2;
        c.order = MemoryOrder::ReadModifyWrite;
        c.access[0] = memoryAccess(MemoryDirection::Read, operand, bytes,
                                   mode, reg, is030,
                                   FaultPhase::RestartInstruction);
        c.access[1] = memoryAccess(MemoryDirection::Write, operand, bytes,
                                   mode, reg, is030, FaultPhase::LastWrite);
        c.lastWrite = 1;
    };

    // MOVE / MOVEA: source access precedes destination access.
    if (line == 0x1000 || line == 0x2000 || line == 0x3000) {
        const uint8_t bytes = line == 0x1000 ? 1 : line == 0x3000 ? 2 : 4;
        const uint8_t sm = mode, sr = reg;
        const uint8_t dm = uint8_t((op >> 6) & 7), dr = uint8_t((op >> 9) & 7);
        const bool srcMem = memoryEa(sm, sr), dstMem = memoryEa(dm, dr);
        uint8_t n = 0;
        if (srcMem)
            c.access[n++] = memoryAccess(MemoryDirection::Read,
                                         MemoryOperand::Source, bytes,
                                         sm, sr, is030,
                                         FaultPhase::RestartInstruction);
        if (dstMem) {
            FaultPhase fault = FaultPhase::LastWrite;
            // The proved 030 family: a register/immediate source followed by
            // the instruction's restartable LASTWRITE destination.
            const bool sourceValue = sm <= 1 || (sm == 7 && sr == 4);
            if (is030 && !srcMem && sourceValue)
                fault = FaultPhase::RestartableLastWrite;
            c.access[n] = memoryAccess(MemoryDirection::Write,
                                       MemoryOperand::Destination, bytes,
                                       dm, dr, is030, fault);
            c.lastWrite = n++;
        }
        c.count = n;
        c.order = n == 0 ? MemoryOrder::None
                         : n == 1 ? MemoryOrder::Single
                                  : MemoryOrder::SourceThenDestination;
        c.cachePairProved = n == 2 && bytes == 4 &&
                            (op == 0x2F38 || op == 0x21DF);
        return c;
    }

    // Calls/returns and the frame/stack helpers each have one implicit
    // stack access. Computing a JSR/PEA target is address arithmetic, not a
    // data read, and is therefore deliberately absent here.
    if (op == 0x4E75) { // RTS
        setSingle(memoryAccess(MemoryDirection::Read, MemoryOperand::Stack,
                               4, 3, 7, is030,
                               FaultPhase::RestartInstruction));
        return c;
    }
    const bool bsr = line == 0x6000 && ((op >> 8) & 15) == 1;
    const bool jsr = (op & 0xFFC0) == 0x4E80;
    const bool link = (op & 0xFFF8) == 0x4E50;
    const bool pea = (op & 0xFFC0) == 0x4840 && mode >= 2;
    if (bsr || jsr || link || pea) {
        setSingle(memoryAccess(MemoryDirection::Write, MemoryOperand::Stack,
                               4, 4, 7, is030, FaultPhase::LastWrite));
        return c;
    }
    if ((op & 0xFFF8) == 0x4E58) { // UNLK
        setSingle(memoryAccess(MemoryDirection::Read, MemoryOperand::Stack,
                               4, 2, reg, is030,
                               FaultPhase::RestartInstruction));
        return c;
    }

    // MOVEM is a variable number of equal-width ordered accesses. Its mask
    // decides the count at compile time; predecrement reverses register order.
    if ((op & 0xFB80) == 0x4880 && mode >= 2) {
        const bool read = (op & 0x0400) != 0;
        const uint8_t bytes = (op & 0x0040) ? 4 : 2;
        c.count = MemoryContract::VariableCount;
        c.order = mode == 4 ? MemoryOrder::RegisterDescending
                            : MemoryOrder::RegisterAscending;
        c.access[0] = memoryAccess(read ? MemoryDirection::Read
                                        : MemoryDirection::Write,
                                   MemoryOperand::RegisterList, bytes,
                                   mode, reg, is030,
                                   read ? FaultPhase::RestartInstruction
                                        : FaultPhase::LastWrite);
        c.access[0].eaCommit = EaCommit::PerElement;
        if (!read) c.lastWrite = MemoryContract::VariableLastWrite;
        return c;
    }

    // Static/dynamic bit operations: BTST reads, BCHG/BCLR/BSET are RMW.
    const bool dynamicBit = (op & 0xF100) == 0x0100;
    const bool staticBit = (op & 0xFF00) == 0x0800;
    if ((dynamicBit || staticBit) && memoryEa(mode, reg)) {
        if (((op >> 6) & 3) == 0)
            setSingle(memoryAccess(MemoryDirection::Read,
                                   MemoryOperand::Operand, 1, mode, reg,
                                   is030, FaultPhase::RestartInstruction));
        else
            setRmw(1, MemoryOperand::Operand);
        return c;
    }

    // Immediate ALU and ADDQ/SUBQ memory destinations.
    if (line == 0x0000 && memoryEa(mode, reg)) {
        const int kind = (op >> 9) & 7, sz = (op >> 6) & 3;
        if (sz <= 2 && (kind == 0 || kind == 1 || kind == 2 || kind == 3 ||
                        kind == 5 || kind == 6)) {
            const uint8_t bytes = uint8_t(1u << sz);
            if (kind == 6)
                setSingle(memoryAccess(MemoryDirection::Read,
                                       MemoryOperand::Destination, bytes,
                                       mode, reg, is030,
                                       FaultPhase::RestartInstruction));
            else
                setRmw(bytes, MemoryOperand::Destination);
            return c;
        }
    }
    if (line == 0x5000 && (op & 0x00C0) != 0x00C0 && memoryEa(mode, reg)) {
        const int sz = (op >> 6) & 3;
        if (sz <= 2) setRmw(uint8_t(1u << sz), MemoryOperand::Destination);
        return c;
    }
    // Scc memory forms write without reading the destination.
    if (line == 0x5000 && (op & 0x00C0) == 0x00C0 &&
        (op & 0xF0F8) != 0x50C8 && memoryEa(mode, reg)) {
        setSingle(memoryAccess(MemoryDirection::Write,
                               MemoryOperand::Destination, 1, mode, reg,
                               is030, FaultPhase::LastWrite));
        return c;
    }

    // Register/effective-address ALU families.
    if (line == 0x8000 || line == 0x9000 || line == 0xB000 ||
        line == 0xC000 || line == 0xD000) {
        const int direction = memoryAluDirection(op);
        const int opmode = (op >> 6) & 7;
        const int sz = direction == 0 ? (opmode <= 2 ? opmode
                                                     : opmode == 3 ? 1 : 2)
                                      : opmode - 4;
        if (direction >= 0 && sz >= 0 && sz <= 2 && memoryEa(mode, reg)) {
            const uint8_t bytes = uint8_t(1u << sz);
            if (direction == 0)
                setSingle(memoryAccess(MemoryDirection::Read,
                                       MemoryOperand::Source, bytes,
                                       mode, reg, is030,
                                       FaultPhase::RestartInstruction));
            else
                setRmw(bytes, MemoryOperand::Destination);
        }
        return c;
    }

    // TST reads, CLR writes, NOT/NEG are true read-modify-write forms.
    if (line == 0x4000 && ((op >> 6) & 3) <= 2 && memoryEa(mode, reg)) {
        const uint8_t bytes = uint8_t(1u << ((op >> 6) & 3));
        switch (op & 0xFF00) {
            case 0x4A00:
                {
                    MemoryAccess a = memoryAccess(
                        MemoryDirection::Read, MemoryOperand::Operand, bytes,
                        mode, reg, is030, FaultPhase::RestartInstruction);
                    // LC II's dominant device poll: its observed timing is
                    // the base instruction plus a device-owned bus delay, so
                    // an inline RAM/cache load is not a conforming lowering.
                    a.exactRequired = is030 && op == 0x4A11;
                    setSingle(a);
                }
                break;
            case 0x4200:
                setSingle(memoryAccess(MemoryDirection::Write,
                                       MemoryOperand::Destination, bytes,
                                       mode, reg, is030,
                                       FaultPhase::LastWrite));
                break;
            case 0x4400: case 0x4600:
                setRmw(bytes, MemoryOperand::Destination);
                break;
            default: break;
        }
    }
    return c;
}

inline MemoryProofPlan memoryProofPlan(const MemoryContract& c,
                                       const MemoryProofOptions& o) {
    MemoryProofPlan p;
    p.lastWrite = c.lastWrite;
    p.lastWriteRestartable = c.lastWrite < 2 &&
        c.access[c.lastWrite].fault == FaultPhase::RestartableLastWrite;
    if (!c.described) return p;
    if (c.count == 0) {
        p.protocol = MemoryProofProtocol::None;
        return p;
    }
    if (c.count == MemoryContract::VariableCount) {
        p.protocol = MemoryProofProtocol::OrderedSpan;
        p.preflightMask = 1;
        return p;
    }
    if (c.count > 2) return p;

    const auto thunkAllowed = [&](unsigned i) {
        const MemoryAccess& a = c.access[i];
        if (a.exactRequired) return true;
        if (a.direction == MemoryDirection::Read) return o.exactReads;
        // A 030 non-restartable LASTWRITE cannot use a post-access thunk and
        // then replay safely. The contract distinguishes the proved subset.
        return o.exactWrites &&
            (!o.restartableWriteRequired ||
             a.fault == FaultPhase::RestartableLastWrite);
    };
    const auto cacheAllowed = [&](unsigned i) {
        const MemoryAccess& a = c.access[i];
        if (a.exactRequired) return false;
        return a.direction == MemoryDirection::Read
            ? o.cacheReads && (a.cache & MemoryCacheRead)
            : o.cacheWrites && (a.cache & MemoryCacheWrite);
    };

    if (c.count == 1) {
        const bool read = c.access[0].direction == MemoryDirection::Read;
        p.protocol = read ? MemoryProofProtocol::SingleRead
                          : MemoryProofProtocol::SingleWrite;
        p.preflightMask = 1;
        if (thunkAllowed(0)) p.exactThunkMask = 1;
        if (cacheAllowed(0)) p.cacheMask = 1;
        return p;
    }
    p.preflightMask = 3; // every multi-access plan proves before access zero
    if (c.order == MemoryOrder::ReadModifyWrite) {
        p.protocol = MemoryProofProtocol::ReadModifyWrite;
        return p;
    }
    if (c.cachePairProved && o.cachePairs && cacheAllowed(0) && cacheAllowed(1) &&
        c.access[0].direction == MemoryDirection::Read &&
        c.access[1].direction == MemoryDirection::Write) {
        p.protocol = MemoryProofProtocol::AtomicCachePair;
        p.cacheMask = 3;
        return p;
    }
    p.protocol = MemoryProofProtocol::PreflightAll;
    return p;
}

// Match the backend's mechanical EA decode to the semantic IR contract and
// return the only token accepted by ordinary native memory helpers. Operand
// identity is part of the key: MOVE source and destination may use the same
// encoded EA but are different ordered accesses.
inline MemoryAccessPlan memoryAccessPlan(const MemoryContract& c,
                                         const MemoryProofPlan& proof,
                                         MemoryDirection direction,
                                         MemoryOperand operand,
                                         uint8_t bytes,
                                         uint8_t mode,
                                         uint8_t reg) {
    MemoryAccessPlan result;
    if (!proof.valid() || c.count == 0) return result;
    const unsigned slots = c.count == MemoryContract::VariableCount
        ? 1u : unsigned(c.count);
    if (slots > 2) return result;
    for (unsigned i = 0; i < slots; i++) {
        const MemoryAccess& a = c.access[i];
        if (a.direction != direction || a.operand != operand ||
            a.bytes != bytes || a.eaMode != mode || a.eaReg != reg)
            continue;
        // Ambiguous matches would let a backend silently choose an order.
        if (result.valid()) return MemoryAccessPlan{};
        result.protocol = proof.protocol;
        result.index = uint8_t(i);
        result.direction = a.direction;
        result.operand = a.operand;
        result.bytes = a.bytes;
        result.eaCommit = a.eaCommit;
        result.fault = a.fault;
        const uint8_t bit = uint8_t(1u << i);
        result.preflight = (proof.preflightMask & bit) != 0;
        result.exactThunk = (proof.exactThunkMask & bit) != 0;
        result.exactRequired = a.exactRequired;
        result.cache = (proof.cacheMask & bit) != 0;
    }
    return result;
}

inline uint8_t memoryContractMask(const MemoryContract& c) {
    if (c.count == 0) return 0;
    if (c.count == MemoryContract::VariableCount) return 1;
    return c.count <= 2 ? uint8_t((1u << c.count) - 1u) : 0xFF;
}

// Every emitted data access must be represented exactly once. This catches
// both omissions and invented accesses after the backend has decoded its EAs.
inline bool memoryAccessesAccounted(const MemoryContract& c,
                                    const MemoryProofPlan& proof,
                                    uint8_t consumedMask) {
    return proof.valid() && memoryContractMask(c) != 0xFF &&
           consumedMask == memoryContractMask(c);
}

// Small per-instruction cursor used by emitters. Calling access() both
// validates and consumes a semantic slot; complete() is the admission gate
// immediately before native code is accepted.
struct InstructionMemoryPlan {
    const MemoryContract* contract = nullptr;
    MemoryProofPlan proof;
    uint8_t consumedMask = 0;

    MemoryAccessPlan access(MemoryDirection direction,
                            MemoryOperand operand,
                            uint8_t bytes,
                            uint8_t mode,
                            uint8_t reg) {
        const MemoryAccessPlan a = contract
            ? memoryAccessPlan(*contract, proof, direction, operand,
                               bytes, mode, reg)
            : MemoryAccessPlan{};
        if (!a.valid() || (consumedMask & a.mask())) return MemoryAccessPlan{};
        consumedMask |= a.mask();
        return a;
    }

    bool complete() const {
        return contract &&
               memoryAccessesAccounted(*contract, proof, consumedMask);
    }
};

inline InstructionMemoryPlan instructionMemoryPlan(
    const MemoryContract& contract, const MemoryProofOptions& options) {
    InstructionMemoryPlan result;
    result.contract = &contract;
    result.proof = memoryProofPlan(contract, options);
    return result;
}

inline bool memoryRmwAccessPair(const MemoryAccessPlan& read,
                                const MemoryAccessPlan& write) {
    return read.valid() && write.valid() &&
           read.protocol == MemoryProofProtocol::ReadModifyWrite &&
           write.protocol == MemoryProofProtocol::ReadModifyWrite &&
           read.direction == MemoryDirection::Read &&
           write.direction == MemoryDirection::Write &&
           read.operand == write.operand && read.bytes == write.bytes &&
           read.preflight && write.preflight &&
           uint8_t(read.mask() | write.mask()) == 3;
}

inline bool memoryRequiresExactAccess(const MemoryContract& contract) {
    const unsigned slots = contract.count == MemoryContract::VariableCount
        ? 1u : unsigned(contract.count);
    if (slots > 2) return false;
    for (unsigned i = 0; i < slots; i++)
        if (contract.access[i].exactRequired) return true;
    return false;
}

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
    // Total cycles this instruction charged when the tracer ran it, followed
    // by its three independently observed components. On cores without a
    // detailed timing probe `baseCycles == cycles` and both other fields are
    // zero. A backend may validate a known-safe 68030 form against the base
    // component without mistaking a cache miss for an opcode cost.
    uint16_t cycles  = 0;
    uint16_t baseCycles = 0;
    uint16_t icacheCycles = 0;
    uint16_t postExceptionCycles = 0;
    // How many instruction-stream words the traced run actually fetched
    // through mmuFetchWord (the pomIcache.fetches delta). NOT words + 1:
    // a form whose last extension is consumed under SKIP_LAST_RD fetches
    // only `words` (MOVEA.L (xxx).W,An was the counterexample the
    // retained-cache lockstep produced, 2026-08-19). 0 = not captured;
    // the 030 emitted charge refuses to guess and must not emit then.
    uint8_t fetchWords = 0;
    uint32_t observedNextPc = 0; // PC after the traced instruction
    uint16_t terminalIrd = 0;    // queue at that same boundary
    uint16_t terminalIrc = 0;
    bool terminalQueueValid = false;
    MemoryContract memory;
    InstructionSemantics semantics;

    // The architectural instruction payload. Ten extension words cover the
    // maximum 68020-family instruction length (22 bytes including opcode).
    // Keeping them on Instr makes BlockIr::code provenance/debug data; it is
    // no longer a semantic input to either native backend.
    static constexpr uint8_t MaxExtensionWords = 10;
    std::array<uint16_t, MaxExtensionWords> extensions{};
    uint8_t extensionCount = 0;
    std::array<DecodedEffectiveAddress, 2> effectiveAddresses{};
    uint8_t effectiveAddressCount = 0;
    ControlFlowPlan control;

    uint16_t extensionWord(unsigned index) const {
        return index < extensionCount ? extensions[index] : 0;
    }
    uint32_t extensionLong(unsigned index) const {
        return uint32_t(extensionWord(index)) << 16 | extensionWord(index + 1);
    }
};

inline DecodedEffectiveAddress decodeEffectiveAddress(
    const Instr& in, OperandRole role, uint8_t mode, uint8_t reg,
    uint8_t sizeIndex, uint8_t extensionOffset) {
    DecodedEffectiveAddress ea;
    ea.role = role;
    ea.mode = mode;
    ea.reg = reg;
    ea.sizeIndex = sizeIndex;
    ea.extensionOffset = extensionOffset;
    ea.extensionAddress = in.pc + 2 + uint32_t(extensionOffset) * 2;
    const auto word = [&](unsigned n) { return in.extensionWord(extensionOffset + n); };
    const auto need = [&](unsigned n) {
        return unsigned(extensionOffset) + n <= in.extensionCount;
    };

    if (mode <= 4) {
        static constexpr EffectiveAddressKind kinds[] = {
            EffectiveAddressKind::DataRegister,
            EffectiveAddressKind::AddressRegister,
            EffectiveAddressKind::AddressIndirect,
            EffectiveAddressKind::PostIncrement,
            EffectiveAddressKind::PreDecrement,
        };
        ea.kind = kinds[mode];
        ea.valid = true;
        return ea;
    }
    if (mode == 5) {
        ea.kind = EffectiveAddressKind::Displacement;
        ea.extensionWords = 1;
        ea.valid = need(1);
        ea.value = int16_t(word(0));
        return ea;
    }
    if (mode == 6 || (mode == 7 && reg == 3)) {
        ea.kind = mode == 6 ? EffectiveAddressKind::BriefIndex
                            : EffectiveAddressKind::PcBriefIndex;
        ea.extensionWords = 1;
        if (!need(1)) return ea;
        const uint16_t ext = word(0);
        ea.indexRegister = uint8_t(((ext >> 12) & 7) +
                                   ((ext & 0x8000) ? 8 : 0));
        ea.indexLong = (ext & 0x0800) != 0;
        ea.indexShift = uint8_t((ext >> 9) & 3);
        if (!(ext & 0x0100)) {
            ea.value = int8_t(ext & 0xFF);
            ea.valid = true;
            return ea;
        }

        // MC68020 full extension: optional base displacement, optional
        // memory-indirect pre/post-indexing, then optional outer
        // displacement. Bit 3, BD=00 and I/IS=100 are reserved encodings.
        ea.kind = mode == 6 ? EffectiveAddressKind::FullIndex
                            : EffectiveAddressKind::PcFullIndex;
        ea.fullFormat = true;
        ea.baseSuppressed = (ext & 0x0080) != 0;
        ea.indexSuppressed = (ext & 0x0040) != 0;
        const uint8_t bd = uint8_t((ext >> 4) & 3);
        const uint8_t iis = uint8_t(ext & 7);
        if ((ext & 0x0008) || bd == 0 || iis == 4) return ea;

        ea.baseDisplacementWords = bd == 2 ? 1 : bd == 3 ? 2 : 0;
        ea.outerDisplacementWords =
            (iis == 2 || iis == 6) ? 1 : (iis == 3 || iis == 7) ? 2 : 0;
        ea.indirect = iis == 0 ? IndexIndirect::None
                    : iis <= 3 ? IndexIndirect::Preindexed
                               : IndexIndirect::Postindexed;
        ea.extensionWords = uint8_t(1 + ea.baseDisplacementWords +
                                    ea.outerDisplacementWords);
        if (!need(ea.extensionWords)) return ea;

        unsigned cursor = 1;
        if (ea.baseDisplacementWords == 1) {
            ea.baseDisplacement = int16_t(word(cursor));
            cursor++;
        } else if (ea.baseDisplacementWords == 2) {
            ea.baseDisplacement = int32_t(uint32_t(word(cursor)) << 16 |
                                          word(cursor + 1));
            cursor += 2;
        }
        if (ea.outerDisplacementWords == 1) {
            ea.outerDisplacement = int16_t(word(cursor));
        } else if (ea.outerDisplacementWords == 2) {
            ea.outerDisplacement = int32_t(uint32_t(word(cursor)) << 16 |
                                           word(cursor + 1));
        }
        ea.valid = true;
        return ea;
    }
    if (mode != 7) return ea;
    switch (reg) {
        case 0:
            ea.kind = EffectiveAddressKind::AbsoluteWord;
            ea.extensionWords = 1;
            ea.valid = need(1);
            ea.value = int16_t(word(0));
            break;
        case 1:
            ea.kind = EffectiveAddressKind::AbsoluteLong;
            ea.extensionWords = 2;
            ea.valid = need(2);
            ea.value = int32_t(uint32_t(word(0)) << 16 | word(1));
            break;
        case 2:
            ea.kind = EffectiveAddressKind::PcDisplacement;
            ea.extensionWords = 1;
            ea.valid = need(1);
            ea.value = int32_t(ea.extensionAddress +
                               uint32_t(int16_t(word(0))));
            break;
        case 4:
            ea.kind = EffectiveAddressKind::Immediate;
            ea.extensionWords = sizeIndex == 2 ? 2 : 1;
            ea.valid = need(ea.extensionWords);
            ea.value = sizeIndex == 2 ? int32_t(uint32_t(word(0)) << 16 | word(1))
                     : sizeIndex == 1 ? int16_t(word(0))
                                      : int8_t(word(0) & 0xFF);
            break;
        default:
            break;
    }
    return ea;
}

// Populate the concrete operand plans after the tracer has copied the
// instruction's extension words. The semantic switch lives in the IR layer;
// A64 and x64 only ask for an already-described operand by its encoding.
inline void describeEffectiveAddresses(Instr& in) {
    in.effectiveAddressCount = 0;
    const InstructionSemantics& s = in.semantics;
    const auto add = [&](OperandRole role, uint8_t mode, uint8_t reg,
                         uint8_t size, uint8_t offset) {
        if (in.effectiveAddressCount >= in.effectiveAddresses.size()) return uint8_t(0);
        DecodedEffectiveAddress ea =
            decodeEffectiveAddress(in, role, mode, reg, size, offset);
        in.effectiveAddresses[in.effectiveAddressCount++] = ea;
        return ea.extensionWords;
    };
    switch (s.operation) {
        case SemanticOp::Move: {
            const uint8_t used = add(OperandRole::Source, s.eaMode, s.eaReg,
                                     s.sizeIndex, 0);
            add(OperandRole::Destination, s.destinationMode, s.destinationReg,
                s.sizeIndex, used);
            break;
        }
        case SemanticOp::ImmediateAlu: {
            const uint8_t used = add(OperandRole::Source, 7, 4, s.sizeIndex, 0);
            add(OperandRole::Destination, s.eaMode, s.eaReg,
                s.sizeIndex, used);
            break;
        }
        case SemanticOp::Bit: {
            uint8_t used = 0;
            if (!s.dynamic) used = add(OperandRole::Source, 7, 4, 0, 0);
            add(OperandRole::Operand, s.eaMode, s.eaReg, s.sizeIndex, used);
            break;
        }
        case SemanticOp::Movem:
        case SemanticOp::Bitfield:
            add(OperandRole::Operand, s.eaMode, s.eaReg, s.sizeIndex, 1);
            break;
        case SemanticOp::Lea:
        case SemanticOp::Pea:
        case SemanticOp::JumpSubroutine:
        case SemanticOp::Jump:
            add(OperandRole::Control, s.eaMode, s.eaReg, s.sizeIndex, 0);
            break;
        case SemanticOp::AluEaToReg:
        case SemanticOp::AddressAlu:
            add(OperandRole::Source, s.eaMode, s.eaReg, s.sizeIndex, 0);
            break;
        case SemanticOp::AluRegToEa:
        case SemanticOp::AddSubQuick:
        case SemanticOp::SetCondition:
        case SemanticOp::Clear:
        case SemanticOp::Negate:
        case SemanticOp::Complement:
            add(OperandRole::Destination, s.eaMode, s.eaReg, s.sizeIndex, 0);
            break;
        case SemanticOp::Test:
            add(OperandRole::Operand, s.eaMode, s.eaReg, s.sizeIndex, 0);
            break;
        default:
            break;
    }
}

inline const DecodedEffectiveAddress* findEffectiveAddress(
    const Instr& in, uint8_t mode, uint8_t reg, uint8_t sizeIndex,
    uint8_t extensionOffset) {
    for (unsigned i = 0; i < in.effectiveAddressCount; i++) {
        const DecodedEffectiveAddress& ea = in.effectiveAddresses[i];
        if (ea.mode == mode && ea.reg == reg && ea.sizeIndex == sizeIndex &&
            ea.extensionOffset == extensionOffset)
            return &ea;
    }
    return nullptr;
}

inline uint32_t immediateValue(const Instr& in, uint8_t sizeIndex,
                               unsigned extensionOffset = 0) {
    return sizeIndex == 2 ? in.extensionLong(extensionOffset)
         : sizeIndex == 0 ? uint32_t(in.extensionWord(extensionOffset) & 0xFF)
                          : uint32_t(in.extensionWord(extensionOffset));
}

inline int32_t branchDisplacement(const Instr& in) {
    if (in.words == 1) return int8_t(in.opcode & 0xFF);
    if (in.words == 2) return int16_t(in.extensionWord(0));
    return int32_t(in.extensionLong(0));
}

inline void describeControlFlow(Instr& in) {
    ControlFlowPlan c;
    const InstructionSemantics& s = in.semantics;
    c.fallthrough = in.pc + uint32_t(in.words) * 2;
    c.condition = s.condition;

    const auto directDisplacement = [&] {
        c.target = uint32_t(in.pc + 2 + uint32_t(branchDisplacement(in)));
        c.targetKnown = true;
        c.valid = true;
    };
    switch (s.operation) {
        case SemanticOp::Branch:
            c.kind = s.condition == 0 ? ControlFlowKind::DirectBranch
                                      : ControlFlowKind::ConditionalBranch;
            c.conditional = s.condition != 0;
            directDisplacement();
            break;
        case SemanticOp::BranchSubroutine:
            c.kind = ControlFlowKind::DirectCall;
            c.pushesReturnAddress = true;
            c.returnAddress = c.fallthrough;
            directDisplacement();
            break;
        case SemanticOp::DecrementBranch:
            c.kind = ControlFlowKind::DecrementBranch;
            c.conditional = true;
            directDisplacement();
            break;
        case SemanticOp::JumpSubroutine:
        case SemanticOp::Jump: {
            const bool call = s.operation == SemanticOp::JumpSubroutine;
            c.kind = call ? ControlFlowKind::IndirectCall
                          : ControlFlowKind::IndirectJump;
            c.pushesReturnAddress = call;
            c.returnAddress = call ? c.fallthrough : 0;
            const DecodedEffectiveAddress* ea = findEffectiveAddress(
                in, s.eaMode, s.eaReg, s.sizeIndex, 0);
            if (!ea || !ea->valid || !ea->memory()) break;
            if (ea->kind == EffectiveAddressKind::AbsoluteWord ||
                ea->kind == EffectiveAddressKind::AbsoluteLong ||
                ea->kind == EffectiveAddressKind::PcDisplacement) {
                c.target = uint32_t(ea->value);
                c.targetKnown = true;
                c.kind = call ? ControlFlowKind::DirectCall
                              : ControlFlowKind::DirectJump;
            }
            c.valid = true;
            break;
        }
        case SemanticOp::ReturnSubroutine:
            c.kind = ControlFlowKind::Return;
            c.valid = true;
            break;
        default:
            break;
    }
    in.control = c;
}

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
    // Every 16-bit word of the block's footprint, [entryPc, exitPc), copied
    // out of the code window while tracing. A code generator needs the
    // extension words (displacements, immediates, absolute addresses) and
    // cannot reach the window itself — the window is engine state and can
    // be re-armed elsewhere between compiling and running.
    std::vector<uint16_t> code;

    // Word at logical address `pc`, or 0 outside the block's footprint.
    uint16_t word(uint32_t pc) const {
        const uint32_t i = (pc - entryPc) / 2;
        return (pc >= entryPc && i < code.size()) ? code[i] : 0;
    }

    // Native backends may consult the raw copy only for architectural queue
    // state beyond/around an instruction. Opcode extensions and EAs are
    // already materialised on Instr and must never be reconstructed here.
    uint16_t prefetchWord(uint32_t pc) const { return word(pc); }

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
                // RTS/TRAPV/RTR/MOVEC/JSR/JMP.
                //
                // LINK, UNLK and NOP are carved out, and that is a measured
                // decision rather than a tidy one. They transfer no control
                // and touch no SR/MMU/cache state, so the only reason to
                // exclude them was that the whole group was easier to
                // write — but they are 3.6 % of a real Mac OS workload and
                // they sit at EVERY function entry and exit, which is
                // exactly where straight-line code begins. Ending a block
                // on them cost far more than the rule saved.
                if ((op & 0xFFF8) == 0x4E50) return Kind::AddrCalc;   // LINK.W
                if ((op & 0xFFF8) == 0x4E58) return Kind::AddrCalc;   // UNLK
                if (op == 0x4E71) return Kind::AddrCalc;              // NOP
                // JSR and RTS terminate a block rather than being kept out
                // of one. They are 7 % of a real Mac OS workload, and every
                // one of them was both an interpreter round trip AND a
                // block boundary the linker could not cross.
                if ((op & 0xFFC0) == 0x4E80) return Kind::Branch;      // JSR <ea>
                if (op == 0x4E75) return Kind::Branch;                // RTS
                // JMP <ea> — a terminator SIMPLER than the JSR above (no
                // stack push), 0.7 % of the idle Finder in the 2026-07-30
                // census. The 68020 indexed modes keep their own decoder
                // problem, so only the plain EAs graduate from Unsafe.
                if ((op & 0xFFC0) == 0x4EC0) {
                    const int m = (op >> 3) & 7, r = op & 7;
                    if (m == 2 || m == 5 ||
                        (m == 7 && (r == 0 || r == 1 || r == 2)))
                        return Kind::Branch;
                }
                return Kind::Unsafe;
            }
            // MOVE SR,Dn is a privileged READ on 68010+: a successful trace
            // is necessarily supervisor-mode, and it changes no mapping or
            // execution state. Memory destinations remain conservative.
            if ((op & 0xFFF8) == 0x40C0) return Kind::Alu;
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
            if ((op & 0xF0F8) == 0x50C8) return Kind::Branch;        // DBcc
            if ((op & 0xF0F8) == 0x50F8) return Kind::Unsafe;        // TRAPcc
            if ((op & 0x00C0) == 0x00C0) return Kind::Cond;          // Scc
            return Kind::Alu;                                        // ADDQ / SUBQ
        }

        case 0x6000:
            // Bcc/BRA are the one control transfer a backend can own: the
            // target is a compile-time constant, so a backward branch into
            // the same block becomes an internal jump and the loop never
            // leaves generated code. BSR ($61xx) also lands here: it stacks
            // a return address, which the x64 backend does model
            // (emitSubroutine), and it is 7 % of a real workload together
            // with JSR/RTS — an earlier version of this comment said it
            // "stays out", which the return below has contradicted since
            // the subroutine emitters landed.
            return Kind::Branch;                     // Bcc / BRA / BSR

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

// A block must stop BEFORE an Unsafe instruction, and AFTER a Branch: the
// branch is the block's terminator, not something that follows it.
inline bool endsBlock(Kind k) { return k == Kind::Unsafe; }
inline bool endsBlockAfter(Kind k) { return k == Kind::Branch; }

// Encoded length of a Kind::Branch, in 16-bit words. A branch is the one
// instruction whose length the tracer cannot deduce from the pc delta,
// because the pc went somewhere else entirely.
inline uint32_t branchWords(uint16_t op) {
    if ((op & 0xF000) == 0x6000) {                 // Bcc / BRA / BSR
        const uint8_t d = uint8_t(op & 0xFF);
        return d == 0x00 ? 2 : d == 0xFF ? 3 : 1;  // .W : .L (68020) : .B
    }
    if (op == 0x4E75) return 1;                    // RTS
    if ((op & 0xFF80) == 0x4E80) {                 // JSR / JMP <ea>
        const int mode = (op >> 3) & 7, reg = op & 7;
        if (mode == 2) return 1;                   // (An)
        if (mode == 5 || mode == 6) return 2;      // d16(An) / indexed
        if (mode == 7) {
            if (reg == 0) return 2;                // (xxx).W
            if (reg == 1) return 3;                // (xxx).L
            return 2;                              // d16(PC) / PC-indexed
        }
        return 1;
    }
    return 2;                                      // DBcc: opcode + disp16
}

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
