// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── AArch64 code generator ───────────────────────────────────────────────
// Native 68040/68030 ALU, MOVE, effective-address and control-flow paths,
// with the same exact per-instruction fallback contract as the x86-64 JIT.

#include "JitBackendA64.h"

#include "../JitCodeBuffer.h"
#include "../JitConfig.h"
#include "../JitCost.h"
#include "../JitEaPlan.h"
#include "Moira.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

namespace jit {
namespace {

using Layout = moira::Moira::PomJitLayout;

// Code is allocated in independently owned arenas. A full arena no longer
// forces the Engine to discard and recompile every live block; Compiled
// wrappers retain their arena and the mapping disappears automatically when
// its last block is evicted. Sixteen MiB limits fragmentation without making
// W^X transitions a per-block mapping operation.
constexpr size_t kCodeArenaBytes = 16u * 1024 * 1024;

MemoryProofOptions proofOptions(const Layout& L) {
    MemoryProofOptions o;
    const int thunks = accessThunkMode();
    o.exactReads = thunks >= 1;
    // Only this backend's 040 MOVE lowering proves the destination mapping
    // before consuming an exact/MMIO source. The 030 retained-cache oracle
    // rejected the wider token, and x64 keeps its direct-only pair path.
    o.preflightedExactSource = !L.is030 && thunks >= 1;
    // This backend's exact write replay is the proved 68030 LASTWRITE
    // subset. 040 stores use the native copyback-line protocol instead.
    o.exactWrites = L.is030 && thunks >= 2;
    o.cacheReads = L.cache040Live && cache040LineReadsEnabled();
    o.cacheWrites = L.cache040Live && cache040LineWritesEnabled();
    o.cachePairs = L.cache040Live && cache040LinePairsEnabled();
    o.restartableWriteRequired = L.is030;
    return o;
}

static_assert(sizeof(moira::Moira::PomJitCache040Entry) == 32);
static_assert(offsetof(moira::Moira::PomJitCache040Entry, physicalTag) == 4);
static_assert(offsetof(moira::Moira::PomJitCache040Entry, generation) == 8);
static_assert(offsetof(moira::Moira::PomJitCache040Entry, line) == 16);
static_assert(offsetof(moira::Cache040::Line, tag) == 0);
static_assert(offsetof(moira::Cache040::Line, valid) == 4);
static_assert(offsetof(moira::Cache040::Line, dirty) == 5);
static_assert(offsetof(moira::Cache040::Line, data) == 6);

extern "C" void pom68kA64Sync(moira::Moira* cpu, uint32_t cycles) noexcept {
    cpu->pomJitSync(int(cycles));
}
extern "C" int pom68kA64Step(moira::Moira* cpu) noexcept {
    if (!cpu->pomJitCovers(cpu->getPC()) || !cpu->pomJitIdle()) return -1;
    return cpu->pomJitExecOne() ? 1 : 0;
}
// The exact data thunks. `pcWords` (fetchWords << 32 | pc, 0 when the
// block emits no i-cache charge) is the peripheral-phase access-clock
// alignment of JIT_BRINGUP § C.4nonies, ported from pom68kJitRead/Write
// (2026-08-22): an I/O access forces a peripheral flush at the access
// clock, and the interpreter reaches the same access with the 030 fetch
// penalty already charged while generated code charges on the success path
// after the body. Bias the clock by the read-only peek for the access alone
// — success or fault, the bias is gone before anything else runs, so the
// success-path charge and the fault re-run both stay exactly as they were.
// Until this port the backend closed the same class with
// `guardIcacheHits`, a runtime replay of every thunk-capable instruction
// whose fetch could still miss; the bias keeps those instructions native.
extern "C" int pom68kA64Read(moira::Moira* cpu, uint32_t addr,
                              uint32_t bytes, uint32_t* out,
                              uint64_t pcWords) noexcept {
    const moira::i64 bias = pcWords
        ? cpu->pomJitIcachePeekPenalty(uint32_t(pcWords), int(pcWords >> 32))
        : 0;
    if (bias) cpu->pomJitBiasClock(bias);
    const int ok = cpu->pomJitReadData(addr, int(bytes), *out) ? 1 : 0;
    if (bias) cpu->pomJitBiasClock(-bias);
    return ok;
}
extern "C" int pom68kA64Write(moira::Moira* cpu, uint32_t addr,
                               uint32_t bytes, uint32_t value,
                               uint64_t pcWords) noexcept {
    const moira::i64 bias = pcWords
        ? cpu->pomJitIcachePeekPenalty(uint32_t(pcWords), int(pcWords >> 32))
        : 0;
    if (bias) cpu->pomJitBiasClock(bias);
    const int ok = cpu->pomJitWriteData(addr, int(bytes), value) ? 1 : 0;
    if (bias) cpu->pomJitBiasClock(-bias);
    return ok;
}

// JSR/JMP's target-word read (Moira::pomJitReadProg): what the interpreted
// control transfer leaves in irc. The 030 has an explicit mode-5 path; a
// 040 full-indirect target needs the same runtime program-space read. 1 =
// `*out` holds the word, 0 = the untouched instruction must be replayed.
extern "C" int pom68kA64ReadProg(moira::Moira* cpu, uint32_t addr,
                                 uint32_t* out) noexcept {
    uint16_t w = 0;
    const int ok = cpu->pomJitReadProg(addr, w) ? 1 : 0;
    *out = w;
    return ok;
}

struct Frame {
    int64_t clockTarget;
    uint32_t instrs;
    uint32_t exit;
    void* dtlbSelf;
    uint8_t* (*dtlbFill)(void*, uint32_t, int);
    const uint8_t* guardHit;
    uint32_t scratch;
    uint32_t saveA;
    uint32_t saveV;
    uint32_t slowInstrs;
    const void* periphClock;
    void* linkTable;
    uint32_t value;
    uint32_t pad;
    uint64_t* slowStaticHisto;
    uint64_t* slowRuntimeHisto;
    int64_t savedClock;
    WriteObserver observeWrite;
    void* observeWriteSelf;
    uint64_t observedHostPointer;
    uint64_t* slowRuntimeReasonHisto;
    const uint32_t* dtlbFillReason;
    uint32_t runtimeReason;
    uint32_t pad2;
    RuntimeAddressObserver runtimeAddressObserver;
    void* runtimeAddressSelf;
    uint32_t runtimeAddress;
    uint32_t runtimeCodeMask;
    uint32_t runtimeBytes;
    uint32_t runtimeWrite;
    PeriphDue periphDue;
    uint64_t progHost;      // +192 a proven push pointer across the JSR target read
};
static_assert(offsetof(Frame, linkTable) == 64);
static_assert(offsetof(Frame, progHost) == 192);
static_assert(offsetof(Frame, slowStaticHisto) == 80);
static_assert(offsetof(Frame, slowRuntimeHisto) == 88);
static_assert(offsetof(Frame, savedClock) == 96);
static_assert(offsetof(Frame, observeWrite) == 104);
static_assert(offsetof(Frame, observeWriteSelf) == 112);
static_assert(offsetof(Frame, observedHostPointer) == 120);
static_assert(offsetof(Frame, slowRuntimeReasonHisto) == 128);
static_assert(offsetof(Frame, dtlbFillReason) == 136);
static_assert(offsetof(Frame, runtimeReason) == 144);
static_assert(offsetof(Frame, runtimeAddressObserver) == 152);
static_assert(offsetof(Frame, runtimeAddressSelf) == 160);
static_assert(offsetof(Frame, runtimeAddress) == 168);
static_assert(offsetof(Frame, runtimeCodeMask) == 172);
static_assert(offsetof(Frame, runtimeBytes) == 176);
static_assert(offsetof(Frame, runtimeWrite) == 180);
static_assert(offsetof(Frame, periphDue) == 184);

// Set once at the start of every compile. Code generation is synchronous;
// thread_local keeps independent machine threads from sharing this option.
thread_local bool gRuntimeReasonHisto = false;
// Compile-time choice only. When pacing is active x19 caches the absolute
// peripheral deadline (or the fixed-batch baseline) across generated
// instructions and linked blocks. Every helper that can advance devices
// refreshes it before generated execution resumes.
thread_local bool gPacingDeadline = false;
thread_local bool gDirectPeriphDue = false;
// x26 mirrors x64's compact live-state ABI when enabled: architectural
// XNZVC in bits 4..0 and the retired count in bits 63..8. Bits 7..5 isolate
// carries. Helpers and exits publish the canonical Moira flag bytes.
thread_local bool gPackedCcr = false;
// Up to two block-local, read-only guest registers live in x27/x28. The
// selector rejects every register an instruction in the block may write;
// memory therefore remains canonical and direct links only need two loads.
thread_local int gCachedGuest[2] = {-1, -1};
thread_local LinkCellLookup gLinkCellLookup = nullptr;
thread_local void* gLinkCellSelf = nullptr;
// The access-thunk clock-alignment operand of the instruction being
// emitted (see pom68kA64Read): its traced fetch stream, packed for the
// thunk's read-only peek. The compile loop sets it per instruction and
// memLoadGuest/memStoreGuest hand it to the thunk as the fifth argument. 0
// — no bias — whenever the block emits no i-cache charge or the IR carries
// no traced count: the bias must model exactly what the end-of-body charge
// will charge, or it is a new skew, not an alignment.
thread_local uint64_t gAccessPcWords = 0;

// Diagnosis only — POM68K_JIT_WATCH_OPCODE (parsed by JitConfig, the
// backend reads no environment): when the compile loop hands one of the
// watched opcodes to the fallback stub, print the admission inputs the
// emitters consulted, once per pc and at most 64 lines, so a "block
// fallback census" row can be turned into WHICH check refused it without
// guessing from the source. The 2026-08-23 MOVEM/JSR census is the first
// customer.
thread_local int gWatchPrinted = 0;

void watchRefusal(const Layout& L, const BlockIr& ir, const Instr& in,
                  const char* stage) {
    if (!watchOpcodeWanted(in.opcode) || gWatchPrinted >= 64) return;
    gWatchPrinted++;
    const MemoryProofPlan p = memoryProofPlan(in.memory, proofOptions(L));
    std::fprintf(stderr,
        "[jit-watch] %04X pc=%08X %s words=%u cyc=%u base=%u ic=%u postEx=%u "
        "fetch=%u queue=%d ird=%04X irc=%04X next=%08X kind=%d op=%d "
        "ea=%d/%d size=%d ext=%u proto=%d preflight=%02X thunk=%02X "
        "cache=%02X lastWrite=%u restartable=%d order=%d super=%d "
        "ext0=%04X action=%u bf4=%d\n",
        in.opcode, in.pc, stage, unsigned(in.words), unsigned(in.cycles),
        unsigned(in.baseCycles), unsigned(in.icacheCycles),
        unsigned(in.postExceptionCycles), unsigned(in.fetchWords),
        int(in.terminalQueueValid), in.terminalIrd, in.terminalIrc,
        in.observedNextPc, int(in.kind), int(in.semantics.operation),
        int(in.semantics.eaMode), int(in.semantics.eaReg),
        int(in.semantics.sizeIndex), unsigned(in.extensionCount),
        int(p.protocol), p.preflightMask, p.exactThunkMask, p.cacheMask,
        unsigned(p.lastWrite), int(p.lastWriteRestartable),
        int(in.memory.order), int(ir.super), in.extensionWord(0),
        unsigned(in.semantics.action),
        int(memoryBitfieldFitsLongword(in.extensionWord(0))));
}

// Minimal fixed-width assembler. Every memory operand is an unsigned scaled
// immediate off x0 (Moira*) or x1 (Frame*); layout() validates the offsets.
class Asm {
public:
    enum Cond : uint32_t { EQ = 0, NE = 1, CS = 2, CC = 3, MI = 4,
                           VS = 6, HI = 8, LS = 9, GE = 10, LT = 11 };
    struct Fix {
        size_t at;
        int label;
        bool conditional;
        uint32_t cond;
        unsigned rt = 0;
    };
    struct LiteralFix { size_t at; unsigned rt; uint64_t value; };
    struct Mark { size_t code, labels, fixes, literals; };

    Mark mark() const {
        return {code_.size(), labels_.size(), fixes_.size(), literals_.size()};
    }
    void rewind(Mark m) {
        code_.resize(m.code); labels_.resize(m.labels); fixes_.resize(m.fixes);
        literals_.resize(m.literals);
    }

    // Labels are allocated before the instruction body so every emitter can
    // request an exact fallback without having to predict whether it will
    // need one. Most register-only instructions never do. Knowing whether a
    // fixup actually targets a label lets the backend omit their otherwise
    // unreachable cold interpreter stubs instead of spending executable
    // memory and host I-cache on them.
    bool referenced(int l) const {
        return std::any_of(fixes_.begin(), fixes_.end(),
                           [l](const Fix& f) { return f.label == l; });
    }

    int label() { labels_.push_back(size_t(-1)); return int(labels_.size() - 1); }
    void bind(int l) { labels_[size_t(l)] = code_.size(); }
    void emit(uint32_t w) { code_.push_back(w); }

    void bCond(Cond c, int l) {
        fixes_.push_back({code_.size(), l, true, uint32_t(c)});
        emit(0x54000000u | uint32_t(c));
    }
    void b(int l) {
        fixes_.push_back({code_.size(), l, false, 0});
        emit(0x14000000u);
    }

    void movW(unsigned rd, uint32_t v) {
        emit(0x52800000u | ((v & 0xFFFFu) << 5) | rd);
        if (v >> 16) emit(0x72A00000u | ((v >> 16) << 5) | rd);
    }
    void movX(unsigned rd, uint64_t v) {
        emit(0xD2800000u | (uint32_t(v & 0xFFFFu) << 5) | rd);
        for (unsigned hw = 1; hw < 4; hw++) {
            const uint32_t part = uint32_t((v >> (16 * hw)) & 0xFFFFu);
            if (part)
                emit(0xF2800000u | (hw << 21) | (part << 5) | rd);
        }
    }
    void address(unsigned rd, unsigned rn, uint32_t off) {
        if (off < 4096) {
            emit(0x91000000u | (off << 10) | (rn << 5) | rd); // ADD imm
        } else {
            movX(rd, off);
            emit(0x8B000000u | (rd << 16) | (rn << 5) | rd);  // ADD reg
        }
    }
    void ldrX(unsigned rt, unsigned rn, uint32_t off) {
        if ((off & 7) || off / 8 >= 4096) {
            address(15, rn, off); emit(0xF9400000u | (15 << 5) | rt); return;
        }
        emit(0xF9400000u | ((off / 8) << 10) | (rn << 5) | rt);
    }
    void ldrW(unsigned rt, unsigned rn, uint32_t off) {
        if ((off & 3) || off / 4 >= 4096) {
            address(15, rn, off); emit(0xB9400000u | (15 << 5) | rt); return;
        }
        emit(0xB9400000u | ((off / 4) << 10) | (rn << 5) | rt);
    }
    void ldrH(unsigned rt, unsigned rn, uint32_t off) {
        if ((off & 1) || off / 2 >= 4096) {
            address(15, rn, off); emit(0x79400000u | (15 << 5) | rt); return;
        }
        emit(0x79400000u | ((off / 2) << 10) | (rn << 5) | rt);
    }
    void ldrB(unsigned rt, unsigned rn, uint32_t off) {
        if (off >= 4096) {
            address(15, rn, off); emit(0x39400000u | (15 << 5) | rt); return;
        }
        emit(0x39400000u | (off << 10) | (rn << 5) | rt);
    }
    void strW(unsigned rt, unsigned rn, uint32_t off) {
        if ((off & 3) || off / 4 >= 4096) {
            address(15, rn, off); emit(0xB9000000u | (15 << 5) | rt); return;
        }
        emit(0xB9000000u | ((off / 4) << 10) | (rn << 5) | rt);
    }
    void strX(unsigned rt, unsigned rn, uint32_t off) {
        if ((off & 7) || off / 8 >= 4096) {
            address(15, rn, off); emit(0xF9000000u | (15 << 5) | rt); return;
        }
        emit(0xF9000000u | ((off / 8) << 10) | (rn << 5) | rt);
    }
    void strH(unsigned rt, unsigned rn, uint32_t off) {
        if ((off & 1) || off / 2 >= 4096) {
            address(15, rn, off); emit(0x79000000u | (15 << 5) | rt); return;
        }
        emit(0x79000000u | ((off / 2) << 10) | (rn << 5) | rt);
    }
    void strB(unsigned rt, unsigned rn, uint32_t off) {
        if (off >= 4096) {
            address(15, rn, off); emit(0x39000000u | (15 << 5) | rt); return;
        }
        emit(0x39000000u | (off << 10) | (rn << 5) | rt);
    }
    void cmpX(unsigned rn, unsigned rm) {
        emit(0xEB00001Fu | (rm << 16) | (rn << 5));
    }
    void cmpW(unsigned rn, unsigned rm) {
        emit(0x6B00001Fu | (rm << 16) | (rn << 5));
    }
    void cmpXZero(unsigned rn) { emit(0xF100001Fu | (rn << 5)); }
    void cmpWZero(unsigned rn) { emit(0x7100001Fu | (rn << 5)); }
    void csetW(unsigned rd, Cond c) {
        // CSET Wd,cond == CSINC Wd,WZR,WZR,invert(cond).
        emit(0x1A9F07E0u | (((uint32_t(c) ^ 1u) & 15u) << 12) | rd);
    }
    void addW(unsigned rd, unsigned rn, unsigned rm) {
        emit(0x0B000000u | (rm << 16) | (rn << 5) | rd);
    }
    void addsW(unsigned rd, unsigned rn, unsigned rm) {
        emit(0x2B000000u | (rm << 16) | (rn << 5) | rd);
    }
    void subW(unsigned rd, unsigned rn, unsigned rm) {
        emit(0x4B000000u | (rm << 16) | (rn << 5) | rd);
    }
    void subsW(unsigned rd, unsigned rn, unsigned rm) {
        emit(0x6B000000u | (rm << 16) | (rn << 5) | rd);
    }
    void subX(unsigned rd, unsigned rn, unsigned rm) {
        emit(0xCB000000u | (rm << 16) | (rn << 5) | rd);
    }
    void addImmW(unsigned rd, unsigned rn, unsigned imm) {
        emit(0x11000000u | ((imm & 0xFFFu) << 10) | (rn << 5) | rd);
    }
    void addImmX(unsigned rd, unsigned rn, unsigned imm) {
        emit(0x91000000u | ((imm & 0xFFFu) << 10) | (rn << 5) | rd);
    }
    void subImmX(unsigned rd, unsigned rn, unsigned imm) {
        emit(0xD1000000u | ((imm & 0xFFFu) << 10) | (rn << 5) | rd);
    }
    void subImmW(unsigned rd, unsigned rn, unsigned imm) {
        emit(0x51000000u | ((imm & 0xFFFu) << 10) | (rn << 5) | rd);
    }
    void addX(unsigned rd, unsigned rn, unsigned rm) {
        emit(0x8B000000u | (rm << 16) | (rn << 5) | rd);
    }
    void movRegW(unsigned rd, unsigned rn) {
        emit(0x2A0003E0u | (rn << 16) | rd);
    }
    void movRegX(unsigned rd, unsigned rn) {
        emit(0xAA0003E0u | (rn << 16) | rd);
    }
    void andW(unsigned rd, unsigned rn, unsigned rm) {
        emit(0x0A000000u | (rm << 16) | (rn << 5) | rd);
    }
    void andX(unsigned rd, unsigned rn, unsigned rm) {
        emit(0x8A000000u | (rm << 16) | (rn << 5) | rd);
    }
    void orrW(unsigned rd, unsigned rn, unsigned rm) {
        emit(0x2A000000u | (rm << 16) | (rn << 5) | rd);
    }
    void orrX(unsigned rd, unsigned rn, unsigned rm) {
        emit(0xAA000000u | (rm << 16) | (rn << 5) | rd);
    }
    void eorW(unsigned rd, unsigned rn, unsigned rm) {
        emit(0x4A000000u | (rm << 16) | (rn << 5) | rd);
    }
    void mvnW(unsigned rd, unsigned rm) {
        emit(0x2A2003E0u | (rm << 16) | rd);
    }
    void lsrW(unsigned rd, unsigned rn, unsigned shift) {
        emit(0x53000000u | (shift << 16) | (31u << 10) | (rn << 5) | rd);
    }
    void ubfxW(unsigned rd, unsigned rn, unsigned lsb, unsigned width) {
        // UBFX Wd,Wn,#lsb,#width == UBFM Wd,Wn,#lsb,#(lsb+width-1).
        emit(0x53000000u | ((lsb & 31u) << 16) |
             (((lsb + width - 1u) & 31u) << 10) | (rn << 5) | rd);
    }
    void lslW(unsigned rd, unsigned rn, unsigned shift) {
        const unsigned immr = (32 - shift) & 31;
        const unsigned imms = 31 - shift;
        emit(0x53000000u | (immr << 16) | (imms << 10) | (rn << 5) | rd);
    }
    void asrW(unsigned rd, unsigned rn, unsigned shift) {
        emit(0x13000000u | (shift << 16) | (31u << 10) | (rn << 5) | rd);
    }
    void asrVarW(unsigned rd, unsigned rn, unsigned rm) {
        emit(0x1AC02800u | (rm << 16) | (rn << 5) | rd);
    }
    void lsrX(unsigned rd, unsigned rn, unsigned shift) {
        emit(0xD3400000u | (shift << 16) | (63u << 10) | (rn << 5) | rd);
    }
    void lslX(unsigned rd, unsigned rn, unsigned shift) {
        const unsigned immr = (64 - shift) & 63;
        const unsigned imms = 63 - shift;
        emit(0xD3400000u | (immr << 16) | (imms << 10) | (rn << 5) | rd);
    }
    void lslVarW(unsigned rd, unsigned rn, unsigned rm) {
        emit(0x1AC02000u | (rm << 16) | (rn << 5) | rd);
    }
    void lsrVarW(unsigned rd, unsigned rn, unsigned rm) {
        emit(0x1AC02400u | (rm << 16) | (rn << 5) | rd);
    }
    void sxtB(unsigned rd, unsigned rn) {
        emit(0x13001C00u | (rn << 5) | rd);
    }
    void sxtH(unsigned rd, unsigned rn) {
        emit(0x13003C00u | (rn << 5) | rd);
    }
    void rorW(unsigned rd, unsigned rn, unsigned shift) {
        emit(0x13800000u | (rn << 16) | (shift << 10) | (rn << 5) | rd);
    }
    void rorVarW(unsigned rd, unsigned rn, unsigned rm) {
        emit(0x1AC02C00u | (rm << 16) | (rn << 5) | rd);
    }
    void rev16W(unsigned rd, unsigned rn) {
        emit(0x5AC00400u | (rn << 5) | rd);
    }
    void rev32W(unsigned rd, unsigned rn) {
        emit(0x5AC00800u | (rn << 5) | rd);
    }
    void clzW(unsigned rd, unsigned rn) {
        emit(0x5AC01000u | (rn << 5) | rd);
    }
    void cbnzW(unsigned rt, int l) {
        fixes_.push_back({code_.size(), l, true, 16, rt});
        emit(0x35000000u | rt);
    }
    void cbzW(unsigned rt, int l) {
        fixes_.push_back({code_.size(), l, true, 17, rt});
        emit(0x34000000u | rt);
    }
    void cbzX(unsigned rt, int l) {
        fixes_.push_back({code_.size(), l, true, 18, rt});
        emit(0xB4000000u | rt);
    }
    void ldrLiteralX(unsigned rt, uint64_t value) {
        literals_.push_back({code_.size(), rt, value});
        emit(0x58000000u | rt);          // imm19 patched by finish()
    }
    void blr(unsigned rn) { emit(0xD63F0000u | (rn << 5)); }
    void br(unsigned rn) { emit(0xD61F0000u | (rn << 5)); }

    bool finish() {
        for (const Fix& f : fixes_) {
            if (f.label < 0 || size_t(f.label) >= labels_.size() ||
                labels_[size_t(f.label)] == size_t(-1)) return false;
            const int64_t d = int64_t(labels_[size_t(f.label)]) - int64_t(f.at);
            if (f.conditional) {
                if (d < -(1 << 18) || d >= (1 << 18)) return false;
                const uint32_t imm = uint32_t(d) & 0x7FFFFu;
                if (f.cond == 16) code_[f.at] = 0x35000000u | (imm << 5) | f.rt;
                else if (f.cond == 17) code_[f.at] = 0x34000000u | (imm << 5) | f.rt;
                else if (f.cond == 18) code_[f.at] = 0xB4000000u | (imm << 5) | f.rt;
                else code_[f.at] = 0x54000000u | (imm << 5) | f.cond;
            } else {
                if (d < -(1 << 25) || d >= (1 << 25)) return false;
                code_[f.at] = 0x14000000u | (uint32_t(d) & 0x03FFFFFFu);
            }
        }
        // Literal pool is unreachable after the generated RET. Loading a
        // stable edge-cell address this way is one instruction instead of
        // a 3-4 instruction MOVZ/MOVK sequence at every constant transfer.
        for (const LiteralFix& l : literals_) {
            if (code_.size() & 1u) emit(0xD503201Fu); // 8-byte alignment
            const size_t literalAt = code_.size();
            const int64_t d = int64_t(literalAt) - int64_t(l.at);
            if (d < -(1 << 18) || d >= (1 << 18)) return false;
            code_[l.at] = 0x58000000u |
                ((uint32_t(d) & 0x7FFFFu) << 5) | l.rt;
            code_.push_back(uint32_t(l.value));
            code_.push_back(uint32_t(l.value >> 32));
        }
        return true;
    }

    const uint8_t* bytes() const {
        return reinterpret_cast<const uint8_t*>(code_.data());
    }
    size_t byteSize() const { return code_.size() * sizeof(uint32_t); }

private:
    std::vector<uint32_t> code_;
    std::vector<size_t> labels_;
    std::vector<Fix> fixes_;
    std::vector<LiteralFix> literals_;
};

void loadQueueLive(Asm& a, const Layout& L, unsigned scratch = 10);
void spillQueueLive(Asm& a, const Layout& L, unsigned scratch = 12);

void reloadPacingDeadline(Asm& a) {
    if (gPacingDeadline) {
        a.ldrX(10, 1, 56);              // Frame::periphClock
        a.ldrX(19, 10, 0);
    }
}

void reloadGeneratedState(Asm& a, const Layout& L) {
    // Native instructions admitted by this backend cannot change Moira's
    // execution-control flags. Helpers can, through device catch-up/IRQ or
    // an interpreted fallback, so refresh the cached boundary state after
    // each such call. x22 is already callee-saved by the generated ABI.
    a.ldrW(22, 0, L.flags);
    reloadPacingDeadline(a);
}

struct IcacheShadow {
    uint32_t tag[16] {};
    uint8_t valid[16] {};
    bool seen[16] {};
};

// The 68040 path already keeps the guest clock in a callee-saved host
// register across directly linked blocks. Do the same for the two hot 68030
// i-cache counters: publishing fetches/hits through memory for every native
// guest instruction cost four loads/stores on top of the actual cache model.
// x23/x24 are part of the linked-block ABI and are spilled before anything
// that can observe CPU state, then on every generated-code exit. Misses stay
// memory-resident because they are updated only on the cold miss edge.
void spillIcacheCounters(Asm& a, const Layout& L) {
    a.strX(23, 0, L.icFetches);
    a.strX(24, 0, L.icHits);
}

void reloadIcacheCounters(Asm& a, const Layout& L) {
    a.ldrX(23, 0, L.icFetches);
    a.ldrX(24, 0, L.icHits);
}

bool icacheCountersLive(const Layout& L) {
    return L.icLive && icacheEmitEnabled();
}

// Compact emitted 68030 i-cache model. Fetches and optimistic hits are
// aggregated once per instruction; each actual miss corrects the hit count,
// updates its compile-time-known line and adds the runtime miss penalty.
void chargeIcache(Asm& a, const Layout& L, const BlockIr& ir,
                  const Instr& in, IcacheShadow& shadow,
                  uint32_t exactFetchWords = 0) {
    const uint32_t words = exactFetchWords ? exactFetchWords
                         : in.fetchWords   ? uint32_t(in.fetchWords)
                                           : uint32_t(in.words) + 1;
    const uint32_t sup = ir.super ? 0x80000000u : 0u;
    a.addImmX(23, 23, words);

    const int disabled = a.label();
    a.cbzW(25, disabled);
    a.addImmX(24, 24, words);

    for (uint32_t w = 0; w < words; w++) {
        const uint32_t addr = in.pc + w * 2;
        const int line = int((addr >> 4) & 15);
        const uint32_t tag = (addr >> 8) | sup;
        const uint8_t bit = uint8_t(1u << ((addr >> 2) & 3));
        if (shadow.seen[line] && shadow.tag[line] == tag &&
            (shadow.valid[line] & bit)) continue;

        const int miss = a.label(), done = a.label(), sameTag = a.label();
        a.ldrW(9, 0, L.icTag + uint32_t(line) * 4); a.movW(10, tag);
        a.cmpW(9, 10); a.bCond(Asm::NE, miss);
        a.ldrB(9, 0, L.icValid + uint32_t(line));
        a.ubfxW(9, 9, std::countr_zero(bit), 1); a.cbnzW(9, done);

        a.bind(miss);
        a.ldrW(9, 0, L.icTag + uint32_t(line) * 4); a.movW(10, tag);
        a.cmpW(9, 10); a.bCond(Asm::EQ, sameTag);
        a.strW(10, 0, L.icTag + uint32_t(line) * 4);
        a.movW(9, 0); a.strB(9, 0, L.icValid + uint32_t(line));
        a.bind(sameTag);
        a.ldrB(9, 0, L.icValid + uint32_t(line)); a.movW(10, bit);
        a.orrW(9, 9, 10); a.strB(9, 0, L.icValid + uint32_t(line));
        a.subImmX(24, 24, 1);
        a.ldrX(9, 0, L.icMisses); a.addImmX(9, 9, 1); a.strX(9, 0, L.icMisses);
        a.ldrW(9, 0, L.icPenalty); a.addX(21, 21, 9);
        a.bind(done);

        if (!shadow.seen[line] || shadow.tag[line] != tag) {
            shadow.tag[line] = tag; shadow.valid[line] = 0; shadow.seen[line] = true;
        }
        shadow.valid[line] |= bit;
    }
    a.bind(disabled);
}

// One path-specific fetch, used by the fall-through of a conditional Bcc.W.
// It deliberately does not advance the compile-time shadow: the taken path
// does not perform this fetch, so no later fold may rely on the update.
void chargeIcacheExtraWord(Asm& a, const Layout& L, const BlockIr& ir,
                           uint32_t addr) {
    const uint32_t sup = ir.super ? 0x80000000u : 0u;
    const int line = int((addr >> 4) & 15);
    const uint32_t tag = (addr >> 8) | sup;
    const uint8_t bit = uint8_t(1u << ((addr >> 2) & 3));

    a.addImmX(23, 23, 1);
    const int disabled = a.label();
    a.cbzW(25, disabled);
    a.addImmX(24, 24, 1);

    const int miss = a.label(), done = a.label(), sameTag = a.label();
    a.ldrW(9, 0, L.icTag + uint32_t(line) * 4); a.movW(10, tag);
    a.cmpW(9, 10); a.bCond(Asm::NE, miss);
    a.ldrB(9, 0, L.icValid + uint32_t(line));
    a.ubfxW(9, 9, std::countr_zero(bit), 1); a.cbnzW(9, done);

    a.bind(miss);
    a.ldrW(9, 0, L.icTag + uint32_t(line) * 4); a.movW(10, tag);
    a.cmpW(9, 10); a.bCond(Asm::EQ, sameTag);
    a.strW(10, 0, L.icTag + uint32_t(line) * 4);
    a.movW(9, 0); a.strB(9, 0, L.icValid + uint32_t(line));
    a.bind(sameTag);
    a.ldrB(9, 0, L.icValid + uint32_t(line)); a.movW(10, bit);
    a.orrW(9, 9, 10); a.strB(9, 0, L.icValid + uint32_t(line));
    a.subImmX(24, 24, 1);
    a.ldrX(9, 0, L.icMisses); a.addImmX(9, 9, 1); a.strX(9, 0, L.icMisses);
    a.ldrW(9, 0, L.icPenalty); a.addX(21, 21, 9);
    a.bind(done);
    a.bind(disabled);
}

// A statically refused instruction is executed by Moira and therefore makes
// the same cache-line transitions as the emitted charge would. Advance only
// the compiler shadow so later folded hits remain justified; emit no guest
// state change here, because the fallback may still be declined at a budget
// boundary before the instruction actually runs.
void shadowIcache(const BlockIr& ir, const Instr& in, IcacheShadow& shadow,
                  uint32_t exactFetchWords = 0) {
    const uint32_t words = exactFetchWords ? exactFetchWords
                         : in.fetchWords   ? uint32_t(in.fetchWords)
                                           : uint32_t(in.words) + 1;
    const uint32_t sup = ir.super ? 0x80000000u : 0u;
    for (uint32_t w = 0; w < words; w++) {
        const uint32_t addr = in.pc + w * 2;
        const int line = int((addr >> 4) & 15);
        const uint32_t tag = (addr >> 8) | sup;
        const uint8_t bit = uint8_t(1u << ((addr >> 2) & 3));
        if (!shadow.seen[line] || shadow.tag[line] != tag) {
            shadow.tag[line] = tag;
            shadow.valid[line] = 0;
            shadow.seen[line] = true;
        }
        shadow.valid[line] |= bit;
    }
}

uint32_t regOff(const Layout& L, bool address, unsigned n) {
    return (address ? L.a : L.d) + n * 4;
}

int cachedHostRegister(bool address, unsigned n) {
    const int guest = int(n) + (address ? 8 : 0);
    for (int slot = 0; slot < 2; slot++)
        if (gCachedGuest[slot] == guest) return 27 + slot;
    return -1;
}

void loadGuestRegister(Asm& a, const Layout& L, unsigned rd, bool address,
                       unsigned n, int bits = 32) {
    const int host = cachedHostRegister(address, n);
    if (host >= 0) {
        if (bits == 32) a.movRegW(rd, unsigned(host));
        else a.ubfxW(rd, unsigned(host), 0, unsigned(bits));
        return;
    }
    const uint32_t off = regOff(L, address, n);
    if (bits == 8) a.ldrB(rd, 0, off);
    else if (bits == 16) a.ldrH(rd, 0, off);
    else a.ldrW(rd, 0, off);
}

void reloadGuestRegisterCache(Asm& a, const Layout& L) {
    for (int slot = 0; slot < 2; slot++) {
        const int guest = gCachedGuest[slot];
        if (guest < 0) continue;
        a.ldrW(unsigned(27 + slot), 0,
               regOff(L, guest >= 8, unsigned(guest & 7)));
    }
}

uint16_t guestRegisterWriteMask(const Instr& in) {
    const InstructionSemantics& s = in.semantics;
    if (!s.valid()) return 0xFFFFu;
    uint16_t mask = 0;
    const auto reg = [&](bool address, unsigned n) {
        if (n < 8) mask |= uint16_t(1u << (n + (address ? 8 : 0)));
    };
    const auto eaUpdate = [&](uint8_t mode, uint8_t n) {
        if ((mode == 3 || mode == 4) && n < 8) reg(true, n);
    };
    eaUpdate(s.eaMode, s.eaReg);
    switch (s.operation) {
        case SemanticOp::Nop:
        case SemanticOp::Test:
        case SemanticOp::Branch:
        case SemanticOp::Jump:
            break;
        case SemanticOp::Move:
            eaUpdate(s.destinationMode, s.destinationReg);
            if (s.destinationMode == 0) reg(false, s.destinationReg);
            else if (s.destinationMode == 1) reg(true, s.destinationReg);
            break;
        case SemanticOp::CompareMemory:
            reg(true, s.eaReg); reg(true, s.destinationReg); break;
        case SemanticOp::MoveQuick:
            reg(false, s.registerIndex); break;
        case SemanticOp::MoveSrToReg:
            reg(false, s.eaReg); break;
        case SemanticOp::Exchange:
            reg(s.action == 1, s.registerIndex);
            reg(s.action != 0, s.eaReg); break;
        case SemanticOp::Bit:
            if (s.action != 0 && s.eaMode == 0) reg(false, s.eaReg);
            break;
        case SemanticOp::ImmediateAlu:
            if (s.alu != AluOperation::Cmp && s.eaMode == 0)
                reg(false, s.eaReg);
            break;
        case SemanticOp::SetCondition:
        case SemanticOp::Clear:
        case SemanticOp::Negate:
        case SemanticOp::Complement:
            if (s.eaMode == 0) reg(false, s.eaReg);
            break;
        case SemanticOp::AddSubQuick:
            if (s.eaMode == 0) reg(false, s.eaReg);
            else if (s.eaMode == 1) reg(true, s.eaReg);
            break;
        case SemanticOp::AluEaToReg:
            if (s.alu != AluOperation::Cmp) reg(false, s.registerIndex);
            break;
        case SemanticOp::AddressAlu:
            if (s.alu != AluOperation::Cmp) reg(true, s.registerIndex);
            break;
        case SemanticOp::AluRegToEa:
            if (s.eaMode == 0) reg(false, s.eaReg);
            break;
        case SemanticOp::Link:
        case SemanticOp::Unlink:
            reg(true, s.eaReg); reg(true, 7); break;
        case SemanticOp::Lea:
            reg(true, s.registerIndex); break;
        case SemanticOp::Pea:
        case SemanticOp::BranchSubroutine:
        case SemanticOp::JumpSubroutine:
        case SemanticOp::ReturnSubroutine:
            reg(true, 7); break;
        case SemanticOp::Movem:
            if (s.toRegisters) mask |= 0xFFFFu;
            break;
        case SemanticOp::Extend:
        case SemanticOp::Swap:
        case SemanticOp::ShiftRegister:
            reg(false, s.eaReg); break;
        case SemanticOp::Bitfield:
            mask |= 0x00FFu; break;
        case SemanticOp::DecrementBranch:
            reg(false, s.eaReg); break;
        default:
            mask = 0xFFFFu; break;
    }
    return mask;
}

void chooseGuestRegisterCache(const BlockIr& ir) {
    gCachedGuest[0] = gCachedGuest[1] = -1;
    if (!registerCacheEnabled()) return;
    uint16_t written = 0;
    unsigned score[16] = {};
    for (const Instr& in : ir.instrs) {
        written |= guestRegisterWriteMask(in);
        for (unsigned e = 0; e < in.effectiveAddressCount; e++) {
            const DecodedEffectiveAddress& ea = in.effectiveAddresses[e];
            if (!ea.valid) continue;
            if (ea.mode == 0 && ea.reg < 8) score[ea.reg] += 2;
            else if (ea.mode == 1 && ea.reg < 8) score[8 + ea.reg] += 2;
            else if (ea.mode >= 2 && ea.mode <= 6 && ea.reg < 8)
                score[8 + ea.reg] += 4;
            if ((ea.kind == EffectiveAddressKind::BriefIndex ||
                 ea.kind == EffectiveAddressKind::FullIndex ||
                 ea.kind == EffectiveAddressKind::PcBriefIndex ||
                 ea.kind == EffectiveAddressKind::PcFullIndex) &&
                !ea.indexSuppressed && ea.indexRegister < 16)
                score[ea.indexRegister] += 2;
        }
        const InstructionSemantics& s = in.semantics;
        switch (s.operation) {
            case SemanticOp::AluEaToReg:
            case SemanticOp::AluRegToEa:
            case SemanticOp::MoveQuick:
                if (s.registerIndex < 8) score[s.registerIndex] += 2; break;
            case SemanticOp::AddressAlu:
            case SemanticOp::Lea:
                if (s.registerIndex < 8) score[8 + s.registerIndex] += 2; break;
            case SemanticOp::Bit:
                if (s.dynamic && s.registerIndex < 8)
                    score[s.registerIndex] += 2;
                break;
            case SemanticOp::Pea:
            case SemanticOp::BranchSubroutine:
            case SemanticOp::JumpSubroutine:
            case SemanticOp::ReturnSubroutine:
                score[15] += 4; break;
            default: break;
        }
    }
    for (int slot = 0; slot < 2; slot++) {
        int best = -1;
        for (int guest = 0; guest < 16; guest++) {
            if ((written & (1u << guest)) || score[guest] == 0 ||
                guest == gCachedGuest[0]) continue;
            if (best < 0 || score[guest] > score[best]) best = guest;
        }
        gCachedGuest[slot] = best;
    }
}

void loadSized(Asm& a, const Layout& L, unsigned rd, bool address,
               unsigned n, int bits) {
    loadGuestRegister(a, L, rd, address, n, bits);
}

void storeSized(Asm& a, const Layout& L, unsigned rs, bool address,
                unsigned n, int bits) {
    const uint32_t o = regOff(L, address, n);
    if (bits == 8) a.strB(rs, 0, o);
    else if (bits == 16) a.strH(rs, 0, o);
    else a.strW(rs, 0, o);
}

void maskResult(Asm& a, unsigned r, int bits) {
    if (bits == 32) return;
    a.ubfxW(r, r, 0, unsigned(bits));
}

constexpr uint64_t kPackedCcrMask = 0x1Fu;
constexpr unsigned kPackedCountShift = 8;

void spillPackedCcr(Asm& a, const Layout& L) {
    if (!gPackedCcr) return;
    const auto put = [&](uint32_t off, unsigned bit) {
        a.ubfxW(17, 26, bit, 1);
        a.strB(17, 0, off);
    };
    put(L.srC, 0); put(L.srV, 1); put(L.srZ, 2);
    put(L.srN, 3); put(L.srX, 4);
}

void reloadPackedCcr(Asm& a, const Layout& L) {
    if (!gPackedCcr) return;
    a.movX(17, ~kPackedCcrMask);
    a.andX(26, 26, 17);
    const auto add = [&](uint32_t off, unsigned bit) {
        a.ldrB(18, 0, off);
        if (bit) a.lslW(18, 18, bit);
        a.orrX(26, 26, 18);
    };
    add(L.srC, 0); add(L.srV, 1); add(L.srZ, 2);
    add(L.srN, 3); add(L.srX, 4);
}

void retireInstruction(Asm& a) {
    if (gPackedCcr) a.addImmX(26, 26, 1u << kPackedCountShift);
    else            a.addImmW(26, 26, 1);
}

void spillRetiredCount(Asm& a) {
    if (gPackedCcr) {
        a.lsrX(17, 26, kPackedCountShift);
        a.strW(17, 1, 8);
    } else {
        a.strW(26, 1, 8);
    }
}

void setPackedCcrBits(Asm& a, unsigned valueReg, uint64_t clearMask) {
    a.movX(16, ~clearMask);
    a.andX(26, 26, 16);
    a.orrX(26, 26, valueReg);
}

void emitZFlag(Asm& a, const Layout& L, unsigned value) {
    if (!gPackedCcr) {
        a.strB(value, 0, L.srZ);
        return;
    }
    a.lslW(17, value, 2);
    setPackedCcrBits(a, 17, 4);
}

void emitNz(Asm& a, const Layout& L, unsigned result, int bits) {
    if (gPackedCcr) {
        a.lsrW(12, result, unsigned(bits - 1));
        a.lslW(12, 12, 3);
        a.cmpWZero(result);
        a.csetW(13, Asm::EQ);
        a.lslW(13, 13, 2);
        a.orrW(12, 12, 13);
        setPackedCcrBits(a, 12, 0x0C);
        return;
    }
    // The result is already masked to its guest width.
    a.lsrW(12, result, unsigned(bits - 1));
    a.strB(12, 0, L.srN);
    a.cmpWZero(result);
    a.csetW(12, Asm::EQ);
    a.strB(12, 0, L.srZ);
}

void emitLogicFlags(Asm& a, const Layout& L, unsigned result, int bits) {
    if (gPackedCcr) {
        a.lsrW(12, result, unsigned(bits - 1));
        a.lslW(12, 12, 3);
        a.cmpWZero(result);
        a.csetW(13, Asm::EQ);
        a.lslW(13, 13, 2);
        a.orrW(12, 12, 13);
        setPackedCcrBits(a, 12, 0x0F); // X survives logical operations
        return;
    }
    emitNz(a, L, result, bits);
    a.movW(12, 0);
    if (L.srC == L.srV + 1) a.strH(12, 0, L.srV);
    else {
        a.strB(12, 0, L.srV);
        a.strB(12, 0, L.srC);
    }
}

// Produce a guest-width add/sub while leaving the host NZCV flags as the
// exact 68k-width operation. For byte/word operations, shifting both inputs
// to bit 31 makes AArch64's N, V and carry boundary coincide with the guest
// sign/carry boundary; shifting the result back yields the masked value.
void emitAddSubResult(Asm& a, int bits, bool sub) {
    if (bits == 32) {
        if (sub) a.subsW(11, 9, 10);
        else a.addsW(11, 9, 10);
        return;
    }
    const unsigned shift = unsigned(32 - bits);
    a.lslW(12, 9, shift);
    a.lslW(13, 10, shift);
    if (sub) a.subsW(12, 12, 13);
    else a.addsW(12, 12, 13);
    a.lsrW(11, 12, shift);
}

// Consume the still-live NZCV from emitAddSubResult. C is inverted only for
// subtraction because AArch64 means "no borrow" while 68k C/X mean borrow.
void emitAddSubFlags(Asm& a, const Layout& L, bool sub, bool setX) {
    if (gPackedCcr) {
        // CSET preserves NZCV, so capture every host condition before the
        // packing instructions consume it.
        a.csetW(12, Asm::MI);
        a.csetW(13, Asm::EQ);
        a.csetW(17, Asm::VS);
        a.csetW(18, sub ? Asm::CC : Asm::CS);
        a.lslW(12, 12, 3);
        a.lslW(13, 13, 2);
        a.lslW(17, 17, 1);
        a.orrW(12, 12, 13);
        a.orrW(12, 12, 17);
        a.orrW(12, 12, 18);
        setPackedCcrBits(a, 12, setX ? 0x1F : 0x0F);
        if (setX) {
            a.lslW(18, 18, 4);
            a.orrX(26, 26, 18);
        }
        return;
    }
    a.csetW(12, Asm::MI); a.strB(12, 0, L.srN);
    a.csetW(12, Asm::EQ); a.strB(12, 0, L.srZ);
    a.csetW(12, Asm::VS); a.strB(12, 0, L.srV);
    a.csetW(12, sub ? Asm::CC : Asm::CS);
    a.strB(12, 0, L.srC);
    if (setX) a.strB(12, 0, L.srX);
}

// The EA cost index, the 68020 cycle columns and the EaPlan struct live in
// JitCost.h / JitEaPlan.h — they model the 68k, not this host, and the x64
// backend reads the SAME tables (TODO.md § 10 wave 2, 2026-08-28). This
// file keeps only a bits-flavoured wrapper: its call sites speak 8/16/32.
using Ea = EaPlan;

bool decodeEa(const Instr& in, int mode, int reg,
              int bits, int extAt, Ea& ea, bool allowFullDirect = false,
              bool allowFullIndirect = false) {
    const int szIdx = bits == 8 ? 0 : bits == 16 ? 1 : 2;
    return decodeEaPlan(in, mode, reg, szIdx, extAt, ea,
                        allowFullDirect, allowFullIndirect);
}

bool canEmitReg(uint16_t op) {
    const InstructionSemantics sem = describeInstruction(op);
    const int mode = sem.eaMode;
    const int ei = eaCostIndex(mode, sem.eaReg);
    const auto controlEa = [](int index) {
        return index == EA_AI || index == EA_DI || index == EA_AW ||
               index == EA_AL || index == EA_DIPC;
    };
    switch (sem.operation) {
        case SemanticOp::Nop:
        case SemanticOp::MoveQuick:
        case SemanticOp::MoveSrToReg:
        case SemanticOp::Link:
        case SemanticOp::Unlink:
        case SemanticOp::ReturnSubroutine:
        case SemanticOp::DecrementBranch:
        case SemanticOp::Branch:
        case SemanticOp::BranchSubroutine:
        case SemanticOp::Extend:
        case SemanticOp::Swap:
        case SemanticOp::Exchange:
            return true;
        case SemanticOp::CompareMemory:
            // With one An the destination address depends on the source
            // postincrement; the preflight-all lowering below is deliberately
            // the independent-EA subset.
            return sem.eaReg != sem.destinationReg;
        case SemanticOp::Bit:
            return mode != 1 && ei >= 0 && ei != EA_AN && ei != EA_IM &&
                   (sem.action == 0 || ei != EA_DIPC);
        case SemanticOp::ImmediateAlu:
            return ei >= 0 && ei != EA_AN && ei != EA_IM && ei != EA_DIPC;
        case SemanticOp::Move: {
            const int dm = sem.destinationMode, dr = sem.destinationReg;
            const int si = ei, di = eaCostIndex(dm, dr);
            if (si < 0 || di < 0 || di == EA_IM || di == EA_DIPC) return false;
            if (sem.sizeIndex == 0 && (mode == 1 || dm == 1)) return false;
            return true;
        }
        case SemanticOp::JumpSubroutine:
        case SemanticOp::Jump:
            return controlEa(ei);
        case SemanticOp::Lea:
            return controlEa(ei) || ei == EA_IX || ei == EA_IXPC;
        case SemanticOp::Pea:
            return controlEa(ei) || ei == EA_IX || ei == EA_IXPC;
        case SemanticOp::Movem:
            return true;
        case SemanticOp::SetCondition:
            return ei >= 0 && ei != EA_AN && ei != EA_DIPC && ei != EA_IM;
        case SemanticOp::Test:
        case SemanticOp::Clear:
        case SemanticOp::Negate:
        case SemanticOp::Complement:
        case SemanticOp::AluRegToEa:
            return ei >= 0 && ei != EA_IM;
        case SemanticOp::AddSubQuick:
            return ei >= 0 && ei != EA_IM && ei != EA_DIPC;
        case SemanticOp::AluEaToReg:
        case SemanticOp::AddressAlu:
            return ei >= 0; // immediate is a legal source EA
        case SemanticOp::Bitfield:
            return mode == 0 || mode == 2 || mode == 5;
        case SemanticOp::ShiftRegister:
            return sem.action != 2; // no ROX
        default: return false;
    }
}

int bitsForSizeIndex(int sz) { return sz == 0 ? 8 : sz == 1 ? 16 : 32; }

// The 68030 timing trace includes the instruction-cache miss penalty in the
// total. Generated code reproduces that penalty separately, so opcode cost
// tables must be checked against the base component. A post-exception trace
// did not retire cleanly and is never a valid native-code admission proof.
unsigned traced030(const Layout& L, const Instr& in) {
    if (!L.is030) return in.cycles;
    if (in.postExceptionCycles != 0) return 0xFFFFFFFFu;
    return in.baseCycles;
}

// A sole read can split its cost safely when the exact access owns the
// variable data-bus component. If a clean 040 trace exceeds the fixed opcode
// cost, force this particular emitted access through pomJitReadData on every
// execution: RAM then adds no wait, cache/MMIO adds its live delay, and a
// fault still replays the untouched instruction. The generated tail owes
// only the fixed cost. Keep the older 030 exception restricted to accesses
// the IR explicitly marks exact-required; widening arbitrary 030 base
// timings has failed lockstep in the past.
bool admitSoleReadTiming(const Layout& L, const Instr& in,
                         MemoryAccessPlan& read, unsigned fixedCycles,
                         uint16_t& nativeCycles) {
    if (!read.valid() || !read.single() || !read.exactThunk ||
        in.postExceptionCycles != 0)
        return false;
    const unsigned observed = traced030(L, in);
    if (observed == fixedCycles) return true;
    const bool exact030 = L.is030 && read.exactRequired &&
                          in.baseCycles >= fixedCycles;
    const bool exact040 = !L.is030 && in.cycles >= fixedCycles;
    if (!exact030 && !exact040) return false;
    if (exact040) read.exactRequired = true;
    nativeCycles = uint16_t(fixedCycles);
    return true;
}

void branchIfCond(Asm& a, const Layout& L, int cc, int taken);

bool emitAluResult(Asm& a, const Layout& L, AluOperation kind, int bits,
                   bool store, bool addressDst, unsigned dst, bool setX) {
    const bool add = kind == AluOperation::Add;
    const bool sub = kind == AluOperation::Sub || kind == AluOperation::Cmp;
    switch (kind) {
        case AluOperation::Or:  a.orrW(11, 9, 10); break;
        case AluOperation::And: a.andW(11, 9, 10); break;
        case AluOperation::Eor: a.eorW(11, 9, 10); break;
        case AluOperation::Add:
        case AluOperation::Sub:
        case AluOperation::Cmp:
            emitAddSubResult(a, bits, sub); break;
        default: return false;
    }
    if (!add && !sub) maskResult(a, 11, bits);
    if (store) storeSized(a, L, 11, addressDst, dst, bits);
    if (add || sub)
        emitAddSubFlags(a, L, sub, setX);
    else
        emitLogicFlags(a, L, 11, bits);
    return true;
}

void addrOf(Asm& a, const Layout& L, const Ea& ea, int bits) {
    const unsigned step = ea.reg == 7 && bits == 8 ? 2u : unsigned(bits / 8);
    switch (ea.idx) {
        case EA_AI: case EA_PI:
            loadGuestRegister(a, L, 9, true, unsigned(ea.reg)); break;
        case EA_PD:
            loadGuestRegister(a, L, 9, true, unsigned(ea.reg));
            a.subImmW(9, 9, step); break;
        case EA_DI:
            loadGuestRegister(a, L, 9, true, unsigned(ea.reg));
            a.movW(10, uint32_t(ea.value)); a.addW(9, 9, 10); break;
        case EA_IX: case EA_IXPC: {
            if (ea.fullFormat && ea.baseSuppressed) a.movW(9, 0);
            else if (ea.idx == EA_IX)
                loadGuestRegister(a, L, 9, true, unsigned(ea.reg));
            else a.movW(9, ea.base);
            if (!ea.fullFormat || !ea.indexSuppressed) {
                loadGuestRegister(a, L, 10, ea.ixReg >= 8,
                                  unsigned(ea.ixReg & 7));
                if (!ea.ixLong) a.sxtH(10, 10);
                if (ea.ixShift) a.lslW(10, 10, unsigned(ea.ixShift));
                a.addW(9, 9, 10);
            }
            const int32_t displacement =
                ea.fullFormat ? ea.baseDisplacement : ea.value;
            if (displacement) {
                a.movW(10, uint32_t(displacement)); a.addW(9, 9, 10);
            }
            break;
        }
        case EA_AW: case EA_AL: case EA_DIPC:
            a.movW(9, uint32_t(ea.value)); break;
        default: break;
    }
}

void addEaIndex(Asm& a, const Layout& L, const Ea& ea) {
    if (ea.indexSuppressed) return;
    loadGuestRegister(a, L, 10, ea.ixReg >= 8, unsigned(ea.ixReg & 7));
    if (!ea.ixLong) a.sxtH(10, 10);
    if (ea.ixShift) a.lslW(10, 10, unsigned(ea.ixShift));
    a.addW(9, 9, 10);
}

// Address of the pointer longword used by a full-format memory-indirect EA.
// Preindexed forms add Xn before the pointer read; postindexed forms add it
// afterwards in finishFullIndirect().
void addrOfFullIndirectPointer(Asm& a, const Layout& L, const Ea& ea) {
    if (ea.baseSuppressed) a.movW(9, 0);
    else if (ea.idx == EA_IX)
        loadGuestRegister(a, L, 9, true, unsigned(ea.reg));
    else
        a.movW(9, ea.base);
    if (ea.indirect == IndexIndirect::Preindexed) addEaIndex(a, L, ea);
    if (ea.baseDisplacement) {
        a.movW(10, uint32_t(ea.baseDisplacement));
        a.addW(9, 9, 10);
    }
}

void finishFullIndirect(Asm& a, const Layout& L, const Ea& ea,
                        unsigned pointerReg) {
    a.movRegW(9, pointerReg);
    if (ea.indirect == IndexIndirect::Postindexed) addEaIndex(a, L, ea);
    if (ea.outerDisplacement) {
        a.movW(10, uint32_t(ea.outerDisplacement));
        a.addW(9, 9, 10);
    }
}

void commitEa(Asm& a, const Layout& L, const Ea& ea, int bits) {
    const unsigned step = ea.reg == 7 && bits == 8 ? 2u : unsigned(bits / 8);
    if (ea.idx != EA_PI && ea.idx != EA_PD) return;
    // w10 commonly still holds the source operand (ADDA/SUBA/CMPA and
    // register-destination ALU).  Clobbering it here made (An)+/-(An)
    // consume the updated address as data.  w13 is dead at every commit
    // point; bit operations that use it have already stored their result.
    loadGuestRegister(a, L, 13, true, unsigned(ea.reg));
    if (ea.idx == EA_PI) a.addImmW(13, 13, step);
    else a.subImmW(13, 13, step);
    a.strW(13, 0, L.a + unsigned(ea.reg) * 4);
}

// The 68030 updates (An)+ before the data access. Generated code still has
// to keep its bailout boundary pristine, so the update happens only after a
// DTLB translation/probe has succeeded (or immediately before an exact
// access thunk). A real fault is replayed by Moira, which then constructs the
// format-A/B restart state itself.
void commitEaBeforeAccess(Asm& a, const Layout& L, const Ea& ea, int bits,
                          const MemoryAccessPlan& access) {
    if (access.eaCommit == EaCommit::BeforeAccess)
        commitEa(a, L, ea, bits);
}

void commitEaAfterAccess(Asm& a, const Layout& L, const Ea& ea, int bits,
                         const MemoryAccessPlan& access) {
    if (access.eaCommit == EaCommit::AfterAccess)
        commitEa(a, L, ea, bits);
}

void rollbackEaBeforeAccess(Asm& a, const Layout& L, const Ea& ea, int bits,
                            const MemoryAccessPlan& access) {
    if (access.eaCommit != EaCommit::BeforeAccess) return;
    const unsigned step = ea.reg == 7 && bits == 8 ? 2u : unsigned(bits / 8);
    loadGuestRegister(a, L, 13, true, unsigned(ea.reg));
    if (ea.idx == EA_PI) a.subImmW(13, 13, step);
    else a.addImmW(13, 13, step);
    a.strW(13, 0, L.a + unsigned(ea.reg) * 4);
}

void loadGuest(Asm& a, int bits, unsigned rd);
void commitQueue(Asm& a, const Layout& L, uint16_t ird, uint16_t irc,
                 unsigned scratch = 9);

// Refuse a write whose first or last byte overlaps a 256-byte slice holding
// translated code. `entry` is a PomJitDtlbEntry*, `pageOff` is addr&4095.
// A JIT access is at most one MOVEM burst (64 bytes), hence at most two
// slices need testing.
void guardCodeSlices(Asm& a, unsigned entry, unsigned pageOff, int bytes,
                     int miss) {
    const int clear = a.label();
    a.ldrW(12, entry, 4);                // PomJitDtlbEntry::codeMask
    a.cbzW(12, clear);
    for (int end = 0; end < (bytes > 1 ? 2 : 1); end++) {
        if (end) a.addImmW(10, pageOff, unsigned(bytes - 1));
        else a.movRegW(10, pageOff);
        a.lsrW(10, 10, moira::Moira::PomJitDtlb::kSliceShift);
        a.lsrVarW(10, 12, 10);
        a.ubfxW(10, 10, 0, 1);
        if (gRuntimeReasonHisto) {
            const int next = a.label();
            a.cbzW(10, next);
            a.strW(12, 1, 172);         // Frame::runtimeCodeMask
            a.b(miss);
            a.bind(next);
        } else {
            a.cbnzW(10, miss);
        }
    }
    a.bind(clear);
}

void markRuntimeReason(Asm& a, RuntimeFallbackReason reason) {
    if (!gRuntimeReasonHisto) return;
    a.movW(12, uint32_t(reason));
    a.strW(12, 1, 144);                 // Frame::runtimeReason
}

void markRuntimeAccess(Asm& a, int bytes, bool write, bool clearMask) {
    if (!gRuntimeReasonHisto) return;
    a.strW(9, 1, 168);                  // Frame::runtimeAddress
    if (clearMask) {
        a.movW(12, 0);
        a.strW(12, 1, 172);             // Frame::runtimeCodeMask
    }
    a.movW(12, unsigned(bytes));
    a.strW(12, 1, 176);                 // Frame::runtimeBytes
    a.movW(12, write ? 1u : 0u);
    a.strW(12, 1, 180);                 // Frame::runtimeWrite
}

void clearRuntimeAccess(Asm& a) {
    if (!gRuntimeReasonHisto) return;
    a.movW(12, 0);
    a.strW(12, 1, 176);                 // zero bytes = no address detail
}

void observeRuntimeAddress(Asm& a, const Layout& L, uint16_t opcode) {
    if (!gRuntimeReasonHisto) return;
    const int record = a.label(), maybeOther = a.label(), done = a.label();
    a.ldrW(9, 1, 144);                  // Frame::runtimeReason
    a.movW(10, uint32_t(RuntimeCodeMask));
    a.cmpW(9, 10);
    a.bCond(Asm::EQ, record);
    a.movW(10, uint32_t(RuntimeCrossPage));
    a.cmpW(9, 10);
    a.bCond(Asm::NE, maybeOther);
    a.b(record);
    a.bind(maybeOther);
    a.movW(10, uint32_t(RuntimeOther));
    a.cmpW(9, 10);
    a.bCond(Asm::NE, done);
    a.ldrW(10, 1, 176);
    a.cmpWZero(10);
    a.bCond(Asm::EQ, done);
    a.bind(record);
    spillQueueLive(a, L);
    if (icacheCountersLive(L)) spillIcacheCounters(a, L);
    spillPackedCcr(a, L);
    a.ldrX(16, 1, 152);                 // Frame::runtimeAddressObserver
    a.ldrX(0, 1, 160);                  // observer self
    a.movRegW(1, 9);                    // reason
    a.movW(2, opcode);
    // x1 has just become the first integer argument. Recover Frame from the
    // prologue save before loading the remaining arguments.
    a.ldrX(14, 31, 40);                 // saved Frame* at [sp,#40]
    a.ldrW(3, 14, 168);
    a.ldrW(4, 14, 176);
    a.ldrW(5, 14, 180);
    a.ldrW(6, 14, 172);
    a.blr(16);
    a.emit(0xA94207E0u);                // ldp x0,x1,[sp,#32]
    reloadPackedCcr(a, L);
    a.bind(done);
}

// Translate guest address w9. On success x14 is the host byte pointer; a
// refused mapping or cross-page access transfers to the untouched slow path.
void memProbe(Asm& a, const Layout& L, bool super, int bytes, bool write,
              int miss, bool cacheRead = false, int cacheWriteHit = -1,
              bool cacheOnly = false, bool countCacheHit = true) {
    const int fill = a.label(), have = a.label(), done = a.label();
    const int crossMiss = gRuntimeReasonHisto ? a.label() : miss;
    const int maskMiss = gRuntimeReasonHisto ? a.label() : miss;
    const int nonPlainMiss = gRuntimeReasonHisto ? a.label() : miss;
    const int fillMiss = gRuntimeReasonHisto ? a.label() : miss;

    const bool cacheWrite = write && cacheWriteHit >= 0;
    if (((cacheRead && !write && cache040LineReadsEnabled()) || cacheWrite) &&
        L.cache040Live) {
        const int cacheMiss = a.label();

        // A line-crossing operand needs two independently translated cache
        // lines. Keep that rare case on the exact path.
        a.ubfxW(13, 9, 0, 4);
        if (bytes > 1) {
            a.movW(12, unsigned(16 - bytes));
            a.cmpW(13, 12); a.bCond(Asm::HI, cacheMiss);
        }

        // Direct-mapped logical-line lookup (32-byte entries).
        a.lsrW(11, 9, 4);
        if (super) {
            a.movW(12, 0x80000000u); a.orrW(11, 11, 12);
        }
        a.ubfxW(10, 9, 4, 8);
        a.lslX(10, 10, 5);
        a.address(14, 0, cacheWrite ? L.cache040W : L.cache040R);
        a.addX(14, 14, 10);
        a.ldrW(12, 14, 0);
        a.cmpW(11, 12); a.bCond(Asm::NE, cacheMiss);

        // ATC-derived state cannot survive an ATC eviction/map change.
        a.ldrW(12, 14, 8);
        a.ldrW(13, 0, L.cache040Gen);
        a.cmpW(12, 13); a.bCond(Asm::NE, cacheMiss);

        // The cache way is stable, its contents are not: validate both the
        // valid bit and physical tag before exposing its big-endian bytes.
        a.ldrW(13, 14, 4);
        a.ldrX(14, 14, 16);
        a.ldrB(12, 14, offsetof(moira::Cache040::Line, valid));
        a.cbzW(12, cacheMiss);
        a.ldrW(12, 14, offsetof(moira::Cache040::Line, tag));
        a.cmpW(12, 13); a.bCond(Asm::NE, cacheMiss);

        // Cache040::hits is diagnostic state, but keep it exact: this path
        // is emitted only for an instruction's sole guest access, so no
        // later speculative probe can force a replay after the increment.
        if (countCacheHit) {
            a.ldrX(12, 0, L.cache040Hits);
            a.addImmX(12, 12, 1);
            a.strX(12, 0, L.cache040Hits);
            if (cache040LineReadStatsEnabled()) {
                const uint32_t counter = cacheWrite ? L.cache040NativeWriteHits
                                                    : L.cache040NativeReadHits;
                a.ldrX(12, 0, counter);
                a.addImmX(12, 12, 1);
                a.strX(12, 0, counter);
            }
        }
        a.ubfxW(13, 9, 0, 4);
        a.addImmX(14, 14, offsetof(moira::Cache040::Line, data));
        a.addX(14, 14, 13);
        a.b(cacheWrite ? cacheWriteHit : done);
        a.bind(cacheMiss);
        if (cacheOnly) a.b(miss);
    }

    a.lsrW(11, 9, 12);                 // logical-page tag
    if (super) {
        a.movW(12, 0x80000000u); a.orrW(11, 11, 12);
    }
    a.ubfxW(10, 9, 12, 8);             // direct-map index
    a.lslX(10, 10, 4);                 // entry index * 16
    a.address(14, 0, write ? L.dtlbW : L.dtlbR);
    a.addX(14, 14, 10);
    a.ldrW(12, 14, 0);
    a.cmpW(11, 12);
    a.bCond(Asm::NE, fill);

    a.bind(have);
    a.ubfxW(13, 9, 0, 12);
    if (bytes > 1) {
        a.movW(12, unsigned(4096 - bytes));
        a.cmpW(13, 12); a.bCond(Asm::HI, crossMiss);
    }
    // Read codeMask before x14 stops being the entry pointer and becomes
    // the host pointer. This is the arm64 half of the 256-byte write guard.
    if (write) guardCodeSlices(a, 14, 13, bytes, maskMiss);
    a.ldrX(14, 14, 8);
    a.cbzX(14, nonPlainMiss);
    a.addX(14, 14, 13);
    a.b(done);

    a.bind(fill);
    // Preserve the guest address and fetch every call operand before x1 is
    // repurposed from Frame* to the thunk's integer argument.
    a.strW(9, 1, 40);
    spillQueueLive(a, L);
    a.ldrX(16, 1, 24);
    spillPackedCcr(a, L);
    a.ldrX(0, 1, 16);
    a.movRegW(1, 9);
    a.movW(2, write ? 1u : 0u);
    a.blr(16);
    a.movRegX(14, 0);
    a.emit(0xA94207E0u);                // ldp x0,x1,[sp,#32]
    reloadPackedCcr(a, L);
    a.ldrW(9, 1, 40);
    a.cbzX(14, fillMiss);
    a.ubfxW(13, 9, 0, 12);
    if (bytes > 1) {
        a.movW(12, unsigned(4096 - bytes));
        a.cmpW(13, 12); a.bCond(Asm::HI, crossMiss);
    }
    if (write) {
        // The fill thunk returns only the host pointer. Recompute the entry
        // address so its freshly installed codeMask is checked as well.
        a.ubfxW(10, 9, 12, 8);
        a.lslX(10, 10, 4);
        a.address(15, 0, L.dtlbW);
        a.addX(15, 15, 10);
        guardCodeSlices(a, 15, 13, bytes, maskMiss);
    }
    a.addX(14, 14, 13);

    if (gRuntimeReasonHisto) {
        // The successful fill path arrives here by fall-through. Skip the
        // diagnostic miss stubs; otherwise every success is mislabeled as
        // the first reason even though the totals still appear to add up.
        a.b(done);
        a.bind(crossMiss);
        markRuntimeAccess(a, bytes, write, true);
        markRuntimeReason(a, RuntimeCrossPage);
        a.b(miss);
        a.bind(maskMiss);
        a.strW(12, 1, 172);
        markRuntimeAccess(a, bytes, write, false);
        markRuntimeReason(a, RuntimeCodeMask);
        a.b(miss);
        a.bind(nonPlainMiss);
        markRuntimeReason(a, RuntimeNonPlain);
        a.b(miss);
        a.bind(fillMiss);
        // fillDtlb records why a null was returned. The pointer is present
        // exactly when this diagnostic code was emitted.
        a.ldrX(15, 1, 136);             // Frame::dtlbFillReason
        a.ldrW(12, 15, 0);
        a.strW(12, 1, 144);
        a.b(miss);
    }
    a.bind(done);
}

// Read one guest operand. When it is the instruction's only guest access,
// a DTLB refusal (normally MMIO) calls the exact Moira access thunk and
// rejoins native code. A fault still reaches the untouched instruction
// fallback. Multi-access/RMW instructions retain the conservative path so
// replay can never duplicate a device side effect.
void memLoadGuest(Asm& a, const Layout& L, bool super, int bits, unsigned rd,
                  int slow, const MemoryAccessPlan& access,
                  const Ea* ea = nullptr) {
    if (!access.valid() || access.direction != MemoryDirection::Read ||
        access.bytes != unsigned(bits / 8)) { a.b(slow); return; }
    const bool cacheRead = access.cache;
    const bool exactAccess = access.exactThunk;
    if (!exactAccess) {
        memProbe(a, L, super, bits / 8, false, slow, cacheRead);
        if (ea) commitEaBeforeAccess(a, L, *ea, bits, access);
        loadGuest(a, bits, rd);
        return;
    }

    const int miss = a.label(), done = a.label();
    if (!access.exactRequired) {
        memProbe(a, L, super, bits / 8, false, miss, cacheRead);
        if (ea) commitEaBeforeAccess(a, L, *ea, bits, access);
        loadGuest(a, bits, rd);
        a.b(done);
    }

    a.bind(miss);
    a.strX(21, 1, 96);                 // failed thunk must leave no cycle debt
    a.strX(21, 0, L.clock);             // a device read may advance time
    a.address(3, 1, 72);                // Frame::value
    a.movRegW(1, 9);                    // guest address
    a.movW(2, unsigned(bits / 8));
    if (ea) commitEaBeforeAccess(a, L, *ea, bits, access);
    if (icacheCountersLive(L)) spillIcacheCounters(a, L);
    spillQueueLive(a, L);
    a.movX(4, gAccessPcWords);          // access-clock alignment operand
    a.movX(16, uint64_t(uintptr_t(&pom68kA64Read)));
    spillPackedCcr(a, L);
    a.blr(16);
    a.movRegW(14, 0);
    a.emit(0xA94207E0u);                // ldp x0,x1,[sp,#32]
    reloadPackedCcr(a, L);
    a.ldrX(21, 0, L.clock);
    reloadGeneratedState(a, L);
    a.cmpWZero(14);
    const int ok = a.label();
    a.bCond(Asm::NE, ok);
    if (ea) rollbackEaBeforeAccess(a, L, *ea, bits, access);
    a.ldrX(21, 1, 96); a.strX(21, 0, L.clock);
    a.b(slow);                          // fault: replay untouched instruction
    a.bind(ok);
    markRuntimeReason(a, RuntimeOther); // handled MMIO was not the fallback
    a.ldrW(rd, 1, 72);                  // thunk result is host-ordered
    a.bind(done);
}

void loadGuest(Asm& a, int bits, unsigned rd) {
    if (bits == 8) a.ldrB(rd, 14, 0);
    else if (bits == 16) { a.ldrH(rd, 14, 0); a.rev16W(rd, rd); }
    else { a.ldrW(rd, 14, 0); a.rev32W(rd, rd); }
}

void storeGuest(Asm& a, int bits, unsigned rs) {
    if (bits == 8) a.strB(rs, 14, 0);
    else if (bits == 16) {
        a.rev16W(12, rs); a.strH(12, 14, 0);
    } else {
        a.rev32W(12, rs); a.strW(12, 14, 0);
    }
}

// Publish the authoritative bytes written through x14 to the line's four
// dirty-longword bits. The cache probe has already proved that this is a
// resident copyback line and that the whole operand fits inside it.
void markCache040Dirty(Asm& a, int bytes) {
    a.ubfxW(13, 9, 0, 4);              // byte offset in the line
    a.subX(15, 14, 13);
    a.subImmX(15, 15, offsetof(moira::Cache040::Line, data));

    a.lsrW(13, 13, 2);
    a.movW(12, 1); a.lslVarW(12, 12, 13);
    if (bytes > 1) {
        a.addImmW(13, 9, unsigned(bytes - 1));
        a.ubfxW(13, 13, 2, 2);
        a.movW(10, 1); a.lslVarW(10, 10, 13);
        a.orrW(12, 12, 10);
    }
    a.ldrB(13, 15, offsetof(moira::Cache040::Line, dirty));
    a.orrW(12, 12, 13);
    a.strB(12, 15, offsetof(moira::Cache040::Line, dirty));
}

// Optional lockstep journal for direct stores, which otherwise bypass the
// machine memory-map callback entirely. w9 is the pre-update guest address;
// Frame::value holds the source. The callback sees the architectural An
// update because callers place it immediately before this hook.
void observeDirectWrite(Asm& a, const Layout& L, int bits,
                        uint32_t instructionPc) {
    const int done = a.label();
    a.ldrX(16, 1, 104);
    // Test the callback pointer exactly so production's null observer is free.
    a.cbzX(16, done);
    a.strW(9, 1, 36);
    a.strX(14, 1, 120);
    a.ldrX(15, 1, 112);
    a.ldrW(2, 1, 36);
    a.ldrW(4, 1, 72);
    a.movRegX(14, 0);
    spillQueueLive(a, L);
    if (icacheCountersLive(L)) spillIcacheCounters(a, L);
    spillPackedCcr(a, L);
    a.movRegX(0, 15);
    a.movRegX(1, 14);
    a.movW(3, unsigned(bits / 8));
    a.movW(5, instructionPc);
    a.movW(6, 1);
    a.blr(16);
    a.emit(0xA94207E0u);                // ldp x0,x1,[sp,#32]
    reloadPackedCcr(a, L);
    a.ldrW(9, 1, 36);
    a.ldrW(11, 1, 72);
    a.ldrX(14, 1, 120);
    a.bind(done);
}

// Store one sole-access operand. Plain RAM keeps the fast direct DTLB path.
// A refused mapping (MMIO, /BERR hole, protected translation) is attempted
// through the exact model-specific Moira write. If that faults, no native
// architectural state has been committed: clock and an early 030 (An)+
// update are restored before the untouched instruction is replayed, which
// is where the format-A/B frame is constructed.
void memStoreGuest(Asm& a, const Layout& L, bool super, int bits, unsigned rs,
                   int slow, const MemoryAccessPlan& access,
                   const Ea* ea = nullptr) {
    if (!access.valid() || access.direction != MemoryDirection::Write ||
        access.bytes != unsigned(bits / 8)) { a.b(slow); return; }
    const bool cacheExactWrite = !L.is030 && access.single() && L.cache040Live;
    const bool cacheWrite = access.cache;
    const bool exactAccess = L.is030 && access.exactThunk;
    if (!exactAccess) {
        // This is the attribution-control half of the J4 write gate. Once
        // the architectural D-cache is live, disabling its native line hit
        // must restore the exact cache-aware instruction path; an ordinary
        // DTLB pointer names backing RAM and would bypass a dirty copyback
        // line. The enabled path below may still consult the DTLB after a W
        // miss, but cache-active fills are tagged-null refusals and therefore
        // reach the same untouched replay.
        if (cacheExactWrite && !cacheWrite) {
            a.b(slow);
            return;
        }
        const int cacheHit = cacheWrite ? a.label() : -1;
        const int done = cacheWrite ? a.label() : -1;
        memProbe(a, L, super, bits / 8, true, slow, false, cacheHit);
        if (ea) commitEaBeforeAccess(a, L, *ea, bits, access);
        a.ldrW(rs, 1, 72);
        storeGuest(a, bits, rs);
        if (cacheWrite) {
            a.b(done);
            a.bind(cacheHit);
            if (ea) commitEaBeforeAccess(a, L, *ea, bits, access);
            a.ldrW(rs, 1, 72);
            storeGuest(a, bits, rs);
            markCache040Dirty(a, bits / 8);
            a.bind(done);
        }
        return;
    }

    const int miss = a.label(), done = a.label(), ok = a.label();
    memProbe(a, L, super, bits / 8, true, miss);
    if (ea) commitEaBeforeAccess(a, L, *ea, bits, access);
    a.ldrW(rs, 1, 72);
    storeGuest(a, bits, rs);
    a.b(done);

    a.bind(miss);
    a.strX(21, 1, 96);
    a.strX(21, 0, L.clock);
    a.ldrW(3, 1, 72);
    a.movRegW(1, 9);
    a.movW(2, unsigned(bits / 8));
    if (ea) commitEaBeforeAccess(a, L, *ea, bits, access);
    if (icacheCountersLive(L)) spillIcacheCounters(a, L);
    spillQueueLive(a, L);
    a.movX(4, gAccessPcWords);          // access-clock alignment operand
    a.movX(16, uint64_t(uintptr_t(&pom68kA64Write)));
    spillPackedCcr(a, L);
    a.blr(16);
    a.movRegW(14, 0);
    a.emit(0xA94207E0u);                // ldp x0,x1,[sp,#32]
    reloadPackedCcr(a, L);
    a.ldrX(21, 0, L.clock);
    reloadGeneratedState(a, L);
    a.cmpWZero(14);
    a.bCond(Asm::NE, ok);
    if (ea) rollbackEaBeforeAccess(a, L, *ea, bits, access);
    a.ldrX(21, 1, 96); a.strX(21, 0, L.clock);
    a.b(slow);
    a.bind(ok);
    markRuntimeReason(a, RuntimeOther); // handled MMIO was not the fallback
    a.ldrW(rs, 1, 72);
    a.bind(done);
}

void loadGuestOff(Asm& a, int bits, unsigned rd, uint32_t off) {
    if (bits == 16) { a.ldrH(rd, 14, off); a.rev16W(rd, rd); }
    else { a.ldrW(rd, 14, off); a.rev32W(rd, rd); }
}

void storeGuestOff(Asm& a, int bits, unsigned rs, uint32_t off) {
    if (bits == 16) { a.rev16W(12, rs); a.strH(12, 14, off); }
    else { a.rev32W(12, rs); a.strW(12, 14, off); }
}

bool emitRegInstr(Asm& a, const Layout& L, const BlockIr& ir, const Instr& in,
                  int slow, uint16_t& nativeCycles) {
    const uint16_t op = in.opcode;
    const InstructionSemantics& sem = in.semantics;
    if (!sem.valid()) return false;
    auto memory = instructionMemoryPlan(in.memory, proofOptions(L));
    if (sem.operation == SemanticOp::Nop) return traced030(L, in) == 2;

    if (sem.operation == SemanticOp::MoveQuick) {
        if (traced030(L, in) != 2) return false;
        const int32_t v = int8_t(op & 0xFF);
        const unsigned dn = sem.registerIndex;
        a.movW(11, uint32_t(v));
        a.strW(11, 0, L.d + dn * 4);
        emitLogicFlags(a, L, 11, 32);
        return true;
    }

    if (sem.operation == SemanticOp::Exchange) {
        if (in.words != 1 || traced030(L, in) != 2 || !memory.complete())
            return false;
        const unsigned rx = sem.registerIndex, ry = sem.eaReg;
        const bool leftAddress = sem.action == 1;
        const bool rightAddress = sem.action != 0;
        loadGuestRegister(a, L, 9, leftAddress, rx);
        loadGuestRegister(a, L, 10, rightAddress, ry);
        a.strW(10, 0, regOff(L, leftAddress, rx));
        a.strW(9, 0, regOff(L, rightAddress, ry));
        return true;
    }

    if (sem.operation == SemanticOp::CompareMemory) {
        // CMPM.<s> (Ay)+,(Ax)+. Distinct address registers let both DTLB
        // mappings be proved while the entry state is still pristine. Once
        // both probes hit, direct RAM loads cannot fault and the architectural
        // source-read / source-commit / destination-read / destination-commit
        // order is reproduced exactly. The same-register form depends on the
        // first increment when calculating the second EA and remains on the
        // precise interpreter path.
        const int bits = bitsForSizeIndex(sem.sizeIndex);
        const uint8_t srcReg = sem.eaReg;
        const uint8_t dstReg = sem.destinationReg;
        if (srcReg == dstReg || in.words != 1 || traced030(L, in) != 9 ||
            slow < 0)
            return false;
        Ea src, dst;
        if (!decodeEa(in, 3, srcReg, bits, 0, src) ||
            !decodeEa(in, 3, dstReg, bits, 0, dst))
            return false;
        const MemoryAccessPlan srcRead = memory.access(
            MemoryDirection::Read, MemoryOperand::Source,
            uint8_t(bits / 8), 3, srcReg);
        const MemoryAccessPlan dstRead = memory.access(
            MemoryDirection::Read, MemoryOperand::Destination,
            uint8_t(bits / 8), 3, dstReg);
        if (!srcRead.valid() || !dstRead.valid() || !srcRead.preflight ||
            !dstRead.preflight ||
            memory.proof.protocol != MemoryProofProtocol::PreflightAll ||
            in.memory.order != MemoryOrder::SourceThenDestination ||
            !memory.complete())
            return false;

        addrOf(a, L, src, bits);
        memProbe(a, L, ir.super, bits / 8, false, slow);
        a.strX(14, 1, 120);                 // source host pointer
        addrOf(a, L, dst, bits);
        memProbe(a, L, ir.super, bits / 8, false, slow);
        a.strX(14, 1, 96);                  // destination host pointer

        commitEaBeforeAccess(a, L, src, bits, srcRead);
        a.ldrX(14, 1, 120);
        loadGuest(a, bits, 10);             // b = source
        commitEaAfterAccess(a, L, src, bits, srcRead);
        commitEaBeforeAccess(a, L, dst, bits, dstRead);
        a.ldrX(14, 1, 96);
        loadGuest(a, bits, 9);              // a = destination
        commitEaAfterAccess(a, L, dst, bits, dstRead);
        return emitAluResult(a, L, AluOperation::Cmp, bits,
                             false, false, 0, false);
    }

    if (sem.operation == SemanticOp::MoveSrToReg) {
        if (in.words != 1 || in.baseCycles != 8 || in.postExceptionCycles != 0)
            return false;
        a.movW(11, 0);
        const auto addSrBit = [&](uint32_t off, unsigned bit) {
            a.ldrB(9, 0, off);
            if (bit) a.lslW(9, 9, bit);
            a.orrW(11, 11, 9);
        };
        addSrBit(L.srT1, 15); addSrBit(L.srT0, 14);
        addSrBit(L.srS, 13);  addSrBit(L.srM, 12);
        a.ldrB(9, 0, L.srIpl); a.lslW(9, 9, 8); a.orrW(11, 11, 9);
        if (gPackedCcr) {
            a.movW(10, uint32_t(kPackedCcrMask));
            a.andW(9, 26, 10);
            a.orrW(11, 11, 9);
        } else {
            addSrBit(L.srX, 4); addSrBit(L.srN, 3); addSrBit(L.srZ, 2);
            addSrBit(L.srV, 1); addSrBit(L.srC, 0);
        }
        a.strH(11, 0, L.d + sem.eaReg * 4);
        return true;
    }

    if (sem.operation == SemanticOp::Move) {         // MOVE/MOVEA
        const int bits = bitsForSizeIndex(sem.sizeIndex);
        const int sm = sem.eaMode, sr = sem.eaReg;
        const int dm = sem.destinationMode, dr = sem.destinationReg;
        Ea src, dst;
        if (!decodeEa(in, sm, sr, bits, 0, src, true, true) ||
            !decodeEa(in, dm, dr, bits, src.ext, dst)) return false;
        if (bits == 8 && (src.idx == EA_AN || dst.idx == EA_AN)) return false;
        const bool dependentPostincAi = src.idx == EA_PI && dst.idx == EA_AI &&
                                        src.reg == dst.reg;
        if ((src.idx == EA_PI || src.idx == EA_PD) && src.reg == dst.reg &&
            (dst.idx == EA_AN || dst.idx == EA_AI || dst.idx == EA_PI ||
             dst.idx == EA_PD || dst.idx == EA_DI) &&
            !dependentPostincAi)
            return false;
        const int sz = bits == 8 ? 0 : bits == 16 ? 1 : 2;
        // A postincrement source whose sole access uses the pre-update /
        // rollback helper is safe to validate against the split base cost.
        // Restartable writes consume it only for restartWrite030's injected-
        // fault-proved family. Brief indexed destination calculation costs
        // five base cycles; keep that admission local so memory-source MOVE
        // forms do not ride on this sole-write proof.
        const bool indirectSource = src.fullFormat &&
                                    src.indirect != IndexIndirect::None;
        if (indirectSource && (L.is030 || dst.memory)) return false;
        MemoryAccessPlan pointerAccess, srcAccess, dstAccess;
        if (src.memory) {
            if (indirectSource)
                pointerAccess = memory.access(MemoryDirection::Read,
                                              MemoryOperand::Control, 4,
                                              uint8_t(sm), uint8_t(sr));
            srcAccess = memory.access(MemoryDirection::Read,
                                      MemoryOperand::Source,
                                      uint8_t(bits / 8), uint8_t(sm),
                                      uint8_t(sr));
            if (!srcAccess.valid() ||
                (indirectSource && !pointerAccess.valid())) return false;
        }
        if (dst.memory) {
            dstAccess = memory.access(MemoryDirection::Write,
                                      MemoryOperand::Destination,
                                      uint8_t(bits / 8), uint8_t(dm),
                                      uint8_t(dr));
            if (!dstAccess.valid()) return false;
        }
        if (!memory.complete()) return false;
        const bool restartWrite = L.is030 &&
                                  memory.proof.restartableLastWrite();
        const int dstCycles = restartWrite && dst.idx == EA_IX
            ? 5 : kMoveDst[dst.idx];
        const int sourcePenalty = src.fullFormat ? fullIndexPenalty(src) : 0;
        const int cycles = kEaRead[src.idx][sz] + sourcePenalty + dstCycles;
        // The emitted i-cache model owns miss penalties. The restartable-
        // write family kept the historical total-cost admission while its
        // coarse-budget divergence was open; that was the peripheral-phase
        // class (JIT_BRINGUP § C.4nonies), closed by the access-clock bias
        // the thunks carry since 2026-08-22 — so the admission follows the
        // same knob as x64, ON by default under this backend's declaration.
        // Under the total cost every push traced on an i-cache miss was
        // refused: 120 M of the idle Finder's 238 M in-block fallbacks.
        const unsigned tracedCycles = restartWrite && !restartBaseAdmission()
            ? unsigned(in.cycles) : traced030(L, in);
        const bool soleReadTiming = src.memory && !indirectSource &&
            cycles >= 0 &&
            admitSoleReadTiming(L, in, srcAccess, unsigned(cycles),
                                nativeCycles);
        if (cycles < 0 ||
            (tracedCycles != unsigned(cycles) && !soleReadTiming) ||
            in.words != unsigned(1 + src.ext + dst.ext)) return false;

        // MOVE.<s> (An)+,(An) is a common ROM trampoline idiom. The
        // destination uses An AFTER the source increment; computing both EAs
        // from the entry register was the reason the generic alias gate had
        // to refuse it. Prove old-An and old-An+step first, then perform the
        // read, architectural increment and write exactly once.
        if (dependentPostincAi) {
            if (slow < 0 ||
                memory.proof.protocol != MemoryProofProtocol::PreflightAll ||
                !srcAccess.preflight || !dstAccess.preflight ||
                in.memory.order != MemoryOrder::SourceThenDestination)
                return false;
            const unsigned step = src.reg == 7 && bits == 8
                ? 2u : unsigned(bits / 8);
            loadGuestRegister(a, L, 9, true, unsigned(src.reg));
            memProbe(a, L, ir.super, bits / 8, false, slow);
            a.strX(14, 1, 120);
            loadGuestRegister(a, L, 9, true, unsigned(src.reg));
            a.addImmW(9, 9, step);
            memProbe(a, L, ir.super, bits / 8, true, slow);
            a.strX(14, 1, 96);

            a.ldrX(14, 1, 120); loadGuest(a, bits, 11);
            loadGuestRegister(a, L, 9, true, unsigned(src.reg));
            a.addImmW(9, 9, step);
            a.strW(9, 0, L.a + unsigned(src.reg) * 4);
            a.ldrX(14, 1, 96); storeGuest(a, bits, 11);
            emitLogicFlags(a, L, 11, bits);
            return true;
        }

        if (src.memory && dst.memory) {
            if (slow < 0) return false;
            // The two dominant cache-on MOVE fallbacks are the paired
            // longword shuttles abs.W -> -(A7) and (A7)+ -> abs.W. Probe
            // BOTH resident lines before reading either one: a miss can
            // then replay a pristine instruction, while two hits make the
            // source read and dirty destination write indivisible here.
            const bool cachePair = memory.proof.atomicCachePair();
            if (cachePair) {
                if (!srcAccess.cache || !dstAccess.cache) return false;
                addrOf(a, L, src, bits);
                memProbe(a, L, ir.super, 4, false, slow, true, -1,
                         true, false);
                a.strX(14, 1, 120);                  // source line bytes
                addrOf(a, L, dst, bits);
                const int writeHit = a.label();
                memProbe(a, L, ir.super, 4, true, slow, false, writeHit,
                         true, false);
                a.b(slow);                           // cache-only success branches
                a.bind(writeHit);

                // Publish both diagnostic hits only after both proofs. If
                // the W probe misses, exact replay owns the source hit and
                // these counters must not be double-incremented.
                a.ldrX(12, 0, L.cache040Hits);
                a.addImmX(12, 12, 2); a.strX(12, 0, L.cache040Hits);
                if (cache040LineReadStatsEnabled()) {
                    a.ldrX(12, 0, L.cache040NativeReadHits);
                    a.addImmX(12, 12, 1);
                    a.strX(12, 0, L.cache040NativeReadHits);
                    a.ldrX(12, 0, L.cache040NativeWriteHits);
                    a.addImmX(12, 12, 1);
                    a.strX(12, 0, L.cache040NativeWriteHits);
                }

                // Large Moira layout offsets use x15 as the assembler's
                // address scratch. Keep neither host pointer live across an
                // EA commit: source and destination get separate Frame
                // slots and are reloaded only at the actual access.
                a.strX(14, 1, 96);                   // destination line bytes
                commitEaBeforeAccess(a, L, src, bits, srcAccess);
                a.ldrX(14, 1, 120); loadGuest(a, bits, 11);
                commitEaAfterAccess(a, L, src, bits, srcAccess);
                commitEaBeforeAccess(a, L, dst, bits, dstAccess);
                a.ldrX(14, 1, 96); storeGuest(a, bits, 11);
                markCache040Dirty(a, 4);
                commitEaAfterAccess(a, L, dst, bits, dstAccess);
                emitLogicFlags(a, L, 11, bits);
                return true;
            }
            if (memory.proof.protocol == MemoryProofProtocol::PreflightAll &&
                srcAccess.exactThunk && dstAccess.preflight &&
                in.memory.order == MemoryOrder::SourceThenDestination) {
                // Prove the destination before the exact source read. Once a
                // device FIFO has been popped it must never be replayed just
                // because the destination translation was unavailable.
                addrOf(a, L, dst, bits);
                memProbe(a, L, ir.super, bits / 8, true, slow);
                a.strX(14, 1, 120);              // proved destination bytes

                addrOf(a, L, src, bits);
                memLoadGuest(a, L, ir.super, bits, 11, slow,
                             srcAccess, &src);
                commitEaAfterAccess(a, L, src, bits, srcAccess);

                commitEaBeforeAccess(a, L, dst, bits, dstAccess);
                a.ldrX(14, 1, 120);
                storeGuest(a, bits, 11);
                commitEaAfterAccess(a, L, dst, bits, dstAccess);
                emitLogicFlags(a, L, 11, bits);
                return true;
            }
            if (memory.proof.protocol != MemoryProofProtocol::PreflightAll ||
                !srcAccess.preflight || !dstAccess.preflight ||
                in.memory.order != MemoryOrder::SourceThenDestination)
                return false;
            // A memory-to-memory MOVE has two independently refusable DTLB
            // probes. The old sequential path committed a 030 source (An)+
            // after the first hit; if the destination probe then missed,
            // untouched replay incremented An a second time. Probe both
            // mappings first and only then expose either EA side effect.
            addrOf(a, L, src, bits);
            memProbe(a, L, ir.super, bits / 8, false, slow);
            a.strX(14, 1, 120);            // source host pointer
            addrOf(a, L, dst, bits);
            memProbe(a, L, ir.super, bits / 8, true, slow);
            a.strX(14, 1, 96);             // destination host pointer

            commitEaBeforeAccess(a, L, src, bits, srcAccess);
            a.ldrX(14, 1, 120);
            loadGuest(a, bits, 11);
            commitEaAfterAccess(a, L, src, bits, srcAccess);

            commitEaBeforeAccess(a, L, dst, bits, dstAccess);
            a.ldrX(14, 1, 96);
            storeGuest(a, bits, 11);
            commitEaAfterAccess(a, L, dst, bits, dstAccess);
            emitLogicFlags(a, L, 11, bits);
            return true;
        }

        if (indirectSource) {
            // Resolve the pointer only after its mapping is proved plain.
            // A later source-mapping miss may replay this RAM read safely;
            // no MMIO/cache side effect has escaped. Once the source mapping
            // succeeds there is no later refusable access because the
            // destination is a register.
            if (slow < 0 ||
                memory.proof.protocol != MemoryProofProtocol::PreflightAll ||
                !pointerAccess.preflight || !srcAccess.preflight ||
                in.memory.order != MemoryOrder::Sequential)
                return false;
            addrOfFullIndirectPointer(a, L, src);
            memProbe(a, L, ir.super, 4, false, slow);
            loadGuest(a, 32, 10);
            finishFullIndirect(a, L, src, 10);
            memLoadGuest(a, L, ir.super, bits, 11, slow, srcAccess);
        } else if (src.memory) {
            if (slow < 0) return false;
            addrOf(a, L, src, bits);
            memLoadGuest(a, L, ir.super, bits, 11, slow, srcAccess, &src);
            if (!dst.memory)
                commitEaAfterAccess(a, L, src, bits, srcAccess);
        } else if (src.idx == EA_IM) {
            a.movW(11, uint32_t(src.value));
            // Brief immediates are decoded through int8_t/int16_t so MOVEA.W
            // can sign-extend them.  Plain MOVE.B/W must materialise flags
            // from the guest-width value: leaving 0xFFFF as 0xFFFFFFFF made
            // emitNz store 0xFF into the C++ bool backing N.
            maskResult(a, 11, bits);
        } else {
            loadSized(a, L, 11, src.idx == EA_AN, unsigned(src.reg), bits);
        }

        if (dst.memory) {
            if (slow < 0) return false;
            a.strW(11, 1, 72);           // survive a possible DTLB fill
            // On a 68030 MOVE publishes N/Z/V/C before entering writeOp's
            // LASTWRITE access. Exact MMIO callbacks therefore observe the
            // final CCR already, just like a fault frame does. Keep w11 live
            // for the store and suppress the ordinary tail update below.
            if (restartWrite && dst.idx != EA_PI)
                emitLogicFlags(a, L, 11, bits);
            addrOf(a, L, dst, bits);
            if (restartWrite && dst.idx != EA_PI) {
                // At Moira's last-write point reg.pc has consumed every
                // extension, while pc0 still names this instruction. An
                // exact MMIO thunk can expose both before the ordinary block
                // epilogue runs, so materialise the running boundary now.
                const uint32_t nextPc = in.pc + uint32_t(in.words) * 2;
                a.movW(12, nextPc); a.strW(12, 0, L.pc);
                a.movW(12, in.pc); a.strW(12, 0, L.pc0);
                const uint16_t heldIrd = in.terminalQueueValid
                    ? in.terminalIrd : in.opcode;
                const uint16_t heldIrc = in.terminalQueueValid
                    ? in.terminalIrc : ir.prefetchWord(nextPc);
                commitQueue(a, L, heldIrd, heldIrc, 12);
            }
            if (restartWrite && dst.idx == EA_PI) {
                // PI is native only for a direct, faultless RAM mapping.
                // Probe before publishing CCR, PC/queue or the An update so
                // MMIO and /BERR reach Moira with a pristine entry boundary.
                memProbe(a, L, ir.super, bits / 8, true, slow);
                commitEaBeforeAccess(a, L, dst, bits, dstAccess);
                a.ldrW(11, 1, 72);
                emitLogicFlags(a, L, 11, bits);
                observeDirectWrite(a, L, bits, in.pc);
                storeGuest(a, bits, 11);
            } else {
                memStoreGuest(a, L, ir.super, bits, 11, slow,
                              dstAccess, &dst);
            }
            if (src.memory)
                commitEaAfterAccess(a, L, src, bits, srcAccess);
            commitEaAfterAccess(a, L, dst, bits, dstAccess);
        } else {
            if (dst.idx == EA_AN && bits == 16) a.sxtH(11, 11);
            storeSized(a, L, 11, dst.idx == EA_AN, unsigned(dst.reg),
                       dst.idx == EA_AN ? 32 : bits);
        }
        if (dst.idx != EA_AN && !(dst.memory && restartWrite))
            emitLogicFlags(a, L, 11, bits);
        return true;
    }

    if (sem.operation == SemanticOp::Bit ||
        sem.operation == SemanticOp::ImmediateAlu) {
        const int mode = sem.eaMode;
        if (sem.operation == SemanticOp::Bit) {      // BTST/BCHG/BCLR/BSET
            const bool dynamicBit = sem.dynamic;
            if (mode == 1) return false;             // MOVEP overlap / An
            const bool toReg = mode == 0;
            const int bits = toReg ? 32 : 8;
            const int action = sem.action;           // 0 test,1 xor,2 clear,3 set
            const int extUsed = dynamicBit ? 0 : 1;
            Ea dst;
            if (!decodeEa(in, mode, sem.eaReg, bits, extUsed, dst) ||
                dst.idx == EA_AN || dst.idx == EA_IM ||
                (action != 0 && dst.idx == EA_DIPC) ||
                in.words != unsigned(1 + extUsed + dst.ext)) return false;
            const int sz = toReg ? 2 : 0;
            const int base = kEaRead[dst.idx][sz];
            const int cycles = toReg ? 4 : base + 2;
            MemoryAccessPlan read, write;
            if (dst.memory) {
                read = memory.access(MemoryDirection::Read,
                                     MemoryOperand::Operand, 1,
                                     uint8_t(mode), sem.eaReg);
                if (!read.valid()) return false;
                if (action != 0) {
                    write = memory.access(MemoryDirection::Write,
                                          MemoryOperand::Operand, 1,
                                          uint8_t(mode), sem.eaReg);
                    if (!memoryRmwAccessPair(read, write))
                        return false;
                }
            }
            if (!memory.complete()) return false;
            const bool soleReadTiming = action == 0 && dst.memory && base >= 0 &&
                admitSoleReadTiming(L, in, read, unsigned(cycles),
                                    nativeCycles);
            if (base < 0 || (traced030(L, in) != unsigned(cycles) &&
                             !soleReadTiming)) return false;
            if (dst.memory) {
                if (slow < 0) return false;
                addrOf(a, L, dst, bits);
                if (action == 0)
                    memLoadGuest(a, L, ir.super, bits, 11, slow, read, &dst);
                else {
                    memProbe(a, L, ir.super, bits / 8, true, slow);
                    commitEaBeforeAccess(a, L, dst, bits, read);
                    loadGuest(a, bits, 11);
                }
            } else {
                loadSized(a, L, 11, false, unsigned(dst.reg), 32);
            }
            if (action == 0) {
                // BTST needs the selected bit, not a materialised one-hot
                // mask. Immediate tests become one UBFX; dynamic tests shift
                // the operand down first. This is the hottest opcode family
                // in the Q605 census.
                if (dynamicBit) {
                    loadGuestRegister(a, L, 10, false, sem.registerIndex);
                    a.ubfxW(10, 10, 0, toReg ? 5 : 3);
                    a.lsrVarW(12, 11, 10);
                    a.ubfxW(12, 12, 0, 1);
                } else {
                    const uint32_t bit = in.extensionWord(0) & 0xFF;
                    a.ubfxW(12, 11, bit & (toReg ? 31u : 7u), 1);
                }
                a.cmpWZero(12); a.csetW(12, Asm::EQ);
                emitZFlag(a, L, 12);
            } else {
                if (dynamicBit) {
                    loadGuestRegister(a, L, 10, false, sem.registerIndex);
                    a.ubfxW(10, 10, 0, toReg ? 5 : 3);
                    a.movW(12, 1); a.lslVarW(12, 12, 10);
                } else {
                    const uint32_t bit = in.extensionWord(0) & 0xFF;
                    a.movW(12, 1u << (bit & (toReg ? 31u : 7u)));
                }
                a.movRegW(13, 11);                // original operand
                a.andW(10, 11, 12);               // preserve mask in w12
                a.cmpWZero(10); a.csetW(10, Asm::EQ);
                emitZFlag(a, L, 10);
                a.movRegW(11, 13);
                if (action == 1) a.eorW(11, 11, 12);
                else if (action == 2) { a.mvnW(12, 12); a.andW(11, 11, 12); }
                else a.orrW(11, 11, 12);
                if (dst.memory) storeGuest(a, bits, 11);
                else a.strW(11, 0, L.d + unsigned(dst.reg) * 4);
            }
            if (dst.memory)
                commitEaAfterAccess(a, L, dst, bits,
                                    action == 0 ? read : write);
            return true;
        }
        const int sz = sem.sizeIndex;
        if (sz > 2) return false;
        const AluOperation kind = sem.alu;
        if (kind == AluOperation::None) return false;
        const int bits = bitsForSizeIndex(sz);
        const int immExt = bits == 32 ? 2 : 1;
        Ea dst;
        if (!decodeEa(in, sem.eaMode, sem.eaReg, bits, immExt, dst) ||
            dst.idx == EA_AN || dst.idx == EA_IM || dst.idx == EA_DIPC ||
            in.words != unsigned(1 + immExt + dst.ext)) return false;
        const int base = kEaRead[dst.idx][sz];
        const int cycles = kind == AluOperation::Cmp ? base
                           : (dst.idx == EA_DN ? 2 : base + 2);
        MemoryAccessPlan read, write;
        if (dst.memory) {
            read = memory.access(MemoryDirection::Read,
                                 MemoryOperand::Destination,
                                 uint8_t(bits / 8), sem.eaMode, sem.eaReg);
            if (!read.valid()) return false;
            if (kind != AluOperation::Cmp) {
                write = memory.access(MemoryDirection::Write,
                                      MemoryOperand::Destination,
                                      uint8_t(bits / 8), sem.eaMode,
                                      sem.eaReg);
                if (!memoryRmwAccessPair(read, write))
                    return false;
            }
        }
        if (!memory.complete()) return false;
        const bool soleReadTiming = kind == AluOperation::Cmp && dst.memory &&
            base >= 0 && admitSoleReadTiming(L, in, read, unsigned(cycles),
                                             nativeCycles);
        if (base < 0 || (traced030(L, in) != unsigned(cycles) &&
                         !soleReadTiming)) return false;
        if (dst.memory) {
            if (slow < 0) return false;
            addrOf(a, L, dst, bits);
            if (kind == AluOperation::Cmp)
                memLoadGuest(a, L, ir.super, bits, 9, slow, read, &dst);
            else {
                memProbe(a, L, ir.super, bits / 8, true, slow);
                commitEaBeforeAccess(a, L, dst, bits, read);
                loadGuest(a, bits, 9);
            }
        } else {
            loadSized(a, L, 9, false, unsigned(dst.reg), bits);
        }
        a.movW(10, jit::immediateValue(in, sem.sizeIndex));
        emitAluResult(a, L, kind, bits,
                      !dst.memory && kind != AluOperation::Cmp,
                      false, unsigned(dst.reg), kind != AluOperation::Cmp);
        if (dst.memory) {
            if (kind != AluOperation::Cmp) storeGuest(a, bits, 11);
            commitEaAfterAccess(a, L, dst, bits,
                                kind == AluOperation::Cmp ? read : write);
        }
        return true;
    }

    if (sem.operation == SemanticOp::SetCondition) { // Scc <ea>
        const int mode = sem.eaMode, reg = sem.eaReg;
        const int cc = sem.condition;
        const auto materialise = [&] {
            const int isTrue = a.label(), done = a.label();
            a.movW(11, 0);
            branchIfCond(a, L, cc, isTrue);
            a.b(done);
            a.bind(isTrue); a.movW(11, 0xFF);
            a.bind(done);
        };

        if (mode == 0) {
            if (in.words != 1 || traced030(L, in) != 4 ||
                !memory.complete()) return false;
            materialise();
            a.strB(11, 0, L.d + unsigned(reg) * 4);
            return true;
        }

        Ea dst;
        if (!decodeEa(in, mode, reg, 8, 0, dst) || !dst.memory ||
            dst.idx == EA_DIPC || dst.idx == EA_IM ||
            in.words != unsigned(1 + dst.ext) || slow < 0) return false;
        // execSccEa, 68020 cycle column. The brief-indexed form is native on
        // AArch64 and costs 13; full-format indexed plans remain rejected by
        // decodeEa().
        const int cycles = kScc[dst.idx];
        if (cycles < 0 || traced030(L, in) != unsigned(cycles)) return false;
        const MemoryAccessPlan write = memory.access(
            MemoryDirection::Write, MemoryOperand::Destination, 1,
            uint8_t(mode), uint8_t(reg));
        if (!write.valid() || !memory.complete()) return false;

        materialise();
        a.strW(11, 1, 72);
        addrOf(a, L, dst, 8);
        memStoreGuest(a, L, ir.super, 8, 11, slow, write, &dst);
        commitEaAfterAccess(a, L, dst, 8, write);
        return true;
    }

    if (sem.operation == SemanticOp::AddSubQuick) {
        const int sz = sem.sizeIndex, mode = sem.eaMode;
        if (sz > 2) return false;
        int imm = sem.registerIndex; if (!imm) imm = 8;
        const bool sub = sem.alu == AluOperation::Sub;
        const int encodedBits = bitsForSizeIndex(sz);
        const int bits = mode == 1 ? 32 : encodedBits;
        Ea dst;
        // ADDQ.W/SUBQ.W An are encoded as word operations but execute on the
        // complete address register.  The IR's decoded-EA key therefore uses
        // the encoded width, while the actual load/store remains 32-bit.
        // Asking decodeEa() for 32 bits made every word-sized An form miss its
        // already-decoded plan and fall back despite a complete lowering.
        if (!decodeEa(in, mode, sem.eaReg, encodedBits, 0, dst) ||
            dst.idx == EA_IM || dst.idx == EA_DIPC ||
            in.words != unsigned(1 + dst.ext)) return false;
        const int base = kEaRead[dst.idx][bits == 8 ? 0 : bits == 16 ? 1 : 2];
        const int cycles = dst.idx <= EA_AN ? 2 : base + 2;
        // The 030 tracer used to expose only the total clock delta. A cache
        // miss therefore made this register-only form look more expensive
        // than its architectural two cycles and forced a fallback. This is
        // the first deliberately narrow consumer of the split timing: An
        // has no data access, update ordering, or restartable write hidden
        // behind that lower cost.
        const unsigned tracedCycles = traced030(L, in);
        if (base < 0 || tracedCycles != unsigned(cycles)) return false;
        MemoryAccessPlan read, write;
        if (dst.memory) {
            read = memory.access(MemoryDirection::Read,
                                 MemoryOperand::Destination,
                                 uint8_t(bits / 8), uint8_t(mode),
                                 sem.eaReg);
            write = memory.access(MemoryDirection::Write,
                                  MemoryOperand::Destination,
                                  uint8_t(bits / 8), uint8_t(mode),
                                  sem.eaReg);
            if (!memoryRmwAccessPair(read, write))
                return false;
        }
        if (!memory.complete()) return false;
        if (dst.memory) {
            if (slow < 0) return false;
            addrOf(a, L, dst, bits);
            memProbe(a, L, ir.super, bits / 8, true, slow);
            commitEaBeforeAccess(a, L, dst, bits, read);
            loadGuest(a, bits, 9);
        } else {
            loadSized(a, L, 9, dst.idx == EA_AN, unsigned(dst.reg), bits);
        }
        a.movW(10, unsigned(imm));
        if (dst.idx == EA_AN) {
            if (sub) a.subW(11, 9, 10); else a.addW(11, 9, 10);
            a.strW(11, 0, L.a + unsigned(dst.reg) * 4);
            return true;                            // address form: no flags
        }
        emitAluResult(a, L, sub ? AluOperation::Sub : AluOperation::Add,
                      bits, !dst.memory, false, unsigned(dst.reg), true);
        if (dst.memory) {
            storeGuest(a, bits, 11);
            commitEaAfterAccess(a, L, dst, bits, write);
        }
        return true;
    }

    if (sem.operation == SemanticOp::AluEaToReg ||
        sem.operation == SemanticOp::AluRegToEa ||
        sem.operation == SemanticOp::AddressAlu) {
        const int direction = sem.operation == SemanticOp::AluRegToEa ? 1 : 0;
        const int mode = sem.eaMode;

        if (sem.operation == SemanticOp::AddressAlu) { // ADDA/SUBA/CMPA
            const int srcBits = bitsForSizeIndex(sem.sizeIndex);
            const int sz = srcBits == 16 ? 1 : 2;
            Ea src;
            // Full-format DIRECT source opts in here, like LEA — but the
            // opt-in alone is inert, and that was measured rather than
            // assumed: enabling it changed exactly zero of D1F0's 42.8 M
            // fallbacks under SimCity 2000 (CHANGELOG 2026-08-27). The refusal
            // is the COST, not the decode. `kEaRead` holds the brief
            // `(d8,An,Xn)` price; the admitted sub-form and its surcharge are
            // the 68k's, so both live in JitCost.h (`fullFormatReadExtra`)
            // where the x64 backend reads the same two values.
            if (!decodeEa(in, mode, sem.eaReg, srcBits, 0, src, true, false) ||
                in.words != unsigned(1 + src.ext)) return false;
            int fullFormatExtra = 0;
            if (src.fullFormat) {
                fullFormatExtra = fullFormatReadExtra(src);
                if (fullFormatExtra < 0) return false;
            }
            const int readCycles = kEaRead[src.idx][sz] < 0
                                 ? kEaRead[src.idx][sz]
                                 : kEaRead[src.idx][sz] + fullFormatExtra;
            // Moira's 68020 timing table charges CMPA two cycles beyond
            // ADDA/SUBA for every EA. The x64 emitter has enforced that
            // distinction since 2026-08-09; omitting it here made A64 reject
            // every CMPA despite already having a complete lowering (about
            // one million short-budget fallbacks). The trace remains the
            // authority, so a future core timing change still refuses code
            // instead of silently mischarging it.
            const int cycles = readCycles +
                (sem.alu == AluOperation::Cmp ? kCmpaExtraCycles : 0);
            MemoryAccessPlan read;
            if (src.memory) {
                read = memory.access(MemoryDirection::Read,
                                     MemoryOperand::Source,
                                     uint8_t(srcBits / 8), uint8_t(mode),
                                     sem.eaReg);
                if (!read.valid()) return false;
            }
            if (!memory.complete()) return false;
            const bool soleReadTiming = src.memory && readCycles >= 0 &&
                admitSoleReadTiming(L, in, read, unsigned(cycles),
                                    nativeCycles);
            if (readCycles < 0 || (traced030(L, in) != unsigned(cycles) &&
                                   !soleReadTiming)) return false;
            if (src.memory) {
                if (slow < 0) return false;
                addrOf(a, L, src, srcBits);
                memLoadGuest(a, L, ir.super, srcBits, 10, slow, read, &src);
                commitEaAfterAccess(a, L, src, srcBits, read);
            } else if (src.idx == EA_IM) a.movW(10, uint32_t(src.value));
            else loadSized(a, L, 10, src.idx == EA_AN, unsigned(src.reg), srcBits);
            if (srcBits == 16) a.sxtH(10, 10);
            a.ldrW(9, 0, L.a + sem.registerIndex * 4);
            const AluOperation kind = sem.alu;
            if (kind == AluOperation::Cmp) emitAddSubResult(a, 32, true);
            else if (kind == AluOperation::Add) a.addW(11, 9, 10);
            else a.subW(11, 9, 10);
            if (kind != AluOperation::Cmp)
                a.strW(11, 0, L.a + sem.registerIndex * 4);
            else
                emitAddSubFlags(a, L, true, false);
            return true;
        }

        const int bits = bitsForSizeIndex(sem.sizeIndex);
        if (bits == 8 && mode == 1) return false;
        const AluOperation kind = sem.alu;
        if (kind == AluOperation::None) return false;
        const unsigned dn = sem.registerIndex;
        if (direction == 0) {
            const int sz = bits == 8 ? 0 : bits == 16 ? 1 : 2;
            Ea src;
            if (!decodeEa(in, mode, sem.eaReg, bits, 0, src, true, true) ||
                in.words != unsigned(1 + src.ext)) return false;
            const bool indirectSource = src.fullFormat &&
                                        src.indirect != IndexIndirect::None;
            if (indirectSource && L.is030) return false;
            const int penalty = src.fullFormat ? fullIndexPenalty(src) : 0;
            const int cycles = kEaRead[src.idx][sz] + penalty;
            MemoryAccessPlan pointerRead, read;
            if (src.memory) {
                if (indirectSource)
                    pointerRead = memory.access(
                        MemoryDirection::Read, MemoryOperand::Control, 4,
                        uint8_t(mode), sem.eaReg);
                read = memory.access(MemoryDirection::Read,
                                     MemoryOperand::Source,
                                     uint8_t(bits / 8), uint8_t(mode),
                                     sem.eaReg);
                if (!read.valid() ||
                    (indirectSource && !pointerRead.valid())) return false;
            }
            if (!memory.complete()) return false;
            const bool soleReadTiming = src.memory && !indirectSource &&
                cycles >= 0 &&
                admitSoleReadTiming(L, in, read, unsigned(cycles),
                                    nativeCycles);
            if (cycles < 0 || (traced030(L, in) != unsigned(cycles) &&
                               !soleReadTiming)) return false;
            if (indirectSource) {
                if (slow < 0 || !pointerRead.preflight || !read.preflight ||
                    memory.proof.protocol != MemoryProofProtocol::PreflightAll ||
                    in.memory.order != MemoryOrder::Sequential)
                    return false;
                addrOfFullIndirectPointer(a, L, src);
                memProbe(a, L, ir.super, 4, false, slow);
                loadGuest(a, 32, 10);
                finishFullIndirect(a, L, src, 10);
                memLoadGuest(a, L, ir.super, bits, 10, slow, read);
            } else if (src.memory) {
                if (slow < 0) return false;
                addrOf(a, L, src, bits);
                memLoadGuest(a, L, ir.super, bits, 10, slow, read, &src);
                commitEaAfterAccess(a, L, src, bits, read);
            } else if (src.idx == EA_IM) a.movW(10, uint32_t(src.value));
            else loadSized(a, L, 10, src.idx == EA_AN, unsigned(src.reg), bits);
            loadSized(a, L, 9, false, dn, bits);       // destination
            return emitAluResult(a, L, kind, bits, kind != AluOperation::Cmp,
                                 false, dn, kind != AluOperation::Cmp);
        }
        if (mode == 0) {                                // EOR Dn,Dm
            if (kind != AluOperation::Eor || in.words != 1 ||
                traced030(L, in) != 2)
                return false;
            loadSized(a, L, 9, false, sem.eaReg, bits);
            loadSized(a, L, 10, false, dn, bits);
            return emitAluResult(a, L, kind, bits, true, false,
                                 sem.eaReg, false);
        }
        // Register-to-memory ALU: one writable translation serves the RMW,
        // so the fallback is still entered before any guest-visible change.
        const int sz = bits == 8 ? 0 : bits == 16 ? 1 : 2;
        Ea dst;
        if (!decodeEa(in, mode, sem.eaReg, bits, 0, dst) || !dst.memory ||
            dst.idx == EA_DIPC || in.words != unsigned(1 + dst.ext)) return false;
        const int cycles = kEaRead[dst.idx][sz] + 2;
        if (traced030(L, in) != unsigned(cycles) || slow < 0) return false;
        const MemoryAccessPlan read = memory.access(
            MemoryDirection::Read, MemoryOperand::Destination,
            uint8_t(bits / 8), uint8_t(mode), sem.eaReg);
        const MemoryAccessPlan write = memory.access(
            MemoryDirection::Write, MemoryOperand::Destination,
            uint8_t(bits / 8), uint8_t(mode), sem.eaReg);
        if (!memoryRmwAccessPair(read, write) || !memory.complete())
            return false;
        addrOf(a, L, dst, bits);
        memProbe(a, L, ir.super, bits / 8, true, slow);
        commitEaBeforeAccess(a, L, dst, bits, read);
        loadGuest(a, bits, 9);
        loadSized(a, L, 10, false, dn, bits);
        emitAluResult(a, L, kind, bits, false, false, 0,
                      kind == AluOperation::Add || kind == AluOperation::Sub);
        storeGuest(a, bits, 11);
        commitEaAfterAccess(a, L, dst, bits, write);
        return true;
    }

    if (sem.operation == SemanticOp::Link ||
        sem.operation == SemanticOp::Unlink ||
        sem.operation == SemanticOp::Lea ||
        sem.operation == SemanticOp::Pea ||
        sem.operation == SemanticOp::Movem ||
        sem.operation == SemanticOp::Extend ||
        sem.operation == SemanticOp::Swap ||
        sem.operation == SemanticOp::Test ||
        sem.operation == SemanticOp::Clear ||
        sem.operation == SemanticOp::Negate ||
        sem.operation == SemanticOp::Complement) {
        const int mode = sem.eaMode, sz = sem.sizeIndex;
        if (sem.operation == SemanticOp::Link) {     // LINK.W An,#d16
            if (traced030(L, in) != 5 || in.words != 2 || slow < 0)
                return false;
            const unsigned an = sem.eaReg;
            const MemoryAccessPlan write = memory.access(
                MemoryDirection::Write, MemoryOperand::Stack, 4, 4, 7);
            if (!write.valid() || !memory.complete() ||
                write.eaCommit != EaCommit::BeforeAccess)
                return false;
            const int32_t disp = int16_t(in.extensionWord(0));
            a.ldrW(9, 0, L.a + 7 * 4); a.subImmW(9, 9, 4);
            a.strW(9, 1, 44);                       // new frame/address
            if (an == 7) a.movRegW(11, 9);
            else a.ldrW(11, 0, L.a + an * 4);
            a.strW(11, 1, 72);
            memStoreGuest(a, L, ir.super, 32, 11, slow, write);
            a.ldrW(9, 1, 44); a.strW(9, 0, L.a + an * 4);
            a.movW(10, uint32_t(disp)); a.addW(9, 9, 10);
            a.strW(9, 0, L.a + 7 * 4);
            return true;
        }
        if (sem.operation == SemanticOp::Unlink) {   // UNLK An
            if (traced030(L, in) != 6 || in.words != 1 || slow < 0)
                return false;
            const unsigned an = sem.eaReg;
            const MemoryAccessPlan read = memory.access(
                MemoryDirection::Read, MemoryOperand::Stack, 4, 2,
                uint8_t(an));
            if (!read.valid() || !memory.complete()) return false;
            a.ldrW(9, 0, L.a + an * 4); a.strW(9, 1, 44);
            memLoadGuest(a, L, ir.super, 32, 11, slow, read);
            a.strW(11, 0, L.a + an * 4);
            if (an != 7) {
                a.ldrW(9, 1, 44); a.addImmW(9, 9, 4);
                a.strW(9, 0, L.a + 7 * 4);
            }
            return true;
        }
        if (sem.operation == SemanticOp::Lea) {      // LEA <ea>,An
            Ea src;
            if (!decodeEa(in, mode, sem.eaReg, 32, 0, src, true, true) ||
                !src.memory ||
                src.idx == EA_PI || src.idx == EA_PD ||
                in.words != unsigned(1 + src.ext)) return false;
            const int penalty = src.fullFormat ? fullIndexPenalty(src) : 0;
            const unsigned fixedCycles = unsigned(kLea[src.idx] + penalty);
            MemoryAccessPlan pointerRead;
            if (src.indirect != IndexIndirect::None)
                pointerRead = memory.access(
                    MemoryDirection::Read, MemoryOperand::Control, 4,
                    uint8_t(mode), sem.eaReg);
            if (!memory.complete()) return false;
            if (src.indirect != IndexIndirect::None) {
                if (L.is030 || !pointerRead.valid() || slow < 0) return false;
                if (!admitSoleReadTiming(L, in, pointerRead, fixedCycles,
                                         nativeCycles))
                    return false;
                addrOfFullIndirectPointer(a, L, src);
                memLoadGuest(a, L, ir.super, 32, 11, slow, pointerRead);
                finishFullIndirect(a, L, src, 11);
            } else {
                if (traced030(L, in) != fixedCycles) return false;
                addrOf(a, L, src, 32);
            }
            a.strW(9, 0, L.a + sem.registerIndex * 4);
            return true;
        }
        if (sem.operation == SemanticOp::Pea) {      // PEA <ea>
            // Compute the source address before touching A7: PEA (A7) and
            // PEA d16(A7) use the old stack pointer as their source base.
            Ea src;
            if (!decodeEa(in, mode, sem.eaReg, 32, 0, src) || !src.memory ||
                src.idx == EA_PI || src.idx == EA_PD ||
                in.words != unsigned(1 + src.ext) || slow < 0) return false;
            if (kPea[src.idx] < 0 ||
                traced030(L, in) != unsigned(kPea[src.idx])) return false;
            const MemoryAccessPlan write = memory.access(
                MemoryDirection::Write, MemoryOperand::Stack, 4, 4, 7);
            if (!write.valid() || !memory.complete()) return false;

            addrOf(a, L, src, 32);
            a.movRegW(11, 9); a.strW(11, 1, 72);
            a.ldrW(9, 0, L.a + 7 * 4); a.subImmW(9, 9, 4);
            a.strW(9, 1, 44);
            memStoreGuest(a, L, ir.super, 32, 11, slow, write);
            a.ldrW(9, 1, 44); a.strW(9, 0, L.a + 7 * 4);
            return true;
        }
        if (sem.operation == SemanticOp::Movem) {
            // The 030's MOVEM contract is the format-$B RESUME: a fault in
            // flight stacks the count/EA (`mmuState`) and RTE continues
            // mid-instruction. Native MOVEM proves EVERY byte of the span
            // before the first access (the OrderedSpan preflight below),
            // so no fault can occur in flight and the resume state is never
            // observable; a span the DTLB cannot prove in one page bails to
            // the untouched instruction and Moira stacks the real frame.
            // Refused outright until 2026-08-23 (JIT_BRINGUP § C.4.4 —
            // 68 % of the idle Finder's remaining fallbacks); x64 keeps its
            // guard until its 030 lockstep runs on an x86-64 host.
            const bool toRegs = sem.toRegisters;
            const int bits = bitsForSizeIndex(sem.sizeIndex), bytes = bits / 8;
            const uint16_t mask = in.extensionWord(0);
            if (!mask || slow < 0) { watchRefusal(L, ir, in, "movem:mask/slow"); return false; }
            Ea ea;
            if (!decodeEa(in, mode, sem.eaReg, bits, 1, ea) || !ea.memory ||
                in.words != unsigned(2 + ea.ext)) { watchRefusal(L, ir, in, "movem:ea"); return false; }
            if ((toRegs && ea.idx == EA_PD) ||
                (!toRegs && (ea.idx == EA_PI || ea.idx == EA_DIPC))) { watchRefusal(L, ir, in, "movem:mode"); return false; }
            int n = 0; for (int b = 0; b < 16; b++) n += (mask >> b) & 1;
            const int baseCost = toRegs ? kMovemToRegs[ea.idx]
                                        : kMovemToMem[ea.idx];
            if (baseCost < 0 ||
                traced030(L, in) != unsigned(baseCost + 4 * n)) { watchRefusal(L, ir, in, "movem:cost"); return false; }
            const MemoryAccessPlan span = memory.access(
                toRegs ? MemoryDirection::Read : MemoryDirection::Write,
                MemoryOperand::RegisterList, uint8_t(bytes), uint8_t(mode),
                sem.eaReg);
            const MemoryOrder emittedOrder = ea.idx == EA_PD
                ? MemoryOrder::RegisterDescending
                : MemoryOrder::RegisterAscending;
            if (!span.valid() || !span.preflight || !memory.complete() ||
                memory.proof.protocol != MemoryProofProtocol::OrderedSpan ||
                in.memory.order != emittedOrder ||
                span.eaCommit != EaCommit::PerElement) {
                if (watchOpcodeWanted(in.opcode))
                    std::fprintf(stderr, "[jit-watch] movem:span valid=%d preflight=%d complete=%d proto=%d order=%d/%d commit=%d\n",
                                 int(span.valid()), int(span.preflight), int(memory.complete()),
                                 int(memory.proof.protocol), int(in.memory.order), int(emittedOrder), int(span.eaCommit));
                watchRefusal(L, ir, in, "movem:span");
                return false;
            }
            a.ldrB(9, 0, L.movemArmed); a.cbnzW(9, slow);

            auto loadR = [&](int b, unsigned rd) {
                loadGuestRegister(a, L, rd, b >= 8, unsigned(b & 7));
            };
            auto storeR = [&](int b, unsigned rs) {
                a.strW(rs, 0, b < 8 ? L.d + unsigned(b) * 4
                                     : L.a + unsigned(b - 8) * 4);
            };

            if (ea.idx == EA_PD) {
                a.ldrW(9, 0, L.a + unsigned(ea.reg) * 4);
                a.movW(10, unsigned(n * bytes)); a.subW(9, 9, 10);
                a.strW(9, 1, 44);
                memProbe(a, L, ir.super, n * bytes, true, slow);
                a.ldrW(10, 0, L.a + unsigned(ea.reg) * 4);
                int j = 0;
                for (int b = 0; b < 16; b++) {
                    if (!(mask & (0x8000u >> b))) continue;
                    if (b == 8 + ea.reg) a.subImmW(11, 10, unsigned(bytes));
                    else loadR(b, 11);
                    storeGuestOff(a, bits, 11, unsigned(j++ * bytes));
                }
                a.ldrW(9, 1, 44); a.strW(9, 0, L.a + unsigned(ea.reg) * 4);
                return true;
            }

            addrOf(a, L, ea, bits); a.strW(9, 1, 44);
            memProbe(a, L, ir.super, n * bytes, !toRegs, slow);
            int j = 0;
            for (int b = 0; b < 16; b++) {
                if (!(mask & (1u << b))) continue;
                if (toRegs) {
                    loadGuestOff(a, bits, 11, unsigned(j * bytes));
                    if (bits == 16) a.sxtH(11, 11);
                    storeR(b, 11);
                } else {
                    loadR(b, 11);
                    storeGuestOff(a, bits, 11, unsigned(j * bytes));
                }
                j++;
            }
            if (ea.idx == EA_PI) {
                a.ldrW(9, 1, 44); a.addImmW(9, 9, unsigned(n * bytes));
                a.strW(9, 0, L.a + unsigned(ea.reg) * 4);
            }
            return true;
        }
        if (sem.operation == SemanticOp::Extend) {   // EXT
            if (traced030(L, in) != 4 || in.words != 1) return false;
            const unsigned dn = sem.eaReg;
            if (sem.sizeIndex == 2) {
                a.ldrH(11, 0, L.d + dn * 4); a.sxtH(11, 11);
                a.strW(11, 0, L.d + dn * 4); emitLogicFlags(a, L, 11, 32);
            } else {
                a.ldrB(11, 0, L.d + dn * 4); a.sxtB(11, 11);
                a.strH(11, 0, L.d + dn * 4); maskResult(a, 11, 16);
                emitLogicFlags(a, L, 11, 16);
            }
            return true;
        }
        if (sem.operation == SemanticOp::Swap) {     // SWAP
            if (traced030(L, in) != 4 || in.words != 1) return false;
            a.ldrW(11, 0, L.d + sem.eaReg * 4); a.rorW(11, 11, 16);
            a.strW(11, 0, L.d + sem.eaReg * 4);
            emitLogicFlags(a, L, 11, 32);
            return true;
        }
        if (sz > 2) return false;
        const int bits = bitsForSizeIndex(sz);
        if (sem.operation != SemanticOp::Test &&
            sem.operation != SemanticOp::Clear &&
            sem.operation != SemanticOp::Complement &&
            sem.operation != SemanticOp::Negate) return false;
        Ea ea;
        if (!decodeEa(in, mode, sem.eaReg, bits, 0, ea) ||
            in.words != unsigned(1 + ea.ext) || ea.idx == EA_IM ||
            (bits == 8 && ea.idx == EA_AN)) return false;
        const bool tst = sem.operation == SemanticOp::Test;
        if (!tst && ea.idx == EA_AN) return false;
        const int base = kEaRead[ea.idx][sz];
        const int cycles = tst ? base : (ea.idx <= EA_AN ? 2 : base + 2);
        // TST (An) is read-only. Ordinarily its measured base component must
        // match the inline access cost. The hot LC II 4A11 exception below
        // uses the exact MMU/device thunk, so the device reproduces the
        // variable part of that component at run time.
        const unsigned tracedCycles = traced030(L, in);
        MemoryAccessPlan read, write;
        if (ea.memory) {
            if (tst) {
                read = memory.access(MemoryDirection::Read,
                                     MemoryOperand::Operand,
                                     uint8_t(bits / 8), uint8_t(mode),
                                     sem.eaReg);
                if (!read.valid()) return false;
            } else if (sem.operation == SemanticOp::Clear) {
                write = memory.access(MemoryDirection::Write,
                                      MemoryOperand::Destination,
                                      uint8_t(bits / 8), uint8_t(mode),
                                      sem.eaReg);
                if (!write.valid()) return false;
            } else {
                read = memory.access(MemoryDirection::Read,
                                     MemoryOperand::Destination,
                                     uint8_t(bits / 8), uint8_t(mode),
                                     sem.eaReg);
                write = memory.access(MemoryDirection::Write,
                                      MemoryOperand::Destination,
                                      uint8_t(bits / 8), uint8_t(mode),
                                      sem.eaReg);
                if (!memoryRmwAccessPair(read, write))
                    return false;
            }
        }
        if (!memory.complete()) return false;
        const bool soleReadTiming = tst && ea.memory && base >= 0 &&
            admitSoleReadTiming(L, in, read, unsigned(cycles), nativeCycles);
        if (base < 0 || (tracedCycles != unsigned(cycles) &&
                         !soleReadTiming)) return false;
        if (ea.memory) {
            if (slow < 0) return false;
            addrOf(a, L, ea, bits);
            if (tst)
                memLoadGuest(a, L, ir.super, bits, 9, slow, read, &ea);
            else if (sem.operation != SemanticOp::Clear) {
                memProbe(a, L, ir.super, bits / 8, true, slow);
                commitEaBeforeAccess(a, L, ea, bits, read);
                loadGuest(a, bits, 9);
            }
        } else {
            loadSized(a, L, 9, ea.idx == EA_AN, unsigned(ea.reg), bits);
            maskResult(a, 9, bits);
        }
        switch (sem.operation) {
            case SemanticOp::Test: emitLogicFlags(a, L, 9, bits); break;
            case SemanticOp::Clear: a.movW(11, 0); break;
            case SemanticOp::Complement:
                a.mvnW(11, 9); maskResult(a, 11, bits);
                emitLogicFlags(a, L, 11, bits); break;
            case SemanticOp::Negate:
                a.movRegW(10, 9); a.movW(9, 0);
                emitAddSubResult(a, bits, true);
                emitAddSubFlags(a, L, true, true); break;
            default: return false;
        }
        if (!tst) {
            if (ea.memory) {
                if (sem.operation == SemanticOp::Clear) {
                    a.strW(11, 1, 72);
                    addrOf(a, L, ea, bits);
                    memStoreGuest(a, L, ir.super, bits, 11, slow, write, &ea);
                } else {
                    storeGuest(a, bits, 11);
                }
            }
            else storeSized(a, L, 11, false, unsigned(ea.reg), bits);
        }
        if (ea.memory)
            commitEaAfterAccess(a, L, ea, bits,
                                tst ? read : write);
        if (sem.operation == SemanticOp::Clear)
            emitLogicFlags(a, L, 11, bits);
        return true;
    }

    if (sem.operation == SemanticOp::Bitfield) {
        // These uncommon forms publish several independently calculated
        // flag bits. Until their packed lowering is proved, the exact
        // per-instruction fallback is preferable to a second CCR format.
        if (gPackedCcr) return false;
        const uint16_t ext = in.extensionWord(0);
        if ((ext & 0x0820) && !dynamicRegisterBitfieldEnabled()) return false;
        const int kind = sem.action;
        const unsigned dst = sem.eaReg, out = (ext >> 12) & 7;
        const bool readOnly = kind == 0 || kind == 1 || kind == 3 || kind == 5;

        // The drawing census's leading residual is the exact subset whose
        // offset/width can never touch the fifth byte. This includes Rogue's
        // BFEXTU (A0){Dn:8},Dn: the signed Dn offset moves the base address by
        // floor(offset/8), while its low three bits select within one long.
        // Possible five-byte forms preflight both plain-memory mappings before
        // the first read and branch around the tail byte when it is not used.
        // 040 by default; the 030 opts in via POM68K_JIT_030_MEMBF
        // (2026-08-28): both mappings are proved before either load escapes,
        // so no fault frame is ever built by generated code — the format-A/B
        // worry that kept this 040-only reduces to timing, which the
        // sole-read contract prices and the 120k lockstep oracles.
        if (sem.eaMode != 0) {
            // 68040 timings by EA class. Rogue's second-largest residual is
            // BFEXTU d16(A1){D0:D2},D1 (E9E9), hence the DI column; keeping
            // the table explicit prevents a traced wait state from being
            // mistaken for an opcode cost.
            static const int8_t memoryCycles[8][2] = {
                {17,18}, {19,20}, {24,25}, {19,20},
                {24,25}, {32,33}, {24,25}, {21,22}
            };
            const int eaColumn = sem.eaMode == 2 ? 0
                               : sem.eaMode == 5 ? 1 : -1;
            // The 030 was excluded wholesale until 2026-08-28; the opt-in
            // admits it through the SAME emission path, priced by the
            // sole-read exact-thunk contract below (admitSoleReadTiming's
            // 030 arm) rather than by this 040 cycle table — a mispriced
            // form refuses, it does not mischarge. The tail (five-byte)
            // path keeps its strict trace equality and therefore stays
            // effectively 040-only until the 030 column is measured.
            // Oracle: the 120k lockstep with POM68K_JIT_030_MEMBF=1.
            if ((L.is030 && !memBitfield030Admission()) || eaColumn < 0 ||
                memoryCycles[kind][eaColumn] < 0) {
                watchRefusal(L, ir, in, "bitfield-memory:shape");
                return false;
            }
            Ea src;
            if (!decodeEa(in, sem.eaMode, sem.eaReg, 32, 1, src) ||
                !src.memory || in.words != unsigned(2 + src.ext)) {
                watchRefusal(L, ir, in, "bitfield-memory:ea");
                return false;
            }
            const unsigned opcodeCycles =
                unsigned(memoryCycles[kind][eaColumn]);
            const bool possibleTail = !memoryBitfieldFitsLongword(ext);
            MemoryAccessPlan read = memory.access(
                MemoryDirection::Read, MemoryOperand::Operand, 4,
                sem.eaMode, sem.eaReg);
            MemoryAccessPlan write;
            if (!readOnly)
                write = memory.access(MemoryDirection::Write,
                                      MemoryOperand::Operand, 4,
                                      sem.eaMode, sem.eaReg);
            MemoryAccessPlan tail;
            if (readOnly && possibleTail)
                tail = memory.access(MemoryDirection::Read,
                                     MemoryOperand::Operand, 1,
                                     sem.eaMode, sem.eaReg);
            if (!read.valid() || (!readOnly &&
                                  (!memoryRmwAccessPair(read, write) ||
                                   possibleTail)) ||
                (readOnly && possibleTail && !tail.valid()) ||
                !memory.complete()) {
                watchRefusal(L, ir, in, "bitfield-memory:contract");
                return false;
            }
            if (readOnly && !possibleTail) {
                MemoryAccessPlan timedRead = read;
                if (!admitSoleReadTiming(L, in, timedRead,
                                         opcodeCycles,
                                         nativeCycles)) {
                    watchRefusal(L, ir, in, "bitfield-memory:timing");
                    return false;
                }
                read = timedRead;
            } else if (traced030(L, in) != opcodeCycles ||
                       in.postExceptionCycles != 0) {
                watchRefusal(L, ir, in, "bitfield-memory:timing");
                return false;
            }
            addrOf(a, L, src, 32);
            if (ext & 0x0800)
                loadGuestRegister(a, L, 16, false, (ext >> 6) & 7);
            else
                a.movW(16, (ext >> 6) & 31);
            a.asrW(12, 16, 3);                    // floor(rawOffset / 8)
            a.addW(9, 9, 12);                     // 32-bit guest wraparound
            if (!readOnly) {
                // One writable proof serves the tailless read-modify-write.
                // Save both the host pointer and the original longword:
                // flag construction uses w14 for the bit offset and the
                // insertion path needs the pristine destination afterwards.
                memProbe(a, L, ir.super, 4, true, slow);
                commitEaBeforeAccess(a, L, src, 32, read);
                loadGuest(a, 32, 11);
                a.strX(14, 1, 120);
                a.strW(11, 1, 72);
            } else if (!possibleTail) {
                memLoadGuest(a, L, ir.super, 32, 11, slow, read, &src);
            } else {
                // Save all values that a DTLB fill helper may clobber. The
                // first successful probe is not itself an observable read;
                // the second mapping is therefore proved before either byte
                // sequence can escape, preventing replay of a side effect.
                a.strW(9, 1, 44);                 // adjusted guest address
                a.strW(16, 1, 48);                // uncropped raw offset
                if (ext & 0x0020)
                    loadGuestRegister(a, L, 17, false, ext & 7);
                else
                    a.movW(17, ext & 31);
                a.subImmW(17, 17, 1);
                a.movW(12, 31); a.andW(17, 17, 12);
                a.addImmW(17, 17, 1);
                a.strW(17, 1, 76);                // normalized width

                memProbe(a, L, ir.super, 4, false, slow);
                a.strX(14, 1, 120);               // first host pointer

                const int noTail = a.label(), haveData = a.label();
                a.ldrW(16, 1, 48);
                a.movW(12, 7); a.andW(14, 16, 12);
                a.ldrW(17, 1, 76);
                a.addW(12, 14, 17);
                a.movW(13, 32); a.cmpW(12, 13);
                a.bCond(Asm::LS, noTail);

                a.ldrW(9, 1, 44); a.addImmW(9, 9, 4);
                memProbe(a, L, ir.super, 1, false, slow);
                loadGuest(a, 8, 10);
                a.ldrX(14, 1, 120); loadGuest(a, 32, 11);
                a.b(haveData);

                a.bind(noTail);
                a.movW(10, 0);
                a.ldrX(14, 1, 120); loadGuest(a, 32, 11);
                a.bind(haveData);
            }

            // A DTLB fill may call C++, so rebuild all extension-derived
            // temporaries after the access rather than carrying caller-saved
            // host registers across it.
            if (ext & 0x0800)
                loadGuestRegister(a, L, 16, false, (ext >> 6) & 7);
            else
                a.movW(16, (ext >> 6) & 31);
            a.movW(12, 7); a.andW(14, 16, 12);     // bit offset in longword
            if (ext & 0x0020)
                loadGuestRegister(a, L, 17, false, ext & 7);
            else
                a.movW(17, ext & 31);
            a.subImmW(17, 17, 1);
            a.movW(12, 31); a.andW(17, 17, 12);
            a.addImmW(17, 17, 1);
            a.movW(13, 32); a.subW(13, 13, 17);
            a.lslVarW(11, 11, 14);
            if (possibleTail) {
                a.lslVarW(10, 10, 14);
                a.lsrW(10, 10, 8);
                a.orrW(11, 11, 10);
            }
            a.lsrVarW(11, 11, 13);                 // selected field

            if (kind != 7) {                       // BFINS uses source flags
                emitNz(a, L, 11, 32);
                a.subImmW(12, 17, 1);
                a.lsrVarW(12, 11, 12); a.strB(12, 0, L.srN);
                a.movW(12, 0); a.strB(12, 0, L.srV); a.strB(12, 0, L.srC);
            }
            if (readOnly) {
                if (kind == 0) return true;         // BFTST
                if (kind == 3) {                   // BFEXTS
                    a.lslVarW(11, 11, 13);
                    a.asrVarW(11, 11, 13);
                } else if (kind == 5) {            // BFFFO
                    a.clzW(12, 11);
                    a.subW(11, 12, 13);
                    a.addW(11, 11, 16);            // + uncropped raw offset
                }
                a.strW(11, 0, L.d + out * 4);
                return true;
            }

            // A memory field never wraps inside its adjusted longword: its
            // top-width mask is shifted right by the residual 0..7 offset.
            // BFCHG/BFCLR/BFSET already published flags from the old field;
            // BFINS replaces them with the cropped, top-aligned source.
            a.ldrW(9, 1, 72);                      // original destination
            if (kind == 7) {
                a.ldrW(11, 0, L.d + out * 4);
                a.lslVarW(11, 11, 13);             // crop + top-align source
                emitNz(a, L, 11, 32);
                a.lsrW(12, 11, 31); a.strB(12, 0, L.srN);
                a.movW(12, 0); a.strB(12, 0, L.srV); a.strB(12, 0, L.srC);
                a.lsrVarW(11, 11, 14);             // align inside longword
            }
            a.movW(12, 0xFFFFFFFFu);
            a.lslVarW(12, 12, 13);
            a.lsrVarW(12, 12, 14);                 // destination field mask
            if (kind == 2) a.eorW(9, 9, 12);       // BFCHG
            else if (kind == 4) { a.mvnW(12, 12); a.andW(9, 9, 12); } // BFCLR
            else if (kind == 6) a.orrW(9, 9, 12);  // BFSET
            else {                                  // BFINS
                a.mvnW(12, 12); a.andW(9, 9, 12); a.orrW(9, 9, 11);
            }
            a.ldrX(14, 1, 120);
            storeGuest(a, 32, 9);
            commitEaAfterAccess(a, L, src, 32, write);
            return true;
        }

        if (in.words != 2) return false;
        static const uint8_t cycles[8] = {6,8,12,8,12,18,12,10};
        if (traced030(L, in) != cycles[kind]) return false;
        a.ldrW(9, 0, L.d + dst * 4);                // original destination

        // w14 = offset modulo 32; w16 preserves rawOffset for BFFFO, whose
        // result starts at the un-cropped Dn value exactly as Moira does.
        if (ext & 0x0800) {
            loadGuestRegister(a, L, 14, false, (ext >> 6) & 7);
            a.movRegW(16, 14);
            a.movW(12, 31); a.andW(14, 14, 12);
        } else {
            const unsigned offset = (ext >> 6) & 31;
            a.movW(14, offset); a.movW(16, offset);
        }

        // w17 = ((rawWidth - 1) & 31) + 1, i.e. 32,1..31. x15 is the
        // assembler's large-layout-offset scratch and cannot hold live state
        // across the architectural flag stores below. This formulation
        // handles both immediate zero and every Dn value without
        // a generated branch. w13 keeps 32-width for extraction/masking.
        if (ext & 0x0020)
            loadGuestRegister(a, L, 17, false, ext & 7);
        else
            a.movW(17, ext & 31);
        a.subImmW(17, 17, 1);
        a.movW(12, 31); a.andW(17, 17, 12);
        a.addImmW(17, 17, 1);
        a.movW(13, 32); a.subW(13, 13, 17);         // 32 - width

        // rotl(original,offset), then right-justify the selected field.
        // A64 variable shifts mask their count to five bits, matching the
        // register-direct bitfield rules for offset 0 and width 32.
        a.subW(12, 31, 14);                         // -offset modulo 32
        a.rorVarW(10, 9, 12);
        a.lsrVarW(11, 10, 13);                      // extracted field

        if (kind != 7) {                            // BFINS uses source flags
            emitNz(a, L, 11, 32);
            a.subImmW(12, 17, 1);
            a.lsrVarW(12, 11, 12); a.strB(12, 0, L.srN);
            a.movW(12, 0); a.strB(12, 0, L.srV); a.strB(12, 0, L.srC);
        }

        if (kind == 0) return true;                 // BFTST
        if (kind == 1 || kind == 3) {               // BFEXTU / BFEXTS
            if (kind == 3) {
                a.lslVarW(11, 11, 13);
                a.asrVarW(11, 11, 13);
            }
            a.strW(11, 0, L.d + out * 4);
            return true;
        }
        if (kind == 5) {                            // BFFFO
            a.clzW(12, 11);
            a.subW(12, 12, 13);                    // zeros inside the field
            a.addW(12, 12, 16);                    // + rawOffset
            a.strW(12, 0, L.d + out * 4);
            return true;
        }

        // top-width mask rotated right by the cropped field offset.
        a.movW(12, 0xFFFFFFFFu);
        a.lslVarW(12, 12, 13);
        a.rorVarW(12, 12, 14);
        if (kind == 2) a.eorW(9, 9, 12);            // BFCHG
        else if (kind == 4) { a.mvnW(12, 12); a.andW(9, 9, 12); } // BFCLR
        else if (kind == 6) a.orrW(9, 9, 12);       // BFSET
        else {                                      // BFINS
            a.ldrW(11, 0, L.d + out * 4);
            a.lslVarW(11, 11, 13);                 // crop + top-align source
            emitNz(a, L, 11, 32);
            a.lsrW(12, 11, 31); a.strB(12, 0, L.srN);
            a.movW(12, 0); a.strB(12, 0, L.srV); a.strB(12, 0, L.srC);
            a.rorVarW(11, 11, 14);
            // emitNz clobbers w12, so reconstruct the destination mask.
            a.movW(12, 0xFFFFFFFFu);
            a.lslVarW(12, 12, 13);
            a.rorVarW(12, 12, 14);
            a.mvnW(12, 12); a.andW(9, 9, 12); a.orrW(9, 9, 11);
        }
        a.strW(9, 0, L.d + dst * 4);
        return true;
    }

    if (sem.operation == SemanticOp::ShiftRegister) {
        if (gPackedCcr) return false;
        const int sz = sem.sizeIndex, type = sem.action;
        if (sz > 2 || type == 2 || in.words != 1) return false;
        const int bits = bitsForSizeIndex(sz);
        int count = sem.registerIndex;
        if (sem.dynamic) {
            // A runtime cycle increment inside the generated body passed the
            // synthetic architectural lockstep but moved Q605 peripheral
            // phase enough to prevent Finder boot. Keep the useful Rogue
            // case without that timing ambiguity: specialize the block for
            // the traced count, guard it before changing any state, and replay
            // through Moira if Dn later changes. Limiting the unroll to eight
            // bounds code size; E5A8's hot drawing sites use four.
            if (slow < 0) return false;
            const unsigned dynamicBase = type == 3 ||
                (type == 0 && sem.left) ? 8u : 6u;
            const unsigned traced = traced030(L, in);
            if (traced < dynamicBase || traced > dynamicBase + 8)
                return false;
            count = int(traced - dynamicBase);
            loadGuestRegister(a, L, 9, false, sem.registerIndex);
            a.movW(10, 63); a.andW(9, 9, 10);
            a.movW(10, unsigned(count)); a.cmpW(9, 10);
            a.bCond(Asm::NE, slow);
        } else if (!count) {
            count = 8;
        }
        const bool left = sem.left;
        const int expected = sem.dynamic
                           ? (type == 3 || (type == 0 && left) ? 8 : 6) + count
                           : type == 1 ? 4 : type == 3 ? 8 : left ? 8 : 6;
        if (traced030(L, in) != unsigned(expected)) return false;
        const unsigned dn = sem.eaReg;
        loadSized(a, L, 11, false, dn, bits); maskResult(a, 11, bits);
        a.movW(13, 0);                              // accumulated ASL overflow
        if (!count) a.movW(10, 0);                  // dynamic count zero: C=0
        for (int k = 0; k < count; k++) {
            if (left) {
                a.lsrW(10, 11, unsigned(bits - 1)); // outgoing bit
                a.lslW(12, 11, 1);
                if (bits != 32) { a.movW(9, bits == 8 ? 0xFFu : 0xFFFFu); a.andW(12, 12, 9); }
                if (type == 3) a.orrW(12, 12, 10);  // ROL
                if (type == 0) {                    // ASL sign changed?
                    a.lsrW(9, 12, unsigned(bits - 1));
                    a.eorW(9, 9, 10); a.orrW(13, 13, 9);
                }
            } else {
                a.movW(9, 1); a.andW(10, 11, 9);   // outgoing bit
                if (type == 0) {
                    if (bits == 8) a.sxtB(12, 11);
                    else if (bits == 16) a.sxtH(12, 11);
                    else a.movRegW(12, 11);
                    a.asrW(12, 12, 1);
                    if (bits != 32) { a.movW(9, bits == 8 ? 0xFFu : 0xFFFFu); a.andW(12, 12, 9); }
                } else {
                    a.lsrW(12, 11, 1);
                    if (type == 3) {
                        a.lslW(9, 10, unsigned(bits - 1)); a.orrW(12, 12, 9);
                    }
                }
            }
            a.movRegW(11, 12);
        }
        storeSized(a, L, 11, false, dn, bits);
        emitNz(a, L, 11, bits);
        a.strB(10, 0, L.srC);
        if (type != 3 && count) a.strB(10, 0, L.srX);
        if (type == 0 && left) a.strB(13, 0, L.srV);
        else { a.movW(12, 0); a.strB(12, 0, L.srV); }
        return true;
    }
    return false;
}

void commitBoundary(Asm& a, const Layout& L, uint32_t pc) {
    a.movW(9, pc); a.strW(9, 0, L.pc); a.strW(9, 0, L.pc0);
}

// w29 is callee-saved by the generated prologue and therefore survives both
// ordinary C++ calls and direct block links. Keep the terminal prefetch queue
// there between observable boundaries: publishing it to Moira after every
// native instruction was a hot two-instruction memory dependency even when
// execution continued directly into another native instruction.
void loadQueueLive(Asm& a, const Layout& L, unsigned scratch) {
    if constexpr (std::endian::native == std::endian::little) {
        if (L.ird == L.irc + 2) {
            a.ldrW(29, 0, L.irc);
            return;
        }
    }
    a.ldrH(29, 0, L.irc);
    a.ldrH(scratch, 0, L.ird);
    a.lslW(scratch, scratch, 16);
    a.orrW(29, 29, scratch);
}

void spillQueueLive(Asm& a, const Layout& L, unsigned scratch) {
    // PrefetchQueue is {u16 irc,u16 ird}. A64 permits an unaligned ordinary
    // word store. The live register is packed as ird:irc so the common
    // little-endian layout needs one store; unusual layouts and big-endian
    // hosts retain a field-wise publication.
    if constexpr (std::endian::native == std::endian::little) {
        if (L.ird == L.irc + 2) {
            a.strW(29, 0, L.irc);
            return;
        }
    }
    a.strH(29, 0, L.irc);
    a.lsrW(scratch, 29, 16);
    a.strH(scratch, 0, L.ird);
}

void commitQueue(Asm& a, const Layout&, uint16_t ird, uint16_t irc,
                 unsigned) {
    a.movW(29, uint32_t(ird) << 16 | irc);
}

void chargeAndRetire(Asm& a, const Layout& L, unsigned cycles,
                     bool paced, int batch, uint32_t,
                     uint32_t, bool) {
    if (paced) {
        // Cpu040::sync's batching decision, inlined. The guest clock stays
        // canonical in Moira memory, so cold calls and linked blocks need no
        // callee-saved clock register protocol.
        const int done = a.label();
        if (cycles) a.addImmX(21, 21, cycles);
        if (batch < 0) {
            a.cmpX(21, 19);              // absolute next-event deadline
        } else {
            a.subX(11, 21, 19);
            a.movX(12, unsigned(batch));
            a.cmpX(11, 12);
        }
        a.bCond(Asm::LT, done);
        a.strX(21, 0, L.clock);
        if (icacheCountersLive(L)) spillIcacheCounters(a, L);
        if (gDirectPeriphDue) {
            // The inline x21 >= deadline comparison has already established
            // the wrapper's catchUp() condition. Call its due handler
            // directly instead of paying virtual sync(0) and repeating it.
            a.ldrX(16, 1, 184);          // Frame::periphDue
        } else {
            a.movW(1, 0);                // cycles already charged
            a.movX(16, uint64_t(uintptr_t(&pom68kA64Sync)));
        }
        spillQueueLive(a, L);
        spillPackedCcr(a, L);
        a.blr(16);
        a.emit(0xA94207E0u);             // ldp x0,x1,[sp,#32]
        reloadPackedCcr(a, L);
        a.ldrX(21, 0, L.clock);
        reloadGeneratedState(a, L);
        a.bind(done);
    } else {
        // x21 is the canonical clock while generated code runs; the helper
        // reads it from the object and writes the charged value back there.
        a.strX(21, 0, L.clock);
        if (icacheCountersLive(L)) spillIcacheCounters(a, L);
        a.movW(1, cycles);
        a.movX(16, uint64_t(uintptr_t(&pom68kA64Sync)));
        spillQueueLive(a, L);
        spillPackedCcr(a, L);
        a.blr(16);
        a.emit(0xA94207E0u);             // ldp x0,x1,[sp,#32]
        reloadPackedCcr(a, L);
        a.ldrX(21, 0, L.clock);
        reloadGeneratedState(a, L);
    }
    retireInstruction(a);               // live Frame::instrs
}

// Emits a jump to `taken` iff the materialised 68k CCR satisfies cc.
void branchIfCond(Asm& a, const Layout& L, int cc, int taken) {
    if (gPackedCcr) {
        const auto branchBit = [&](unsigned bit, bool one) {
            a.ubfxW(9, 26, bit, 1);
            if (one) a.cbnzW(9, taken); else a.cbzW(9, taken);
        };
        switch (cc & 15) {
            case 0: a.b(taken); return;
            case 1: return;
            case 2: {                               // HI = !(C | Z)
                a.movW(10, 0x05); a.andW(9, 26, 10);
                a.cbzW(9, taken); return;
            }
            case 3:                                 // LS = C | Z
                a.movW(10, 0x05); a.andW(9, 26, 10);
                a.cbnzW(9, taken); return;
            case 4: branchBit(0, false); return;
            case 5: branchBit(0, true); return;
            case 6: branchBit(2, false); return;
            case 7: branchBit(2, true); return;
            case 8: branchBit(1, false); return;
            case 9: branchBit(1, true); return;
            case 10: branchBit(3, false); return;
            case 11: branchBit(3, true); return;
            case 12:                                // GE = N == V
            case 13:                                // LT = N != V
                a.ubfxW(9, 26, 3, 1); a.ubfxW(10, 26, 1, 1);
                a.eorW(9, 9, 10);
                if ((cc & 15) == 12) a.cbzW(9, taken);
                else                 a.cbnzW(9, taken);
                return;
            case 14: {                              // GT = !Z && N == V
                const int fail = a.label();
                a.ubfxW(9, 26, 2, 1); a.cbnzW(9, fail);
                a.ubfxW(9, 26, 3, 1); a.ubfxW(10, 26, 1, 1);
                a.eorW(9, 9, 10); a.cbzW(9, taken);
                a.bind(fail); return;
            }
            default:                                // LE = Z || N != V
                a.ubfxW(9, 26, 2, 1); a.cbnzW(9, taken);
                a.ubfxW(9, 26, 3, 1); a.ubfxW(10, 26, 1, 1);
                a.eorW(9, 9, 10); a.cbnzW(9, taken); return;
        }
    }
    switch (cc & 15) {
        case 0: a.b(taken); return;                         // T
        case 1: return;                                    // F
        case 2: {                                          // HI = !C && !Z
            const int fail = a.label();
            a.ldrB(9, 0, L.srC); a.cbnzW(9, fail);
            a.ldrB(9, 0, L.srZ); a.cbzW(9, taken);
            a.bind(fail); return;
        }
        case 3:                                            // LS = C || Z
            a.ldrB(9, 0, L.srC); a.cbnzW(9, taken);
            a.ldrB(9, 0, L.srZ); a.cbnzW(9, taken); return;
        case 4: a.ldrB(9, 0, L.srC); a.cbzW(9, taken); return;
        case 5: a.ldrB(9, 0, L.srC); a.cbnzW(9, taken); return;
        case 6: a.ldrB(9, 0, L.srZ); a.cbzW(9, taken); return;
        case 7: a.ldrB(9, 0, L.srZ); a.cbnzW(9, taken); return;
        case 8: a.ldrB(9, 0, L.srV); a.cbzW(9, taken); return;
        case 9: a.ldrB(9, 0, L.srV); a.cbnzW(9, taken); return;
        case 10: a.ldrB(9, 0, L.srN); a.cbzW(9, taken); return;
        case 11: a.ldrB(9, 0, L.srN); a.cbnzW(9, taken); return;
        case 12:                                          // GE = N == V
            a.ldrB(9, 0, L.srN); a.ldrB(10, 0, L.srV);
            a.eorW(9, 9, 10); a.cbzW(9, taken); return;
        case 13:                                          // LT = N != V
            a.ldrB(9, 0, L.srN); a.ldrB(10, 0, L.srV);
            a.eorW(9, 9, 10); a.cbnzW(9, taken); return;
        case 14: {                                        // GT = !Z && N == V
            const int fail = a.label();
            a.ldrB(9, 0, L.srZ); a.cbnzW(9, fail);
            a.ldrB(9, 0, L.srN); a.ldrB(10, 0, L.srV);
            a.eorW(9, 9, 10); a.cbzW(9, taken);
            a.bind(fail); return;
        }
        case 15:                                          // LE = Z || N != V
            a.ldrB(9, 0, L.srZ); a.cbnzW(9, taken);
            a.ldrB(9, 0, L.srN); a.ldrB(10, 0, L.srV);
            a.eorW(9, 9, 10); a.cbnzW(9, taken); return;
    }
}

int findTarget(const BlockIr& ir, uint32_t pc) {
    for (size_t i = 0; i < ir.instrs.size(); i++)
        if (ir.instrs[i].pc == pc) return int(i);
    return -1;
}

void leaveTo(Asm& a, const BlockIr& ir, uint32_t pc, uint32_t linkMask,
             int epilogue) {
    if (linkMask) {
        if (gLinkCellLookup) {
            void* cell = gLinkCellLookup(gLinkCellSelf, pc, ir.super);
            const int miss = a.label();
            a.ldrLiteralX(14, uint64_t(uintptr_t(cell)));
            a.ldrX(16, 14, 0);
            a.cbzX(16, miss);
            a.br(16);
            a.bind(miss);
            a.movW(9, uint32_t(Exit::BlockEnd)); a.strW(9, 1, 12);
            a.b(epilogue);
            return;
        }
        const uint32_t off = ((pc >> 1) & linkMask) * 16;
        const int miss = a.label();
        a.ldrX(14, 1, 64);               // Frame::linkTable
        a.address(15, 14, off);
        a.ldrW(9, 15, 0);
        a.movW(10, pc | uint32_t(ir.super));
        a.cmpW(9, 10); a.bCond(Asm::NE, miss);
        a.ldrX(16, 15, 8); a.br(16);
        a.bind(miss);
    }
    a.movW(9, uint32_t(Exit::BlockEnd)); a.strW(9, 1, 12);
    a.b(epilogue);
}

void leaveToDynamic(Asm& a, const Layout& L, const BlockIr& ir,
                    uint32_t linkMask, int epilogue) {
    if (linkMask) {
        const int miss = a.label();
        a.ldrW(11, 0, L.pc);
        a.ubfxW(10, 11, 1, std::bit_width(linkMask));
        a.lslX(10, 10, 4);
        a.ldrX(14, 1, 64);
        a.addX(15, 14, 10);
        a.ldrW(9, 15, 0);
        if (ir.super) { a.movW(12, 1); a.orrW(11, 11, 12); }
        a.cmpW(9, 11); a.bCond(Asm::NE, miss);
        a.ldrX(16, 15, 8); a.br(16);
        a.bind(miss);
    }
    a.movW(9, uint32_t(Exit::BlockEnd)); a.strW(9, 1, 12);
    a.b(epilogue);
}

// A JSR/JMP target read, at run time: w9 = target, w11 = the word
// execJsr would leave in irc. A read that would fault bails to `slow`,
// which replays the untouched instruction — so the caller must have
// committed NOTHING before this (the push is only PROVED beforehand and
// performed after). Same spill/reload dance as an exact access thunk.
void readProgWord(Asm& a, const Layout& L, int slow) {
    a.strX(21, 1, 96);
    a.strX(21, 0, L.clock);
    a.address(2, 1, 72);                // out = Frame::value
    a.movRegW(1, 9);                    // target address
    if (icacheCountersLive(L)) spillIcacheCounters(a, L);
    spillQueueLive(a, L);
    a.movX(16, uint64_t(uintptr_t(&pom68kA64ReadProg)));
    spillPackedCcr(a, L);
    a.blr(16);
    a.movRegW(14, 0);
    a.emit(0xA94207E0u);                // ldp x0,x1,[sp,#32]
    reloadPackedCcr(a, L);
    a.ldrX(21, 0, L.clock);
    reloadGeneratedState(a, L);
    a.cmpWZero(14);
    const int ok = a.label();
    a.bCond(Asm::NE, ok);
    a.ldrX(21, 1, 96); a.strX(21, 0, L.clock);
    a.b(slow);
    a.bind(ok);
    markRuntimeReason(a, RuntimeOther);
    a.ldrW(11, 1, 72);
}

bool emitBranchInstr(Asm& a, const Layout& L, const BlockIr& ir,
                     const Instr& in, const std::vector<int>& entries,
                     int epilogue, int slow, bool paced, int batch,
                     uint32_t linkMask, bool icache,
                     IcacheShadow& icacheShadow) {
    const uint16_t op = in.opcode;
    const InstructionSemantics& sem = in.semantics;
    const ControlFlowPlan& control = in.control;
    const uint16_t entryLookahead = ir.prefetchWord(in.pc + 2);
    // Mode-5 has no tail refill. Instructions that consume all but their
    // last extension with readExt(), then SKIP_LAST_RD/fullPrefetch(), hold
    // the last encoded word. A one-word transfer still holds its entry
    // lookahead. The traced path below must confirm every formula before a
    // 68030 branch emitter is admitted.
    const uint16_t lastHeld = in.words > 1
        ? in.extensionWord(in.words - 2) : entryLookahead;
    const auto tracedQueueIs = [&](uint16_t irc) {
        return !L.is030 || !in.terminalQueueValid ||
               (in.terminalIrd == op && in.terminalIrc == irc);
    };

    if (sem.operation == SemanticOp::ReturnSubroutine) {
        if (!control.valid || control.kind != ControlFlowKind::Return ||
            in.words != 1 || traced030(L, in) != 10) return false;
        auto memory = instructionMemoryPlan(in.memory, proofOptions(L));
        const MemoryAccessPlan read = memory.access(
            MemoryDirection::Read, MemoryOperand::Stack, 4, 3, 7);
        if (!read.valid() || !memory.complete()) return false;
        a.ldrW(9, 0, L.a + 7 * 4);
        memLoadGuest(a, L, ir.super, 32, 11, slow, read);
        a.ubfxW(9, 11, 0, 1); a.cbnzW(9, slow);
        a.ldrW(9, 0, L.a + 7 * 4); a.addImmW(9, 9, 4);
        a.strW(9, 0, L.a + 7 * 4);
        a.strW(11, 0, L.pc); a.strW(11, 0, L.pc0);
        if (!tracedQueueIs(entryLookahead)) return false;
        if (icache) chargeIcache(a, L, ir, in, icacheShadow);
        commitQueue(a, L, op, entryLookahead);
        chargeAndRetire(a, L, 10, paced, batch, in.pc,
                        uint32_t(in.words) + 1, ir.super);
        leaveToDynamic(a, L, ir, linkMask, epilogue);
        return true;
    }

    if (sem.operation == SemanticOp::BranchSubroutine) {
        const unsigned bsrCost = traced030(L, in);
        if (!control.valid || control.kind != ControlFlowKind::DirectCall ||
            !control.targetKnown || !control.pushesReturnAddress ||
            in.words > 3 || (bsrCost != 7 && bsrCost != 5)) return false;
        auto memory = instructionMemoryPlan(in.memory, proofOptions(L));
        const MemoryAccessPlan write = memory.access(
            MemoryDirection::Write, MemoryOperand::Stack, 4, 4, 7);
        if (!write.valid() || !memory.complete()) return false;
        const uint32_t target = control.target;
        if (target & 1) return false;
        a.ldrW(9, 0, L.a + 7 * 4); a.subImmW(9, 9, 4);
        a.strW(9, 1, 44);
        a.movW(11, control.returnAddress); a.strW(11, 1, 72);
        // The return-address push is BSR's only data access. Route it
        // through the same write-authorized copyback proof as MOVE stores;
        // a miss reaches the untouched instruction before A7 or the target
        // boundary is committed.
        memStoreGuest(a, L, ir.super, 32, 11, slow, write);
        a.ldrW(9, 1, 44); a.strW(9, 0, L.a + 7 * 4);
        if (!tracedQueueIs(lastHeld)) return false;
        if (icache) chargeIcache(a, L, ir, in, icacheShadow);
        commitBoundary(a, L, target); commitQueue(a, L, op, lastHeld);
        chargeAndRetire(a, L, bsrCost, paced, batch, in.pc,
                        uint32_t(in.words) + 1, ir.super);
        leaveTo(a, ir, target, linkMask, epilogue);
        return true;
    }

    if (sem.operation == SemanticOp::JumpSubroutine ||
        sem.operation == SemanticOp::Jump) {
        const bool jsr = sem.operation == SemanticOp::JumpSubroutine;
        if (!control.valid || control.pushesReturnAddress != jsr) { watchRefusal(L, ir, in, "jsr:control"); return false; }
        Ea ea;
        if (!decodeEa(in, sem.eaMode, sem.eaReg, 32, 0, ea, true, true) ||
            !ea.memory || ea.idx == EA_PI || ea.idx == EA_PD ||
            in.words != unsigned(1 + ea.ext)) {
            watchRefusal(L, ir, in, "jsr:ea"); return false;
        }
        const bool fullIndirect = ea.fullFormat &&
                                  ea.indirect != IndexIndirect::None;
        auto memory = instructionMemoryPlan(in.memory, proofOptions(L));
        MemoryAccessPlan pointerRead;
        if (fullIndirect)
            pointerRead = memory.access(MemoryDirection::Read,
                                        MemoryOperand::Control, 4,
                                        sem.eaMode, sem.eaReg);
        MemoryAccessPlan write;
        if (jsr) {
            write = memory.access(MemoryDirection::Write,
                                  MemoryOperand::Stack, 4, 4, 7);
            if (!write.valid()) { watchRefusal(L, ir, in, "jsr:write"); return false; }
        }
        if (fullIndirect && !pointerRead.valid()) {
            watchRefusal(L, ir, in, "jsr:pointer"); return false;
        }
        if (!memory.complete()) { watchRefusal(L, ir, in, "jsr:complete"); return false; }
        const int penalty = ea.fullFormat ? fullIndexPenalty(ea) : 0;
        const unsigned opCost = kJsrJmp[ea.idx] < 0 ? 0
            : unsigned(kJsrJmp[ea.idx] + penalty);
        if (kJsrJmp[ea.idx] < 0 || traced030(L, in) != opCost ||
            (fullIndirect && L.is030)) {
            watchRefusal(L, ir, in, "jsr:cost"); return false;
        }

        if (fullIndirect) {
            addrOfFullIndirectPointer(a, L, ea);
            if (!jsr) {
                memLoadGuest(a, L, ir.super, 32, 11, slow, pointerRead);
                finishFullIndirect(a, L, ea, 11);
            } else {
                // computeEA reads the pointer before JSR pushes. Prove both
                // plain mappings first so a stack refusal can never replay a
                // device read, then perform the architectural order once.
                a.strW(9, 1, 40);                  // pointer guest address
                memProbe(a, L, ir.super, 4, false, slow);
                a.strX(14, 1, 120);                // pointer host address
                a.ldrW(9, 0, L.a + 7 * 4); a.subImmW(9, 9, 4);
                a.strW(9, 1, 44);                  // new A7
                memProbe(a, L, ir.super, 4, true, slow);
                a.strX(14, 1, 192);                // stack host address
                a.ldrX(14, 1, 120); loadGuest(a, 32, 11);
                finishFullIndirect(a, L, ea, 11);
            }
        } else {
            addrOf(a, L, ea, 32);
        }
        a.strW(9, 1, 48);                           // target (Frame::saveV)
        a.ubfxW(9, 9, 0, 1); a.cbnzW(9, slow);
        if (fullIndirect) {
            a.ldrW(9, 1, 48);
            readProgWord(a, L, slow);
            a.strW(11, 1, 40);                     // runtime queue.irc
        }
        if (fullIndirect && jsr) {
            a.ldrX(14, 1, 192);
            a.ldrW(9, 1, 44);
            a.movW(11, control.returnAddress); a.strW(11, 1, 72);
            observeDirectWrite(a, L, 32, in.pc);
            storeGuest(a, 32, 11);
            a.ldrW(9, 1, 44); a.strW(9, 0, L.a + 7 * 4);
        }
        if (jsr && L.is030) {
            // Mode-5 has no queue: execJsr leaves the TARGET's first word
            // in irc (`queue.irc = read<PROG, Word>(ea)`), which no
            // compile-time formula may predict — a trap patch rewriting a
            // routine's first word would be run through a stale irc. So
            // the word is read at run time, in the interpreter's order
            // made restartable: PROVE the push mapping without storing,
            // read the target (a fault bails with nothing committed; the
            // replay pushes and faults exactly as execJsr does), then push
            // through the proven pointer. 2026-08-23: JSR was ~35 M of the
            // idle Finder's in-block fallbacks, refused by that formula.
            if (in.terminalQueueValid && in.terminalIrd != op) {
                watchRefusal(L, ir, in, "jsr:ird"); return false;
            }
            if (write.exactRequired) { watchRefusal(L, ir, in, "jsr:write-plan"); return false; }
            a.ldrW(9, 0, L.a + 7 * 4); a.subImmW(9, 9, 4);
            a.strW(9, 1, 44);                       // the new A7
            memProbe(a, L, ir.super, 4, true, slow); // x14 = proven host
            a.strX(14, 1, 192);                     // Frame::progHost
            a.ldrW(9, 1, 48);
            readProgWord(a, L, slow);               // w11 = irc (nothing committed yet)
            a.strW(11, 1, 40);                      // Frame::scratch
            a.ldrX(14, 1, 192);
            a.ldrW(9, 1, 44);
            a.movW(11, control.returnAddress); a.strW(11, 1, 72);
            observeDirectWrite(a, L, 32, in.pc);
            storeGuest(a, 32, 11);
            a.ldrW(9, 1, 44); a.strW(9, 0, L.a + 7 * 4);
            a.ldrW(11, 1, 48);
            a.strW(11, 0, L.pc); a.strW(11, 0, L.pc0);
            if (icache) chargeIcache(a, L, ir, in, icacheShadow);
            a.ldrW(11, 1, 40);
            a.movW(29, uint32_t(op) << 16); a.orrW(29, 29, 11);
            chargeAndRetire(a, L, opCost, paced, batch, in.pc,
                            uint32_t(in.words) + 1, ir.super);
            if (control.targetKnown) leaveTo(a, ir, control.target, linkMask, epilogue);
            else leaveToDynamic(a, L, ir, linkMask, epilogue);
            return true;
        }
        if (jsr && !fullIndirect) {
            a.ldrW(9, 0, L.a + 7 * 4); a.subImmW(9, 9, 4);
            a.strW(9, 1, 44);
            a.movW(11, control.returnAddress); a.strW(11, 1, 72);
            memStoreGuest(a, L, ir.super, 32, 11, slow, write);
            a.ldrW(9, 1, 44); a.strW(9, 0, L.a + 7 * 4);
        }
        a.ldrW(11, 1, 48);
        a.strW(11, 0, L.pc); a.strW(11, 0, L.pc0);
        if (!tracedQueueIs(lastHeld)) { watchRefusal(L, ir, in, "jsr:queue"); return false; }
        if (icache) chargeIcache(a, L, ir, in, icacheShadow);
        if (fullIndirect) {
            a.ldrW(11, 1, 40);
            a.movW(29, uint32_t(op) << 16); a.orrW(29, 29, 11);
        } else {
            commitQueue(a, L, op, lastHeld);
        }
        chargeAndRetire(a, L, opCost, paced, batch, in.pc,
                        uint32_t(in.words) + 1, ir.super);
        const bool constant = control.targetKnown;
        if (constant) leaveTo(a, ir, control.target, linkMask, epilogue);
        else leaveToDynamic(a, L, ir, linkMask, epilogue);
        return true;
    }

    if (sem.operation == SemanticOp::Branch) {
        const int cc = sem.condition;
        if (!control.valid || !control.targetKnown || in.words > 3) return false;
        const uint32_t target = control.target;
        const uint32_t fall = control.fallthrough;
        if (target & 1) return false;
        const int takenCycles = cc == 0 ? 10 : 6;
        const int fallCycles = in.words == 1 ? 4 : 6;
        const unsigned traced = traced030(L, in);
        if (traced != unsigned(takenCycles) &&
            traced != unsigned(fallCycles)) return false;

        const uint16_t takenIrc = lastHeld;
        const uint16_t fallIrc = ir.prefetchWord(fall);
        if (L.is030 && in.terminalQueueValid) {
            const bool targetOnly = in.observedNextPc == target && target != fall;
            const bool fallOnly = in.observedNextPc == fall && target != fall;
            if (in.terminalIrd != op ||
                (targetOnly && in.terminalIrc != takenIrc) ||
                (fallOnly && in.terminalIrc != fallIrc) ||
                (!targetOnly && !fallOnly && in.terminalIrc != takenIrc &&
                 in.terminalIrc != fallIrc)) return false;
        }

        // A conditional Bcc.W fetches pc and pc+2 on both paths; only the
        // fall-through consumes the displacement with readExt and fetches
        // pc+4. Charge the common prefix here and the path-specific word
        // below. Longer conditional forms remain refused by compile().
        const bool bccWord = L.is030 && icache && in.words == 2 && cc != 0;
        if (icache)
            chargeIcache(a, L, ir, in, icacheShadow, bccWord ? 2u : 0u);

        const int taken = a.label();
        branchIfCond(a, L, cc, taken);
        // Not taken.
        if (bccWord) chargeIcacheExtraWord(a, L, ir, in.pc + 4);
        commitBoundary(a, L, fall);
        commitQueue(a, L, op, fallIrc);
        chargeAndRetire(a, L, unsigned(fallCycles), paced, batch, in.pc,
                        uint32_t(in.words) + 1, ir.super);
        leaveTo(a, ir, fall, linkMask, epilogue);

        a.bind(taken);
        commitBoundary(a, L, target);
        commitQueue(a, L, op, takenIrc);
        chargeAndRetire(a, L, unsigned(takenCycles), paced, batch, in.pc,
                        uint32_t(in.words) + 1, ir.super);
        const int ti = findTarget(ir, target);
        if (ti >= 0) a.b(entries[size_t(ti)]);
        else leaveTo(a, ir, target, linkMask, epilogue);
        return true;
    }

    if (sem.operation == SemanticOp::DecrementBranch) {
        if (!control.valid || control.kind != ControlFlowKind::DecrementBranch ||
            !control.targetKnown || in.words != 2 ||
            (traced030(L, in) != 6 && traced030(L, in) != 10)) return false;
        const int cc = sem.condition, dn = sem.eaReg;
        const uint32_t target = control.target;
        const uint32_t fall = control.fallthrough;
        if (target & 1) return false;
        if (!tracedQueueIs(entryLookahead)) return false;
        if (icache) chargeIcache(a, L, ir, in, icacheShadow, 2);
        const int condTrue = a.label(), expired = a.label();
        if (cc != 1) branchIfCond(a, L, cc, condTrue);
        a.ldrH(9, 0, L.d + unsigned(dn) * 4);        // pre-decrement value
        a.subImmW(10, 9, 1);
        a.strH(10, 0, L.d + unsigned(dn) * 4);
        a.cbzW(9, expired);

        commitBoundary(a, L, target);
        commitQueue(a, L, op, in.extensionWord(0));
        chargeAndRetire(a, L, 6, paced, batch, in.pc,
                        uint32_t(in.words) + 1, ir.super);
        { const int ti = findTarget(ir, target);
          if (ti >= 0) a.b(entries[size_t(ti)]);
          else leaveTo(a, ir, target, linkMask, epilogue); }

        a.bind(expired);
        commitBoundary(a, L, fall);
        commitQueue(a, L, op, in.extensionWord(0));
        chargeAndRetire(a, L, 10, paced, batch, in.pc,
                        uint32_t(in.words) + 1, ir.super);
        leaveTo(a, ir, fall, linkMask, epilogue);

        a.bind(condTrue);
        commitBoundary(a, L, fall);
        commitQueue(a, L, op, in.extensionWord(0));
        chargeAndRetire(a, L, 6, paced, batch, in.pc,
                        uint32_t(in.words) + 1, ir.super);
        leaveTo(a, ir, fall, linkMask, epilogue);
        return true;
    }
    return false;
}

struct A64Arena {
    CodeBuffer code;
};

class A64Compiled : public Compiled {
public:
    uint8_t* entry = nullptr;
    uint8_t* linked = nullptr;
    // Keeps `entry` executable even after the backend has advanced to a new
    // arena. Precise block eviction drops the reference and reclaims a fully
    // dead arena without a global cache flush.
    std::shared_ptr<A64Arena> arena;
};

class A64Backend final : public Backend {
public:
    Backend* clone() const override { return new A64Backend; }
    const char* name() const override { return "aarch64"; }
    const char* description() const override {
        return "native AArch64 code generation";
    }
    bool usable() const override { return CodeBuffer::supported(); }
    BackendCaps caps() const override {
        BackendCaps c;
        c.nativeCode = true;
        c.aluReg = c.aluMem = c.moves = c.branches = c.addrModes = true;
        c.dtlbCodeMask = true;
        c.maxBlockInstrs = 64;
        // The 68030 emitter is lockstep-clean at the real 260480-cycle LC II
        // frame cadence, and since 2026-08-18 the declaration says so: an explicit
        // POM68K_JIT_BACKEND=a64 on a 68030 is honoured without the unsafe
        // override.
        c.guestFamilies = kGuest68040 | kGuest68030;
        // The 68030 earns the automatic path independently: production-
        // cadence lockstep is green through 6,000 frames, the native LLE
        // platform gates are green, and same-process ABBA at the fixed
        // 6,000-frame budget beats threaded with the same fingerprint.
        c.autoFamilies = kGuest68040 | kGuest68030;
        c.profitScore68030 = 64;
        // pom68kA64Read/Write carry the § C.4nonies access-clock bias since
        // 2026-08-22 (ported from x64, replacing the guardIcacheHits
        // replay) — the declaration that turns the restart-base and BSR.W
        // admission DEFAULTS on for this backend too, on the a64 120k
        // lockstep's evidence.
        c.accessClockBias = true;
        return c;
    }
    bool canEmit(uint16_t op) const override {
        return canEmitReg(op);
    }
    CompileResult compile(const BlockIr& ir, const Context& ctx) override;
    void* linkEntry(Compiled* c) const override {
        return static_cast<A64Compiled*>(c)->linked;
    }
    RunResult run(Compiled* c, Context& ctx) override;
    void release(Compiled* c) override { delete c; }
    void flushAll() override { arena_.reset(); }

private:
    std::shared_ptr<A64Arena> arena_;
    Frame frame_{};
    bool frameReady_ = false;
    Layout layout_{};
    bool haveLayout_ = false;
    int diagLeft_ = -1;
};

CompileResult A64Backend::compile(const BlockIr& ir, const Context& ctx) {
    if (diagLeft_ < 0) diagLeft_ = verboseBlocks();
    gRuntimeReasonHisto = ctx.slowRuntimeReasonHisto != nullptr;
    gPackedCcr = packedCcrEnabled();
    const auto reject = [this](CompileReject reason,
                               const char* why) -> CompileResult {
        if (verbose() && diagLeft_ > 0) {
            diagLeft_--;
            std::fprintf(stderr, "[jit/a64] refused: %s\n", why);
        }
        return {nullptr, reason};
    };
    if (!ctx.cpu || ir.instrs.empty() || ir.code.empty() ||
        !ctx.cpu->pomJitSimpleIpl())
        return reject(CompileReject::Context, "empty/context/IPL mode");
    if (verbose() && diagLeft_ > 0) {
        diagLeft_--;
        std::fprintf(stderr, "[jit/a64] native block $%08X (%zu):",
                     ir.entryPc, ir.instrs.size());
        for (const Instr& in : ir.instrs)
            std::fprintf(stderr, " %04X/%u=%u+%u+%u", in.opcode, in.cycles,
                         in.baseCycles, in.icacheCycles,
                         in.postExceptionCycles);
        std::fprintf(stderr, "\n");
    }
    if (!haveLayout_) { layout_ = ctx.cpu->pomJitLayout(); haveLayout_ = true; }
    const Layout& L = layout_;
    const bool paced = ctx.periphClock && ctx.periphBatch != 0 &&
                       a64PacingEnabled();
    gPacingDeadline = paced;
    gDirectPeriphDue = paced && ctx.periphBatch < 0 && ctx.periphDue;
    gLinkCellLookup = ctx.linkMask ? ctx.linkCell : nullptr;
    gLinkCellSelf = ctx.linkCellSelf;
    const int batch = paced ? ctx.periphBatch : 0;
    // Restartable-write blocks may link once the i-cache charge sits past the
    // last runtime bail. The old chain-boundary divergence was the same stale
    // pre-charge signature: a helper could decline the replay after counters
    // and clock had already moved for an instruction that never executed.
    const uint32_t linkMask = ctx.linkTable ? ctx.linkMask : 0;
    const bool icache = L.icLive && icacheEmitEnabled();
    chooseGuestRegisterCache(ir);
    // Linked blocks share the source block's stack frame but return through
    // the target block's epilogue.  The frame ABI must therefore depend on
    // the immutable Engine option, not on whether this particular block found
    // a profitable guest register to cache.
    const bool guestCacheAbi = registerCacheEnabled();

    Asm a;
    const int epilogue = a.label();
    const int exitLost = a.label();
    const int exitFault = a.label();
    // x20 keeps the immutable caller clock target, x21 the guest clock and
    // w22 the execution-control flags. All three survive linked blocks and
    // helper calls. On 030, x23/x24 keep the i-cache counters. x26 keeps the
    // retired count, and in packed-CCR mode its low byte also keeps XNZVC;
    // the count starts at bit 8. x0/x1 remain the CPU/Frame ABI pair.
    a.emit(guestCacheAbi ? 0xA9B97BFDu    // stp x29,x30,[sp,#-112]!
                         : 0xA9BA7BFDu);  // stp x29,x30,[sp,#-96]!
    a.emit(0xA9015BF5u);                 // stp x21,x22,[sp,#16]
    a.emit(0xA90207E0u);                 // stp x0,x1,[sp,#32]
    if (icache) a.emit(0xA90363F7u);      // stp x23,x24,[sp,#48]
    a.emit(0xA9046BF9u);                 // stp x25,x26,[sp,#64]
    a.emit(0xA90553F3u);                 // stp x19,x20,[sp,#80]
    if (guestCacheAbi) a.emit(0xA90673FBu); // stp x27,x28,[sp,#96]
    a.ldrX(20, 1, 0);                    // Frame::clockTarget
    a.ldrX(21, 0, L.clock);
    a.ldrW(26, 1, 8);                    // Frame::instrs
    if (gPackedCcr) {
        a.lslX(26, 26, kPackedCountShift);
        reloadPackedCcr(a, L);
    }
    loadQueueLive(a, L);                 // packed ird:irc in callee-saved w29
    reloadGeneratedState(a, L);
    // TAS/CAS are unsafe block terminators, hence no linked native chain can
    // set the 030 locked-RMW latch. Clear the value inherited from the last
    // interpreted instruction, matching mmuExecuteStart's contract.
    if (L.is030) { a.movW(9, 0); a.strB(9, 0, L.mmuRmw); }
    if (icache) {
        reloadIcacheCounters(a, L);
        // CACR.EI cannot change inside a generated/linkable chain: MOVEC is
        // unsafe and ends the block. This mirrors the 68040 live-clock ABI
        // and removes a CACR load/mask from every native 030 instruction.
        a.ldrB(25, 0, L.cacr);
        a.movW(9, 1);
        a.andW(25, 25, 9);
    }
    // This is deliberately part of the linked entry. Source blocks keep
    // memory canonical, so each target can choose a different read-only
    // pair without per-edge register shuffles.
    const size_t linkEntryOffset = a.byteSize();
    reloadGuestRegisterCache(a, L);
    struct ExitLabels { int budget, flags; };
    std::vector<ExitLabels> exits;
    std::vector<int> entries;
    std::vector<int> slowStatic;
    std::vector<int> slowRuntime;
    std::vector<int> slowBody;
    exits.reserve(ir.instrs.size());
    entries.reserve(ir.instrs.size());
    slowStatic.reserve(ir.instrs.size());
    slowRuntime.reserve(ir.instrs.size());
    slowBody.reserve(ir.instrs.size());
    for (size_t i = 0; i < ir.instrs.size(); i++) {
        entries.push_back(a.label());
        slowStatic.push_back(a.label());
        slowRuntime.push_back(a.label());
        slowBody.push_back(a.label());
    }

    size_t nativeCount = 0;
    IcacheShadow icacheShadow;

    for (size_t i = 0; i < ir.instrs.size(); i++) {
        a.bind(entries[i]);
        markRuntimeReason(a, RuntimeOther);
        clearRuntimeAccess(a);
        exits.push_back({a.label(), a.label()});
        a.cmpX(21, 20);
        a.bCond(Asm::GE, exits.back().budget);
        a.cbnzW(22, exits.back().flags);

        const Instr& in = ir.instrs[i];
        // Multi-word control flow needs a proved path-specific fetch model.
        // DBcc fetches exactly two words on all three paths. Conditional
        // Bcc.W has two common words plus pc+4 on fall-through. JSR d16(PC)
        // is a single path with a traced count, and so is BSR.W ($6100,
        // fetchWords = 2): its historical step-16097 divergence was the
        // peripheral-phase class (JIT_BRINGUP § C.4nonies), closed by the
        // access-clock bias the thunks carry, so it rides the same knob as
        // on x64 — ON by default under this backend's declaration. Wider
        // transfers remain refused.
        const bool dbcc =
            in.semantics.operation == SemanticOp::DecrementBranch;
        const bool bccWord = icache && in.words == 2 &&
            in.semantics.operation == SemanticOp::Branch &&
            in.semantics.condition != 0;
        // $4EBA decodes as JumpSubroutine, $6100 as BranchSubroutine: the
        // exemption used to test BranchSubroutine for both, which made the
        // JSR half dead code — every JSR d16(PC) fell back (2026-08-23,
        // found by POM68K_JIT_WATCH_OPCODE=4EBA: 19 M of the idle Finder's
        // in-block fallbacks).
        // Every two-word JSR — d16(An), d16(PC), abs.W — is the same
        // single path with a traced fetch count of 2 (4EAD JSR d16(A5) was
        // the last 64-site refusal of the 2026-08-23 watch once d16(PC)
        // compiled); the emitter's cost table and the runtime target read
        // cover all three forms alike.
        const bool jsrD16Pc = icache && in.words == 2 &&
            ((in.semantics.operation == SemanticOp::JumpSubroutine &&
              in.fetchWords == 2) ||
             (in.opcode == 0x6100 && bsrWideAdmission() &&
              in.semantics.operation == SemanticOp::BranchSubroutine));
        if (icache && in.kind == Kind::Branch && in.words > 1 &&
            !dbcc && !bccWord && !jsrD16Pc) {
            watchRefusal(L, ir, in, "multi-word-branch-guard");
            a.b(slowStatic[i]);
            continue;
        }
        // Do not guess an instruction-stream fetch count from encoded length:
        // SKIP_LAST_RD forms are the counterexample. A missing count also
        // invalidates the compile-time shadow, so later instructions perform
        // full runtime tag/valid checks instead of folding a presumed hit.
        if (icache && !dbcc && !in.fetchWords) {
            watchRefusal(L, ir, in, "no-fetch-count");
            icacheShadow = {};
            a.b(slowStatic[i]);
            continue;
        }
        // A successful exact MMIO thunk may call Cpu030::stall/catchUp from
        // inside the operand access, and post-success charging would
        // present that access to peripherals the fetch penalty too early.
        // The thunks align the clock themselves (pom68kA64Read/Write, the
        // § C.4nonies bias) from this operand; until 2026-08-22 the same
        // class was closed by `guardIcacheHits`, which replayed the whole
        // instruction through Moira unless the block-local shadow proved a
        // zero-penalty fetch.
        gAccessPcWords = icache && in.fetchWords
            ? (uint64_t(in.fetchWords) << 32) | in.pc : 0;
        const Asm::Mark mark = a.mark();
        // mmu040InstrStart's simple POLL_IPL is already satisfied here.
        // setIPL() raises CHECK_IRQ whenever the pin changes, and guards()
        // exits before this instruction if any control flag is set. The
        // interpreter only clears CHECK_IRQ once sampled IPL equals the pin;
        // therefore flags == 0 proves reg.ipl == ipl. Re-storing that same
        // byte at every native boundary was two instructions of pure churn.

        uint16_t emittedCycles = L.is030 ? in.baseCycles : in.cycles;
        bool native = false;
        if (in.kind == Kind::Branch) {
            native = emitBranchInstr(a, L, ir, in, entries, epilogue,
                                     slowRuntime[i],
                                     paced, batch, linkMask, icache,
                                     icacheShadow);
        } else if (canEmit(in.opcode)) {
            native = emitRegInstr(a, L, ir, in, slowRuntime[i],
                                  emittedCycles);
        }
        if (!native) {
            // An emitter is allowed to discover a length/cycle mismatch
            // after writing a few instructions. Erase that partial attempt
            // and hand the untouched guest instruction to Moira.
            watchRefusal(L, ir, in, canEmit(in.opcode) || in.kind == Kind::Branch
                                        ? "emitter-refused" : "not-in-canEmit");
            a.rewind(mark);
            if (icache)
                shadowIcache(ir, in, icacheShadow, dbcc ? 2u : 0u);
            a.b(slowStatic[i]);
            continue;
        }
        nativeCount++;
        if (in.kind == Kind::Branch) continue;

        // Charge only after the instruction body has crossed its last
        // runtime-bail edge. A refused re-run can then never leave cache
        // counters, tags or miss cycles for an instruction that did not run.
        if (icache) chargeIcache(a, L, ir, in, icacheShadow);

        const uint16_t ird = in.terminalQueueValid
            ? in.terminalIrd : in.opcode;
        const uint16_t irc = in.terminalQueueValid
            ? in.terminalIrc
            : ir.prefetchWord(in.pc + uint32_t(in.words) * 2);
        commitQueue(a, L, ird, irc);

        // On the 030, chargeIcache() has already reproduced the instruction
        // fetch component. Native instructions therefore owe only the base
        // component recorded by the tracer. Existing instructions traced on
        // cache hits have baseCycles == cycles, so this changes behaviour
        // only for forms explicitly admitted using split timing above.
        // Sole exact reads may split their variable data-bus delay from the
        // fixed opcode cost during admission; emittedCycles carries that
        // proved fixed component. All other forms retain the trace verbatim.
        chargeAndRetire(a, L, emittedCycles, paced, batch, in.pc,
                        uint32_t(in.words) + 1, ir.super);

    }

    if (ir.instrs.back().kind != Kind::Branch) {
        const uint32_t endPc = ir.instrs.back().pc +
                               uint32_t(ir.instrs.back().words) * 2;
        commitBoundary(a, L, endPc);
        leaveTo(a, ir, endPc, linkMask, epilogue);
    } else {
        // An unsupported terminating branch reaches its own slow stub;
        // supported branches already emitted an explicit transfer.
        a.b(epilogue);
    }

    // Cold, exact per-instruction fallback. This is also the safety net for
    // memory mappings that cannot be represented by the inline data TLB.
    for (size_t i = 0; i < ir.instrs.size(); i++) {
        const bool hasStaticFallback = a.referenced(slowStatic[i]);
        const bool hasRuntimeFallback = a.referenced(slowRuntime[i]);
        // A successfully emitted register-only instruction has no edge to
        // either label. It used to carry an unreachable copy of the entire
        // helper-call continuation nevertheless, making short hot blocks
        // several times larger and exhausting the bump cache prematurely.
        if (!hasStaticFallback && !hasRuntimeFallback) continue;

        const auto emitHisto = [&](uint32_t frameOff) {
            a.ldrX(14, 1, frameOff);
            a.address(15, 14, uint32_t(ir.instrs[i].opcode) * 8);
            a.ldrX(9, 15, 0);
            a.addImmX(9, 9, 1);
            a.strX(9, 15, 0);
        };
        if (hasStaticFallback) {
            a.bind(slowStatic[i]);
            if (ctx.slowStaticHisto) emitHisto(80);
            a.b(slowBody[i]);
        }
        if (hasRuntimeFallback) {
            a.bind(slowRuntime[i]);
            if (ctx.slowRuntimeHisto) emitHisto(88);
            if (ctx.slowRuntimeReasonHisto) {
                // Flat [reason][opcode] table. One reason plane is 65536 * 8
                // bytes, hence the 19-bit shift.
                a.ldrX(14, 1, 128);
                a.ldrW(9, 1, 144);
                a.lslX(9, 9, 19);
                a.addX(14, 14, 9);
                a.address(15, 14, uint32_t(ir.instrs[i].opcode) * 8);
                a.ldrX(9, 15, 0);
                a.addImmX(9, 9, 1);
                a.strX(9, 15, 0);
            }
            if (ctx.runtimeAddressObserver)
                observeRuntimeAddress(a, L, ir.instrs[i].opcode);
            a.b(slowBody[i]);
        }
        a.bind(slowBody[i]);
        commitBoundary(a, L, ir.instrs[i].pc);
        a.strX(21, 0, L.clock);
        if (icache) spillIcacheCounters(a, L);
        spillQueueLive(a, L);
        a.movX(16, uint64_t(uintptr_t(&pom68kA64Step)));
        spillPackedCcr(a, L);
        a.blr(16);
        a.movRegW(14, 0);                 // preserve helper result
        a.emit(0xA94207E0u);              // ldp x0,x1,[sp,#32]
        reloadPackedCcr(a, L);
        loadQueueLive(a, L);              // fallback owns its terminal queue
        a.ldrX(21, 0, L.clock);
        reloadGeneratedState(a, L);
        reloadGuestRegisterCache(a, L);
        if (icache) {
            reloadIcacheCounters(a, L);
            // Defensive for any future non-Unsafe fallback that gains a
            // CACR side effect; today's MOVEC exits before this continuation.
            a.ldrB(25, 0, L.cacr);
            a.movW(9, 1);
            a.andW(25, 25, 9);
        }
        a.cmpWZero(14);
        a.bCond(Asm::LT, exitLost);
        a.bCond(Asm::EQ, exitFault);
        retireInstruction(a);             // retired
        a.ldrW(9, 1, 52);                 // slowInstrs
        a.addImmW(9, 9, 1);
        a.strW(9, 1, 52);
        a.ldrX(14, 1, 32);                // guardHit
        a.ldrB(9, 14, 0);
        a.cbnzW(9, exitLost);
        if (ir.instrs[i].kind == Kind::Branch ||
            ir.instrs[i].kind == Kind::Unsafe || i + 1 == ir.instrs.size()) {
            a.movW(9, uint32_t(Exit::BlockEnd)); a.strW(9, 1, 12);
            a.b(epilogue);
        } else {
            a.b(entries[i + 1]);
        }
    }

    a.bind(exitLost);
    a.movW(9, uint32_t(Exit::WindowLost)); a.strW(9, 1, 12);
    a.b(epilogue);
    a.bind(exitFault);
    retireInstruction(a);
    a.movW(9, uint32_t(Exit::Fault)); a.strW(9, 1, 12);
    a.b(epilogue);

    for (size_t i = 0; i < exits.size(); i++) {
        for (const auto [label, why] : {
                 std::pair{exits[i].budget, Exit::ClockBudget},
                 std::pair{exits[i].flags, Exit::CpuFlags}}) {
            a.bind(label);
            a.movW(9, ir.instrs[i].pc);
            a.strW(9, 0, L.pc); a.strW(9, 0, L.pc0);
            a.movW(9, uint32_t(why)); a.strW(9, 1, 12);
            a.b(epilogue);
        }
    }
    a.bind(epilogue);
    a.strX(21, 0, L.clock);
    spillPackedCcr(a, L);
    spillRetiredCount(a);
    spillQueueLive(a, L);
    if (icache) {
        spillIcacheCounters(a, L);
        a.emit(0xA94363F7u);              // ldp x23,x24,[sp,#48]
    }
    if (guestCacheAbi) a.emit(0xA94673FBu); // ldp x27,x28,[sp,#96]
    a.emit(0xA94553F3u);                 // ldp x19,x20,[sp,#80]
    a.emit(0xA9446BF9u);                 // ldp x25,x26,[sp,#64]
    a.emit(0xA9415BF5u);                 // ldp x21,x22,[sp,#16]
    a.emit(guestCacheAbi ? 0xA8C77BFDu    // ldp x29,x30,[sp],#112
                         : 0xA8C67BFDu);  // ldp x29,x30,[sp],#96
    a.emit(0xD65F03C0u);                 // ret
    if (!a.finish()) return reject(CompileReject::Emit, "branch fixup");

    // Match the x86-64 policy: a block dominated by interpreter calls is
    // slower than the threaded window and should not consume code cache.
    if (nativeCount * 100 < ir.instrs.size() * size_t(minNativePercent()))
        return reject(CompileReject::Coverage, "native coverage");

    if (!arena_) {
        auto first = std::make_shared<A64Arena>();
        if (!first->code.reserve(kCodeArenaBytes))
            return reject(CompileReject::CodeMemory, "code reserve");
        arena_ = std::move(first);
    }
    if (!arena_->code.makeWritable())
        return reject(CompileReject::CodeMemory, "W^X -> writable");
    // Keep an indirect entry away from the final 16 bytes of an Apple
    // 16-KiB page. arm64 fetches a wider instruction window at a branch
    // target; a prologue straddling that boundary faults under MAP_JIT on
    // current macOS even though vm_region reports both pages RWX.
    uint8_t* dst = arena_->code.alloc(a.byteSize(), 64);
    if (!dst) {
        // Seal the full arena before publishing a new writable one. Existing
        // blocks retain it through A64Compiled::arena, so no link or entry is
        // retracted and no hot code is recompiled.
        if (!arena_->code.makeExecutable())
            return reject(CompileReject::CodeMemory, "full arena W^X");
        auto next = std::make_shared<A64Arena>();
        if (!next->code.reserve(kCodeArenaBytes))
            return reject(CompileReject::CodeCapacity, "new code arena");
        arena_ = std::move(next);
        dst = arena_->code.alloc(a.byteSize(), 64);
        if (!dst)
            return reject(CompileReject::Emit, "block exceeds code arena");
    }
    std::memcpy(dst, a.bytes(), a.byteSize());
    if (!arena_->code.makeExecutable())
        return reject(CompileReject::CodeMemory, "W^X -> executable");
    auto* c = new A64Compiled;
    c->entry = dst;
    c->linked = dst + linkEntryOffset;
    c->arena = arena_;
    return {c, CompileReject::None};
}

RunResult A64Backend::run(Compiled* c, Context& ctx) {
    static const uint8_t noGuard = 0;
    // A backend clone belongs to one Engine. Its ABI pointers are stable for
    // that lifetime, so retain them instead of rebuilding the 184-byte frame
    // after every native-chain exit (millions of times per boot). The write
    // observer is deliberately refreshed: lockstep instrumentation toggles
    // it while the engine is live. Generated scratch slots are written before
    // use and the four per-call results are reset below.
    Frame& f = frame_;
    if (!frameReady_) {
        f.dtlbSelf = ctx.dtlbSelf;
        f.dtlbFill = ctx.dtlbFill;
        f.guardHit = ctx.guard
            ? reinterpret_cast<const uint8_t*>(&ctx.guard->hit) : &noGuard;
        f.linkTable = ctx.linkTable;
        f.slowStaticHisto = ctx.slowStaticHisto;
        f.slowRuntimeHisto = ctx.slowRuntimeHisto;
        f.slowRuntimeReasonHisto = ctx.slowRuntimeReasonHisto;
        f.dtlbFillReason = ctx.dtlbFillReason;
        f.runtimeAddressObserver = ctx.runtimeAddressObserver;
        f.runtimeAddressSelf = ctx.runtimeAddressSelf;
        f.periphDue = ctx.periphDue;
        frameReady_ = true;
    }
    f.clockTarget = ctx.clockTarget;
    f.instrs = 0;
    f.exit = uint32_t(Exit::BlockEnd);
    f.periphClock = ctx.periphClock;
    f.observeWrite = ctx.observeWrite;
    f.observeWriteSelf = ctx.observeWriteSelf;
    f.slowInstrs = 0;
    using Fn = void (*)(moira::Moira*, Frame*);
    reinterpret_cast<Fn>(static_cast<A64Compiled*>(c)->entry)(ctx.cpu, &f);
    return RunResult{f.instrs, f.slowInstrs, Exit(f.exit)};
}

}  // namespace

Backend* a64Backend() {
    static A64Backend backend;
    return &backend;
}

}  // namespace jit
