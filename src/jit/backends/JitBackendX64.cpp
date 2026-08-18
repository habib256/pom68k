// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── x86-64 code generator (J2) ──
// The first backend that emits host machine code. It translates a traced
// block into a single straight-line x86-64 routine, with the 68k guest
// register file addressed in memory off one base pointer.
//
// Three decisions shape everything below; each is a consequence of the
// invariants in src/jit/POM68K_JIT.md rather than a preference.
//
// 1. NO C++ EXCEPTION MAY CROSS GENERATED CODE. There is no unwind
//    information for bytes we emitted ourselves, so a Moira fault thrown
//    through a JIT frame would reach std::terminate, not a handler. Every
//    call out of generated code therefore goes to a `noexcept` thunk that
//    reports failure as a return value, and every failure is taken at an
//    instruction boundary with NOTHING committed — the interpreter then
//    re-runs that instruction and faults exactly as it always did.
//
// 2. GUEST REGISTERS STAY IN MEMORY. The 68k's byte and word operations
//    leave the upper bits of the destination alone; x86's 8- and 16-bit
//    forms have exactly that semantics on a memory operand, so operating in
//    place on reg.d[n] gets the rule for free, with no masking and no
//    register allocator. It also means every bail-out is trivially correct:
//    there is never a live guest value in a host register across an exit.
//
// 3. CYCLE COUNTS ARE CHECKED, NOT TRUSTED. The cost tables below are
//    transcribed from Moira's own CYCLES_* tables (the 68020 column, which
//    is what the 68040 core uses), and every instruction is compiled only
//    if the table agrees with what the tracer actually measured when it ran
//    that instruction. A wrong or missing entry costs coverage, never
//    correctness — and jit_lockstep_test compares `clock` at every single
//    instruction boundary, so a disagreement that slipped through would
//    fail within seconds.
//
// What is NOT here, on purpose: 68020 indexed addressing modes (their
// extension-word format carries scale, base suppression and memory
// indirection — a decoder in its own right), everything that touches the
// SR/MMU/caches, and every instruction with more than one memory operand
// to commit. All of them simply fall back, per instruction, to Moira.

#include "JitBackendX64.h"

#include "../JitCodeBuffer.h"
#include "../JitConfig.h"
#include "X64Asm.h"

#include "Moira.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

namespace jit {

namespace {

using namespace x64;

using Layout = moira::Moira::PomJitLayout;

MemoryProofOptions proofOptions(const Layout& L) {
    MemoryProofOptions o;
    const int thunks = accessThunkMode();
    o.exactReads = thunks >= 1;
    o.exactWrites = thunks >= 2;
    o.cacheReads = L.cache040Live && cache040LineReadsEnabled();
    o.cacheWrites = L.cache040Live && cache040LineWritesEnabled();
    o.cachePairs = L.cache040Live && cache040LinePairsEnabled();
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

// ── the runtime thunks ───────────────────────────────────────────────────
// Everything generated code calls. All noexcept, all reporting failure by
// return value (decision 1 above).

// Runs one instruction through Moira, from a clean boundary.
//   1 = retired normally, 0 = did not retire (fault/exception processed),
//  -1 = the code window no longer covers this pc — leave, do not count.
extern "C" int pom68kJitStep(moira::Moira* cpu) noexcept {
    if (!cpu->pomJitCovers(cpu->getPC())) return -1;
    if (!cpu->pomJitIdle()) return -1;
    return cpu->pomJitExecOne() ? 1 : 0;
}

// Charging the clock. Generated code cannot just add to `clock`: the CPU
// wrapper's sync() is where peripheral time is handed out, and a block that
// only bumped the counter would run the guest forward with the VIA, the
// ASC, SWIM and the Cuda/Egret MCU frozen behind it. The interpreter makes
// exactly one of these calls per 68040 instruction (CYCLES_68020), so the
// JIT makes exactly one too, in the same place — at the end.
extern "C" void pom68kJitSync(moira::Moira* cpu, uint32_t cycles) noexcept {
    cpu->pomJitSync(int(cycles));
}

// The slow half of the data path: a real mmu040 access, fault turned into
// a status. `bytes` is 1, 2 or 4.
extern "C" int pom68kJitRead(moira::Moira* cpu, uint32_t addr, uint32_t bytes,
                             uint32_t* out) noexcept {
    return cpu->pomJitReadData(addr, int(bytes), *out) ? 1 : 0;
}
extern "C" int pom68kJitWrite(moira::Moira* cpu, uint32_t addr, uint32_t bytes,
                              uint32_t val) noexcept {
    return cpu->pomJitWriteData(addr, int(bytes), val) ? 1 : 0;
}

// ── the frame generated code runs against ────────────────────────────────
struct Frame {
    int64_t   clockTarget;    // +0
    uint32_t  instrs;         // +8   retired, written back on exit
    uint32_t  exit;           // +12  jit::Exit
    void*     dtlbSelf;       // +16
    uint8_t*  (*dtlbFill)(void*, uint32_t, int);   // +24
    const uint8_t* guardHit;  // +32  &CodeGuard::hit
    uint32_t  scratch;        // +40  address, across a TLB fill call
    uint32_t  saveA;          // +44  address, across a load in an RMW
    uint32_t  saveV;          // +48  value, across a TLB fill call
    uint32_t  slowInstrs;     // +52  retired through the fallback stub
    const void* periphClock;  // +56
    void*     linkTable;      // +64  the block-link table (Context)
    uint32_t  value;          // +72  operand across an access thunk
    uint32_t  pad2;
    // POM68K_JIT_HISTO only; null otherwise, and then nothing is emitted for
    // them at all. Two separate censuses because they name two different
    // pieces of work: `slowStatic` is an opcode this backend cannot emit AT
    // ALL (the target list for the next emitter), while `slowRuntime` is an
    // instruction it DID compile whose runtime access or guard could not be
    // inlined (the target list for the data path). Summing them — which is
    // what a single counter amounts to — reads as missing ISA coverage and
    // sends the work to the wrong place.
    //
    // The a64 backend has had these since it was written; x64 never wrote
    // them, so `[jit] block fallback census` printed 0 on x86-64 for as long
    // as it has existed, next to a JitBackend.h comment describing what it
    // was telling us. Fixed 2026-08-09, and it is the instrument the 68040
    // coverage-tail work is picked with.
    uint64_t* slowStaticHisto;   // +80
    uint64_t* slowRuntimeHisto;  // +88
};
constexpr int32_t kFClockTarget = 0, kFInstrs = 8, kFExit = 12;
constexpr int32_t kFDtlbSelf = 16, kFDtlbFill = 24, kFGuard = 32;
constexpr int32_t kFScratch = 40, kFSaveA = 44, kFSaveV = 48;
constexpr int32_t kFSlow = 52, kFPeriph = 56, kFLinkTab = 64, kFValue = 72;
constexpr int32_t kFHistoStatic = 80, kFHistoRuntime = 88;
static_assert(offsetof(Frame, slowStaticHisto) == kFHistoStatic);
static_assert(offsetof(Frame, slowRuntimeHisto) == kFHistoRuntime);

// Sizes. A 64-instruction block of memory-touching forms tops out around
// 12 KB with its cold half; the code buffer holds thousands of blocks and
// the engine flushes when it fills.
constexpr size_t kMaxBlockBytes = 64u * 1024;
constexpr size_t kCodeBufferBytes = 128u * 1024 * 1024;

// What generated code reads when no guard is attached. Never non-zero.
const uint8_t kNoGuard = 0;

// Host register roles. rbx/rbp/r14/r15 are callee-saved, so they survive
// every thunk call; everything else is scratch and is assumed clobbered.
constexpr Reg kCpu = RBX;      // moira::Moira*
constexpr Reg kFrm = RBP;      // Frame*
constexpr Reg kTgt = R14;      // clock target, cached
constexpr Reg kCnt = R15;      // instructions retired so far (32-bit half)
constexpr Reg kPer = R12;      // &lastPeriphClock_, for the inline pacing test
// The cycle clock, live in a register for the whole chain of linked blocks.
// It used to live only in the Moira object, which put a store-to-load
// forward on the critical path of EVERY emitted instruction: the cycle
// charge stored it and the next instruction's budget guard loaded it
// straight back. Anything that calls out spills it first and reloads after,
// and all of those paths are cold.
constexpr Reg kClk = R13;

// ── 68020-column cycle tables (Moira MoiraExec_cpp.h) ────────────────────
// Indexed by 68k addressing mode: 0 Dn, 1 An, 2 (An), 3 (An)+, 4 -(An),
// 5 d16(An), 6 indexed (unsupported), 7.0 abs.w, 7.1 abs.l, 7.2 d16(PC),
// 7.4 immediate. Index 7..11 hold the mode-7 sub-forms.
enum : int { kM_DN = 0, kM_AN, kM_AI, kM_PI, kM_PD, kM_DI, kM_IX,
             kM_AW, kM_AL, kM_DIPC, kM_IXPC, kM_IM, kM_COUNT };

// Mode index for (mode, reg), or -1 when the backend does not support it.
int eaIndex(int mode, int reg) {
    if (mode < 7) return mode == 6 ? -1 : mode;
    switch (reg) {
        case 0: return kM_AW;
        case 1: return kM_AL;
        case 2: return kM_DIPC;
        case 4: return kM_IM;
        default: return -1;                 // 7.3 (PC indexed), 7.5+ reserved
    }
}

// "Read an operand through <ea>" — execMove0 / execAndEaRg / execTst /
// execCmpiRg all agree on these on the 68020.
const int8_t kEaRead[kM_COUNT][3] = {   // [mode][0=B 1=W 2=L]
    { 2, 2, 2 },   // Dn
    { 2, 2, 2 },   // An
    { 6, 6, 6 },   // (An)
    { 6, 6, 6 },   // (An)+
    { 7, 7, 7 },   // -(An)
    { 7, 7, 7 },   // d16(An)
    { -1, -1, -1 },// indexed — unsupported
    { 6, 6, 6 },   // abs.w
    { 6, 6, 6 },   // abs.l
    { 7, 7, 7 },   // d16(PC)
    { -1, -1, -1 },// PC indexed — unsupported
    { 4, 4, 6 },   // #imm
};

// "Read-modify-write <ea>" — execAndRgEa / execAddqEa / execAndiEa /
// execClr / execNegEa / the BTST family all agree: the read cost plus two,
// and a plain 2 when the destination is a register.
int eaRmwCost(int m, int szIdx) {
    const int r = kEaRead[m][szIdx];
    if (r < 0) return -1;
    return (m == kM_DN || m == kM_AN) ? 2 : r + 2;
}

// MOVE's destination surcharge (execMove0/2/3/4/5/7/8, 68020 column).
//
// The `(xxx).W` cell read 3 until 2026-08-09 and is 2: execMove7's 68020
// column is byte-for-byte execMove2's and execMove3's — on the 020 an
// absolute-short destination costs exactly what a register-indirect one
// does, because its extension word is already in the prefetch queue. The
// off-by-one made the cross-check refuse EVERY `MOVE <ea>,(xxx).W`, and
// the first honest fallback census on x86-64 put that single wrong cell at
// **47.4 % of all block fallbacks** on the idle Finder (opcodes 21DF and
// 21CF, 3.94 M executions each over 648 M instructions).
//
// It cost coverage and never correctness, which is the whole point of
// cross-checking the table against the tracer's own measurement instead of
// trusting it — but it stayed invisible for as long as it did because the
// census that would have named it was never wired on this backend.
const int8_t kMoveDst[kM_COUNT] = { 0, 0, 2, 2, 3, 3, -1, 2, 4, -1, -1, -1 };

// ── decoded effective address ────────────────────────────────────────────
struct Ea {
    int  mode = 0, reg = 0;
    int  idx = -1;                  // kM_* index
    int32_t value = 0;              // displacement, absolute address or imm
    int  ext = 0;                   // extension words consumed
    bool memory = false;            // needs a real guest access
};

int sizeBytes(int szIdx) { return szIdx == 0 ? 1 : szIdx == 1 ? 2 : 4; }
Sz  hostSz(int szIdx) { return szIdx == 0 ? Sz::B : szIdx == 1 ? Sz::W : Sz::L; }

bool hostAluOperation(AluOperation operation, Asm::Op& out) {
    switch (operation) {
        case AluOperation::Or:  out = Asm::Op::OR;  return true;
        case AluOperation::And: out = Asm::Op::AND; return true;
        case AluOperation::Eor: out = Asm::Op::XOR; return true;
        case AluOperation::Add: out = Asm::Op::ADD; return true;
        case AluOperation::Sub: out = Asm::Op::SUB; return true;
        case AluOperation::Cmp: out = Asm::Op::CMP; return true;
        default: return false;
    }
}

// ── the compiler ─────────────────────────────────────────────────────────
class Emitter {
public:
    Emitter(Asm& a, const Layout& L, const BlockIr& ir, const Context& ctx)
        : a_(a), L_(L), ir_(ir),
          paced_(ctx.periphClock != nullptr && ctx.periphBatch != 0),
          batch_(ctx.periphBatch), linkMask_(ctx.linkTable ? ctx.linkMask : 0),
          histo_(ctx.slowStaticHisto != nullptr),
          ic_(L.icLive && icacheEmitEnabled()) {}

    // Emits the whole block. False = give up; the engine keeps the IR and
    // runs the block through the fetch-window loop instead.
    bool emit();

    int nativeCount() const { return native_; }
    size_t linkEntryOffset() const { return linkEntry_; }
    // For POM68K_JIT_VERBOSE: whether the block's terminating branch stayed
    // inside generated code. A loop that closes on itself is the whole
    // reason this backend compiles branches at all, so when it does NOT
    // close, that is the first thing worth seeing.
    int  loopedTo() const { return loopTo_; }

private:
    // ── per-instruction ──────────────────────────────────────────────────
    bool emitInstr(size_t i);
    bool emitMove(size_t i, int szIdx);
    bool emitScc(size_t i);
    void chargeIcache(size_t i);
    void unchargeIcache(size_t i);
    bool emitAluEaRg(size_t i);
    bool emitAluRgEa(size_t i);
    bool emitAddSubQ(size_t i);
    bool emitMoveq(size_t i);
    bool emitImmediate(size_t i);
    bool emitBitOp(size_t i);
    bool emitLine4(size_t i);        // TST / CLR / NOT / NEG / EXT / SWAP / LEA
    bool emitBranch(size_t i);
    bool emitDbcc(size_t i);         // DBcc — terminator (census 2026-07-30)
    bool emitJmp(size_t i);          // JMP <ea> — terminator, JSR minus the push
    bool emitMovem(size_t i);        // MOVEM — straight-line, one span probe

    // ── operand plumbing ─────────────────────────────────────────────────
    bool decode(size_t i, int mode, int reg, int szIdx, int extAt, Ea& ea);
    void addrOf(const Ea& ea, Reg dst, int szIdx);
    void commitEa(const Ea& ea, int szIdx,
                  const MemoryAccessPlan& access);
    // The decoded operands must account for EXACTLY the instruction length
    // the tracer measured. If they do not, this backend has misread the
    // encoding and every extension word it baked in is suspect.
    bool lengthOk(size_t i, int extWords) const {
        return 1 + extWords == int(ir_.instrs[i].words);
    }
    // An auto-increment source updates its address register BEFORE the
    // destination address is computed. Rather than model that (and lose the
    // "nothing is committed until every access has succeeded" property that
    // makes bail-out safe), refuse the aliasing cases outright.
    static bool aliases(const Ea& src, const Ea& dst) {
        if (src.idx != kM_PI && src.idx != kM_PD) return false;
        switch (dst.idx) {
            case kM_AN: case kM_AI: case kM_PI: case kM_PD: case kM_DI:
                return dst.reg == src.reg;
            default:
                return false;
        }
    }
    // Loads <ea> into `dst` (32-bit host register, value right-justified).
    void load(const Ea& ea, int szIdx, Reg dst,
              const MemoryAccessPlan& access, bool signExtend = false);
    void store(const Ea& ea, int szIdx, Reg src,
               const MemoryAccessPlan& access);
    // Guest memory at the address in `addr`, host pointer left in rsi.
    // `miss` is where a translation the inline TLB cannot serve goes.
    // `bytes` is the SPAN the caller will touch through the pointer — a
    // size for a plain access, n×size for a MOVEM burst; a span crossing
    // the page boundary misses (two translations, someone else's problem).
    void memProbe(Reg addr, int bytes, bool write, Label& miss,
                  bool cacheRead = false, Label* cacheWriteHit = nullptr,
                  bool cacheOnly = false, bool countCacheHit = true);
    // Access capabilities are minted from Instr::memory. In particular,
    // exact-thunk and cache eligibility are never guessed at a call site.
    void memLoad(Reg addr, int szIdx, Reg dst,
                 const MemoryAccessPlan& access);
    void memStore(Reg addr, int szIdx, Reg src,
                  const MemoryAccessPlan& access);
    void memRmwLoad(Reg addr, int szIdx, Reg dst,
                    const MemoryAccessPlan& read,
                    const MemoryAccessPlan& write);
    void memRmwStore(int szIdx, Reg src,
                     const MemoryAccessPlan& read,
                     const MemoryAccessPlan& write);
    void markCache040Dirty(Reg addr, int bytes);

    // ── flags ────────────────────────────────────────────────────────────
    void flagsLogic(Sz sz);          // N,Z from the result; V=C=0
    void flagsAddSub(bool withX);    // N,Z,V,C from EFLAGS (+X = C)
    void clearVC();
    void condToAl(int cc);           // 68k condition -> ZF set == taken

    // ── boilerplate ──────────────────────────────────────────────────────
    Mem D(int n) const { return mem(kCpu, int32_t(L_.d + 4u * unsigned(n))); }
    Mem A(int n) const { return mem(kCpu, int32_t(L_.a + 4u * unsigned(n))); }
    // MOVEM's flat view: r[0-7] = D0-D7, r[8-15] = A0-A7 (Moira reg.r).
    Mem R(int n) const { return n < 8 ? D(n) : A(n - 8); }
    Mem F(int32_t off) const { return mem(kFrm, off); }
    Mem at(uint32_t off) const { return mem(kCpu, int32_t(off)); }

    void guards(size_t i);
    // The architectural state at an instruction BOUNDARY: pc, pc0 and the
    // prefetch queue. Nothing inside a block reads any of it — only the
    // exception, trace and interpreter paths downstream of an exit do — so
    // it is emitted in the COLD stubs that lead to those exits rather than
    // after every instruction. That is 30 of the ~150 bytes a compiled
    // instruction used to cost, and on this workload code size is the
    // binding constraint (src/jit/POM68K_JIT.md § 7).
    void emitBoundary(uint32_t pc, int prevIdx);
    void call(void* fn);
    // Leaves the block for `pc`. With a link table available this is a tag
    // compare and an indirect jump straight into the next compiled block —
    // no return to the engine, no hash lookup, no frame set-up. Only exits
    // whose target is a compile-time constant can use it, which is exactly
    // the three that matter: branch taken, branch not taken, and running
    // off the end of the recorded straight line.
    void leaveTo(uint32_t pc);
    // The same, for a target only known at run time (RTS, JSR (An)). The
    // slot index has to be computed instead of folded, which is four more
    // instructions — still nothing against returning to the engine.
    void leaveToDynamic();
    bool emitSubroutine(size_t i);   // JSR / BSR / RTS
    void chargeCycles(int cycles);
    void spillClock() { a_.movMR(Sz::Q, at(L_.clock), kClk); }
    void fillClock()  { a_.movRM(Sz::Q, kClk, at(L_.clock)); }
    // The per-instruction state mmu040InstrStart maintains and generated
    // code would otherwise skip. Both are read AFTER a block exits, by code
    // the JIT never sees, so both have to be exactly what the interpreter
    // would have left behind at that instruction boundary.
    void commitQueue(uint16_t ird, uint16_t irc);
    void pollIpl();

    Asm& a_;
    const Layout& L_;
    const BlockIr& ir_;

    std::vector<Label*> entry_;      // start of each instruction
    std::vector<Label*> slow_;       // its "hand this one to Moira" stub
    std::vector<Label*> budget_;     // …and the two guard exits, which have
    std::vector<Label*> flags_;      // to commit the boundary state first
    // POM68K_JIT_HISTO only: two counting doors onto the SAME stub body, so
    // the census can tell "this backend has no emitter for that opcode" from
    // "it compiled the instruction and the access bailed out". Unused (and
    // never emitted) when the census is off — the two labels then simply
    // alias slow_[i] and cost nothing.
    std::vector<Label*> slowStatic_;
    std::vector<Label*> slowRuntime_;
    Label *exitBudget_ = nullptr, *exitFlags_ = nullptr, *exitFault_ = nullptr;
    Label *exitLost_ = nullptr, *epilogue_ = nullptr;
    int  loopTo_ = -1;
    uint32_t linkMask_ = 0;
    // Emission deferred to AFTER the block body. Everything a memory access
    // does when the inline TLB cannot serve it — asking the engine to fill
    // an entry, calling the access thunk — is cold, and leaving it inline
    // was costing far more than it looked like: a block of 33 instructions
    // came to 13 KB, and generated code that does not fit in cache is not
    // fast however good the instruction selection is.
    std::vector<std::function<void()>> cold_;
    bool paced_ = false;
    int  batch_ = 0;
    size_t linkEntry_ = 0;
    int native_ = 0;
    size_t cur_ = 0;                 // instruction being emitted
    bool histo_ = false;             // POM68K_JIT_HISTO
    // 68030 i-cache overlay (Layout::icLive). When set, every NATIVELY
    // emitted instruction charges the model for the words it fetches; the
    // cold fallback stub charges itself, through pomJitExecOne ->
    // mmuExecuteStart -> mmuFetchWord. `icSkip_` is the block-entry branch
    // that jumps over all of it when CACR bit 0 is clear.
    bool    ic_ = false;
    Label*  icOff_ = nullptr;
    // Which (line, tag, longword-bit) triples this block has already
    // charged. Direct-mapped, 16 lines: within a block, a second fetch of
    // the same longword of the same line is a guaranteed HIT, because
    // nothing but instruction fetch touches the i-cache and MOVEC — the
    // only way CACR moves — is Kind::Unsafe and cannot appear inside one.
    uint32_t icTag_[16] = {};
    uint8_t  icValid_[16] = {};
    bool     icSeen_[16] = {};

    // The door a RUNTIME bail-out takes. With the census off it is the stub
    // itself, so nothing about the hot path changes; with it on, it is the
    // counting door. Written as one accessor so that a new bail-out site
    // cannot forget to be counted — the sites all read `runtimeStub(i)`.
    // A RUNTIME bail-out needs its own door whenever anything has to be
    // undone or counted on the way out; a STATIC one never does, because
    // rewind() has already removed everything the instruction emitted.
    bool needRuntimeDoor() const { return histo_ || ic_; }
    Label& runtimeStub(size_t i) {
        return needRuntimeDoor() ? *slowRuntime_[i] : *slow_[i];
    }
    Label& staticStub(size_t i)  { return histo_ ? *slowStatic_[i]  : *slow_[i]; }
};

// ── the 68030 instruction cache, charged by generated code ───────────────
// MC68030UM §6: 256 bytes = 16 lines x 4 longwords, LOGICAL, direct-mapped,
// tag = A[31:8] + supervisor, per-longword valid bits, gated on CACR bit 0,
// `missPenalty` cycles charged on a miss. The interpreter charges it inside
// mmuFetchWord (MoiraExecMMU_cpp.h:421-454) — before the code window hook,
// so the fetch window and the threaded backend inherit it. Generated code
// fetches nothing, so it has to do this itself or the clock drifts from the
// interpreter's inside one block. Measured: with x64 forced onto an LC II
// the two engines' miss counts part company at the first block that runs.
//
// Everything the model needs is a COMPILE-TIME constant for a known pc:
//
//   line = (addr >> 4) & 15   tag = (addr >> 8) | (super ? 1<<31 : 0)
//   bit  = 1 << ((addr >> 2) & 3)          super = BlockIr::super
//
// so a fetch is a load of tag[line] at a fixed offset, a compare against an
// immediate, a test of valid[line] against an immediate, and a cold miss
// stub. Two folds make it cheaper still: within a block, only the FIRST
// touch of a (line, tag, longword) can miss — nothing but instruction fetch
// touches this cache, and MOVEC is Kind::Unsafe so CACR cannot move inside
// a block — and the whole thing is skipped at block entry when the cache is
// disabled.
//
// WHERE it is emitted matters as much as what: immediately before the
// instruction's own code, on the natively-emitted path ONLY. The fallback
// stub re-enters Moira through pomJitExecOne(), whose 030 branch runs
// mmuExecuteStart<C68020>() and therefore charges the model itself
// (Moira.cpp:327). Charging here as well would double-count; charging
// before the bail-out decision would charge an instruction that then re-runs
// and charges again.
void Emitter::chargeIcache(size_t i) {
    if (!ic_) return;
    const Instr& in = ir_.instrs[i];
    // What the interpreter fetches for this instruction: mmuExecuteStart
    // reads ird at pc and irc at pc + 2, then readExt walks the extension
    // words — so every word of the instruction, plus the lookahead at
    // pc + 2 for a one-word instruction.
    // HOW MANY WORDS THE INTERPRETER ACTUALLY FETCHES, which is not the
    // instruction's length. mmuExecuteStart reads ird at pc and irc at
    // pc + 2 — two fetches for every instruction, however short — and then
    // each readExt<C> on the 68030 CONSUMES queue.irc and refetches the next
    // word (MoiraDataflow_cpp.h, `queue.irc = mmuFetchWord(reg.pc)` after
    // `reg.pc += 2`). An instruction of W words therefore performs **W + 1**
    // fetches, at pc, pc+2, … pc+2W — one past its own last word, because
    // the queue always runs a word ahead.
    //
    // The first cut charged W (floored at 2), which is right only for
    // one-word instructions and under-charges every other by exactly one.
    // The 030 lockstep measured the shortfall directly: 2 084 fetches missing
    // over one bring-up run, with the miss count and the clock already
    // correct — the shape of a systematic off-by-one, not of a lost event.
    const uint32_t words = uint32_t(in.words) + 1;
    const uint32_t sup = ir_.super ? 0x80000000u : 0u;

    // `fetches` counts every fetch while armed, cache ENABLED OR NOT — it is
    // a diagnostic, but Cpu030::icacheStats() exposes it and the 030 lockstep
    // compares it, so it is kept exact. It sits outside the CACR gate for the
    // same reason the interpreter's does.
    a_.aluMI(Asm::Op::ADD, Sz::Q, at(L_.icFetches), int32_t(words));

    // ONE gate for the whole instruction: CACR cannot change inside a block
    // (MOVEC is Kind::Unsafe), so one test covers every word.
    //
    // The gate is also what makes the compile-time fold below SOUND. A
    // "guaranteed hit" is only guaranteed because an earlier fetch in this
    // block performed the tag/valid update — which it did only if the cache
    // was enabled. Putting the folded hit under the same gate as the update
    // that justifies it keeps the two on the same side of that question.
    Label& gateOff = *a_.fresh();
    a_.testMI(Sz::B, at(L_.cacr), 1);
    a_.jcc(Cc::E, gateOff);

    for (uint32_t w = 0; w < words; w++) {
        const uint32_t addr = in.pc + w * 2;
        const int line = int((addr >> 4) & 15);
        const uint32_t tag = (addr >> 8) | sup;
        const uint8_t bit = uint8_t(1u << ((addr >> 2) & 3));

        // Already charged in this block, same tag, same longword: the
        // interpreter would hit, and a hit costs nothing but the counter.
        if (icSeen_[line] && icTag_[line] == tag && (icValid_[line] & bit)) {
            a_.aluMI(Asm::Op::ADD, Sz::Q, at(L_.icHits), 1);
            continue;
        }

        Label& done = *a_.fresh();
        Label& miss = *a_.fresh();
        a_.aluMI(Asm::Op::CMP, Sz::L, at(L_.icTag + uint32_t(line) * 4),
                 int32_t(tag));
        a_.jcc(Cc::NE, miss);
        a_.testMI(Sz::B, at(L_.icValid + uint32_t(line)), bit);
        a_.jcc(Cc::E, miss);
        a_.aluMI(Asm::Op::ADD, Sz::Q, at(L_.icHits), 1);
        a_.jmp(done);

        // Miss: direct-mapped, so a tag change evicts the WHOLE line before
        // the new longword is marked valid. Cold, but not deferred to the
        // block's cold half — it falls through into the next instruction and
        // deferring it would need a jump back for no gain.
        a_.bind(miss);
        Label& sameTag = *a_.fresh();
        a_.aluMI(Asm::Op::CMP, Sz::L, at(L_.icTag + uint32_t(line) * 4),
                 int32_t(tag));
        a_.jcc(Cc::E, sameTag);
        a_.movMI(Sz::L, at(L_.icTag + uint32_t(line) * 4), int32_t(tag));
        a_.movMI(Sz::B, at(L_.icValid + uint32_t(line)), 0);
        a_.bind(sameTag);
        a_.aluMI(Asm::Op::OR, Sz::B, at(L_.icValid + uint32_t(line)), bit);
        a_.aluMI(Asm::Op::ADD, Sz::Q, at(L_.icMisses), 1);
        // The penalty is a wrapper knob read at run time, not a constant:
        // POM68K_ICACHE_MISS can differ per machine and the interpreter
        // reads it from the same field.
        // A 32-bit load zeroes the upper half, and missPenalty is clamped to
        // [0, 64] where it is read from the environment (Cpu030.cpp), so no
        // sign extension is needed to reach the 64-bit clock.
        a_.movRM(Sz::L, RAX, at(L_.icPenalty));
        a_.aluRR(Asm::Op::ADD, Sz::Q, kClk, RAX);
        a_.bind(done);

        // Model the same transition in the COMPILER's shadow copy, so the
        // rest of this block can fold. The shadow is only ever used to prove
        // a later fetch is a guaranteed hit, and it is conservative by
        // construction: it starts empty at every block.
        if (!icSeen_[line] || icTag_[line] != tag) {
            icTag_[line] = tag; icValid_[line] = 0; icSeen_[line] = true;
        }
        icValid_[line] |= bit;
    }
    a_.bind(gateOff);
}

// The exact inverse of one instruction's charge, for every path that hands
// the instruction back to be RE-RUN. The charge is emitted before the
// instruction, in the interpreter's order, and the re-run goes through
// pomJitExecOne -> mmuExecuteStart -> mmuFetchWord, which charges it a second
// time. That second charge is a HIT for every word — the first one left the
// tag and the valid bit set — so the over-count is exactly N fetches and N
// hits whichever way the first charge went, and the miss count and the clock
// are already right. Subtracting those two is therefore exact, not an
// approximation.
//
// `hits` only ever moves while the cache is enabled, so its half sits behind
// the same CACR test the charge did; `fetches` counts regardless, like the
// interpreter's.
void Emitter::unchargeIcache(size_t i) {
    if (!ic_) return;
    const uint32_t words = uint32_t(ir_.instrs[i].words) + 1;
    a_.aluMI(Asm::Op::SUB, Sz::Q, at(L_.icFetches), int32_t(words));
    Label& noHits = *a_.fresh();
    a_.testMI(Sz::B, at(L_.cacr), 1);
    a_.jcc(Cc::E, noHits);
    a_.aluMI(Asm::Op::SUB, Sz::Q, at(L_.icHits), int32_t(words));
    a_.bind(noHits);
}

// One call per instruction, matching the interpreter's one CYCLES_68020
// per instruction. It is the single most expensive thing in an emitted
// instruction, and it is not negotiable: see pom68kJitSync above.
void Emitter::chargeCycles(int cycles) {
    if (!paced_) {
        // No pacing information: call every time. The spill and the reload
        // are NOT optional here, and leaving them out is how this path threw
        // every cycle charge silently away. `clock` lives in a callee-saved
        // register for the whole chain of linked blocks (kClk), so the
        // callee — which reads it and adds to it — has to be handed the
        // current value, and the register has to be re-read afterwards or
        // the epilogue's spillClock() writes the stale one back OVER the
        // charge. The cold half of the paced path below has always done
        // exactly this; this branch simply never did.
        //
        // It went unnoticed because the four wrappers that pace the engine
        // (Cpu040, CentrisCpu, Q630Cpu, Q700Cpu) are precisely the four
        // families the code generators declare, so no generated code had
        // ever taken this branch. The first thing that did — an x86-64
        // block on a 68030 — ran its only compiled instruction for FREE,
        // which let the guest run one instruction past its cycle budget and
        // part company with the interpreter. Found 2026-08-10 by
        // jit_lockstep_030_test; docs/JIT_BRINGUP.md § C.4.
        spillClock();
        a_.movRR(Sz::Q, RDI, kCpu);
        a_.movRI(RSI, uint32_t(cycles));
        call(reinterpret_cast<void*>(&pom68kJitSync));
        fillClock();
        return;
    }
    // The wrapper's own batching test, inlined: sync() advances the clock and
    // then only runs the machine forward once `batch_` cycles have piled up.
    // Doing that test here rather than in the callee is what keeps an emitted
    // instruction down to a handful of host instructions — and the decision
    // it reaches is bit-for-bit the one sync() would have reached.
    // Load-add-store rather than add-to-memory-then-reload: the reload
    // would be a load of an address stored one instruction earlier, and
    // store-to-load forwarding puts that latency directly on the dependency
    // chain of every single emitted instruction.
    Label& done = *a_.fresh();
    Label& due = *a_.fresh();
    if (cycles) a_.aluRI(Asm::Op::ADD, Sz::Q, kClk, cycles);
    a_.movRR(Sz::Q, RAX, kClk);
    if (batch_ < 0) {
        a_.aluRM(Asm::Op::CMP, Sz::Q, RAX, mem(kPer, 0));
    } else {
        a_.aluRM(Asm::Op::SUB, Sz::Q, RAX, mem(kPer, 0));
        a_.aluRI(Asm::Op::CMP, Sz::Q, RAX, batch_);
    }
    a_.jcc(Cc::GE, due);
    a_.bind(done);
    // Cold: peripheral time is actually owed. sync() reads and advances the
    // clock, so the register goes back to memory around the call — and the
    // call sequence itself leaves the hot path with it.
    cold_.push_back([this, &due, &done] {
        a_.bind(due);
        spillClock();
        a_.movRR(Sz::Q, RDI, kCpu);
        a_.aluRR(Asm::Op::XOR, Sz::L, RSI, RSI);    // the cycles are already in
        call(reinterpret_cast<void*>(&pom68kJitSync));
        fillClock();
        a_.jmp(done);
    });
}

// The prefetch queue. On the 68040 there is no queue refill at the end of an
// instruction (prefetch() returns early), so at a boundary `ird` still holds
// the instruction that just ran and `irc` the word the fetch left behind —
// which is the word at the new pc for everything except a TAKEN Bcc, whose
// irc is still its own displacement word. Nothing in a compiled block reads
// either; the exception and trace paths downstream of a block exit do.
void Emitter::commitQueue(uint16_t ird, uint16_t irc) {
    // PrefetchQueue is { u16 irc; u16 ird; } and both halves are known at
    // compile time, so on a little-endian host the pair is one 32-bit store
    // rather than two 16-bit ones. Checked, not assumed.
    if (L_.ird == L_.irc + 2) {
        a_.movMI(Sz::L, at(L_.irc), int32_t((uint32_t(ird) << 16) | irc));
        return;
    }
    a_.movMI(Sz::W, at(L_.ird), int32_t(ird));
    a_.movMI(Sz::W, at(L_.irc), int32_t(irc));
}

// POLL_IPL, which mmu040InstrStart performs at the head of every
// instruction: the interrupt-priority pin is sampled into the register the
// exception logic compares against. Skipping it would let a block decide,
// one instruction later, that an interrupt it should have taken was not
// pending yet.
void Emitter::pollIpl() {
    a_.movRM(Sz::B, RAX, at(L_.iplPin));
    a_.movMR(Sz::B, at(L_.regIpl), RAX);
}

void Emitter::leaveTo(uint32_t pc) {
    if (linkMask_) {
        // The slot index is a function of a CONSTANT, so it is folded here
        // and the run-time cost is one load, one compare and one jump.
        const int32_t off = int32_t(((pc >> 1) & linkMask_) * 16);
        Label& miss = *a_.fresh();
        a_.movRM(Sz::Q, RAX, F(kFLinkTab));
        a_.aluMI(Asm::Op::CMP, Sz::L, mem(RAX, off),
                 int32_t(pc | uint32_t(ir_.super)));
        a_.jcc(Cc::NE, miss);
        a_.jmpM(mem(RAX, off + 8));
        a_.bind(miss);
    }
    a_.movMI(Sz::L, F(kFExit), int32_t(Exit::BlockEnd));
    a_.jmp(*epilogue_);
}

// The dynamic target is read back from MEMORY (at(L_.pc), which every
// caller stores before charging cycles), never trusted in a register:
// chargeCycles' pacing callout clobbers all caller-saved registers, and a
// garbage tag that happens to match a populated slot jumps into an
// UNRELATED block — with the deadline pacing calling out on nearly every
// RTS in a device-poll loop, that was the 2026-08-04 red gate (TODO § 1).
void Emitter::leaveToDynamic() {
    if (linkMask_) {
        Label& miss = *a_.fresh();
        a_.movRM(Sz::L, RDX, at(L_.pc));
        a_.movRR(Sz::L, RCX, RDX);
        a_.shiftRI(Sz::L, RCX, 5, 1);                 // pc >> 1
        a_.aluRI(Asm::Op::AND, Sz::L, RCX, int32_t(linkMask_));
        a_.shiftRI(Sz::L, RCX, 4, 4);                 // * sizeof(slot)
        a_.movRM(Sz::Q, RAX, F(kFLinkTab));
        a_.aluRR(Asm::Op::ADD, Sz::Q, RAX, RCX);
        if (ir_.super) a_.aluRI(Asm::Op::OR, Sz::L, RDX, 1);
        a_.aluRM(Asm::Op::CMP, Sz::L, RDX, mem(RAX, 0));
        a_.jcc(Cc::NE, miss);
        a_.jmpM(mem(RAX, 8));
        a_.bind(miss);
    }
    a_.movMI(Sz::L, F(kFExit), int32_t(Exit::BlockEnd));
    a_.jmp(*epilogue_);
}

// JSR <ea>, BSR and RTS. All three end the block; all three are compiled
// rather than handed back, because together they are 7 % of a real Mac OS
// workload and each one used to be an interpreter round trip AND a link the
// block linker could not cross.
//
// The 68040 raises an address error on an odd target, BEFORE anything is
// committed. A constant target is checked here at compile time; a computed
// one is tested at run time and bails to the interpreter, which raises it
// properly.
bool Emitter::emitSubroutine(size_t i) {
    const Instr& in = ir_.instrs[i];
    const uint16_t op = in.opcode;
    const InstructionSemantics& sem = in.semantics;
    const ControlFlowPlan& control = in.control;
    // What the prefetch queue holds after any of these: the 68040 refills
    // nothing at the end of an instruction, so `irc` is still the word
    // mmu040InstrStart read at pc + 2.
    const uint16_t ircAfter = ir_.prefetchWord(in.pc + 2);

    if (sem.operation == SemanticOp::ReturnSubroutine) {
        if (!control.valid || control.kind != ControlFlowKind::Return ||
            in.cycles != 10) return false;
        auto memory = instructionMemoryPlan(in.memory, proofOptions(L_));
        const MemoryAccessPlan read = memory.access(
            MemoryDirection::Read, MemoryOperand::Stack, 4, 3, 7);
        if (!read.valid() || !memory.complete()) return false;
        a_.movRM(Sz::L, RAX, A(7));
        memLoad(RAX, 2, RDI, read);                 // the return address
        // Odd target: the 68040 raises an address error, and it does so
        // before the stack pointer moves. Nothing is committed yet, so
        // handing the whole instruction over is exact.
        a_.testRI(Sz::L, RDI, 1);
        a_.jcc(Cc::NE, runtimeStub(i));
        a_.aluMI(Asm::Op::ADD, Sz::L, A(7), 4);
        a_.movMR(Sz::L, at(L_.pc), RDI);
        a_.movMR(Sz::L, at(L_.pc0), RDI);
        commitQueue(op, ircAfter);
        chargeCycles(10);
        a_.aluRI(Asm::Op::ADD, Sz::L, kCnt, 1);
        leaveToDynamic();
        return true;
    }

    if (sem.operation == SemanticOp::BranchSubroutine) {
        if (!control.valid || control.kind != ControlFlowKind::DirectCall ||
            !control.targetKnown || !control.pushesReturnAddress ||
            in.words > 2 || in.cycles != 7) return false;
        auto memory = instructionMemoryPlan(in.memory, proofOptions(L_));
        const MemoryAccessPlan write = memory.access(
            MemoryDirection::Write, MemoryOperand::Stack, 4, 4, 7);
        if (!write.valid() || !memory.complete()) return false;
        const uint32_t target = control.target;
        if (target & 1) return false;
        // The return address is the next instruction, for both forms
        // (Moira: reg.pc + 2 on the word form, and reg.pc is already past
        // the opcode).
        a_.movRM(Sz::L, RAX, A(7));
        a_.aluRI(Asm::Op::SUB, Sz::L, RAX, 4);
        a_.movMR(Sz::L, F(kFSaveA), RAX);
        a_.movRI(RDI, control.returnAddress);
        memStore(RAX, 2, RDI, write);
        a_.movRM(Sz::L, RAX, F(kFSaveA));
        a_.movMR(Sz::L, A(7), RAX);
        a_.movMI(Sz::L, at(L_.pc), int32_t(target));
        a_.movMI(Sz::L, at(L_.pc0), int32_t(target));
        commitQueue(op, ircAfter);
        chargeCycles(7);
        a_.aluRI(Asm::Op::ADD, Sz::L, kCnt, 1);
        leaveTo(target);
        return true;
    }

    if (sem.operation != SemanticOp::JumpSubroutine || !control.valid ||
        !control.pushesReturnAddress) return false;
    auto memory = instructionMemoryPlan(in.memory, proofOptions(L_));
    const MemoryAccessPlan write = memory.access(
        MemoryDirection::Write, MemoryOperand::Stack, 4, 4, 7);
    if (!write.valid() || !memory.complete()) return false;
    const int mode = sem.eaMode, reg = sem.eaReg;
    Ea ea;
    if (!decode(i, mode, reg, 2, 0, ea)) return false;
    if (!ea.memory) return false;                    // JSR needs an address
    if (ea.idx == kM_PI || ea.idx == kM_PD) return false;   // not encodable
    if (!lengthOk(i, ea.ext)) return false;
    static const int8_t kJsr[kM_COUNT] =
        { -1, -1, 4, -1, -1, 5, -1, 4, 4, 5, -1, -1 };
    if (kJsr[ea.idx] < 0 || kJsr[ea.idx] != int(in.cycles)) return false;

    const bool constant = control.targetKnown;
    if (constant && (control.target & 1)) return false;

    // The target first: it must be known good before the stack moves,
    // because an odd one is an address error with nothing committed.
    addrOf(ea, RAX, 2);
    a_.movMR(Sz::L, F(kFValue), RAX);
    if (!constant) {
        a_.testRI(Sz::L, RAX, 1);
        a_.jcc(Cc::NE, runtimeStub(i));
    }
    // Push the return address — the next instruction, since computeEA has
    // already consumed this one's extension words.
    a_.movRM(Sz::L, RAX, A(7));
    a_.aluRI(Asm::Op::SUB, Sz::L, RAX, 4);
    a_.movMR(Sz::L, F(kFSaveA), RAX);
    a_.movRI(RDI, control.returnAddress);
    memStore(RAX, 2, RDI, write);
    a_.movRM(Sz::L, RAX, F(kFSaveA));
    a_.movMR(Sz::L, A(7), RAX);

    a_.movRM(Sz::L, RDI, F(kFValue));
    a_.movMR(Sz::L, at(L_.pc), RDI);
    a_.movMR(Sz::L, at(L_.pc0), RDI);
    commitQueue(op, ircAfter);
    chargeCycles(int(in.cycles));
    a_.aluRI(Asm::Op::ADD, Sz::L, kCnt, 1);
    if (constant) leaveTo(control.target);
    else          leaveToDynamic();
    return true;
}

void Emitter::call(void* fn) {
    a_.movRI64(RAX, uint64_t(uintptr_t(fn)));
    a_.callR(RAX);
}

// Clock budget and Moira's flag word, tested before every instruction —
// exactly the two checks the threaded backend makes, and for the same
// reason: a peripheral that ticked inside the previous instruction may have
// raised an interrupt, and the caller's cycle target is a hard ceiling.
void Emitter::guards(size_t i) {
    a_.aluRR(Asm::Op::CMP, Sz::Q, kClk, kTgt);
    a_.jcc(Cc::GE, *budget_[i]);
    a_.aluMI(Asm::Op::CMP, Sz::L, at(L_.flags), 0);
    a_.jcc(Cc::NE, *flags_[i]);
}

void Emitter::emitBoundary(uint32_t pc, int prevIdx) {
    (void)prevIdx;
    a_.movMI(Sz::L, at(L_.pc), int32_t(pc));
    a_.movMI(Sz::L, at(L_.pc0), int32_t(pc));
    // The prefetch queue is NOT deferred with them, and that is not an
    // oversight. `ird` at a boundary is the opcode of whatever ran last,
    // and an instruction a backward branch jumps to is reached from two
    // different predecessors — the branch, and the instruction above it.
    // One cold stub cannot carry both answers, and guessing cost a
    // divergence 800 million cycles into a boot. So the queue stays where
    // it is unambiguous: written by each instruction as it retires. pc and
    // pc0 have no such problem, and they are two thirds of the saving.
}

// ── flags ────────────────────────────────────────────────────────────────
void Emitter::clearVC() {
    a_.movMI(Sz::B, at(L_.srV), 0);
    a_.movMI(Sz::B, at(L_.srC), 0);
}

// 68k logical/move operations: N and Z from the result at its own width,
// V and C cleared, X untouched. x86 sets SF/ZF from the same width, so the
// two setcc do the whole job.
void Emitter::flagsLogic(Sz) {
    a_.setccM(Cc::S, at(L_.srN));
    a_.setccM(Cc::E, at(L_.srZ));
    clearVC();
}

// 68k add/subtract: N,Z,V,C map one-to-one onto SF,ZF,OF,CF for operands of
// the same width — including the carry, because x86's CF after SUB is a
// borrow, which is what the 68k calls C. X follows C.
void Emitter::flagsAddSub(bool withX) {
    a_.setccM(Cc::S, at(L_.srN));
    a_.setccM(Cc::E, at(L_.srZ));
    a_.setccM(Cc::O, at(L_.srV));
    a_.setccM(Cc::B, at(L_.srC));
    if (withX) a_.setccM(Cc::B, at(L_.srX));
}

// Leaves ZF SET when the 68k condition `cc` is TRUE, so the caller can
// branch with `je`. Condition codes are read out of Moira's per-flag bytes.
void Emitter::condToAl(int cc) {
    switch (cc) {
        case 0:                                     // T
            a_.aluRR(Asm::Op::XOR, Sz::L, RAX, RAX);
            a_.testRR(Sz::B, RAX, RAX);
            return;
        case 1:                                     // F
            a_.movRI(RAX, 1);
            a_.testRR(Sz::B, RAX, RAX);
            return;
        case 2:                                     // HI: !C && !Z
            a_.movRM(Sz::B, RAX, at(L_.srC));
            a_.aluRM(Asm::Op::OR, Sz::B, RAX, at(L_.srZ));
            a_.testRR(Sz::B, RAX, RAX);
            return;
        case 3:                                     // LS: C || Z
            a_.movRM(Sz::B, RAX, at(L_.srC));
            a_.aluRM(Asm::Op::OR, Sz::B, RAX, at(L_.srZ));
            a_.aluRI(Asm::Op::XOR, Sz::B, RAX, 1);
            a_.testRR(Sz::B, RAX, RAX);
            return;
        case 4: a_.aluMI(Asm::Op::CMP, Sz::B, at(L_.srC), 0); return;    // CC: !C
        case 5: a_.aluMI(Asm::Op::CMP, Sz::B, at(L_.srC), 1); return;    // CS:  C
        case 6: a_.aluMI(Asm::Op::CMP, Sz::B, at(L_.srZ), 0); return;    // NE
        case 7: a_.aluMI(Asm::Op::CMP, Sz::B, at(L_.srZ), 1); return;    // EQ
        case 8: a_.aluMI(Asm::Op::CMP, Sz::B, at(L_.srV), 0); return;    // VC
        case 9: a_.aluMI(Asm::Op::CMP, Sz::B, at(L_.srV), 1); return;    // VS
        case 10: a_.aluMI(Asm::Op::CMP, Sz::B, at(L_.srN), 0); return;   // PL
        case 11: a_.aluMI(Asm::Op::CMP, Sz::B, at(L_.srN), 1); return;   // MI
        case 12:                                    // GE: N == V
            a_.movRM(Sz::B, RAX, at(L_.srN));
            a_.aluRM(Asm::Op::XOR, Sz::B, RAX, at(L_.srV));
            a_.testRR(Sz::B, RAX, RAX);
            return;
        case 13:                                    // LT: N != V
            a_.movRM(Sz::B, RAX, at(L_.srN));
            a_.aluRM(Asm::Op::XOR, Sz::B, RAX, at(L_.srV));
            a_.aluRI(Asm::Op::XOR, Sz::B, RAX, 1);
            a_.testRR(Sz::B, RAX, RAX);
            return;
        case 14:                                    // GT: !Z && N == V
            a_.movRM(Sz::B, RAX, at(L_.srN));
            a_.aluRM(Asm::Op::XOR, Sz::B, RAX, at(L_.srV));
            a_.aluRM(Asm::Op::OR, Sz::B, RAX, at(L_.srZ));
            a_.testRR(Sz::B, RAX, RAX);
            return;
        default:                                    // LE: Z || N != V
            a_.movRM(Sz::B, RAX, at(L_.srN));
            a_.aluRM(Asm::Op::XOR, Sz::B, RAX, at(L_.srV));
            a_.aluRM(Asm::Op::OR, Sz::B, RAX, at(L_.srZ));
            a_.aluRI(Asm::Op::XOR, Sz::B, RAX, 1);
            a_.testRR(Sz::B, RAX, RAX);
            return;
    }
}

// ── effective addresses ──────────────────────────────────────────────────
bool Emitter::decode(size_t i, int mode, int reg, int szIdx, int extAt, Ea& ea) {
    ea.mode = mode; ea.reg = reg;
    ea.idx = eaIndex(mode, reg);
    if (ea.idx < 0) return false;
    const Instr& in = ir_.instrs[i];
    const DecodedEffectiveAddress* decoded = findEffectiveAddress(
        in, uint8_t(mode), uint8_t(reg), uint8_t(szIdx), uint8_t(extAt));
    // Full 68020 index plans are valid shared IR, not brief extensions.
    // Native support remains closed until this backend has a proved lowering.
    if (!decoded || !decoded->valid || decoded->fullFormat) return false;
    ea.memory = decoded->memory();
    ea.value = decoded->value;
    ea.ext = decoded->extensionWords;
    return true;
}

// Guest effective address into `dst`. For (An)+ and -(An) the register
// update is NOT applied here: it is a guest-visible commit and must not
// happen until the access has succeeded (see commitEa).
void Emitter::addrOf(const Ea& ea, Reg dst, int szIdx) {
    const int step = (ea.reg == 7 && szIdx == 0) ? 2 : sizeBytes(szIdx);
    switch (ea.idx) {
        case kM_AI: case kM_PI:
            a_.movRM(Sz::L, dst, A(ea.reg));
            return;
        case kM_PD:
            a_.movRM(Sz::L, dst, A(ea.reg));
            a_.aluRI(Asm::Op::SUB, Sz::L, dst, step);
            return;
        case kM_DI:
            a_.movRM(Sz::L, dst, A(ea.reg));
            if (ea.value) a_.aluRI(Asm::Op::ADD, Sz::L, dst, ea.value);
            return;
        case kM_AW: case kM_AL: case kM_DIPC:
            a_.movRI(dst, uint32_t(ea.value));
            return;
        default:
            return;
    }
}

void Emitter::commitEa(const Ea& ea, int szIdx,
                       const MemoryAccessPlan& access) {
    // x64 keeps the instruction boundary transactional: even a semantic
    // BeforeAccess update is published only after every fallible native
    // operation has succeeded. A fault replays the pristine instruction in
    // Moira. The contract still authorises whether an EA commit exists.
    if (access.eaCommit == EaCommit::None) return;
    const int step = (ea.reg == 7 && szIdx == 0) ? 2 : sizeBytes(szIdx);
    if (ea.idx == kM_PI) a_.aluMI(Asm::Op::ADD, Sz::L, A(ea.reg), step);
    else if (ea.idx == kM_PD) a_.aluMI(Asm::Op::SUB, Sz::L, A(ea.reg), step);
}

// Inline address translation. On entry `addr` holds the guest address; on
// exit RSI points at the host bytes. A tag miss goes to the fill stub (and
// retries); anything the fill refuses, and any access straddling a page,
// goes to this instruction's slow path with nothing committed.
void Emitter::memProbe(Reg addr, int bytes, bool write, Label& miss,
                       bool cacheRead, Label* cacheWriteHit,
                       bool cacheOnly, bool countCacheHit) {
    const int n = bytes;
    const int32_t table = int32_t(write ? L_.dtlbW : L_.dtlbR);
    Label& have = *a_.fresh();
    Label& fill = *a_.fresh();
    Label& done = *a_.fresh();

    const bool cacheWrite = write && cacheWriteHit != nullptr;
    if (((cacheRead && !write && cache040LineReadsEnabled()) || cacheWrite) &&
        L_.cache040Live) {
        Label& cacheMiss = *a_.fresh();

        // A line-crossing operand needs two translations/cache ways. The
        // exact thunk owns that rare case.
        a_.movRR(Sz::L, RDX, addr);
        a_.aluRI(Asm::Op::AND, Sz::L, RDX, 15);
        if (n > 1) {
            a_.aluRI(Asm::Op::CMP, Sz::L, RDX, 16 - n);
            a_.jcc(Cc::A, cacheMiss);
        }

        // Direct-mapped logical line table (32-byte entries).
        a_.movRR(Sz::L, RCX, addr);
        a_.shiftRI(Sz::L, RCX, 5, 4);
        a_.movRR(Sz::L, R8, RCX);
        if (ir_.super)
            a_.aluRI(Asm::Op::OR, Sz::L, R8, int32_t(0x80000000u));
        a_.aluRI(Asm::Op::AND, Sz::L, RCX,
                 int32_t(moira::Moira::PomJitCache040Table::kEntries - 1));
        a_.shiftRI(Sz::L, RCX, 4, 5);
        a_.leaIdx(RCX, kCpu, RCX, 1,
                  int32_t(cacheWrite ? L_.cache040W : L_.cache040R));
        a_.aluRM(Asm::Op::CMP, Sz::L, R8, mem(RCX, 0));
        a_.jcc(Cc::NE, cacheMiss);

        // Invalidate with the DATA ATC generation, then validate the live
        // fixed cache way against eviction/CINV/CPUSH.
        a_.movRM(Sz::L, R9, mem(RCX, 8));
        a_.aluRM(Asm::Op::CMP, Sz::L, R9, at(L_.cache040Gen));
        a_.jcc(Cc::NE, cacheMiss);
        a_.movRM(Sz::L, R9, mem(RCX, 4));
        a_.movRM(Sz::Q, RSI, mem(RCX, 16));
        a_.aluMI(Asm::Op::CMP, Sz::B,
                 mem(RSI, int32_t(offsetof(moira::Cache040::Line, valid))), 0);
        a_.jcc(Cc::E, cacheMiss);
        a_.aluRM(Asm::Op::CMP, Sz::L, R9,
                 mem(RSI, int32_t(offsetof(moira::Cache040::Line, tag))));
        a_.jcc(Cc::NE, cacheMiss);

        // Only sole-access instructions enter this path, so the diagnostic
        // hit count cannot become speculative and then be replayed.
        if (countCacheHit) {
            a_.aluMI(Asm::Op::ADD, Sz::Q, at(L_.cache040Hits), 1);
            if (cache040LineReadStatsEnabled())
                a_.aluMI(Asm::Op::ADD, Sz::Q,
                         at(cacheWrite ? L_.cache040NativeWriteHits
                                       : L_.cache040NativeReadHits), 1);
        }
        a_.aluRI(Asm::Op::ADD, Sz::Q, RSI,
                 int32_t(offsetof(moira::Cache040::Line, data)));
        a_.aluRR(Asm::Op::ADD, Sz::Q, RSI, RDX);
        a_.jmp(cacheWrite ? *cacheWriteHit : done);
        a_.bind(cacheMiss);
        if (cacheOnly) a_.jmp(miss);
    }

    // Entry address into RCX. x86 SIB scales stop at 8 and an entry is 16
    // bytes, so the index is shifted rather than scaled.
    auto entryPtr = [this, addr, table] {
        a_.movRR(Sz::L, RCX, addr);
        a_.shiftRI(Sz::L, RCX, 5, 12);                // page = addr >> 12
        a_.movRR(Sz::L, R8, RCX);
        // The DTLB tag carries the privilege in bit 31 (Moira.h,
        // pomJitDataTag). A block's privilege is a compile-time constant —
        // an SR write is Unsafe and ends the block — so the OR is emitted
        // only for supervisor blocks and folds to nothing for user ones.
        if (ir_.super) a_.aluRI(Asm::Op::OR, Sz::L, R8, int32_t(0x80000000u));
        a_.aluRI(Asm::Op::AND, Sz::L, RCX,
                 int32_t(moira::Moira::PomJitDtlb::kEntries - 1));
        a_.shiftRI(Sz::L, RCX, 4, 4);                 // * sizeof(entry)
        a_.leaIdx(RCX, kCpu, RCX, 1, table);
    };

    entryPtr();
    a_.aluRM(Asm::Op::CMP, Sz::L, R8, mem(RCX, 0));
    a_.jcc(Cc::NE, fill);              // the fill path is cold: out of rel8 range

    a_.bind(have);
    a_.movRM(Sz::Q, RSI, mem(RCX, 8));
    // A tagged entry with a NULL host is a remembered refusal: an I/O
    // register, the ROM window seen by a store, a page holding translated
    // code. Asking again would cost a call per access, and a hardware poll
    // loop asks on every iteration.
    a_.testRR(Sz::Q, RSI, RSI);
    a_.jcc(Cc::E, miss);               // ditto — the thunk is cold
    a_.movRR(Sz::L, RDX, addr);
    a_.aluRI(Asm::Op::AND, Sz::L, RDX, 4095);
    if (n > 1) {
        // An access straddling two pages needs two translations. Rare
        // enough to be someone else's problem.
        a_.aluRI(Asm::Op::CMP, Sz::L, RDX, 4096 - n);
        a_.jcc(Cc::A, miss);
    }
    // ── the per-slice code mask, writes only (Moira.h § PomJitDtlbEntry) ──
    // An entry maps 4 KB; CodeGuard works at 256 bytes. A store into a slice
    // that some cached block was translated from must go through the memory
    // map so the guard sees it — but a store 3 KB away from that block must
    // not pay for sharing a page with it, which is what refusing the whole
    // entry used to cost (95.6 % of all remembered refusals, -9.8 % of wall
    // clock; POM68K_JIT.md § 8).
    //
    // Hot path: two instructions and a branch that is not taken on any page
    // with no code in it, which is nearly all of them. The precise slice
    // test is cold, because a page that holds code at all is rare and a
    // store landing in the code's own slice is rarer still.
    Label& maskOk = *a_.fresh();
    if (write) {
        Label& maskCold = *a_.fresh();
        a_.aluMI(Asm::Op::CMP, Sz::L, mem(RCX, 4), 0);
        a_.jcc(Cc::NE, maskCold);
        // Cold: which slices does THIS access touch? `n` is at most 64
        // (a full MOVEM burst) and a slice is 256 bytes, so an access spans
        // at most two of them — first and last, tested separately rather
        // than as a range, which needs no mask construction.
        cold_.push_back([this, &maskCold, &maskOk, &miss, n] {
            a_.bind(maskCold);
            a_.movRM(Sz::L, R8, mem(RCX, 4));          // the mask
            for (int end = 0; end < (n > 1 ? 2 : 1); end++) {
                a_.movRR(Sz::L, RCX, RDX);             // entry ptr is done with
                if (end) a_.aluRI(Asm::Op::ADD, Sz::L, RCX, n - 1);
                a_.shiftRI(Sz::L, RCX, 5, moira::Moira::PomJitDtlb::kSliceShift);
                a_.movRI(R9, 1);
                a_.shiftRCl(Sz::L, R9, 4);             // 1 << slice
                a_.testRR(Sz::L, R8, R9);
                a_.jcc(Cc::NE, miss);
            }
            a_.jmp(maskOk);
        });
    }
    a_.bind(maskOk);
    // `and edx, 4095` already zeroed the upper half, so this is a clean
    // 64-bit index with no extension step.
    a_.aluRR(Asm::Op::ADD, Sz::Q, RSI, RDX);

    // Tag miss — cold. Ask the engine once; the answer is then taken on
    // trust rather than re-probed, because fillDtlb wrote exactly the entry
    // this address indexes. Re-probing would risk a loop that never
    // terminates if the two ever disagreed.
    cold_.push_back([this, &fill, &have, &miss, addr, write, entryPtr] {
        a_.bind(fill);
        spillClock();
        a_.movMR(Sz::L, F(kFScratch), addr);
        a_.movRM(Sz::Q, RDI, F(kFDtlbSelf));
        a_.movRR(Sz::L, RSI, addr);
        a_.movRI(RDX, uint32_t(write ? 1 : 0));
        a_.callM(F(kFDtlbFill));
        // Test the ANSWER before restoring the address: `addr` is RAX at
        // every call site, so restoring first would test the address
        // instead of the host pointer. `mov` leaves the flags alone.
        a_.testRR(Sz::Q, RAX, RAX);
        a_.movRM(Sz::L, addr, F(kFScratch));
        fillClock();
        a_.jcc(Cc::E, miss);
        entryPtr();
        a_.jmp(have);
    });
    a_.bind(done);
}

// Big-endian guest, little-endian host: every multi-byte access byte-swaps.
//
// The miss path is the interesting half. An address the inline TLB cannot
// serve is usually an I/O register — a hardware poll loop lives entirely on
// this path — and running the WHOLE instruction through Moira to reach it
// throws away everything the block already did. So the access alone goes
// through a thunk, and only a FAULT falls all the way back.
void Emitter::memLoad(Reg addr, int szIdx, Reg dst,
                      const MemoryAccessPlan& access) {
    if (!access.valid() || access.direction != MemoryDirection::Read ||
        access.bytes != unsigned(sizeBytes(szIdx))) {
        a_.jmp(runtimeStub(cur_));
        return;
    }
    const bool cacheRead = access.cache;
    const bool exactAccess = access.exactThunk;
    Label& miss = exactAccess ? *a_.fresh() : runtimeStub(cur_);

    memProbe(addr, sizeBytes(szIdx), false, miss, cacheRead);
    if (szIdx == 0) {
        a_.movzx(Sz::B, dst, mem(RSI, 0));
    } else if (szIdx == 1) {
        a_.movzx(Sz::W, dst, mem(RSI, 0));
        a_.rolR16(dst, 8);
    } else {
        a_.movRM(Sz::L, dst, mem(RSI, 0));
        a_.bswap(dst);
    }
    if (!exactAccess) return;

    Label& done = *a_.fresh();
    a_.bind(done);
    const size_t idx = cur_;
    cold_.push_back([this, &miss, &done, addr, szIdx, dst, idx] {
        a_.bind(miss);
        spillClock();                    // a device access can stall the CPU
        a_.movRR(Sz::Q, RDI, kCpu);
        a_.movRR(Sz::L, RSI, addr);
        a_.movRI(RDX, uint32_t(sizeBytes(szIdx)));
        a_.leaRM(RCX, F(kFValue));
        call(reinterpret_cast<void*>(&pom68kJitRead));
        fillClock();
        a_.testRR(Sz::L, RAX, RAX);
        a_.jcc(Cc::E, runtimeStub(idx));       // it faulted: nothing was committed
        a_.movRM(Sz::L, dst, F(kFValue)); // already host-ordered
        a_.jmp(done);
    });
}

void Emitter::memStore(Reg addr, int szIdx, Reg src,
                       const MemoryAccessPlan& access) {
    if (!access.valid() || access.direction != MemoryDirection::Write ||
        access.bytes != unsigned(sizeBytes(szIdx))) {
        a_.jmp(runtimeStub(cur_));
        return;
    }
    const bool cacheWrite = access.cache;
    const bool exactAccess = access.exactThunk;
    Label& miss = exactAccess ? *a_.fresh() : runtimeStub(cur_);
    Label* cacheHit = cacheWrite ? a_.fresh() : nullptr;
    Label* cacheDone = cacheWrite ? a_.fresh() : nullptr;

    // The value is live across memProbe, whose miss path calls out and
    // therefore clobbers every caller-saved register, `src` included.
    a_.movMR(Sz::L, F(kFSaveV), src);
    memProbe(addr, sizeBytes(szIdx), true, miss, false, cacheHit);
    const auto emitStore = [&] {
        a_.movRM(Sz::L, src, F(kFSaveV));
        if (szIdx == 0) {
            a_.movMR(Sz::B, mem(RSI, 0), src);
        } else if (szIdx == 1) {
            a_.rolR16(src, 8);
            a_.movMR(Sz::W, mem(RSI, 0), src);
        } else {
            a_.bswap(src);
            a_.movMR(Sz::L, mem(RSI, 0), src);
        }
    };
    emitStore();
    if (cacheWrite) {
        a_.jmp(*cacheDone);
        a_.bind(*cacheHit);
        emitStore();
        markCache040Dirty(addr, sizeBytes(szIdx));
        a_.bind(*cacheDone);
    }
    if (!exactAccess) return;

    Label& done = *a_.fresh();
    a_.bind(done);
    const size_t idx = cur_;
    cold_.push_back([this, &miss, &done, addr, szIdx, idx] {
        a_.bind(miss);
        spillClock();                    // a device access can stall the CPU
        a_.movRR(Sz::Q, RDI, kCpu);
        a_.movRR(Sz::L, RSI, addr);
        a_.movRI(RDX, uint32_t(sizeBytes(szIdx)));
        a_.movRM(Sz::L, RCX, F(kFSaveV));
        call(reinterpret_cast<void*>(&pom68kJitWrite));
        fillClock();
        a_.testRR(Sz::L, RAX, RAX);
        a_.jcc(Cc::E, runtimeStub(idx));
        // A store the memory map performed itself may have landed in a page
        // holding translated code; the guard is the only thing that can see
        // that, and the block must not run any further if it did.
        //
        // This is the ONE exit that does not pass through an instruction's
        // boundary stub, so it commits the boundary itself. It used to get
        // away without: before pc and pc0 were deferred to the cold stubs
        // they were already correct here, because the PREVIOUS instruction
        // had written them. Deferring them turned that into an exit at a pc
        // several instructions stale, and the engine re-entered there.
        Label& ok = *a_.fresh();
        a_.movRM(Sz::Q, RAX, F(kFGuard));
        a_.aluMI(Asm::Op::CMP, Sz::B, mem(RAX, 0), 0);
        a_.jccShort(Cc::E, ok);
        // …and because it re-enters at THIS instruction's pc, the instruction
        // is about to be run a second time — so the 68030 i-cache charge it
        // already took has to come back off, exactly as on the runtime-bail
        // door. This exit is the one the door does not cover, and it is what
        // the 030 lockstep's residual +2 fetches were (2026-08-10): visible
        // only with POM68K_JIT_ACCESS_THUNK=2, since only a STORE reaches it.
        unchargeIcache(idx);
        emitBoundary(ir_.instrs[idx].pc, -1);
        a_.jmp(*exitLost_);
        a_.bind(ok);
        a_.jmp(done);
    });
}

// A true RMW is proved once with the writable translation before its read
// becomes observable. The host pointer is then kept across flag generation,
// so the write cannot discover a late refusal after CCR state changed.
void Emitter::memRmwLoad(Reg addr, int szIdx, Reg dst,
                         const MemoryAccessPlan& read,
                         const MemoryAccessPlan& write) {
    if (!memoryRmwAccessPair(read, write) ||
        read.bytes != unsigned(sizeBytes(szIdx))) {
        a_.jmp(runtimeStub(cur_));
        return;
    }
    memProbe(addr, sizeBytes(szIdx), true, runtimeStub(cur_));
    a_.movMR(Sz::Q, F(kFValue), RSI);
    if (szIdx == 0) {
        a_.movzx(Sz::B, dst, mem(RSI, 0));
    } else if (szIdx == 1) {
        a_.movzx(Sz::W, dst, mem(RSI, 0));
        a_.rolR16(dst, 8);
    } else {
        a_.movRM(Sz::L, dst, mem(RSI, 0));
        a_.bswap(dst);
    }
}

void Emitter::memRmwStore(int szIdx, Reg src,
                          const MemoryAccessPlan& read,
                          const MemoryAccessPlan& write) {
    if (!memoryRmwAccessPair(read, write) ||
        write.bytes != unsigned(sizeBytes(szIdx))) {
        a_.jmp(runtimeStub(cur_));
        return;
    }
    a_.movRM(Sz::Q, RSI, F(kFValue));
    if (szIdx == 0) {
        a_.movMR(Sz::B, mem(RSI, 0), src);
    } else if (szIdx == 1) {
        a_.rolR16(src, 8);
        a_.movMR(Sz::W, mem(RSI, 0), src);
    } else {
        a_.bswap(src);
        a_.movMR(Sz::L, mem(RSI, 0), src);
    }
}

void Emitter::markCache040Dirty(Reg addr, int bytes) {
    // RSI currently addresses Line::data[addr&15]. Recover the stable line
    // object, then OR the first/last covered longword bits after the bytes
    // themselves have been stored. A non-crossing operand simply ORs the
    // same bit twice.
    a_.movRR(Sz::L, RDX, addr);
    a_.aluRI(Asm::Op::AND, Sz::L, RDX, 15);
    a_.aluRR(Asm::Op::SUB, Sz::Q, RSI, RDX);
    a_.aluRI(Asm::Op::SUB, Sz::Q, RSI,
             int32_t(offsetof(moira::Cache040::Line, data)));

    a_.movRR(Sz::L, RCX, RDX);
    a_.shiftRI(Sz::L, RCX, 5, 2);
    a_.movRI(R8, 1);
    a_.shiftRCl(Sz::L, R8, 4);
    if (bytes > 1) {
        a_.movRR(Sz::L, RCX, addr);
        a_.aluRI(Asm::Op::ADD, Sz::L, RCX, bytes - 1);
        a_.shiftRI(Sz::L, RCX, 5, 2);
        a_.aluRI(Asm::Op::AND, Sz::L, RCX, 3);
        a_.movRI(R9, 1);
        a_.shiftRCl(Sz::L, R9, 4);
        a_.aluRR(Asm::Op::OR, Sz::L, R8, R9);
    }
    a_.aluMR(Asm::Op::OR, Sz::B,
             mem(RSI, int32_t(offsetof(moira::Cache040::Line, dirty))), R8);
}

void Emitter::load(const Ea& ea, int szIdx, Reg dst,
                   const MemoryAccessPlan& access, bool signExtend) {
    switch (ea.idx) {
        case kM_DN:
            if (signExtend && szIdx != 2) {
                a_.movsx(hostSz(szIdx), dst, D(ea.reg));
            } else {
                a_.movRM(Sz::L, dst, D(ea.reg));
            }
            return;
        case kM_AN:
            if (signExtend && szIdx == 1) a_.movsx(Sz::W, dst, A(ea.reg));
            else a_.movRM(Sz::L, dst, A(ea.reg));
            return;
        case kM_IM:
            a_.movRI(dst, uint32_t(ea.value));
            return;
        default:
            addrOf(ea, RAX, szIdx);
            memLoad(RAX, szIdx, dst, access);
            if (signExtend && szIdx != 2) a_.movsxRR(hostSz(szIdx), dst, dst);
            return;
    }
}

void Emitter::store(const Ea& ea, int szIdx, Reg src,
                    const MemoryAccessPlan& access) {
    switch (ea.idx) {
        case kM_DN: a_.movMR(hostSz(szIdx), D(ea.reg), src); return;
        case kM_AN: a_.movMR(Sz::L, A(ea.reg), src); return;
        default:
            addrOf(ea, RAX, szIdx);
            memStore(RAX, szIdx, src, access);
            return;
    }
}

// ── instruction families ─────────────────────────────────────────────────

// MOVE / MOVEA. Source is read, destination written; MOVEA sign-extends a
// word source to the full address register and touches no flag.
bool Emitter::emitMove(size_t i, int szIdx) {
    const Instr& in = ir_.instrs[i];
    const InstructionSemantics& sem = in.semantics;
    if (sem.operation != SemanticOp::Move || sem.sizeIndex != szIdx)
        return false;
    auto memory = instructionMemoryPlan(in.memory, proofOptions(L_));
    const int srcMode = sem.eaMode, srcReg = sem.eaReg;
    const int dstMode = sem.destinationMode, dstReg = sem.destinationReg;

    Ea src, dst;
    if (!decode(i, srcMode, srcReg, szIdx, 0, src)) return false;
    if (!decode(i, dstMode, dstReg, szIdx, src.ext, dst)) return false;
    if (src.idx == kM_AN && szIdx == 0) return false;     // MOVE.B An is illegal
    // A destination has to be writable: not an immediate, not PC-relative.
    if (dst.idx == kM_IM || dst.idx == kM_DIPC) return false;
    if (aliases(src, dst)) return false;
    if (!lengthOk(i, src.ext + dst.ext)) return false;

    const int rc = kEaRead[src.idx][szIdx];
    const int dc = kMoveDst[dst.idx];
    if (rc < 0 || dc < 0) return false;
    const int cycles = rc + dc;
    if (cycles != ir_.instrs[i].cycles) return false;

    MemoryAccessPlan srcAccess, dstAccess;
    if (src.memory) {
        srcAccess = memory.access(MemoryDirection::Read,
                                  MemoryOperand::Source,
                                  uint8_t(sizeBytes(szIdx)),
                                  uint8_t(srcMode), uint8_t(srcReg));
        if (!srcAccess.valid()) return false;
    }
    if (dst.memory) {
        dstAccess = memory.access(MemoryDirection::Write,
                                  MemoryOperand::Destination,
                                  uint8_t(sizeBytes(szIdx)),
                                  uint8_t(dstMode), uint8_t(dstReg));
        if (!dstAccess.valid()) return false;
    }
    if (!memory.complete()) return false;

    const bool movea = (dst.idx == kM_AN);
    const bool cachePair = memory.proof.atomicCachePair();
    if (cachePair) {
        if (!srcAccess.cache || !dstAccess.cache) return false;
        // Probe both cache lines before the first load. R10/R11 are untouched
        // by the cache-only probes and hold the two resident byte pointers;
        // any miss reaches the whole-instruction fallback with pristine EA,
        // flags and diagnostic counters.
        addrOf(src, RAX, szIdx);
        memProbe(RAX, 4, false, runtimeStub(i), true, nullptr, true, false);
        a_.movRR(Sz::Q, R10, RSI);
        addrOf(dst, RAX, szIdx);
        Label& writeHit = *a_.fresh();
        memProbe(RAX, 4, true, runtimeStub(i), false, &writeHit, true, false);
        a_.jmp(runtimeStub(i));
        a_.bind(writeHit);
        a_.movRR(Sz::Q, R11, RSI);

        a_.aluMI(Asm::Op::ADD, Sz::Q, at(L_.cache040Hits), 2);
        if (cache040LineReadStatsEnabled()) {
            a_.aluMI(Asm::Op::ADD, Sz::Q, at(L_.cache040NativeReadHits), 1);
            a_.aluMI(Asm::Op::ADD, Sz::Q, at(L_.cache040NativeWriteHits), 1);
        }
        a_.movRM(Sz::L, RDI, mem(R10, 0)); a_.bswap(RDI);
        a_.testRR(Sz::L, RDI, RDI); flagsLogic(Sz::L);
        a_.bswap(RDI); a_.movMR(Sz::L, mem(R11, 0), RDI);
        a_.movRR(Sz::Q, RSI, R11); markCache040Dirty(RAX, 4);
        commitEa(src, szIdx, srcAccess); commitEa(dst, szIdx, dstAccess);
        return true;
    }
    if (src.memory && dst.memory) {
        if (memory.proof.protocol != MemoryProofProtocol::PreflightAll ||
            !srcAccess.preflight || !dstAccess.preflight ||
            in.memory.order != MemoryOrder::SourceThenDestination)
            return false;

        // Prove both mappings before the source read. This is the concrete
        // x64 expansion of PreflightAll: a destination refusal cannot occur
        // after flags or a source device side effect have become visible.
        addrOf(src, RAX, szIdx);
        memProbe(RAX, sizeBytes(szIdx), false, runtimeStub(i));
        a_.movMR(Sz::Q, F(kFValue), RSI);
        addrOf(dst, RAX, szIdx);
        memProbe(RAX, sizeBytes(szIdx), true, runtimeStub(i));
        a_.movRR(Sz::Q, R11, RSI);                  // destination bytes
        a_.movRM(Sz::Q, R10, F(kFValue));           // source bytes

        if (szIdx == 0) {
            a_.movzx(Sz::B, RDI, mem(R10, 0));
        } else if (szIdx == 1) {
            a_.movzx(Sz::W, RDI, mem(R10, 0));
            a_.rolR16(RDI, 8);
        } else {
            a_.movRM(Sz::L, RDI, mem(R10, 0));
            a_.bswap(RDI);
        }
        a_.testRR(hostSz(szIdx), RDI, RDI);
        flagsLogic(hostSz(szIdx));
        if (szIdx == 0) {
            a_.movMR(Sz::B, mem(R11, 0), RDI);
        } else if (szIdx == 1) {
            a_.rolR16(RDI, 8);
            a_.movMR(Sz::W, mem(R11, 0), RDI);
        } else {
            a_.bswap(RDI);
            a_.movMR(Sz::L, mem(R11, 0), RDI);
        }
        commitEa(src, szIdx, srcAccess);
        commitEa(dst, szIdx, dstAccess);
        return true;
    }
    load(src, szIdx, RDI, srcAccess, movea);
    if (movea) {
        a_.movMR(Sz::L, A(dst.reg), RDI);
    } else {
        // The flags come from the value itself; test it at its own width.
        a_.testRR(hostSz(szIdx), RDI, RDI);
        flagsLogic(hostSz(szIdx));
        store(dst, szIdx, RDI, dstAccess);
    }
    commitEa(src, szIdx, srcAccess);
    commitEa(dst, szIdx, dstAccess);
    return true;
}

// ADD/SUB/AND/OR/CMP <ea>,Dn — the "ea to register" direction, plus the
// ADDA/SUBA/CMPA forms that land in an address register.
bool Emitter::emitAluEaRg(size_t i) {
    const Instr& in = ir_.instrs[i];
    const InstructionSemantics& sem = in.semantics;
    if (sem.operation != SemanticOp::AluEaToReg &&
        sem.operation != SemanticOp::AddressAlu)
        return false;
    auto memory = instructionMemoryPlan(in.memory, proofOptions(L_));
    const int dn = sem.registerIndex;
    const int mode = sem.eaMode, reg = sem.eaReg;

    Asm::Op x86op;
    if (!hostAluOperation(sem.alu, x86op)) return false;
    const bool isCmp = sem.alu == AluOperation::Cmp;
    const bool isAddr = sem.operation == SemanticOp::AddressAlu;
    const int szIdx = sem.sizeIndex;

    Ea src;
    if (!decode(i, mode, reg, szIdx, 0, src)) return false;
    if (src.idx == kM_AN && szIdx == 0) return false;
    const int rc = kEaRead[src.idx][szIdx];
    if (rc < 0) return false;
    if (!lengthOk(i, src.ext)) return false;
    // ADDA/SUBA charge exactly the register forms on the 68020 (execAdda's
    // 020 column IS kEaRead). CMPA does NOT: execCmpa's own column is
    // kEaRead + 2 in every mode, byte, word and long alike — it holds a
    // SYNC(2) that the ADDA path only takes for a word or a register source
    // (MoiraExec_cpp.h:2129 vs :421-423). Charging both alike refused every
    // single CMPA; on the idle Finder that was 1.04 M fallbacks, 12 % of all
    // of them, second only to the MOVE cost cell fixed above (2026-08-09).
    const int cost = (isAddr && isCmp) ? rc + 2 : rc;
    if (cost != ir_.instrs[i].cycles) return false;
    if (isAddr) {
        Ea an; an.idx = kM_AN; an.reg = dn;
        if (aliases(src, an)) return false;
    }
    MemoryAccessPlan read;
    if (src.memory) {
        read = memory.access(MemoryDirection::Read, MemoryOperand::Source,
                             uint8_t(sizeBytes(szIdx)), uint8_t(mode),
                             uint8_t(reg));
        if (!read.valid()) return false;
    }
    if (!memory.complete()) return false;

    if (isAddr) {
        // An operations are always full-width, with a word source sign
        // extended first, and they never touch the CCR — except CMPA, which
        // compares at 32 bits.
        load(src, szIdx, RDI, read, true);
        if (isCmp) {
            a_.movRM(Sz::L, RDX, A(dn));
            a_.aluRR(Asm::Op::CMP, Sz::L, RDX, RDI);
            flagsAddSub(false);
        } else {
            a_.aluMR(x86op, Sz::L, A(dn), RDI);
        }
        commitEa(src, szIdx, read);
        return true;
    }

    load(src, szIdx, RDI, read, false);
    if (isCmp) {
        a_.movRM(Sz::L, RDX, D(dn));
        a_.aluRR(Asm::Op::CMP, hostSz(szIdx), RDX, RDI);
        flagsAddSub(false);                     // CMP leaves X alone
    } else {
        a_.aluMR(x86op, hostSz(szIdx), D(dn), RDI);
        if (x86op == Asm::Op::ADD || x86op == Asm::Op::SUB) flagsAddSub(true);
        else flagsLogic(hostSz(szIdx));
    }
    commitEa(src, szIdx, read);
    return true;
}

// ADD/SUB/AND/OR/EOR Dn,<ea> — the read-modify-write direction.
bool Emitter::emitAluRgEa(size_t i) {
    const Instr& in = ir_.instrs[i];
    const InstructionSemantics& sem = in.semantics;
    if (sem.operation != SemanticOp::AluRegToEa) return false;
    auto memory = instructionMemoryPlan(in.memory, proofOptions(L_));
    const int dn = sem.registerIndex;
    const int szIdx = sem.sizeIndex;
    const int mode = sem.eaMode, reg = sem.eaReg;
    if (szIdx > 2) return false;

    Asm::Op x86op;
    if (!hostAluOperation(sem.alu, x86op) ||
        sem.alu == AluOperation::Cmp) return false;

    Ea dst;
    if (!decode(i, mode, reg, szIdx, 0, dst)) return false;
    if (dst.idx == kM_AN || dst.idx == kM_IM || dst.idx == kM_DIPC) return false;
    if (!lengthOk(i, dst.ext)) return false;
    const int cycles = eaRmwCost(dst.idx, szIdx);
    if (cycles < 0 || cycles != ir_.instrs[i].cycles) return false;

    MemoryAccessPlan read, write;
    if (dst.memory) {
        read = memory.access(MemoryDirection::Read,
                             MemoryOperand::Destination,
                             uint8_t(sizeBytes(szIdx)), uint8_t(mode),
                             uint8_t(reg));
        write = memory.access(MemoryDirection::Write,
                              MemoryOperand::Destination,
                              uint8_t(sizeBytes(szIdx)), uint8_t(mode),
                              uint8_t(reg));
        if (!memoryRmwAccessPair(read, write))
            return false;
    }
    if (!memory.complete()) return false;

    if (dst.idx == kM_DN) {
        a_.movRM(Sz::L, RDI, D(dn));
        a_.aluMR(x86op, hostSz(szIdx), D(dst.reg), RDI);
    } else {
        addrOf(dst, RAX, szIdx);
        memRmwLoad(RAX, szIdx, RDI, read, write);
        a_.movRM(Sz::L, RDX, D(dn));
        a_.aluRR(x86op, hostSz(szIdx), RDI, RDX);
        // Flags first: the store byte-swaps its operand in place.
        if (x86op == Asm::Op::ADD || x86op == Asm::Op::SUB) flagsAddSub(true);
        else { a_.testRR(hostSz(szIdx), RDI, RDI); flagsLogic(hostSz(szIdx)); }
        memRmwStore(szIdx, RDI, read, write);
        commitEa(dst, szIdx, write);
        return true;
    }
    if (x86op == Asm::Op::ADD || x86op == Asm::Op::SUB) flagsAddSub(true);
    else flagsLogic(hostSz(szIdx));
    return true;
}

// ADDQ / SUBQ #1..8,<ea>
bool Emitter::emitAddSubQ(size_t i) {
    const Instr& in = ir_.instrs[i];
    const uint16_t op = in.opcode;
    const InstructionSemantics& sem = in.semantics;
    if (sem.operation != SemanticOp::AddSubQuick) return false;
    auto memory = instructionMemoryPlan(in.memory, proofOptions(L_));
    const int szIdx = sem.sizeIndex;
    if (szIdx > 2) return false;
    int imm = sem.registerIndex;
    if (imm == 0) imm = 8;
    const bool sub = sem.alu == AluOperation::Sub;
    if (!sub && sem.alu != AluOperation::Add) return false;
    const int mode = sem.eaMode, reg = sem.eaReg;

    Ea dst;
    if (!decode(i, mode, reg, szIdx, 0, dst)) return false;
    if (dst.idx == kM_IM || dst.idx == kM_DIPC) return false;
    if (!lengthOk(i, dst.ext)) return false;
    const int cycles = eaRmwCost(dst.idx, szIdx);
    if (cycles < 0 || cycles != ir_.instrs[i].cycles) return false;

    MemoryAccessPlan read, write;
    if (dst.memory) {
        read = memory.access(MemoryDirection::Read,
                             MemoryOperand::Destination,
                             uint8_t(sizeBytes(szIdx)), uint8_t(mode),
                             uint8_t(reg));
        write = memory.access(MemoryDirection::Write,
                              MemoryOperand::Destination,
                              uint8_t(sizeBytes(szIdx)), uint8_t(mode),
                              uint8_t(reg));
        if (!memoryRmwAccessPair(read, write))
            return false;
    }
    if (!memory.complete()) return false;

    const Asm::Op x86op = sub ? Asm::Op::SUB : Asm::Op::ADD;

    if (dst.idx == kM_AN) {
        // Address-register form: full width whatever the size, no flags.
        a_.aluMI(x86op, Sz::L, A(dst.reg), imm);
        return true;
    }
    if (dst.idx == kM_DN) {
        a_.aluMI(x86op, hostSz(szIdx), D(dst.reg), imm);
        flagsAddSub(true);
        return true;
    }
    addrOf(dst, RAX, szIdx);
    memRmwLoad(RAX, szIdx, RDI, read, write);
    a_.aluRI(x86op, hostSz(szIdx), RDI, imm);
    flagsAddSub(true);
    memRmwStore(szIdx, RDI, read, write);
    commitEa(dst, szIdx, write);
    return true;
}

bool Emitter::emitMoveq(size_t i) {
    const Instr& in = ir_.instrs[i];
    const uint16_t op = in.opcode;
    const InstructionSemantics& sem = in.semantics;
    if (sem.operation != SemanticOp::MoveQuick || in.cycles != 2)
        return false;
    const int dn = sem.registerIndex;
    const int32_t v = int8_t(op & 0xFF);
    a_.movMI(Sz::L, D(dn), v);
    a_.movMI(Sz::B, at(L_.srN), v < 0 ? 1 : 0);
    a_.movMI(Sz::B, at(L_.srZ), v == 0 ? 1 : 0);
    clearVC();
    return true;
}

// ORI/ANDI/SUBI/ADDI/EORI/CMPI #imm,<ea>
bool Emitter::emitImmediate(size_t i) {
    const Instr& in = ir_.instrs[i];
    const InstructionSemantics& sem = in.semantics;
    if (sem.operation != SemanticOp::ImmediateAlu) return false;
    auto memory = instructionMemoryPlan(in.memory, proofOptions(L_));
    const int szIdx = sem.sizeIndex;
    if (szIdx > 2) return false;
    const int mode = sem.eaMode, reg = sem.eaReg;

    Asm::Op x86op;
    if (!hostAluOperation(sem.alu, x86op)) return false;
    const bool isCmp = sem.alu == AluOperation::Cmp;

    Ea imm, dst;
    if (!decode(i, 7, 4, szIdx, 0, imm)) return false;
    if (!decode(i, mode, reg, szIdx, imm.ext, dst)) return false;
    if (dst.idx == kM_AN || dst.idx == kM_IM || dst.idx == kM_DIPC) return false;
    if (!lengthOk(i, imm.ext + dst.ext)) return false;

    const int cycles = isCmp ? kEaRead[dst.idx][szIdx] : eaRmwCost(dst.idx, szIdx);
    if (cycles < 0 || cycles != ir_.instrs[i].cycles) return false;

    MemoryAccessPlan read, write;
    if (dst.memory) {
        read = memory.access(MemoryDirection::Read,
                             MemoryOperand::Destination,
                             uint8_t(sizeBytes(szIdx)), uint8_t(mode),
                             uint8_t(reg));
        if (!read.valid()) return false;
        if (!isCmp) {
            write = memory.access(MemoryDirection::Write,
                                  MemoryOperand::Destination,
                                  uint8_t(sizeBytes(szIdx)), uint8_t(mode),
                                  uint8_t(reg));
            if (!memoryRmwAccessPair(read, write))
                return false;
        }
    }
    if (!memory.complete()) return false;

    if (dst.idx == kM_DN) {
        a_.aluMI(x86op, hostSz(szIdx), D(dst.reg), imm.value);
        if (isCmp || x86op == Asm::Op::ADD || x86op == Asm::Op::SUB)
            flagsAddSub(!isCmp);
        else
            flagsLogic(hostSz(szIdx));
        return true;
    }

    addrOf(dst, RAX, szIdx);
    if (isCmp) {
        memLoad(RAX, szIdx, RDI, read);
        a_.aluRI(Asm::Op::CMP, hostSz(szIdx), RDI, imm.value);
        flagsAddSub(false);
        commitEa(dst, szIdx, read);
        return true;
    }
    memRmwLoad(RAX, szIdx, RDI, read, write);
    a_.aluRI(x86op, hostSz(szIdx), RDI, imm.value);
    if (x86op == Asm::Op::ADD || x86op == Asm::Op::SUB) flagsAddSub(true);
    else flagsLogic(hostSz(szIdx));
    memRmwStore(szIdx, RDI, read, write);
    commitEa(dst, szIdx, write);
    return true;
}

// BTST only, static (#imm) and dynamic (Dn) forms. BSET/BCLR/BCHG write
// back and are left out of this pass; BTST is what a Mac ROM's hardware
// poll loops are made of, and it is read-only, which keeps the bail-out
// argument trivial.
bool Emitter::emitBitOp(size_t i) {
    const Instr& in = ir_.instrs[i];
    const InstructionSemantics& sem = in.semantics;
    if (sem.operation != SemanticOp::Bit || sem.action != 0) return false;
    auto memory = instructionMemoryPlan(in.memory, proofOptions(L_));
    const int mode = sem.eaMode, reg = sem.eaReg;

    int bitReg = -1;
    int32_t bitImm = 0;
    int extUsed = 0;
    const bool dynamic = sem.dynamic;
    if (dynamic) {                                    // BTST Dn,<ea>
        // …which is also MOVEP's encoding when the mode field is 001. The
        // shared decoder has already excluded that overlap.
        if (mode == 1) return false;
        bitReg = sem.registerIndex;
    } else {                                         // BTST #imm,<ea>
        bitImm = in.extensionWord(0) & 0xFF;
        extUsed = 1;
    }

    // A data register is tested modulo 32, memory modulo 8 (a byte).
    const bool toReg = (mode == 0);
    const int szIdx = toReg ? 2 : 0;

    Ea dst;
    if (!decode(i, mode, reg, szIdx, extUsed, dst)) return false;
    if (dst.idx == kM_AN || dst.idx == kM_IM) return false;
    if (!lengthOk(i, extUsed + dst.ext)) return false;

    const int cycles = toReg ? 4 : eaRmwCost(dst.idx, szIdx);
    if (cycles < 0 || cycles != ir_.instrs[i].cycles) return false;

    MemoryAccessPlan read;
    if (dst.memory) {
        read = memory.access(MemoryDirection::Read, MemoryOperand::Operand,
                             1, uint8_t(mode), uint8_t(reg));
        if (!read.valid()) return false;
    }
    if (!memory.complete()) return false;

    // Z = !bit. Everything else is untouched.
    //
    // ORDER MATTERS: the operand is fetched FIRST. A memory access here is
    // not three instructions — it is a TLB probe, possibly a call to fill
    // it, possibly a call to perform the access — and it treats every
    // caller-saved register as scratch. A bit mask computed before the
    // fetch would not survive it. Only RDI crosses an access, and only
    // because memLoad is what leaves it there.
    if (toReg) {
        a_.movRM(Sz::L, RDI, D(dst.reg));
    } else {
        addrOf(dst, RAX, szIdx);
        memLoad(RAX, szIdx, RDI, read);
    }
    if (dynamic) {
        a_.movRM(Sz::L, RCX, D(bitReg));
        a_.aluRI(Asm::Op::AND, Sz::L, RCX, toReg ? 31 : 7);
        a_.movRI(RDX, 1);
        a_.shiftRCl(Sz::L, RDX, 4);                  // 1 << (bit & mask)
    } else {
        a_.movRI(RDX, uint32_t(1u << (uint32_t(bitImm) & (toReg ? 31u : 7u))));
    }
    a_.testRR(Sz::L, RDI, RDX);
    a_.setccM(Cc::E, at(L_.srZ));
    commitEa(dst, szIdx, read);
    return true;
}

// The $4xxx group the backend can own: TST, CLR, NOT, NEG, EXT, SWAP, LEA.
bool Emitter::emitLine4(size_t i) {
    const Instr& in = ir_.instrs[i];
    const InstructionSemantics& sem = in.semantics;
    const int mode = sem.eaMode, reg = sem.eaReg;
    const int szIdx = sem.sizeIndex;

    if (sem.operation == SemanticOp::ReturnSubroutine ||
        sem.operation == SemanticOp::JumpSubroutine)
        return emitSubroutine(i);

    if (sem.operation == SemanticOp::Nop) {
        return ir_.instrs[i].cycles == 2;
    }

    if (sem.operation == SemanticOp::Link) {         // LINK.W An,#d16
        if (ir_.instrs[i].cycles != 5 || !lengthOk(i, 1)) return false;
        auto memory = instructionMemoryPlan(in.memory, proofOptions(L_));
        const MemoryAccessPlan write = memory.access(
            MemoryDirection::Write, MemoryOperand::Stack, 4, 4, 7);
        if (!write.valid() || !memory.complete()) return false;
        const int an = sem.eaReg;
        const int32_t disp = int16_t(in.extensionWord(0));

        // sp = A7 - 4 is both the address written and the value An takes.
        // For LINK A7 the value PUSHED is that same decremented pointer,
        // not the old one — Moira's `readA(ax) - (ax == 7 ? 4 : 0)`.
        a_.movRM(Sz::L, RAX, A(7));
        a_.aluRI(Asm::Op::SUB, Sz::L, RAX, 4);
        if (an == 7) a_.movRR(Sz::L, RDI, RAX);
        else         a_.movRM(Sz::L, RDI, A(an));
        a_.movMR(Sz::L, F(kFSaveA), RAX);            // the address, past the store
        memStore(RAX, 2, RDI, write);
        // Nothing above committed guest state, so a bail-out re-runs the
        // whole instruction cleanly. Everything below is the commit.
        a_.movRM(Sz::L, RAX, F(kFSaveA));
        a_.movMR(Sz::L, A(an), RAX);
        if (an != 7) a_.movRM(Sz::L, RAX, A(7)), a_.aluRI(Asm::Op::SUB, Sz::L, RAX, 4);
        if (disp) a_.aluRI(Asm::Op::ADD, Sz::L, RAX, disp);
        a_.movMR(Sz::L, A(7), RAX);
        return true;
    }

    if (sem.operation == SemanticOp::Unlink) {       // UNLK An
        if (ir_.instrs[i].cycles != 6 || !lengthOk(i, 0)) return false;
        const int an = sem.eaReg;
        auto memory = instructionMemoryPlan(in.memory, proofOptions(L_));
        const MemoryAccessPlan read = memory.access(
            MemoryDirection::Read, MemoryOperand::Stack, 4, 2,
            uint8_t(an));
        if (!read.valid() || !memory.complete()) return false;
        a_.movRM(Sz::L, RAX, A(an));                 // the new stack pointer
        a_.movMR(Sz::L, F(kFSaveA), RAX);
        memLoad(RAX, 2, RDI, read);
        // An takes the word read back; A7 lands just past it, except for
        // UNLK A7 where the two are the same register and the read value
        // wins (Moira: `if (an != 7) reg.sp += 4`).
        a_.movMR(Sz::L, A(an), RDI);
        if (an != 7) {
            a_.movRM(Sz::L, RAX, F(kFSaveA));
            a_.aluRI(Asm::Op::ADD, Sz::L, RAX, 4);
            a_.movMR(Sz::L, A(7), RAX);
        }
        return true;
    }

    if (sem.operation == SemanticOp::Lea) {          // LEA <ea>,An
        const int an = sem.registerIndex;
        Ea src;
        if (!decode(i, mode, reg, 2, 0, src)) return false;
        if (!src.memory) return false;               // LEA needs an address
        if (src.idx == kM_PI || src.idx == kM_PD) return false;  // not encodable
        if (!lengthOk(i, src.ext)) return false;
        static const int8_t kLea[kM_COUNT] =
            { -1, -1, 6, -1, -1, 7, -1, 6, 6, 7, -1, -1 };
        const int cycles = kLea[src.idx];
        if (cycles < 0 || cycles != ir_.instrs[i].cycles) return false;
        addrOf(src, RAX, 2);
        a_.movMR(Sz::L, A(an), RAX);
        return true;
    }

    if (sem.operation == SemanticOp::Extend) {       // EXT.W / EXT.L
        if (ir_.instrs[i].cycles != 4) return false;
        const int dn = sem.eaReg;
        const bool toLong = sem.sizeIndex == 2;
        if (toLong) {
            a_.movsx(Sz::W, RDI, D(dn));
            a_.movMR(Sz::L, D(dn), RDI);
            a_.testRR(Sz::L, RDI, RDI);
        } else {
            a_.movsx(Sz::B, RDI, D(dn));
            a_.movMR(Sz::W, D(dn), RDI);
            a_.testRR(Sz::W, RDI, RDI);
        }
        flagsLogic(toLong ? Sz::L : Sz::W);
        return true;
    }

    if (sem.operation == SemanticOp::Swap) {         // SWAP Dn
        if (ir_.instrs[i].cycles != 4) return false;
        const int dn = sem.eaReg;
        a_.movRM(Sz::L, RDI, D(dn));
        a_.shiftRI(Sz::L, RDI, 0, 16);               // ROL 16
        a_.movMR(Sz::L, D(dn), RDI);
        a_.testRR(Sz::L, RDI, RDI);
        flagsLogic(Sz::L);
        return true;
    }

    if (sem.operation == SemanticOp::Pea) {          // PEA <ea>
        // The address computation must come BEFORE the stack moves: Moira
        // is computeEA then push, and `PEA (A7)` / `PEA d16(A7)` are legal
        // and read the OLD A7. Same shape as BSR's push above; the only
        // difference is what goes on the stack.
        Ea src;
        if (!decode(i, mode, reg, 2, 0, src)) return false;
        if (!src.memory) return false;               // PEA needs an address
        if (src.idx == kM_PI || src.idx == kM_PD) return false;  // not encodable
        if (!lengthOk(i, src.ext)) return false;
        // execPea, 68020 column. No Dn/An row: those encodings are SWAP.
        static const int8_t kPea[kM_COUNT] =
            { -1, -1, 9, -1, -1, 10, -1, 9, 9, 10, -1, -1 };
        const int cycles = kPea[src.idx];
        if (cycles < 0 || cycles != ir_.instrs[i].cycles) return false;
        auto memory = instructionMemoryPlan(in.memory, proofOptions(L_));
        const MemoryAccessPlan write = memory.access(
            MemoryDirection::Write, MemoryOperand::Stack, 4, 4, 7);
        if (!write.valid() || !memory.complete()) return false;
        addrOf(src, RDI, 2);
        a_.movRM(Sz::L, RAX, A(7));
        a_.aluRI(Asm::Op::SUB, Sz::L, RAX, 4);
        a_.movMR(Sz::L, F(kFSaveA), RAX);
        memStore(RAX, 2, RDI, write);
        // A7 moves only once the store has committed: a bail-out inside
        // memStore re-runs the whole instruction through Moira, and a
        // pre-decremented A7 would make it push twice.
        a_.movRM(Sz::L, RAX, F(kFSaveA));
        a_.movMR(Sz::L, A(7), RAX);
        return true;
    }

    if (szIdx > 2) return false;

    if (sem.operation == SemanticOp::Test) {         // TST <ea>
        Ea src;
        if (!decode(i, mode, reg, szIdx, 0, src)) return false;
        if (src.idx == kM_IM) return false;
        if (src.idx == kM_AN && szIdx == 0) return false;
        if (!lengthOk(i, src.ext)) return false;
        const int cycles = kEaRead[src.idx][szIdx];
        if (cycles < 0 || cycles != ir_.instrs[i].cycles) return false;
        auto memory = instructionMemoryPlan(in.memory, proofOptions(L_));
        MemoryAccessPlan read;
        if (src.memory) {
            read = memory.access(MemoryDirection::Read,
                                 MemoryOperand::Operand,
                                 uint8_t(sizeBytes(szIdx)), uint8_t(mode),
                                 uint8_t(reg));
            if (!read.valid()) return false;
        }
        if (!memory.complete()) return false;
        load(src, szIdx, RDI, read, false);
        a_.testRR(hostSz(szIdx), RDI, RDI);
        flagsLogic(hostSz(szIdx));
        commitEa(src, szIdx, read);
        return true;
    }

    if (sem.operation != SemanticOp::Clear &&
        sem.operation != SemanticOp::Complement &&
        sem.operation != SemanticOp::Negate) return false;

    Ea dst;
    if (!decode(i, mode, reg, szIdx, 0, dst)) return false;
    if (dst.idx == kM_AN || dst.idx == kM_IM || dst.idx == kM_DIPC) return false;
    if (!lengthOk(i, dst.ext)) return false;
    const int cycles = eaRmwCost(dst.idx, szIdx);
    if (cycles < 0 || cycles != ir_.instrs[i].cycles) return false;

    auto memory = instructionMemoryPlan(in.memory, proofOptions(L_));
    MemoryAccessPlan read, write;
    if (dst.memory) {
        if (sem.operation != SemanticOp::Clear) {
            read = memory.access(MemoryDirection::Read,
                                 MemoryOperand::Destination,
                                 uint8_t(sizeBytes(szIdx)), uint8_t(mode),
                                 uint8_t(reg));
            if (!read.valid()) return false;
        }
        write = memory.access(MemoryDirection::Write,
                              MemoryOperand::Destination,
                              uint8_t(sizeBytes(szIdx)), uint8_t(mode),
                              uint8_t(reg));
        if (!write.valid()) return false;
        if (sem.operation != SemanticOp::Clear &&
            !memoryRmwAccessPair(read, write))
            return false;
    }
    if (!memory.complete()) return false;

    if (sem.operation == SemanticOp::Clear) {        // CLR: write zero
        if (dst.idx == kM_DN) {
            a_.movMI(hostSz(szIdx), D(dst.reg), 0);
        } else {
            addrOf(dst, RAX, szIdx);
            a_.aluRR(Asm::Op::XOR, Sz::L, RDI, RDI);
            memStore(RAX, szIdx, RDI, write);
            commitEa(dst, szIdx, write);
        }
        a_.movMI(Sz::B, at(L_.srN), 0);
        a_.movMI(Sz::B, at(L_.srZ), 1);
        clearVC();
        return true;
    }

    // NOT ($4600) and NEG ($4400)
    const bool isNeg = sem.operation == SemanticOp::Negate;
    if (dst.idx == kM_DN) {
        if (isNeg) {
            a_.negM(hostSz(szIdx), D(dst.reg));
            flagsAddSub(true);
        } else {
            a_.notM(hostSz(szIdx), D(dst.reg));
            a_.aluMI(Asm::Op::CMP, hostSz(szIdx), D(dst.reg), 0);
            flagsLogic(hostSz(szIdx));
        }
        return true;
    }
    addrOf(dst, RAX, szIdx);
    memRmwLoad(RAX, szIdx, RDI, read, write);
    if (isNeg) {
        a_.negR(hostSz(szIdx), RDI);
        flagsAddSub(true);
    } else {
        a_.notR(hostSz(szIdx), RDI);
        a_.testRR(hostSz(szIdx), RDI, RDI);
        flagsLogic(hostSz(szIdx));
    }
    memRmwStore(szIdx, RDI, read, write);
    commitEa(dst, szIdx, write);
    return true;
}

// Bcc / BRA. The block's terminator, and the reason a code generator is
// worth having at all here: when the target is another instruction of THIS
// block — a loop closing on itself — the branch becomes an internal jump
// and the loop never returns to the engine.
// Scc <ea> — sets a BYTE to $FF or $00 and branches nothing, which is why it
// is Kind::Cond and sits inside a block rather than ending one. On the 68020
// column the register form is a flat 4 cycles whatever the condition (the
// 68000's `data ? 6 : 4` is a 68000-only quirk), and the memory forms never
// READ the destination — execSccEa's 020 branch computes the EA, updates An
// and writes, so this is a SINGLE guest access and may use the access thunk.
bool Emitter::emitScc(size_t i) {
    const Instr& in = ir_.instrs[i];
    const InstructionSemantics& sem = in.semantics;
    if (sem.operation != SemanticOp::SetCondition) return false;
    auto memory = instructionMemoryPlan(in.memory, proofOptions(L_));
    const int cc = sem.condition;
    const int mode = sem.eaMode, reg = sem.eaReg;

    // condToAl leaves ZF SET when the condition is true, and clobbers RAX.
    // setcc NE therefore yields 1 for FALSE and 0 for true; subtracting one
    // turns that into the 68k's $00 / $FF without a branch.
    const auto materialise = [&](Reg dst) {
        condToAl(cc);
        a_.setccR(Cc::NE, dst);
        a_.aluRI(Asm::Op::SUB, Sz::B, dst, 1);
    };

    if (mode == 0) {                                  // Scc Dn — execSccRg
        if (in.cycles != 4) return false;
        if (!lengthOk(i, 0)) return false;
        if (!memory.complete()) return false;
        materialise(RDI);
        a_.movMR(Sz::B, D(reg), RDI);                 // byte only; D(n) upper
        return true;                                  // bits are left alone
    }

    Ea dst;
    if (!decode(i, mode, reg, 0, 0, dst)) return false;
    if (!dst.memory) return false;                    // An is not a Scc target
    if (dst.idx == kM_DIPC || dst.idx == kM_IM) return false;   // not writable
    if (!lengthOk(i, dst.ext)) return false;
    // execSccEa, 68020 column (byte row).
    static const int8_t kScc[kM_COUNT] =
        { -1, -1, 10, 10, 11, 11, -1, 10, 10, -1, -1, -1 };
    const int cycles = kScc[dst.idx];
    if (cycles < 0 || cycles != int(in.cycles)) return false;
    const MemoryAccessPlan write = memory.access(
        MemoryDirection::Write, MemoryOperand::Destination, 1,
        uint8_t(mode), uint8_t(reg));
    if (!write.valid() || !memory.complete()) return false;

    // Value first: condToAl clobbers RAX, which is where the address goes.
    // Neither half reads the other's state, so the order is free.
    materialise(RDI);
    addrOf(dst, RAX, 0);
    memStore(RAX, 0, RDI, write);
    commitEa(dst, 0, write);                          // (A7)+/-(A7) step 2
    return true;
}

bool Emitter::emitBranch(size_t i) {
    const Instr& in = ir_.instrs[i];
    const uint16_t op = in.opcode;
    const InstructionSemantics& sem = in.semantics;
    if (sem.operation != SemanticOp::Branch) return false;
    const ControlFlowPlan& control = in.control;
    if (!control.valid || !control.targetKnown) return false;
    const int cc = sem.condition;

    if (in.words > 2) return false;                  // .L form: rare, skipped

    const uint32_t target = control.target;
    const uint32_t fall = control.fallthrough;
    if (target & 1) return false;                    // 68040 raises before cond

    const bool always = (cc == 0);
    const int takenCycles = always ? 10 : 6;         // execBra / execBcc
    const int fallCycles = (in.words == 1) ? 4 : 6;
    // A branch charges different amounts on its two paths, so the tracer's
    // single measurement can only confirm the path it took.
    const int traced = in.cycles;
    if (traced != takenCycles && traced != fallCycles) return false;

    // Is the target inside this block? Only then can it stay in host code.
    int targetIdx = -1;
    for (size_t k = 0; k < ir_.instrs.size(); k++)
        if (ir_.instrs[k].pc == target) { targetIdx = int(k); break; }

    Label& notTaken = *a_.fresh();
    if (!always) {
        condToAl(cc);
        a_.jcc(Cc::NE, notTaken);                    // condToAl: ZF set == taken
    }

    // Taken. Neither mode-5 core refills the queue at instruction end, so
    // irc still holds the word fetched at pc + 2: the DISPLACEMENT for a
    // word branch, the ENTRY LOOKAHEAD for a short one. The first version
    // committed `extensionWord(0)` unconditionally — which for a one-word
    // branch is not a machine word at all but the accessor's 0 default —
    // and the 030 lockstep at HEAD caught it as a terminal queue of
    // 66FA/0000 against the interpreter's 66FA/4E75 (a taken BNE.S with an
    // RTS in the lookahead). a64's `lastHeld` (JitBackendA64.cpp:2251) is
    // the same formula; on the 030 the tracer's terminal capture must also
    // CONFIRM it before the emitter is admitted, exactly as a64 does.
    const uint16_t takenIrc = in.words > 1 ? in.extensionWord(0)
                                           : ir_.prefetchWord(in.pc + 2);
    const uint16_t fallIrcQ = ir_.prefetchWord(fall);
    if (L_.is030 && in.terminalQueueValid) {
        const bool targetOnly = in.observedNextPc == target && target != fall;
        const bool fallOnly = in.observedNextPc == fall && target != fall;
        if (in.terminalIrd != op ||
            (targetOnly && in.terminalIrc != takenIrc) ||
            (fallOnly && in.terminalIrc != fallIrcQ) ||
            (!targetOnly && !fallOnly && in.terminalIrc != takenIrc &&
             in.terminalIrc != fallIrcQ)) return false;
    }
    a_.movMI(Sz::L, at(L_.pc), int32_t(target));
    a_.movMI(Sz::L, at(L_.pc0), int32_t(target));
    commitQueue(op, takenIrc);
    chargeCycles(takenCycles);
    a_.aluRI(Asm::Op::ADD, Sz::L, kCnt, 1);
    loopTo_ = targetIdx;
    if (targetIdx >= 0) {
        a_.jmp(*entry_[size_t(targetIdx)]);   // the loop stays in host code
    } else {
        leaveTo(target);
    }

    if (!always) {
        a_.bind(notTaken);
        a_.movMI(Sz::L, at(L_.pc), int32_t(fall));
        a_.movMI(Sz::L, at(L_.pc0), int32_t(fall));
        commitQueue(op, fallIrcQ);
        chargeCycles(fallCycles);
        a_.aluRI(Asm::Op::ADD, Sz::L, kCnt, 1);
        leaveTo(fall);
    }
    return true;
}

// DBcc — the census's sharp edge (2026-07-30): 1.26 % of the idle Finder,
// and every iteration of every DBRA loop used to leave the block. The 040
// order (execDbcc + WinUAE op_51c8_31): odd-target address error FIRST
// (before the condition and the decrement — compile-time here), then the
// condition; a TRUE condition falls through without touching Dn; a false
// one tests Dn.w BEFORE decrementing it. fullPrefetch refills nothing on
// the 040, so irc stays the displacement word on every path.
bool Emitter::emitDbcc(size_t i) {
    const Instr& in = ir_.instrs[i];
    const uint16_t op = in.opcode;
    const InstructionSemantics& sem = in.semantics;
    const ControlFlowPlan& control = in.control;
    // `canEmit` says yes for every DBcc, so a refusal here is invisible from
    // outside and the census reports it as an unsupported opcode with no
    // hint why. On the 68030 DBcc is the LARGEST single in-block fallback
    // (37.8 %, docs/JIT_BRINGUP.md § C.4ter), which is exactly the case
    // that had to be diagnosed by elimination. Name the guard instead.
    const auto refuse = [&](const char* why) {
        if (verbose())
            std::fprintf(stderr, "[jit]   DBcc $%08X %04X refused: %s "
                         "(words=%u cycles=%u ctrl=%d/%d target=$%08X)\n",
                         in.pc, op, why, in.words, in.cycles,
                         int(control.valid), int(control.targetKnown),
                         control.target);
        return false;
    };
    if (!control.valid || control.kind != ControlFlowKind::DecrementBranch ||
        !control.targetKnown)
        return refuse("control-flow plan");
    if (in.words != 2) return refuse("not 2 words");
    if (sem.operation != SemanticOp::DecrementBranch)
        return refuse("semantics");
    const int cc = sem.condition;
    const int dn = sem.eaReg;
    const uint32_t target = control.target;
    const uint32_t fall = control.fallthrough;
    if (target & 1) return refuse("odd target");
    // execDbcc, 68020 column: taken 6, expired 10, condition-true 6. The
    // tracer's one measurement can only confirm the path it took.
    if (in.cycles != 6 && in.cycles != 10) return refuse("cycle cross-check");
    const uint16_t ircAfter = in.extensionWord(0);

    int targetIdx = -1;
    for (size_t k = 0; k < ir_.instrs.size(); k++)
        if (ir_.instrs[k].pc == target) { targetIdx = int(k); break; }

    Label& condTrue = *a_.fresh();
    Label& expired = *a_.fresh();
    if (cc != 1) {                       // DBF/DBRA: the condition never holds
        condToAl(cc);
        a_.jcc(Cc::E, condTrue);         // condToAl: ZF set == condition TRUE
    }
    a_.movzx(Sz::W, RAX, D(dn));         // takeBranch = Dn.w != 0, PRE-decrement
    a_.aluMI(Asm::Op::SUB, Sz::W, D(dn), 1);
    a_.testRR(Sz::W, RAX, RAX);
    a_.jcc(Cc::E, expired);

    // Taken: the loop closes. Jumping to entry_[target] keeps it in host
    // code with the guards re-run every iteration (budget + interrupts).
    a_.movMI(Sz::L, at(L_.pc), int32_t(target));
    a_.movMI(Sz::L, at(L_.pc0), int32_t(target));
    commitQueue(op, ircAfter);
    chargeCycles(6);
    a_.aluRI(Asm::Op::ADD, Sz::L, kCnt, 1);
    loopTo_ = targetIdx;
    if (targetIdx >= 0) a_.jmp(*entry_[size_t(targetIdx)]);
    else                leaveTo(target);

    a_.bind(expired);                    // Dn hit -1: fall through, 10 cycles
    a_.movMI(Sz::L, at(L_.pc), int32_t(fall));
    a_.movMI(Sz::L, at(L_.pc0), int32_t(fall));
    commitQueue(op, ircAfter);
    chargeCycles(10);
    a_.aluRI(Asm::Op::ADD, Sz::L, kCnt, 1);
    leaveTo(fall);

    if (cc != 1) {                       // condition true: no decrement, 6
        a_.bind(condTrue);
        a_.movMI(Sz::L, at(L_.pc), int32_t(fall));
        a_.movMI(Sz::L, at(L_.pc0), int32_t(fall));
        commitQueue(op, ircAfter);
        chargeCycles(6);
        a_.aluRI(Asm::Op::ADD, Sz::L, kCnt, 1);
        leaveTo(fall);
    }
    return true;
}

// JMP <ea> — the JSR emitter minus the push. execJmp's 68020 column:
// (An) 4, d16(An) 5, abs.w 4, abs.l 4, d16(PC) 5.
bool Emitter::emitJmp(size_t i) {
    const Instr& in = ir_.instrs[i];
    const uint16_t op = in.opcode;
    const InstructionSemantics& sem = in.semantics;
    const ControlFlowPlan& control = in.control;
    if (sem.operation != SemanticOp::Jump || !control.valid ||
        control.pushesReturnAddress) return false;
    const int mode = sem.eaMode, reg = sem.eaReg;
    Ea ea;
    if (!decode(i, mode, reg, 2, 0, ea)) return false;
    if (!ea.memory) return false;
    if (ea.idx == kM_PI || ea.idx == kM_PD) return false;    // not encodable
    if (!lengthOk(i, ea.ext)) return false;
    static const int8_t kJmp[kM_COUNT] =
        { -1, -1, 4, -1, -1, 5, -1, 4, 4, 5, -1, -1 };
    if (kJmp[ea.idx] < 0 || kJmp[ea.idx] != int(in.cycles)) return false;
    const uint16_t ircAfter = ir_.prefetchWord(in.pc + 2);

    const bool constant = control.targetKnown;
    if (constant && (control.target & 1)) return false;

    addrOf(ea, RDI, 2);
    if (!constant) {                     // odd target: 040 address error
        a_.testRI(Sz::L, RDI, 1);
        a_.jcc(Cc::NE, runtimeStub(i));
    }
    a_.movMR(Sz::L, at(L_.pc), RDI);
    a_.movMR(Sz::L, at(L_.pc0), RDI);
    commitQueue(op, ircAfter);
    chargeCycles(int(in.cycles));
    a_.aluRI(Asm::Op::ADD, Sz::L, kCnt, 1);
    if (constant) leaveTo(control.target);
    else          leaveToDynamic();
    return true;
}

// MOVEM — 3.3 % of the idle Finder across its push/pop/load forms, and a
// block-truncator besides (it sat at every function entry/exit the way
// LINK/UNLK did before they were carved out). Straight-line, not a
// terminator: the main loop charges the cycles and commits the queue.
//
// The whole burst is ONE span probe: n registers × size bytes are
// contiguous, so a single DTLB entry either serves all of them or the
// instruction bails to Moira with NOTHING committed — which is also what
// makes multi-access safety trivial (no thunk, no partial commit).
//
// Two 040-specific rules, both from execMovem*: a compiled MOVEM must
// bail while the RESTART LATCH is armed (a fault handler may have changed
// the base register, and the restart must resume from the SAVED ea); and
// the -(An) form with the base register in the mask stores INITIAL - size,
// with An written once at the end.
bool Emitter::emitMovem(size_t i) {
    const Instr& in = ir_.instrs[i];
    const InstructionSemantics& sem = in.semantics;
    auto memory = instructionMemoryPlan(in.memory, proofOptions(L_));
    if (sem.operation != SemanticOp::Movem) return false;
    // The 030's MOVEM restart contract is its own (mmu030 fixups, not the
    // 040 restart latch modelled below) and is NOT implemented. Until
    // 2026-08-18 this was refused only by ACCIDENT — the cost cross-check
    // rejects any instruction whose traced cycles carry an i-cache penalty
    // — and JIT_BRINGUP § C.4.4 says to make that a real guard before any
    // C.5 flip: an accident of the cross-check is not a safety property.
    if (L_.is030) return false;
    const bool toRegs = sem.toRegisters;
    const int szIdx = sem.sizeIndex;
    const int size = sizeBytes(szIdx);
    const int mode = sem.eaMode, reg = sem.eaReg;
    const uint16_t mask = in.extensionWord(0);
    if (mask == 0) return false;                     // legal, rare, not worth it

    Ea ea;
    if (!decode(i, mode, reg, szIdx, /*extAt=*/1, ea)) return false;
    if (!ea.memory) return false;
    if (!lengthOk(i, 1 + ea.ext)) return false;
    if (toRegs) {
        if (ea.idx == kM_PD) return false;           // not encodable
    } else {
        if (ea.idx == kM_PI || ea.idx == kM_DIPC) return false;
    }

    int n = 0;
    for (int b = 0; b < 16; b++) if (mask & (1 << b)) n++;

    // execMovem{EaRg,RgEa}, 68020 column: base[mode] + 4 per transfer.
    static const int8_t kBaseToRegs[kM_COUNT] =
        { -1, -1, 12, 8, -1, 13, -1, 12, 12, 9, -1, -1 };
    static const int8_t kBaseToMem[kM_COUNT] =
        { -1, -1, 8, -1, 4, 9, -1, 8, 8, -1, -1, -1 };
    const int base = toRegs ? kBaseToRegs[ea.idx] : kBaseToMem[ea.idx];
    if (base < 0 || base + 4 * n != int(in.cycles)) return false;
    const MemoryAccessPlan span = memory.access(
        toRegs ? MemoryDirection::Read : MemoryDirection::Write,
        MemoryOperand::RegisterList, uint8_t(size), uint8_t(mode),
        uint8_t(reg));
    const MemoryOrder emittedOrder = ea.idx == kM_PD
        ? MemoryOrder::RegisterDescending : MemoryOrder::RegisterAscending;
    if (!span.valid() || !span.preflight || !memory.complete() ||
        memory.proof.protocol != MemoryProofProtocol::OrderedSpan ||
        in.memory.order != emittedOrder || span.eaCommit != EaCommit::PerElement)
        return false;

    // The restart latch: armed between a faulted MOVEM and its completed
    // re-run. Cold in every normal execution, so one byte test.
    a_.aluMI(Asm::Op::CMP, Sz::B, at(L_.movemArmed), 0);
    a_.jcc(Cc::NE, runtimeStub(i));

    if (ea.idx == kM_PD) {
        // -(An): descending stores from An-size down to An-n*size, which
        // is also the span base and the final An.
        a_.movRM(Sz::L, RAX, A(ea.reg));
        a_.aluRI(Asm::Op::SUB, Sz::L, RAX, n * size);
        memProbe(RAX, n * size, /*write=*/true, runtimeStub(i));
        // The probe's fill path clobbers caller-saved registers; the base
        // register itself is unchanged until the end, so re-read it.
        a_.movRM(Sz::L, RDI, A(ea.reg));             // initial
        // Moira stores DESCENDING register indices at DESCENDING addresses
        // (A7 at initial−S first, down to the lowest masked register at the
        // span base) — so ascending register order lands at ascending span
        // offsets: masked register number j sits at spanBase + j·size.
        int j = 0;                                   // transfer number
        for (int b = 0; b < 16; b++) {
            if (!(mask & (0x8000 >> b))) continue;   // predec: reversed mask
            const int32_t off = j * size;
            if (b == 8 + ea.reg) {                   // base-in-list rule
                a_.movRR(Sz::L, RDX, RDI);
                a_.aluRI(Asm::Op::SUB, Sz::L, RDX, size);
            } else {
                a_.movRM(Sz::L, RDX, R(b));
            }
            if (szIdx == 1) {
                a_.rolR16(RDX, 8);
                a_.movMR(Sz::W, mem(RSI, off), RDX);
            } else {
                a_.bswap(RDX);
                a_.movMR(Sz::L, mem(RSI, off), RDX);
            }
            j++;
        }
        a_.aluMI(Asm::Op::SUB, Sz::L, A(ea.reg), n * size);   // An last
        return true;
    }

    // Ascending forms, both directions: base address into RAX.
    addrOf(ea, RAX, szIdx);
    memProbe(RAX, n * size, toRegs ? false : true, runtimeStub(i));
    int j = 0;
    for (int b = 0; b < 16; b++) {
        if (!(mask & (1 << b))) continue;
        const int32_t off = j * size;
        if (toRegs) {
            // MOVEM.W sign-extends into the FULL register, D and A alike.
            if (szIdx == 1) {
                a_.movzx(Sz::W, RDX, mem(RSI, off));
                a_.rolR16(RDX, 8);
                a_.movsxRR(Sz::W, RDX, RDX);
            } else {
                a_.movRM(Sz::L, RDX, mem(RSI, off));
                a_.bswap(RDX);
            }
            a_.movMR(Sz::L, R(b), RDX);
        } else {
            a_.movRM(Sz::L, RDX, R(b));
            if (szIdx == 1) {
                a_.rolR16(RDX, 8);
                a_.movMR(Sz::W, mem(RSI, off), RDX);
            } else {
                a_.bswap(RDX);
                a_.movMR(Sz::L, mem(RSI, off), RDX);
            }
        }
        j++;
    }
    if (ea.idx == kM_PI) {
        // (An)+ writes the FINAL address into An after the loop — even
        // when An itself was in the mask (execMovemEaRg's writeA order).
        // RAX survived the probe (the fill path saves it in kFScratch),
        // but a mask load may have clobbered... RAX is not a load target
        // (RDX is), so base + n*size off RAX is the value Moira writes.
        a_.aluRI(Asm::Op::ADD, Sz::L, RAX, n * size);
        a_.movMR(Sz::L, A(ea.reg), RAX);
    }
    return true;
}

bool Emitter::emitInstr(size_t i) {
    const InstructionSemantics& sem = ir_.instrs[i].semantics;
    switch (sem.operation) {
        case SemanticOp::ImmediateAlu: return emitImmediate(i);
        case SemanticOp::Bit: return emitBitOp(i);
        case SemanticOp::Move: return emitMove(i, sem.sizeIndex);
        case SemanticOp::Jump: return emitJmp(i);
        case SemanticOp::Movem: return emitMovem(i);
        case SemanticOp::Nop:
        case SemanticOp::Link:
        case SemanticOp::Unlink:
        case SemanticOp::Lea:
        case SemanticOp::Extend:
        case SemanticOp::Swap:
        case SemanticOp::Pea:
        case SemanticOp::Test:
        case SemanticOp::Clear:
        case SemanticOp::Negate:
        case SemanticOp::Complement:
        case SemanticOp::JumpSubroutine:
        case SemanticOp::ReturnSubroutine:
            return emitLine4(i);
        case SemanticOp::DecrementBranch: return emitDbcc(i);
        case SemanticOp::SetCondition: return emitScc(i);
        case SemanticOp::AddSubQuick: return emitAddSubQ(i);
        case SemanticOp::BranchSubroutine: return emitSubroutine(i);
        case SemanticOp::Branch: return emitBranch(i);
        case SemanticOp::MoveQuick: return emitMoveq(i);
        case SemanticOp::AluEaToReg:
        case SemanticOp::AddressAlu:
            return emitAluEaRg(i);
        case SemanticOp::AluRegToEa: return emitAluRgEa(i);
        default:
            // The dispatch keys on the TRACED `in.semantics.operation`,
            // while `canEmit()` keys on the static `describeInstruction(op)`
            // — so an opcode the census reports as "unsupported" may have a
            // perfectly good emitter that was never reached. Say so.
            if (verbose())
                std::fprintf(stderr, "[jit]   no emitter for $%08X %04X: "
                             "traced semantics op=%d (canEmit=%d)\n",
                             ir_.instrs[i].pc, ir_.instrs[i].opcode,
                             int(ir_.instrs[i].semantics.operation),
                             int(describeInstruction(ir_.instrs[i].opcode)
                                     .operation));
            return false;
    }
}

bool Emitter::emit() {
    const size_t n = ir_.instrs.size();
    if (n == 0 || ir_.code.empty()) return false;

    entry_.resize(n); slow_.resize(n); budget_.resize(n); flags_.resize(n);
    if (histo_) slowStatic_.resize(n);
    if (needRuntimeDoor()) slowRuntime_.resize(n);
    for (size_t i = 0; i < n; i++) {
        entry_[i] = a_.fresh(); slow_[i] = a_.fresh();
        budget_[i] = a_.fresh(); flags_[i] = a_.fresh();
        if (histo_) slowStatic_[i] = a_.fresh();
        if (needRuntimeDoor()) slowRuntime_[i] = a_.fresh();
    }
    // Which instruction, if any, the block's own terminating branch jumps
    // back to: its boundary is reached from two directions, so the cold
    // stubs must not assume which instruction preceded it.
    int loopTarget = -1;
    if (n && ir_.instrs[n - 1].kind == Kind::Branch) {
        const Instr& br = ir_.instrs[n - 1];
        if (br.semantics.operation == SemanticOp::Branch &&
            br.control.valid && br.control.targetKnown && br.words <= 2) {
            const uint32_t t = br.control.target;
            for (size_t k = 0; k < n; k++)
                if (ir_.instrs[k].pc == t) { loopTarget = int(k); break; }
        }
    }
    exitBudget_ = a_.fresh(); exitFlags_ = a_.fresh();
    exitFault_ = a_.fresh(); exitLost_ = a_.fresh(); epilogue_ = a_.fresh();

    // ── prologue ─────────────────────────────────────────────────────────
    // Five pushes: the four callee-saved registers the block lives in, plus
    // one more so that rsp is 16-byte aligned at every call site.
    // Six pushes plus eight bytes of padding: the stack must be 16-byte
    // aligned at every call site, and a LINKED block inherits this frame.
    a_.push(RBX); a_.push(RBP); a_.push(R14); a_.push(R15);
    a_.push(kPer); a_.push(kClk);
    a_.aluRI(Asm::Op::SUB, Sz::Q, RSP, 8);
    a_.movRR(Sz::Q, kCpu, RDI);
    a_.movRR(Sz::Q, kFrm, RSI);
    a_.movRM(Sz::Q, kTgt, F(kFClockTarget));
    if (paced_) a_.movRM(Sz::Q, kPer, F(kFPeriph));
    fillClock();
    a_.aluRR(Asm::Op::XOR, Sz::L, kCnt, kCnt);
    // The 68030's per-instruction contract, minus the seven fields the fault
    // re-run re-establishes (Moira.h § PomJitLayout::mmuRmw). Emitted in the
    // PROLOGUE, before linkEntry_, and once per chain rather than once per
    // instruction: TAS and CAS are the only things that set the flag, both
    // are Kind::Unsafe, so no block in a linked chain can raise it again —
    // the same argument that lets a chain inherit privilege and the MMU
    // generation without re-checking them (POM68K_JIT.md § 9).
    if (L_.is030) a_.movMI(Sz::B, at(L_.mmuRmw), 0);
    linkEntry_ = a_.size();

    size_t emitted = 0;
    for (size_t i = 0; i < n; i++) {
        cur_ = i;
        a_.bind(*entry_[i]);
        guards(i);

        // A 68030 branch longer than one word fetches a DIFFERENT number of
        // words on its two paths: the not-taken path consumes the
        // displacement through readExt (one extra fetch), the taken path
        // reads it straight out of queue.irc and never does. The i-cache
        // charge below is emitted once, before the condition is even
        // evaluated, so it cannot express that. Refuse the form rather than
        // charge one of its two paths wrongly — coverage, not correctness,
        // and only until the charge is split across emitBranch's own paths.
        if (ic_ && ir_.instrs[i].kind == Kind::Branch && ir_.instrs[i].words > 1) {
            a_.jmp(staticStub(i));
            emitted = i + 1;
            continue;
        }

        const Asm::Mark mark = a_.mark();
        pollIpl();
        // In mmuExecuteStart's own order: POLL_IPL, then the fetch that
        // charges the 030 i-cache. INSIDE the mark, deliberately — if the
        // instruction turns out not to be emittable, rewind() takes the
        // charge away with the rest and the fallback stub charges it itself
        // through mmuFetchWord. The compiler's shadow of the cache is
        // advanced either way, because the real cache changes either way.
        chargeIcache(i);
        const bool native = emitInstr(i);
        emitted = i + 1;

        if (!native) {
            // Leave no trace of a family that gave up half-way — including
            // the IPL sample, which the fallback's own mmu040InstrStart
            // will perform. Then send
            // the instruction to its stub in the cold half.
            a_.rewind(mark);
            a_.jmp(staticStub(i));
            continue;
        }
        native_++;
        if (ir_.instrs[i].kind == Kind::Branch) break;   // it committed its own

        // No pc/pc0 here: nothing downstream reads them until the block
        // exits, and every exit commits them on the way out. The queue does
        // stay — see emitBoundary.
        commitQueue(ir_.instrs[i].opcode,
                    ir_.prefetchWord(ir_.instrs[i].pc +
                                     uint32_t(ir_.instrs[i].words) * 2));
        chargeCycles(ir_.instrs[i].cycles);
        a_.aluRI(Asm::Op::ADD, Sz::L, kCnt, 1);
    }

    // Falling off the end of the recorded straight line. pc, pc0 and the
    // clock were committed by the last instruction, so this is a plain,
    // successful block end — and a linkable one.
    if (emitted && ir_.instrs[emitted - 1].kind != Kind::Branch) {
        const uint32_t endPc = ir_.instrs[emitted - 1].pc +
                               uint32_t(ir_.instrs[emitted - 1].words) * 2;
        emitBoundary(endPc, int(emitted) - 1);
        leaveTo(endPc);
    } else {
        a_.movMI(Sz::L, F(kFExit), int32_t(Exit::BlockEnd));
        a_.jmp(*epilogue_);
    }

    // ── the cold half ────────────────────────────────────────────────────
    // One stub per instruction: run THIS instruction through Moira from the
    // boundary, then rejoin the compiled stream. Reached both by forms the
    // backend cannot emit and by the bail-outs of ones it can — a data
    // address that turned out not to be plain memory, an access straddling
    // two pages. Guest state is untouched at every entry into it, which is
    // what makes handing the instruction over safe.
    // Everything the hot path deferred: TLB fills and access thunks.
    for (auto& fn : cold_) fn();
    cold_.clear();

    // The two guard exits and the interpreter fallback all leave from the
    // SAME boundary — the one before instruction i — so they share the
    // commit and differ only in what they do next.
    for (size_t i = 0; i < emitted; i++) {
        Label& prep = *a_.fresh();
        a_.bind(*budget_[i]);
        a_.movMI(Sz::L, F(kFExit), int32_t(Exit::ClockBudget));
        a_.jmp(prep);
        a_.bind(*flags_[i]);
        a_.movMI(Sz::L, F(kFExit), int32_t(Exit::CpuFlags));
        a_.bind(prep);
        emitBoundary(ir_.instrs[i].pc, int(i) == loopTarget ? -1 : int(i) - 1);
        a_.jmp(*epilogue_);
    }

    for (size_t i = 0; i < emitted; i++) {
        // POM68K_JIT_HISTO: the two counting doors onto the shared body.
        // Emitted BEFORE the boundary commit, which is safe because they
        // touch nothing but a host scratch register and a counter array the
        // guest cannot see — and they must not sit after it, or a bail-out
        // that then exits would count an instruction it did not run.
        if (histo_) {
            const int32_t off = int32_t(ir_.instrs[i].opcode) * 8;
            a_.bind(*slowStatic_[i]);
            a_.movRM(Sz::Q, RAX, F(kFHistoStatic));
            a_.aluMI(Asm::Op::ADD, Sz::Q, mem(RAX, off), 1);
            a_.jmp(*slow_[i]);
        }
        if (needRuntimeDoor()) {
            a_.bind(*slowRuntime_[i]);
            if (histo_) {
                const int32_t off = int32_t(ir_.instrs[i].opcode) * 8;
                a_.movRM(Sz::Q, RAX, F(kFHistoRuntime));
                a_.aluMI(Asm::Op::ADD, Sz::Q, mem(RAX, off), 1);
            }
            // The instruction is about to be re-run by the interpreter, so
            // the 68030 i-cache charge it already took comes back off.
            unchargeIcache(i);
        }
        a_.bind(*slow_[i]);
        emitBoundary(ir_.instrs[i].pc, int(i) == loopTarget ? -1 : int(i) - 1);
        spillClock();
        a_.movRR(Sz::Q, RDI, kCpu);
        call(reinterpret_cast<void*>(&pom68kJitStep));
        fillClock();
        a_.aluRI(Asm::Op::CMP, Sz::L, RAX, 0);
        a_.jcc(Cc::L, *exitLost_);                   // -1: the window went away
        a_.jcc(Cc::E, *exitFault_);                  //  0: it did not retire
        a_.aluRI(Asm::Op::ADD, Sz::L, kCnt, 1);
        a_.aluMI(Asm::Op::ADD, Sz::L, F(kFSlow), 1);
        // Moira may have written guest memory, and a write into this very
        // block's page leaves everything compiled after it stale. The
        // engine's write guard is the only thing that can see that.
        a_.movRM(Sz::Q, RAX, F(kFGuard));
        a_.aluMI(Asm::Op::CMP, Sz::B, mem(RAX, 0), 0);
        a_.jcc(Cc::NE, *exitLost_);
        if (i + 1 < emitted) {
            a_.jmp(*entry_[i + 1]);
        } else {
            a_.movMI(Sz::L, F(kFExit), int32_t(Exit::BlockEnd));
            a_.jmp(*epilogue_);
        }
    }

    // ── exits ────────────────────────────────────────────────────────────
    a_.bind(*exitLost_);
    a_.movMI(Sz::L, F(kFExit), int32_t(Exit::WindowLost));
    a_.jmp(*epilogue_);
    a_.bind(*exitFault_);
    a_.aluRI(Asm::Op::ADD, Sz::L, kCnt, 1);          // it ran, it just faulted
    a_.movMI(Sz::L, F(kFExit), int32_t(Exit::Fault));

    a_.bind(*epilogue_);
    a_.movMR(Sz::L, F(kFInstrs), kCnt);
    spillClock();
    a_.aluRI(Asm::Op::ADD, Sz::Q, RSP, 8);
    a_.pop(kClk); a_.pop(kPer); a_.pop(R15); a_.pop(R14); a_.pop(RBP); a_.pop(RBX);
    a_.ret();

    // Any instruction past `emitted` was never reached (a branch ended the
    // block early), so its entry and stub labels stay unbound. Bind them on
    // the epilogue rather than leaving finish() with a dangling reference.
    for (size_t i = emitted; i < n; i++) {
        if (entry_[i]->bound < 0) entry_[i]->bound = epilogue_->bound;
        if (slow_[i]->bound < 0) slow_[i]->bound = epilogue_->bound;
        if (histo_ && slowStatic_[i]->bound < 0)
            slowStatic_[i]->bound = epilogue_->bound;
        if (needRuntimeDoor() && slowRuntime_[i]->bound < 0)
            slowRuntime_[i]->bound = epilogue_->bound;
        if (budget_[i]->bound < 0) budget_[i]->bound = epilogue_->bound;
        if (flags_[i]->bound < 0) flags_[i]->bound = epilogue_->bound;
    }
    if (exitBudget_->bound < 0) exitBudget_->bound = epilogue_->bound;
    if (exitFlags_->bound < 0) exitFlags_->bound = epilogue_->bound;
    return a_.finish() && !a_.overflowed();
}

// ── the backend ──────────────────────────────────────────────────────────

class X64Compiled : public Compiled {
public:
    uint8_t* entry = nullptr;
    // Past the prologue. A linked jump arrives with rbx/rbp/r14/r15/r12
    // already holding what the chain's FIRST block put there, and with the
    // retired-instruction count accumulated so far — which is the point.
    uint8_t* linked = nullptr;
};

class X64Backend : public Backend {
public:
    Backend* clone() const override { return new X64Backend; }
    const char* name() const override { return "x86-64"; }
    const char* description() const override {
        return "native code generation";
    }

    bool usable() const override { return CodeBuffer::supported(); }

    BackendCaps caps() const override {
        BackendCaps c;
        c.nativeCode = true;
        c.aluReg = c.aluMem = c.moves = c.branches = c.addrModes = true;
        c.dtlbCodeMask = true;             // memProbe tests it on every store
        c.maxBlockInstrs = 64;
        // 68040 FAMILY ONLY, and this is a correctness statement, not a
        // measurement. Everything in this file is written against the 040's
        // instruction-boundary contract: the cost tables are CYCLES_68020
        // "which is what the 68040 core uses", `fullPrefetch()` does not
        // refill the queue on the 040 so `irc` is assumed to still hold the
        // word fetched at pc+2, the odd-target address error is raised
        // before the condition is evaluated, and `(An)+` follows the 040's
        // update-after-access order. Each of those differs on the 68030
        // (MoiraDataflow_cpp.h:326-332, :355-361), and the data thunks
        // reach mmu040Read/mmu040Write unconditionally.
        //
        // Measured consequence of pretending otherwise (2026-07-30): given
        // the 68030 LC II, generated code wedged the guest in the ROM's
        // Egret handshake poll loop around $40A148xx-$40A149xx and
        // `jit_lcii_boot_etalon` timed out at an hour, where the same
        // machine boots in 2 min 21 s on `threaded`.
        //
        // Widening this is real work, not a flag flip: it means the 030's
        // update order and prefetch semantics in the emitters, a 030 branch
        // in pomJitProbeData, model-correct access thunks, and an
        // lcii/x64 lockstep gate to prove it. See POM68K_JIT.md § 7.
        c.guestFamilies = kGuest68040;
        return c;
    }

    bool canEmit(uint16_t op) const override;

    Compiled* compile(const BlockIr& ir, const Context& ctx) override;
    void* linkEntry(Compiled* c) const override {
        return static_cast<X64Compiled*>(c)->linked;
    }
    RunResult run(Compiled* c, Context& ctx) override;
    void release(Compiled* c) override { delete c; }
    void flushAll() override {
        if (buf_.valid()) { buf_.makeWritable(); buf_.reset(); }
    }

private:
    CodeBuffer buf_;
    int diagLeft_ = -1;  // resolved POM68K_JIT_VERBOSE block dump budget
    Layout layout_{};
    bool haveLayout_ = false;
    std::vector<uint8_t> scratch_;
};

// A cheap host-admission answer used by the opcode census and by the
// minimum-coverage policy in compile(). Instruction meaning comes solely
// from the shared IR decoder; only x64 EA/cost mechanics remain here. It may
// still over-report because it cannot know whether the traced cycle count
// will agree, which is the right direction for a target-list census.
bool X64Backend::canEmit(uint16_t op) const {
    const InstructionSemantics sem = describeInstruction(op);
    const int mode = sem.eaMode, reg = sem.eaReg;
    const bool eaOk = eaIndex(mode, reg) >= 0;
    switch (sem.operation) {
        case SemanticOp::ImmediateAlu:
            return eaOk;
        case SemanticOp::Bit:
            return sem.action == 0 && eaOk && (!sem.dynamic || mode != 1);
        case SemanticOp::Move: {
            const int dm = sem.destinationMode, dr = sem.destinationReg;
            return eaOk && eaIndex(dm, dr) >= 0 && kMoveDst[eaIndex(dm, dr)] >= 0;
        }
        case SemanticOp::Nop:
        case SemanticOp::Link:
        case SemanticOp::Unlink:
        case SemanticOp::Extend:
        case SemanticOp::Swap:
        case SemanticOp::ReturnSubroutine:
        case SemanticOp::DecrementBranch:
        case SemanticOp::Branch:
        case SemanticOp::BranchSubroutine:
        case SemanticOp::MoveQuick:
            return true;
        case SemanticOp::JumpSubroutine:
        case SemanticOp::Jump:
        case SemanticOp::Lea:
        case SemanticOp::Pea:
            return eaOk;
        case SemanticOp::Movem:
            return true;
        case SemanticOp::Test:
        case SemanticOp::Clear:
        case SemanticOp::Negate:
        case SemanticOp::Complement:
        case SemanticOp::AddSubQuick:
        case SemanticOp::AluEaToReg:
        case SemanticOp::AluRegToEa:
        case SemanticOp::AddressAlu:
            return eaOk;
        case SemanticOp::SetCondition:
            return mode == 0 || (eaOk && mode != 1 &&
                                 eaIndex(mode, reg) != kM_DIPC &&
                                 eaIndex(mode, reg) != kM_IM);
        default: return false;
    }
}

Compiled* X64Backend::compile(const BlockIr& ir, const Context& ctx) {
    if (diagLeft_ < 0) diagLeft_ = verboseBlocks();
    if (!ctx.cpu) return nullptr;
    // Generated code models POLL_IPL as a plain assignment. If the deferred
    // IPL-recognition feature is ever armed, this backend has nothing to say.
    if (!ctx.cpu->pomJitSimpleIpl()) return nullptr;
    if (!haveLayout_) {
        layout_ = ctx.cpu->pomJitLayout();
        haveLayout_ = true;
    }
    if (!buf_.valid() && !buf_.reserve(kCodeBufferBytes)) return nullptr;
    if (ir.code.empty()) return nullptr;

    // Compile into a scratch buffer first: the executable page is W^X, and
    // flipping it per block would cost an mprotect pair each time.
    scratch_.resize(kMaxBlockBytes);
    Asm a(scratch_.data(), scratch_.size());
    Emitter e(a, layout_, ir, ctx);
    const bool ok = e.emit() && !a.overflowed();
    if (verbose() && diagLeft_ > 0) {
        diagLeft_--;
        std::fprintf(stderr, "[jit] block $%08X %2zu instr, %2d native, %zu bytes%s",
                     ir.entryPc, ir.instrs.size(), e.nativeCount(), a.size(),
                     ok ? "" : "  REFUSED(emit)");
        if (!ir.instrs.empty() && ir.instrs.back().kind == Kind::Branch)
            std::fprintf(stderr, "  branch->%s",
                         e.loopedTo() >= 0 ? "INTERNAL" : "exit");
        std::fprintf(stderr, "  [");
        for (const Instr& in : ir.instrs)
            std::fprintf(stderr, "%04X/%uc ", in.opcode, in.cycles);
        std::fprintf(stderr, "]\n");
    }
    if (!ok) return nullptr;

    // A block of nothing but fallbacks is strictly worse than the fetch
    // window loop: same interpreter work, plus a call and a frame. Refuse
    // it and let the engine run the window instead. The bar is
    // POM68K_JIT_MIN_NATIVE percent (JitConfig.h owns the rationale and the
    // 68030 measurement that made it a knob).
    if (e.nativeCount() * 100 < int(ir.instrs.size()) * minNativePercent()) {
        if (verbose() && diagLeft_ > 0) std::fprintf(stderr, "[jit]   ^ REFUSED(coverage)\n");
        return nullptr;
    }

    if (!buf_.makeWritable()) return nullptr;
    uint8_t* dst = buf_.alloc(a.size(), 16);
    if (!dst) return nullptr;                        // full: the engine flushes
    // Every branch the block contains is internal and rel32-encoded, and
    // the only absolute addresses baked in are the thunks', so the finished
    // bytes are position independent and a plain copy is a valid move.
    std::memcpy(dst, scratch_.data(), a.size());
    if (!buf_.makeExecutable()) return nullptr;

    X64Compiled* c = new X64Compiled();
    c->entry = dst;
    c->linked = dst + e.linkEntryOffset();
    return c;
}

RunResult X64Backend::run(Compiled* c, Context& ctx) {
    Frame f{};
    f.clockTarget = ctx.clockTarget;
    f.exit = uint32_t(Exit::BlockEnd);
    f.dtlbSelf = ctx.dtlbSelf;
    f.dtlbFill = ctx.dtlbFill;
    f.guardHit = ctx.guard ? reinterpret_cast<const uint8_t*>(&ctx.guard->hit)
                           : &kNoGuard;
    f.periphClock = ctx.periphClock;
    f.linkTable = ctx.linkTable;
    f.slowStaticHisto = ctx.slowStaticHisto;
    f.slowRuntimeHisto = ctx.slowRuntimeHisto;

    using Fn = void (*)(moira::Moira*, Frame*);
    reinterpret_cast<Fn>(static_cast<X64Compiled*>(c)->entry)(ctx.cpu, &f);

    RunResult r;
    r.instrs = f.instrs;
    r.slowInstrs = f.slowInstrs;
    r.exit = Exit(f.exit);
    return r;
}

}  // namespace

Backend* x64Backend() {
    static X64Backend backend;
    return &backend;
}

}  // namespace jit
