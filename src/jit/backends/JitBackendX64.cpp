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
// Full 68020 indexed plans also come from the shared decoder. Direct LEA is
// lowered below, including base/index suppression; memory indirection and
// full-format use by every other opcode remain out on purpose. Everything
// that touches the SR/MMU/caches and every unproved multi-memory commit also
// falls back, per instruction, to Moira.

#include "JitBackendX64.h"

#include "../JitCodeBuffer.h"
#include "../JitConfig.h"
#include "../JitCost.h"
#include "../JitEaPlan.h"
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
    // NOTE deliberately absent here: a64 sets restartableWriteRequired
    // globally, which on x64 strips the exact thunk from every
    // NON-restartable 030 write and diverged the LC II lockstep at an
    // 8192-cycle boundary (peripheral-phase class, 2026-08-18) while
    // regressing reach. x64 scopes the option to the restartable MOVE
    // family inside emitMove instead — everything else keeps the plans
    // this backend was proved green with.
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
//
// `pcWords` (fetchWords << 32 | pc, 0 when the block emits no i-cache
// charge) is the peripheral-phase alignment of 2026-08-21: this path is
// where I/O lands, an I/O access forces a peripheral flush at the access
// clock, and the interpreter reaches the same access with the fetch
// penalty already on the clock while the emitted charge sits after the
// body. Bias the clock by the read-only peek for the access alone —
// success or fault, the bias is gone before anything else runs, so the
// success-path charge and the fault re-run both stay exactly as they were.
extern "C" int pom68kJitRead(moira::Moira* cpu, uint32_t addr, uint32_t bytes,
                             uint32_t* out, uint64_t pcWords) noexcept {
    const moira::i64 bias = pcWords
        ? cpu->pomJitIcachePeekPenalty(uint32_t(pcWords), int(pcWords >> 32))
        : 0;
    if (bias) cpu->pomJitBiasClock(bias);
    const int ok = cpu->pomJitReadData(addr, int(bytes), *out) ? 1 : 0;
    if (bias) cpu->pomJitBiasClock(-bias);
    return ok;
}
extern "C" int pom68kJitWrite(moira::Moira* cpu, uint32_t addr, uint32_t bytes,
                              uint32_t val, uint64_t pcWords) noexcept {
    const moira::i64 bias = pcWords
        ? cpu->pomJitIcachePeekPenalty(uint32_t(pcWords), int(pcWords >> 32))
        : 0;
    if (bias) cpu->pomJitBiasClock(bias);
    const int ok = cpu->pomJitWriteData(addr, int(bytes), val) ? 1 : 0;
    if (bias) cpu->pomJitBiasClock(-bias);
    return ok;
}

// The 030 JSR reads the first word at its target and leaves that run-time
// value in queue.irc.  No compile-time word is exact: a trap patch may have
// changed it since the block was recorded.  A false result means the read
// would fault, so generated code replays the still-untouched instruction.
extern "C" int pom68kJitReadProg(moira::Moira* cpu, uint32_t addr,
                                 uint32_t* out) noexcept {
    uint16_t word = 0;
    const int ok = cpu->pomJitReadProg(addr, word) ? 1 : 0;
    *out = word;
    return ok;
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
    uint8_t*  progHost;          // +96 proven JSR push pointer across target read
    uint8_t*  pointerHost;       // +104 full-index pointer across stack probe
};
constexpr int32_t kFClockTarget = 0, kFInstrs = 8, kFExit = 12;
constexpr int32_t kFDtlbSelf = 16, kFDtlbFill = 24, kFGuard = 32;
constexpr int32_t kFScratch = 40, kFSaveA = 44, kFSaveV = 48;
constexpr int32_t kFSlow = 52, kFPeriph = 56, kFLinkTab = 64, kFValue = 72;
constexpr int32_t kFHistoStatic = 80, kFHistoRuntime = 88;
constexpr int32_t kFProgHost = 96, kFPointerHost = 104;
static_assert(offsetof(Frame, slowStaticHisto) == kFHistoStatic);
static_assert(offsetof(Frame, slowRuntimeHisto) == kFHistoRuntime);
static_assert(offsetof(Frame, progHost) == kFProgHost);
static_assert(offsetof(Frame, pointerHost) == kFPointerHost);

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
// With POM68K_JIT_PACKED_CCR, r15 packs two independently observable values:
// XNZVC in bits 4..0 (the architectural 68k layout) and the retired count in
// bits 63..8. Bits 7..5 are a carry moat, so incrementing the count by $100
// can never perturb the CCR. A linked block inherits both values.
constexpr Reg kCnt = R15;
constexpr int32_t kCcrMask = 0x1F;
constexpr int kCountShift = 8;
constexpr int32_t kRetireUnit = 1 << kCountShift;
constexpr Reg kPer = R12;      // &lastPeriphClock_, for the inline pacing test
// The cycle clock, live in a register for the whole chain of linked blocks.
// It used to live only in the Moira object, which put a store-to-load
// forward on the critical path of EVERY emitted instruction: the cycle
// charge stored it and the next instruction's budget guard loaded it
// straight back. Anything that calls out spills it first and reloads after,
// and all of those paths are cold.
constexpr Reg kClk = R13;

// ── 68020-column cycle tables + decoded effective address ────────────────
// The EaCostIndex enum, kEaRead/kMoveDst/eaRmwCost columns, the CMPA
// surcharge and the EaPlan struct live in JitCost.h / JitEaPlan.h — they
// model the 68k, not this host, and the a64 backend reads the SAME tables
// (TODO.md § 10 wave 2, 2026-08-28). The `(xxx).W` off-by-one that was
// 47.4 % of all block fallbacks here (2026-08-09) is the reason the cells
// are written once: its a64 twin paid the same toll silently for three days.
using Ea = EaPlan;

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
          // The 030 oracle proved (a64 lockstep step 10455) that a block
          // containing a native 030 write is an architectural chain
          // boundary in BOTH directions: returning to the Engine services
          // every deferred condition before another compiled block can
          // observe the store. So such a block takes no outgoing links —
          // and compile() below also withholds its published entry. x64
          // never had this contract because hit-traced 030 writes compile
          // here too; the latent hole predates the split-timing admissions.
          restartWrite030_(L.is030 &&
              std::any_of(ir.instrs.begin(), ir.instrs.end(),
                          [&L](const Instr& in) {
                              return memoryProofPlan(in.memory,
                                                     proofOptions(L))
                                  .restartableLastWrite();
                          })),
          linkMask_(ctx.linkTable ? ctx.linkMask : 0),
          linkCell_(ctx.linkMask ? ctx.linkCell : nullptr),
          linkCellSelf_(ctx.linkCellSelf),
          paced_(ctx.periphClock != nullptr && ctx.periphBatch != 0),
          batch_(ctx.periphBatch),
          histo_(ctx.slowStaticHisto != nullptr),
          packedCcr_(packedCcrEnabled()),
          ic_(L.icLive && icacheEmitEnabled()) {}

    bool restartWrite030() const { return restartWrite030_; }

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
    bool emitExg(size_t i);
    bool emitShiftRegister(size_t i);
    bool emitBitfield(size_t i);
    bool emitMultiplyWord(size_t i);
    bool emitMultiplyLong(size_t i);
    bool emitDivideWord(size_t i);
    bool emitDivideLong(size_t i);
    bool emitCmpm(size_t i);
    // `exactFetchWords` overrides the words+1 rule for the forms whose
    // mmuFetchWord count is NOT linear (DBcc: 2 on every path — see the
    // call site in emit()). 0 = the linear rule.
    void chargeIcache(size_t i, uint32_t exactFetchWords = 0);
    // Advances the compiler's shadow of the cache WITHOUT emitting the
    // charge — for instructions the interpreter will execute (static
    // refusals): their mmuFetchWord run performs the same line transitions,
    // so later folds stay justified, but no charge may be emitted for them.
    void shadowIcache(size_t i, uint32_t exactFetchWords = 0);
    // One extra word on ONE path of a conditional branch (the fall-through
    // of a two-word Bcc). Never folded and never advances the shadow: the
    // other path does not perform this fetch, so nothing downstream may
    // rely on it.
    void chargeIcacheExtraWord(uint32_t addr);
    bool emitAluEaRg(size_t i);
    bool emitAluRgEa(size_t i);
    bool emitAddSubQ(size_t i);
    bool emitAddSubExtend(size_t i);
    bool emitMoveq(size_t i);
    bool emitImmediate(size_t i);
    bool emitBitOp(size_t i);
    bool emitLine4(size_t i);        // TST / CLR / NOT / NEG / EXT / SWAP / LEA
    bool emitBranch(size_t i);
    bool emitDbcc(size_t i);         // DBcc — terminator (census 2026-07-30)
    bool emitJmp(size_t i);          // JMP <ea> — terminator, JSR minus the push
    bool emitMovem(size_t i);        // MOVEM — straight-line, one span probe

    // ── operand plumbing ─────────────────────────────────────────────────
    bool decode(size_t i, int mode, int reg, int szIdx, int extAt, Ea& ea,
                bool allowFullDirect = false,
                bool allowFullIndirect = false);
    void addrOf(const Ea& ea, Reg dst, int szIdx);
    void addEaIndex(const Ea& ea, Reg dst);
    void addrOfFullIndirectPointer(const Ea& ea, Reg dst);
    void finishFullIndirect(const Ea& ea, Reg pointer, Reg dst);
    void commitEa(const Ea& ea, int szIdx,
                  const MemoryAccessPlan& access);
    // The decoded operands must account for EXACTLY the instruction length
    // the tracer measured. If they do not, this backend has misread the
    // encoding and every extension word it baked in is suspect.
    bool lengthOk(size_t i, int extWords) const {
        return 1 + extWords == int(ir_.instrs[i].words);
    }
    // An auto-update source changes its address register BEFORE the
    // destination address is computed. Most aliasing cases stay refused;
    // emitMove owns the transactional (An)+ -> (An)/d16(An) exception.
    static bool aliases(const Ea& src, const Ea& dst) {
        if (src.idx != EA_PI && src.idx != EA_PD) return false;
        switch (dst.idx) {
            case EA_AN: case EA_AI: case EA_PI: case EA_PD: case EA_DI:
            case EA_IX:
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
    bool loadReplayableMemory(size_t i, const Ea& ea, int szIdx, Reg dst,
                              const MemoryAccessPlan& access);
    void commitReplayableMemory(const Ea& ea, int szIdx,
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
    void flagsAddSubExtend(Reg oldZ);// ADDX/SUBX sticky Z, X=C
    void clearVC();
    void flagZFromEflags();
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
    void loadPackedCcr();
    void spillPackedCcr();
    void retire() {
        a_.aluRI(Asm::Op::ADD, packedCcr_ ? Sz::Q : Sz::L, kCnt,
                 packedCcr_ ? kRetireUnit : 1);
    }
    void spillRetiredCount();
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
    uint64_t accessPcWords(size_t i) const;
    void spillClock() { a_.movMR(Sz::Q, at(L_.clock), kClk); }
    void fillClock()  { a_.movRM(Sz::Q, kClk, at(L_.clock)); }
    // The per-instruction state mmu040InstrStart maintains and generated
    // code would otherwise skip. Both are read AFTER a block exits, by code
    // the JIT never sees, so both have to be exactly what the interpreter
    // would have left behind at that instruction boundary.
    void commitQueue(uint16_t ird, uint16_t irc);
    // What the prefetch queue holds after instruction i: neither mode-5
    // core refills at instruction end, so for a control-flow form `irc` is
    // the LAST word its fetch path consumed — the final extension word of a
    // multiword instruction (SKIP_LAST_RD leaves it in place), or the entry
    // lookahead at pc+2 for a one-word one (the queue runs a word ahead).
    // a64's `lastHeld` (JitBackendA64.cpp:2251); the x64 flavours used
    // prefetchWord(pc+2) or extensionWord(0), each right only for the
    // two-word case — the extended restart-write oracle caught JMP (xxx).L
    // holding the address HIGH half where the machine holds the low.
    uint16_t heldIrc(size_t i) const {
        const Instr& in = ir_.instrs[i];
        return in.words > 1 ? in.extensionWord(in.words - 2)
                            : ir_.prefetchWord(in.pc + 2);
    }
    // On the 030 the tracer's terminal capture must CONFIRM a control-flow
    // emitter's irc formula before it is admitted (a64:2253).
    // The admission cross-check's view of the traced cost. On the 030 the
    // traced total carries the i-cache penalties of THAT run (JIT_BRINGUP
    // § C.4: `Instr::cycles` is not the table cost there), and the emitted
    // charge reproduces them at run time — so comparing the TOTAL against
    // the table refused every instruction that happened to miss during its
    // trace: 0C42 CMPI, the d16(A7) loads, RTS, UNLK, one-word Bcc — the
    // whole 2026-08-19 fallback census. The base split is what the table
    // must match. A trace that carried post-exception cycles did not retire
    // cleanly and never matches (a64:1944's rule). Widening this beyond the
    // narrow a64 families diverged every time it was tried BEFORE the
    // charge-on-success fix (steps 31162/7798/10902) — those divergences
    // were the uncharge hole, not this rule; the retained-cache lockstep
    // now holds with the rule global.
    unsigned traced030(size_t i) const {
        const Instr& in = ir_.instrs[i];
        if (!L_.is030) return in.cycles;
        if (in.postExceptionCycles != 0) return 0xFFFFFFFFu;  // never matches
        return in.baseCycles;
    }

    // A model-required sole read owns its variable 030 data-bus delay in
    // pom68kJitRead. The native instruction must therefore charge only the
    // fixed opcode cost, just as A64's admitSoleReadTiming does. Keep this
    // deliberately narrower than the 040 timing split: only IR policy may
    // opt a 030 opcode into the exact-required path.
    bool admitSoleReadTiming(size_t i, MemoryAccessPlan& read,
                             unsigned fixedCycles) {
        const Instr& in = ir_.instrs[i];
        if (!L_.is030 || !read.valid() || !read.single() ||
            !read.exactThunk || !read.exactRequired ||
            in.postExceptionCycles != 0 || in.baseCycles < fixedCycles)
            return false;
        instructionCycles_ = uint16_t(fixedCycles);
        return true;
    }

    bool tracedQueueIs(size_t i, uint16_t irc) const {
        const Instr& in = ir_.instrs[i];
        return !L_.is030 || !in.terminalQueueValid ||
               (in.terminalIrd == in.opcode && in.terminalIrc == irc);
    }
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
    bool restartWrite030_ = false;
    uint32_t linkMask_ = 0;
    LinkCellLookup linkCell_ = nullptr;
    void* linkCellSelf_ = nullptr;
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
    uint16_t instructionCycles_ = 0; // fixed cost after exact-read splitting
    bool histo_ = false;             // POM68K_JIT_HISTO
    bool packedCcr_ = false;
    // 68030 i-cache overlay (Layout::icLive). When set, every NATIVELY
    // emitted instruction charges the model for the words it fetches; the
    // cold fallback stub charges itself, through pomJitExecOne ->
    // mmuExecuteStart -> mmuFetchWord. `icSkip_` is the block-entry branch
    // that jumps over all of it when CACR bit 0 is clear.
    bool    ic_ = false;
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
    // The runtime door needs a body of its own only for the census
    // counters now: since 2026-08-19 the i-cache charge sits after the
    // body, so a runtime bail has nothing to undo on its way out.
    bool needRuntimeDoor() const { return histo_; }
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
void Emitter::chargeIcache(size_t i, uint32_t exactFetchWords) {
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
    // The TRACED fetch count, never a guess from the length: a form whose
    // last extension is consumed under SKIP_LAST_RD fetches `words`, not
    // `words + 1` (MOVEA.L (xxx).W,An — the retained-cache lockstep's
    // counterexample, 2026-08-19). emit() refuses any instruction whose
    // trace did not capture the count.
    const uint32_t words = exactFetchWords ? exactFetchWords
                         : in.fetchWords   ? uint32_t(in.fetchWords)
                                           : uint32_t(in.words) + 1;
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

    // Optimistic aggregation — a64's scheme (JitBackendA64.cpp:365): every
    // word is counted as a hit ONCE, up front, and each word that actually
    // misses corrects the pair (hits -1, misses +1) in its own cold tail.
    // Exact whichever way each check goes, and the common case — a folded
    // guaranteed hit — costs ZERO instructions instead of an add each.
    a_.aluMI(Asm::Op::ADD, Sz::Q, at(L_.icHits), int32_t(words));

    for (uint32_t w = 0; w < words; w++) {
        const uint32_t addr = in.pc + w * 2;
        const int line = int((addr >> 4) & 15);
        const uint32_t tag = (addr >> 8) | sup;
        const uint8_t bit = uint8_t(1u << ((addr >> 2) & 3));

        // Already charged in this block, same tag, same longword: the
        // interpreter would hit, and the aggregate above already counted it.
        if (icSeen_[line] && icTag_[line] == tag && (icValid_[line] & bit))
            continue;

        Label& done = *a_.fresh();
        Label& miss = *a_.fresh();
        a_.aluMI(Asm::Op::CMP, Sz::L, at(L_.icTag + uint32_t(line) * 4),
                 int32_t(tag));
        a_.jcc(Cc::NE, miss);
        a_.testMI(Sz::B, at(L_.icValid + uint32_t(line)), bit);
        a_.jcc(Cc::NE, done);            // hit: the aggregate covered it

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
        a_.aluMI(Asm::Op::SUB, Sz::Q, at(L_.icHits), 1);
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

// The shadow half of chargeIcache alone. An instruction the backend hands
// to the interpreter still FETCHES through mmuFetchWord when it runs,
// performing exactly the line transitions the charge would have modelled —
// so the compiler's shadow must advance either way, or a later
// instruction's folded "guaranteed hit" would rest on an update this block
// never promised.
//
// (Until 2026-08-19 the charge itself was emitted BEFORE the instruction,
// paired with an `unchargeIcache` on the runtime-bail door that pre-
// subtracted the re-run's expected hit counts. That pair was exact only
// when the re-run actually happened. When pom68kJitStep DECLINED — covers
// or idle gone at that very boundary — the block exited carrying a charge
// for an instruction that never ran; if that charge had missed, a miss
// count, a tag update and a penalty came with it. The retained-cache 030
// lockstep caught it as a budget boundary cut 4 cycles and 2 fetches apart
// with every other observable equal, at trap-dispatch code the old
// flush-on-every-CACR-strobe regime never kept compiled long enough to
// expose. Charging on the SUCCESS path only removes the whole class, and
// the uncharge with it.)
// The access-thunk clock-alignment operand (see pom68kJitRead): the traced
// fetch stream of instruction i, packed for the thunk's read-only peek. 0 —
// no bias — whenever the block emits no i-cache charge (not an 030, or the
// attribution knob turned the model off) or the IR carries no traced count:
// the bias must model exactly what the end-of-body charge will charge, or
// it is a new skew, not an alignment.
uint64_t Emitter::accessPcWords(size_t i) const {
    if (!ic_ || !ir_.instrs[i].fetchWords) return 0;
    return (uint64_t(ir_.instrs[i].fetchWords) << 32) | ir_.instrs[i].pc;
}

void Emitter::chargeIcacheExtraWord(uint32_t addr) {
    if (!ic_) return;
    const uint32_t sup = ir_.super ? 0x80000000u : 0u;
    const int line = int((addr >> 4) & 15);
    const uint32_t tag = (addr >> 8) | sup;
    const uint8_t bit = uint8_t(1u << ((addr >> 2) & 3));
    a_.aluMI(Asm::Op::ADD, Sz::Q, at(L_.icFetches), 1);
    Label& gateOff = *a_.fresh();
    a_.testMI(Sz::B, at(L_.cacr), 1);
    a_.jcc(Cc::E, gateOff);
    a_.aluMI(Asm::Op::ADD, Sz::Q, at(L_.icHits), 1);
    Label& done = *a_.fresh();
    Label& miss = *a_.fresh();
    a_.aluMI(Asm::Op::CMP, Sz::L, at(L_.icTag + uint32_t(line) * 4),
             int32_t(tag));
    a_.jcc(Cc::NE, miss);
    a_.testMI(Sz::B, at(L_.icValid + uint32_t(line)), bit);
    a_.jcc(Cc::NE, done);
    a_.bind(miss);
    Label& sameTag = *a_.fresh();
    a_.aluMI(Asm::Op::CMP, Sz::L, at(L_.icTag + uint32_t(line) * 4),
             int32_t(tag));
    a_.jcc(Cc::E, sameTag);
    a_.movMI(Sz::L, at(L_.icTag + uint32_t(line) * 4), int32_t(tag));
    a_.movMI(Sz::B, at(L_.icValid + uint32_t(line)), 0);
    a_.bind(sameTag);
    a_.aluMI(Asm::Op::OR, Sz::B, at(L_.icValid + uint32_t(line)), bit);
    a_.aluMI(Asm::Op::SUB, Sz::Q, at(L_.icHits), 1);
    a_.aluMI(Asm::Op::ADD, Sz::Q, at(L_.icMisses), 1);
    a_.movRM(Sz::L, RAX, at(L_.icPenalty));
    a_.aluRR(Asm::Op::ADD, Sz::Q, kClk, RAX);
    a_.bind(done);
    a_.bind(gateOff);
}

void Emitter::shadowIcache(size_t i, uint32_t exactFetchWords) {
    if (!ic_) return;
    const Instr& in = ir_.instrs[i];
    const uint32_t words = exactFetchWords ? exactFetchWords
                         : in.fetchWords   ? uint32_t(in.fetchWords)
                                           : uint32_t(in.words) + 1;
    const uint32_t sup = ir_.super ? 0x80000000u : 0u;
    for (uint32_t w = 0; w < words; w++) {
        const uint32_t addr = in.pc + w * 2;
        const int line = int((addr >> 4) & 15);
        const uint32_t tag = (addr >> 8) | sup;
        const uint8_t bit = uint8_t(1u << ((addr >> 2) & 3));
        if (!icSeen_[line] || icTag_[line] != tag) {
            icTag_[line] = tag; icValid_[line] = 0; icSeen_[line] = true;
        }
        icValid_[line] |= bit;
    }
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
        if (linkCell_) {
            void* cell = linkCell_(linkCellSelf_, pc, ir_.super);
            Label& miss = *a_.fresh();
            a_.movRI64(RAX, uint64_t(uintptr_t(cell)));
            a_.aluMI(Asm::Op::CMP, Sz::Q, mem(RAX, 0), 0);
            a_.jcc(Cc::E, miss);
            a_.jmpM(mem(RAX, 0));
            a_.bind(miss);
            a_.movMI(Sz::L, F(kFExit), int32_t(Exit::BlockEnd));
            a_.jmp(*epilogue_);
            return;
        }
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
    // What the prefetch queue holds after any of these: heldIrc — the last
    // consumed extension for BSR.L/JSR-long forms, the pc+2 lookahead for
    // RTS and the two-word forms (where the two formulas coincide).
    const uint16_t ircAfter = heldIrc(i);

    if (sem.operation == SemanticOp::ReturnSubroutine) {
        if (!control.valid || control.kind != ControlFlowKind::Return ||
            traced030(i) != 10 || !tracedQueueIs(i, ircAfter)) return false;
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
        chargeIcache(i);                 // success path: past the last bail
        a_.aluMI(Asm::Op::ADD, Sz::L, A(7), 4);
        a_.movMR(Sz::L, at(L_.pc), RDI);
        a_.movMR(Sz::L, at(L_.pc0), RDI);
        commitQueue(op, ircAfter);
        chargeCycles(10);
        retire();
        leaveToDynamic();
        return true;
    }

    if (sem.operation == SemanticOp::BranchSubroutine) {
        // BSR prices at 7; the PC-relative JSR forms (4EBA d16(PC)) carry
        // the SAME semantics record and price at 5 (execJsr's DIPC column,
        // = kJsr below). Accept both and charge what the trace confirmed —
        // the LC II census had 4EBA at 6.8 % of block fallbacks, refused
        // by nothing but this constant (2026-08-19).
        const unsigned bsrCost = traced030(i);
        if (!control.valid || control.kind != ControlFlowKind::DirectCall ||
            !control.targetKnown || !control.pushesReturnAddress ||
            in.words > 2 || (bsrCost != 7 && bsrCost != 5) ||
            !tracedQueueIs(i, ircAfter)) return false;
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
        chargeIcache(i);                 // success path: past the last bail
        a_.movMI(Sz::L, at(L_.pc), int32_t(target));
        a_.movMI(Sz::L, at(L_.pc0), int32_t(target));
        commitQueue(op, ircAfter);
        chargeCycles(int(bsrCost));
        retire();
        leaveTo(target);
        return true;
    }

    if (sem.operation != SemanticOp::JumpSubroutine || !control.valid ||
        !control.pushesReturnAddress) return false;
    auto memory = instructionMemoryPlan(in.memory, proofOptions(L_));
    const int mode = sem.eaMode, reg = sem.eaReg;
    Ea ea;
    if (!decode(i, mode, reg, 2, 0, ea, /*allowFullDirect=*/L_.is030,
                /*allowFullIndirect=*/L_.is030)) return false;
    if (!ea.memory) return false;                    // JSR needs an address
    if (ea.idx == EA_PI || ea.idx == EA_PD) return false;   // not encodable
    if (!lengthOk(i, ea.ext)) return false;
    const bool fullIndirect = ea.fullFormat &&
                              ea.indirect != IndexIndirect::None;
    MemoryAccessPlan pointerRead;
    if (fullIndirect)
        pointerRead = memory.access(MemoryDirection::Read,
                                    MemoryOperand::Control, 4, mode, reg);
    const MemoryAccessPlan write = memory.access(
        MemoryDirection::Write, MemoryOperand::Stack, 4, 4, 7);
    if (!write.valid() ||
        (fullIndirect &&
         (!pointerRead.valid() || !pointerRead.preflight || !write.preflight ||
          memory.proof.protocol != MemoryProofProtocol::PreflightAll ||
          in.memory.order != MemoryOrder::SourceThenDestination)) ||
        !memory.complete()) return false;
    const int penalty = ea.fullFormat ? fullIndexPenalty(ea) : 0;
    const unsigned opCost = kJsrJmp[ea.idx] < 0 ? 0
        : unsigned(kJsrJmp[ea.idx] + penalty);
    if (kJsrJmp[ea.idx] < 0 || opCost != traced030(i)) return false;

    const bool constant = control.targetKnown;
    if (constant && (control.target & 1)) return false;

    // The target first: it must be known good before the stack moves,
    // because an odd one is an address error with nothing committed.
    if (fullIndirect) {
        // computeEA reads the pointer before JSR pushes. Prove both plain
        // mappings while the instruction is pristine, then perform that
        // read once; a refused stack can therefore never duplicate MMIO.
        addrOfFullIndirectPointer(ea, RAX);
        memProbe(RAX, 4, /*write=*/false, runtimeStub(i));
        a_.movMR(Sz::Q, F(kFPointerHost), RSI);
        a_.movRM(Sz::L, RAX, A(7));
        a_.aluRI(Asm::Op::SUB, Sz::L, RAX, 4);
        a_.movMR(Sz::L, F(kFSaveA), RAX);
        memProbe(RAX, 4, /*write=*/true, runtimeStub(i));
        a_.movMR(Sz::Q, F(kFProgHost), RSI);
        a_.movRM(Sz::Q, RSI, F(kFPointerHost));
        a_.movRM(Sz::L, RDI, mem(RSI, 0));
        a_.bswap(RDI);
        finishFullIndirect(ea, RDI, RAX);
    } else {
        addrOf(ea, RAX, 2);
    }
    a_.movMR(Sz::L, F(kFValue), RAX);
    if (!constant) {
        a_.testRI(Sz::L, RAX, 1);
        a_.jcc(Cc::NE, runtimeStub(i));
    }

    if (L_.is030) {
        // execJsr's order is target calculation, stack push, then target
        // program-space read.  Make it transactional without changing the
        // observable order: prove the push mapping first (no store), perform
        // the target read (a fault bails with nothing committed), then write
        // through the pointer kept across the ABI call.  The 030 MOVEM-style
        // span proof makes the direct store infallible once reached.
        if (in.terminalQueueValid && in.terminalIrd != op) return false;
        if (write.exactRequired) return false;

        if (!fullIndirect) {
            a_.movRM(Sz::L, RAX, A(7));
            a_.aluRI(Asm::Op::SUB, Sz::L, RAX, 4);
            a_.movMR(Sz::L, F(kFSaveA), RAX);
            memProbe(RAX, 4, /*write=*/true, runtimeStub(i));
            a_.movMR(Sz::Q, F(kFProgHost), RSI);
        }

        // Interpreter order is push then target read. The transactional
        // lowering reverses them, so an overlapping [SP-4,SP) / [target,
        // target+2) pair must replay before either access is observable.
        a_.movRM(Sz::L, RCX, F(kFValue));
        a_.movRM(Sz::L, RDX, F(kFSaveA));
        a_.aluRR(Asm::Op::SUB, Sz::L, RCX, RDX);
        a_.aluRI(Asm::Op::CMP, Sz::L, RCX, 3);
        a_.jcc(Cc::BE, runtimeStub(i));
        a_.movRM(Sz::L, RCX, F(kFSaveA));
        a_.movRM(Sz::L, RDX, F(kFValue));
        a_.aluRR(Asm::Op::SUB, Sz::L, RCX, RDX);
        a_.aluRI(Asm::Op::CMP, Sz::L, RCX, 1);
        a_.jcc(Cc::BE, runtimeStub(i));

        spillClock();
        a_.movRR(Sz::Q, RDI, kCpu);
        a_.movRM(Sz::L, RSI, F(kFValue));
        a_.leaRM(RDX, F(kFScratch));
        call(reinterpret_cast<void*>(&pom68kJitReadProg));
        fillClock();
        a_.testRR(Sz::L, RAX, RAX);
        a_.jcc(Cc::E, runtimeStub(i));

        a_.movRI(RDX, control.returnAddress);
        a_.bswap(RDX);
        a_.movRM(Sz::Q, RSI, F(kFProgHost));
        a_.movMR(Sz::L, mem(RSI, 0), RDX);
        a_.movRM(Sz::L, RAX, F(kFSaveA));
        a_.movMR(Sz::L, A(7), RAX);

        a_.movRM(Sz::L, RDI, F(kFValue));
        a_.movMR(Sz::L, at(L_.pc), RDI);
        a_.movMR(Sz::L, at(L_.pc0), RDI);
        chargeIcache(i);
        if (L_.ird == L_.irc + 2) {
            a_.movzx(Sz::W, RAX, F(kFScratch));
            a_.aluRI(Asm::Op::OR, Sz::L, RAX, int32_t(uint32_t(op) << 16));
            a_.movMR(Sz::L, at(L_.irc), RAX);
        } else {
            a_.movMI(Sz::W, at(L_.ird), int32_t(op));
            a_.movRM(Sz::W, RAX, F(kFScratch));
            a_.movMR(Sz::W, at(L_.irc), RAX);
        }
        chargeCycles(int(opCost));
        retire();
        if (constant) leaveTo(control.target);
        else          leaveToDynamic();
        return true;
    }

    if (!tracedQueueIs(i, ircAfter)) return false;
    // Push the return address — the next instruction, since computeEA has
    // already consumed this one's extension words.
    a_.movRM(Sz::L, RAX, A(7));
    a_.aluRI(Asm::Op::SUB, Sz::L, RAX, 4);
    a_.movMR(Sz::L, F(kFSaveA), RAX);
    a_.movRI(RDI, control.returnAddress);
    memStore(RAX, 2, RDI, write);
    a_.movRM(Sz::L, RAX, F(kFSaveA));
    a_.movMR(Sz::L, A(7), RAX);
    chargeIcache(i);                     // success path: past the last bail

    a_.movRM(Sz::L, RDI, F(kFValue));
    a_.movMR(Sz::L, at(L_.pc), RDI);
    a_.movMR(Sz::L, at(L_.pc0), RDI);
    commitQueue(op, ircAfter);
    chargeCycles(int(opCost));           // table/base cost; the emitted
                                         // i-cache charge owns the misses
    retire();
    if (constant) leaveTo(control.target);
    else          leaveToDynamic();
    return true;
}

void Emitter::call(void* fn) {
    // Helpers execute against the canonical Moira object. Publish a deferred
    // CCR before they can observe it (fault frame, SR read, interpreter
    // fallback), then accept any CCR they produced on return. The retired
    // count in r15's upper bits survives both operations and the ABI call.
    if (packedCcr_) spillPackedCcr();
    a_.movRI64(RAX, uint64_t(uintptr_t(fn)));
    a_.callR(RAX);
    if (packedCcr_) loadPackedCcr();
}

void Emitter::loadPackedCcr() {
    // Preserve the accumulated count and replace only XNZVC. All flag bytes
    // are canonical booleans, so zero-extension plus a shift is sufficient.
    a_.aluRI(Asm::Op::AND, Sz::Q, kCnt, ~kCcrMask);
    const auto add = [&](uint32_t off, uint8_t bit) {
        a_.movzx(Sz::B, RAX, at(off));
        if (bit) a_.shiftRI(Sz::L, RAX, 4, bit);
        a_.aluRR(Asm::Op::OR, Sz::Q, kCnt, RAX);
    };
    add(L_.srC, 0); add(L_.srV, 1); add(L_.srZ, 2);
    add(L_.srN, 3); add(L_.srX, 4);
}

void Emitter::spillPackedCcr() {
    const auto put = [&](uint32_t off, uint8_t bit) {
        a_.movRR(Sz::L, RAX, kCnt);
        if (bit) a_.shiftRI(Sz::L, RAX, 5, bit);
        a_.aluRI(Asm::Op::AND, Sz::L, RAX, 1);
        a_.movMR(Sz::B, at(off), RAX);
    };
    put(L_.srC, 0); put(L_.srV, 1); put(L_.srZ, 2);
    put(L_.srN, 3); put(L_.srX, 4);
}

void Emitter::spillRetiredCount() {
    if (!packedCcr_) {
        a_.movMR(Sz::L, F(kFInstrs), kCnt);
        return;
    }
    a_.movRR(Sz::Q, RAX, kCnt);
    a_.shiftRI(Sz::Q, RAX, 5, kCountShift);
    a_.movMR(Sz::L, F(kFInstrs), RAX);
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
    if (packedCcr_) {
        a_.aluRI(Asm::Op::AND, Sz::Q, kCnt, ~3);
        return;
    }
    a_.movMI(Sz::B, at(L_.srV), 0);
    a_.movMI(Sz::B, at(L_.srC), 0);
}

// 68k logical/move operations: N and Z from the result at its own width,
// V and C cleared, X untouched. x86 sets SF/ZF from the same width, so the
// two setcc do the whole job.
void Emitter::flagsLogic(Sz) {
    if (packedCcr_) {
        // SETcc does not alter EFLAGS: capture both host flags before the
        // mask/shift sequence consumes them. RCX/RDX are caller-saved
        // scratch registers and no flag emitter promises to preserve them.
        a_.setccR(Cc::S, RCX);
        a_.setccR(Cc::E, RDX);
        a_.movzxRR(Sz::B, RCX, RCX);
        a_.movzxRR(Sz::B, RDX, RDX);
        a_.shiftRI(Sz::L, RCX, 4, 3);
        a_.shiftRI(Sz::L, RDX, 4, 2);
        a_.aluRI(Asm::Op::AND, Sz::Q, kCnt, ~0x0F); // preserve X + count
        a_.aluRR(Asm::Op::OR, Sz::Q, kCnt, RCX);
        a_.aluRR(Asm::Op::OR, Sz::Q, kCnt, RDX);
        return;
    }
    a_.setccM(Cc::S, at(L_.srN));
    a_.setccM(Cc::E, at(L_.srZ));
    clearVC();
}

// 68k add/subtract: N,Z,V,C map one-to-one onto SF,ZF,OF,CF for operands of
// the same width — including the carry, because x86's CF after SUB is a
// borrow, which is what the 68k calls C. X follows C.
void Emitter::flagsAddSub(bool withX) {
    if (packedCcr_) {
        a_.setccR(Cc::S, RCX);
        a_.setccR(Cc::E, RDX);
        a_.setccR(Cc::O, R8);
        a_.setccR(Cc::B, R9);
        a_.movzxRR(Sz::B, RCX, RCX);
        a_.movzxRR(Sz::B, RDX, RDX);
        a_.movzxRR(Sz::B, R8, R8);
        a_.movzxRR(Sz::B, R9, R9);
        a_.shiftRI(Sz::L, RCX, 4, 3);
        a_.shiftRI(Sz::L, RDX, 4, 2);
        a_.shiftRI(Sz::L, R8, 4, 1);
        a_.aluRI(Asm::Op::AND, Sz::Q, kCnt, withX ? ~0x1F : ~0x0F);
        a_.aluRR(Asm::Op::OR, Sz::Q, kCnt, RCX);
        a_.aluRR(Asm::Op::OR, Sz::Q, kCnt, RDX);
        a_.aluRR(Asm::Op::OR, Sz::Q, kCnt, R8);
        a_.aluRR(Asm::Op::OR, Sz::Q, kCnt, R9);
        if (withX) {
            a_.shiftRI(Sz::L, R9, 4, 4);
            a_.aluRR(Asm::Op::OR, Sz::Q, kCnt, R9);
        }
        return;
    }
    a_.setccM(Cc::S, at(L_.srN));
    a_.setccM(Cc::E, at(L_.srZ));
    a_.setccM(Cc::O, at(L_.srV));
    a_.setccM(Cc::B, at(L_.srC));
    if (withX) a_.setccM(Cc::B, at(L_.srX));
}

// Consume the still-live ADC/SBB flags. Unlike ordinary ADD/SUB, ADDX/SUBX
// keep Z clear once any limb in a multi-precision chain was non-zero.
void Emitter::flagsAddSubExtend(Reg oldZ) {
    a_.setccR(Cc::S, RCX);
    a_.setccR(Cc::E, RAX);
    a_.setccR(Cc::O, R8);
    a_.setccR(Cc::B, R9);
    a_.movzxRR(Sz::B, RCX, RCX);
    a_.movzxRR(Sz::B, RAX, RAX);
    a_.movzxRR(Sz::B, R8, R8);
    a_.movzxRR(Sz::B, R9, R9);
    a_.aluRR(Asm::Op::AND, Sz::L, RAX, oldZ);
    if (packedCcr_) {
        a_.shiftRI(Sz::L, RCX, 4, 3);
        a_.shiftRI(Sz::L, RAX, 4, 2);
        a_.shiftRI(Sz::L, R8, 4, 1);
        a_.aluRI(Asm::Op::AND, Sz::Q, kCnt, ~0x1F);
        a_.aluRR(Asm::Op::OR, Sz::Q, kCnt, RCX);
        a_.aluRR(Asm::Op::OR, Sz::Q, kCnt, RAX);
        a_.aluRR(Asm::Op::OR, Sz::Q, kCnt, R8);
        a_.aluRR(Asm::Op::OR, Sz::Q, kCnt, R9);
        a_.shiftRI(Sz::L, R9, 4, 4);
        a_.aluRR(Asm::Op::OR, Sz::Q, kCnt, R9);
        return;
    }
    a_.movMR(Sz::B, at(L_.srN), RCX);
    a_.movMR(Sz::B, at(L_.srZ), RAX);
    a_.movMR(Sz::B, at(L_.srV), R8);
    a_.movMR(Sz::B, at(L_.srC), R9);
    a_.movMR(Sz::B, at(L_.srX), R9);
}

void Emitter::flagZFromEflags() {
    if (!packedCcr_) {
        a_.setccM(Cc::E, at(L_.srZ));
        return;
    }
    a_.setccR(Cc::E, RCX);
    a_.movzxRR(Sz::B, RCX, RCX);
    a_.shiftRI(Sz::L, RCX, 4, 2);
    a_.aluRI(Asm::Op::AND, Sz::Q, kCnt, ~4);
    a_.aluRR(Asm::Op::OR, Sz::Q, kCnt, RCX);
}

// Leaves ZF SET when the 68k condition `cc` is TRUE, so the caller can
// branch with `je`. Condition codes are read out of Moira's per-flag bytes.
void Emitter::condToAl(int cc) {
    if (packedCcr_) {
        const auto bitEquals = [&](uint8_t bit, uint8_t value) {
            a_.movRR(Sz::L, RAX, kCnt);
            if (bit) a_.shiftRI(Sz::L, RAX, 5, bit);
            a_.aluRI(Asm::Op::AND, Sz::L, RAX, 1);
            a_.aluRI(Asm::Op::CMP, Sz::L, RAX, value);
        };
        const auto nvOrZ = [&] {
            // RAX = Z || (N xor V), represented in bit 1. Zero means GT.
            a_.movRR(Sz::L, RAX, kCnt);
            a_.shiftRI(Sz::L, RAX, 5, 1);           // Z -> bit 1
            a_.aluRI(Asm::Op::AND, Sz::L, RAX, 2);
            a_.movRR(Sz::L, RDX, kCnt);
            a_.shiftRI(Sz::L, RDX, 5, 2);           // N -> bit 1
            a_.aluRR(Asm::Op::XOR, Sz::L, RDX, kCnt);
            a_.aluRI(Asm::Op::AND, Sz::L, RDX, 2);  // N xor V
            a_.aluRR(Asm::Op::OR, Sz::L, RAX, RDX);
        };
        switch (cc) {
            case 0: a_.aluRR(Asm::Op::XOR, Sz::L, RAX, RAX);
                    a_.testRR(Sz::L, RAX, RAX); return;
            case 1: a_.movRI(RAX, 1); a_.testRR(Sz::L, RAX, RAX); return;
            case 2: a_.testRI(Sz::L, kCnt, 0x05); return;       // HI
            case 3: a_.movRR(Sz::L, RAX, kCnt);
                    a_.aluRI(Asm::Op::AND, Sz::L, RAX, 0x05);
                    a_.aluRI(Asm::Op::CMP, Sz::L, RAX, 0);
                    a_.setccR(Cc::NE, RAX);
                    a_.aluRI(Asm::Op::CMP, Sz::B, RAX, 1); return;
            case 4: bitEquals(0, 0); return;                    // CC
            case 5: bitEquals(0, 1); return;                    // CS
            case 6: bitEquals(2, 0); return;                    // NE
            case 7: bitEquals(2, 1); return;                    // EQ
            case 8: bitEquals(1, 0); return;                    // VC
            case 9: bitEquals(1, 1); return;                    // VS
            case 10: bitEquals(3, 0); return;                   // PL
            case 11: bitEquals(3, 1); return;                   // MI
            case 12:                                           // GE: N == V
            case 13:                                           // LT: N != V
                a_.movRR(Sz::L, RAX, kCnt);
                a_.shiftRI(Sz::L, RAX, 5, 2);
                a_.aluRR(Asm::Op::XOR, Sz::L, RAX, kCnt);
                a_.aluRI(Asm::Op::AND, Sz::L, RAX, 2);
                a_.aluRI(Asm::Op::CMP, Sz::L, RAX, cc == 12 ? 0 : 2);
                return;
            case 14: nvOrZ(); a_.aluRI(Asm::Op::CMP, Sz::L, RAX, 0); return;
            default: nvOrZ();
                     a_.setccR(Cc::NE, RAX);
                     a_.aluRI(Asm::Op::CMP, Sz::B, RAX, 1); return;
        }
    }
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
bool Emitter::decode(size_t i, int mode, int reg, int szIdx, int extAt, Ea& ea,
                     bool allowFullDirect, bool allowFullIndirect) {
    // Shared admission wrapper (JitEaPlan.h). Call sites must opt into
    // memory indirection only when they also implement its ordered pointer
    // access; ordinary operands retain the conservative default refusal.
    return decodeEaPlan(ir_.instrs[i], mode, reg, szIdx, extAt, ea,
                        allowFullDirect, allowFullIndirect);
}

// Guest effective address into `dst` (brief indexed forms also clobber RDX;
// no caller passes RDX as `dst`). For (An)+ and -(An) the register update is
// NOT applied here: it is a guest-visible commit and must not happen until
// the access has succeeded (see commitEa).
void Emitter::addrOf(const Ea& ea, Reg dst, int szIdx) {
    const int step = (ea.reg == 7 && szIdx == 0) ? 2 : sizeBytes(szIdx);
    switch (ea.idx) {
        case EA_AI: case EA_PI:
            a_.movRM(Sz::L, dst, A(ea.reg));
            return;
        case EA_PD:
            a_.movRM(Sz::L, dst, A(ea.reg));
            a_.aluRI(Asm::Op::SUB, Sz::L, dst, step);
            return;
        case EA_DI:
            a_.movRM(Sz::L, dst, A(ea.reg));
            if (ea.value) a_.aluRI(Asm::Op::ADD, Sz::L, dst, ea.value);
            return;
        case EA_IX: case EA_IXPC: {
            if (ea.fullFormat && ea.baseSuppressed) a_.movRI(dst, 0);
            else if (ea.idx == EA_IX) a_.movRM(Sz::L, dst, A(ea.reg));
            else a_.movRI(dst, ea.base);
            if (!ea.fullFormat || !ea.indexSuppressed) {
                if (ea.ixReg < 8) {
                    if (ea.ixLong) a_.movRM(Sz::L, RDX, D(ea.ixReg));
                    else a_.movsx(Sz::W, RDX, D(ea.ixReg));
                } else {
                    if (ea.ixLong) a_.movRM(Sz::L, RDX, A(ea.ixReg - 8));
                    else a_.movsx(Sz::W, RDX, A(ea.ixReg - 8));
                }
                if (ea.ixShift) a_.shiftRI(Sz::L, RDX, 4, uint8_t(ea.ixShift));
                a_.aluRR(Asm::Op::ADD, Sz::L, dst, RDX);
            }
            const int32_t displacement =
                ea.fullFormat ? ea.baseDisplacement : ea.value;
            if (displacement)
                a_.aluRI(Asm::Op::ADD, Sz::L, dst, displacement);
            return;
        }
        case EA_AW: case EA_AL: case EA_DIPC:
            a_.movRI(dst, uint32_t(ea.value));
            return;
        default:
            return;
    }
}

void Emitter::addEaIndex(const Ea& ea, Reg dst) {
    if (ea.indexSuppressed) return;
    if (ea.ixReg < 8) {
        if (ea.ixLong) a_.movRM(Sz::L, RDX, D(ea.ixReg));
        else a_.movsx(Sz::W, RDX, D(ea.ixReg));
    } else {
        if (ea.ixLong) a_.movRM(Sz::L, RDX, A(ea.ixReg - 8));
        else a_.movsx(Sz::W, RDX, A(ea.ixReg - 8));
    }
    if (ea.ixShift) a_.shiftRI(Sz::L, RDX, 4, uint8_t(ea.ixShift));
    a_.aluRR(Asm::Op::ADD, Sz::L, dst, RDX);
}

// Address of the data-space pointer used by a full-format memory-indirect
// EA. Preindexed forms add Xn here; postindexed forms add it only after the
// big-endian pointer longword has been read.
void Emitter::addrOfFullIndirectPointer(const Ea& ea, Reg dst) {
    if (ea.baseSuppressed) a_.movRI(dst, 0);
    else if (ea.idx == EA_IX) a_.movRM(Sz::L, dst, A(ea.reg));
    else a_.movRI(dst, ea.base);
    if (ea.indirect == IndexIndirect::Preindexed) addEaIndex(ea, dst);
    if (ea.baseDisplacement)
        a_.aluRI(Asm::Op::ADD, Sz::L, dst, ea.baseDisplacement);
}

void Emitter::finishFullIndirect(const Ea& ea, Reg pointer, Reg dst) {
    a_.movRR(Sz::L, dst, pointer);
    if (ea.indirect == IndexIndirect::Postindexed) addEaIndex(ea, dst);
    if (ea.outerDisplacement)
        a_.aluRI(Asm::Op::ADD, Sz::L, dst, ea.outerDisplacement);
}

void Emitter::commitEa(const Ea& ea, int szIdx,
                       const MemoryAccessPlan& access) {
    // x64 keeps the instruction boundary transactional: even a semantic
    // BeforeAccess update is published only after every fallible native
    // operation has succeeded. A fault replays the pristine instruction in
    // Moira. The contract still authorises whether an EA commit exists.
    if (access.eaCommit == EaCommit::None) return;
    const int step = (ea.reg == 7 && szIdx == 0) ? 2 : sizeBytes(szIdx);
    if (ea.idx == EA_PI) a_.aluMI(Asm::Op::ADD, Sz::L, A(ea.reg), step);
    else if (ea.idx == EA_PD) a_.aluMI(Asm::Op::SUB, Sz::L, A(ea.reg), step);
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

    if (!access.exactRequired) {
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
    } else if (exactAccess) {
        // The device delay is semantically part of this read even when a
        // host mapping happens to exist. Always take the exact thunk.
        a_.jmp(miss);
    } else {
        a_.jmp(runtimeStub(cur_));
        return;
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
        a_.movRI64(R8, accessPcWords(idx));
        call(reinterpret_cast<void*>(&pom68kJitRead));
        fillClock();
        a_.testRR(Sz::L, RAX, RAX);
        a_.jcc(Cc::E, runtimeStub(idx));       // it faulted: nothing was committed
        a_.movRM(Sz::L, dst, F(kFValue)); // already host-ordered
        a_.jmp(done);
    });
}

// Speculative source for a trap-capable instruction. A plain host mapping is
// side-effect free and can be read again by Moira on a later semantic bail;
// with the 040 D-cache active, only a published resident line is admissible.
// Its diagnostic hit is deferred to commitReplayableMemory().
bool Emitter::loadReplayableMemory(size_t i, const Ea& ea, int szIdx, Reg dst,
                                   const MemoryAccessPlan& access) {
    if (!replayableSpeculativeRead(access) ||
        access.bytes != unsigned(sizeBytes(szIdx)) ||
        (L_.cache040Live && !access.cache))
        return false;
    addrOf(ea, RAX, szIdx);
    if (L_.cache040Live)
        memProbe(RAX, sizeBytes(szIdx), false, runtimeStub(i),
                 /*cacheRead=*/true, /*cacheWriteHit=*/nullptr,
                 /*cacheOnly=*/true, /*countCacheHit=*/false);
    else
        memProbe(RAX, sizeBytes(szIdx), false, runtimeStub(i));
    if (szIdx == 1) {
        a_.movzx(Sz::W, dst, mem(RSI, 0));
        a_.rolR16(dst, 8);
    } else {
        a_.movRM(Sz::L, dst, mem(RSI, 0));
        a_.bswap(dst);
    }
    return true;
}

void Emitter::commitReplayableMemory(const Ea& ea, int szIdx,
                                     const MemoryAccessPlan& access) {
    commitEa(ea, szIdx, access);
    if (!L_.cache040Live) return;
    a_.aluMI(Asm::Op::ADD, Sz::Q, at(L_.cache040Hits), 1);
    if (cache040LineReadStatsEnabled())
        a_.aluMI(Asm::Op::ADD, Sz::Q, at(L_.cache040NativeReadHits), 1);
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
    // The cold lambda runs after this frame is gone: carry the rollback by
    // value, never by pointer.
    cold_.push_back([this, &miss, &done, addr, szIdx, idx] {
        a_.bind(miss);
        spillClock();                    // a device access can stall the CPU
        a_.movRR(Sz::Q, RDI, kCpu);
        a_.movRR(Sz::L, RSI, addr);
        a_.movRI(RDX, uint32_t(sizeBytes(szIdx)));
        a_.movRM(Sz::L, RCX, F(kFSaveV));
        a_.movRI64(R8, accessPcWords(idx));
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
        // …and because it re-enters at THIS instruction's pc, the
        // instruction is about to be run a second time. Its 68030 i-cache
        // charge sits AFTER the body on the success path (2026-08-19), so
        // at this exit nothing has been charged and nothing needs undoing —
        // the re-run's own mmuFetchWord charge is the one that counts.
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
        case EA_DN:
            if (signExtend && szIdx != 2) {
                a_.movsx(hostSz(szIdx), dst, D(ea.reg));
            } else {
                a_.movRM(Sz::L, dst, D(ea.reg));
            }
            return;
        case EA_AN:
            if (signExtend && szIdx == 1) a_.movsx(Sz::W, dst, A(ea.reg));
            else a_.movRM(Sz::L, dst, A(ea.reg));
            return;
        case EA_IM:
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
        case EA_DN: a_.movMR(hostSz(szIdx), D(ea.reg), src); return;
        case EA_AN: a_.movMR(Sz::L, A(ea.reg), src); return;
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
    if (!decode(i, srcMode, srcReg, szIdx, 0, src,
                /*allowFullDirect=*/false,
                /*allowFullIndirect=*/L_.is030)) return false;
    if (!decode(i, dstMode, dstReg, szIdx, src.ext, dst)) return false;
    if (src.idx == EA_AN && szIdx == 0) return false;     // MOVE.B An is illegal
    // A destination has to be writable: not an immediate, not PC-relative.
    if (dst.idx == EA_IM || dst.idx == EA_DIPC) return false;
    const bool dependentPostincDestination =
        src.idx == EA_PI && src.reg == dst.reg &&
        (dst.idx == EA_AI || dst.idx == EA_DI);
    if (aliases(src, dst) && !dependentPostincDestination) return false;
    if (!lengthOk(i, src.ext + dst.ext)) return false;
    const bool indirectSource = src.fullFormat &&
                                src.indirect != IndexIndirect::None;
    // Speedometer's measured family is MOVE.L/MOVEA.L from a full-indirect
    // source into a register. Memory destinations need a third proof slot;
    // byte/word timing and MOVEA sign extension are intentionally unopened.
    if (indirectSource && (szIdx != 2 || dst.memory)) return false;

    // a64's proven restartable-write family: register/immediate source into
    // (An)/(An)+/-(An)/d16(An)/brief-indexed/absolute, marked by the IR's
    // own proof — the emitter never re-derives it from the opcode. The
    // restartable option is SCOPED to this second plan (see proofOptions):
    // when the instruction is in-family, the family-aware plan replaces the
    // baseline one wholesale, so its accesses carry the restart fault
    // phase the thunk contract needs; out of family, the baseline plans —
    // the ones every green run was proved with — stay untouched.
    bool restartWrite = false;
    if (L_.is030 && dst.memory) {
        MemoryProofOptions o2 = proofOptions(L_);
        o2.restartableWriteRequired = true;
        auto memoryR = instructionMemoryPlan(in.memory, o2);
        if (memoryR.proof.restartableLastWrite()) {
            restartWrite = true;
            memory = memoryR;
        }
    }
    const int sourcePenalty = indirectSource ? fullIndexPenalty(src) : 0;
    const int rc = kEaRead[src.idx][szIdx] < 0
                 ? kEaRead[src.idx][szIdx]
                 : kEaRead[src.idx][szIdx] + sourcePenalty;
    // Brief-indexed destination calculation costs five base cycles in the
    // restartable family (a64:1268). Speedometer's 2191/31A9 forms add the
    // independently proved memory-source pair: both mappings are checked by
    // the PreflightAll body before it performs the source read. Full-format
    // destinations remain behind decode()'s default refusal.
    const bool preflightIndexedPair = L_.is030 && src.memory &&
                                      dst.idx == EA_IX;
    const int dc = ((restartWrite || preflightIndexedPair) &&
                    dst.idx == EA_IX) ? 5 : kMoveDst[dst.idx];
    if (rc < 0 || dc < 0) return false;
    const int cycles = rc + dc;
    // On an 030 the traced total carries the i-cache penalty of THAT run
    // (JIT_BRINGUP § C.4: `Instr::cycles` is not the table cost there), so
    // comparing it against the table refuses every instruction that missed
    // during its trace — a motive foreign to its cost. Consume the split
    // base cost for exactly the families a64 proved: a postincrement source
    // whose sole access is a read into a register, and the restartable
    // writes above. Wider admissions have DIVERGED every time (steps
    // 31162/7798/10902); mirror, don't extend.
    // The RESTARTABLE-WRITE family keeps the TOTAL-cost check — i.e. a
    // restartable MOVE whose trace carried an i-cache miss stays refused.
    // Its base admission used to diverge the 120k gate (step 19658) and
    // that divergence is CLOSED: it was the peripheral-phase class — the
    // admission moved IRQ-handler delay loops into native blocks, whose
    // I/O accesses flushed device time at a clock missing the fetch
    // penalty (JIT_BRINGUP § C.4nonies; the access thunks now bias the
    // clock for the access alone, and jit_lockstep_030_x64_alignment_test
    // holds the proof). The admission's default rides the backend's
    // accessClockBias declaration (ON here since 2026-08-22 on the
    // measured −4.3 % / −8.0 % evidence); the check below is the knob.
    const unsigned tracedMove =
        restartWrite && !restartBaseAdmission()
            ? unsigned(ir_.instrs[i].cycles) : traced030(i);

    MemoryAccessPlan pointerAccess, srcAccess, dstAccess;
    if (src.memory) {
        if (indirectSource)
            pointerAccess = memory.access(MemoryDirection::Read,
                                          MemoryOperand::Control, 4,
                                          uint8_t(srcMode), uint8_t(srcReg));
        srcAccess = memory.access(MemoryDirection::Read,
                                  MemoryOperand::Source,
                                  uint8_t(sizeBytes(szIdx)),
                                  uint8_t(srcMode), uint8_t(srcReg));
        if (!srcAccess.valid() ||
            (indirectSource && !pointerAccess.valid())) return false;
    }
    if (dst.memory) {
        dstAccess = memory.access(MemoryDirection::Write,
                                  MemoryOperand::Destination,
                                  uint8_t(sizeBytes(szIdx)),
                                  uint8_t(dstMode), uint8_t(dstReg));
        if (!dstAccess.valid()) return false;
    }
    if (!memory.complete()) return false;
    if (indirectSource) {
        if (memory.proof.protocol != MemoryProofProtocol::PreflightAll ||
            !pointerAccess.preflight || !srcAccess.preflight ||
            in.memory.order != MemoryOrder::Sequential ||
            pointerAccess.exactRequired || srcAccess.exactRequired)
            return false;
        // The pair is all-or-replay. Refuse a non-plain pointer or final
        // operand before changing the destination register, and let the
        // untouched instruction execute through Moira's exact MMIO path.
        pointerAccess.exactThunk = false;
        pointerAccess.cache = false;
        srcAccess.exactThunk = false;
        srcAccess.cache = false;
    }
    const bool soleReadTiming = src.memory && !indirectSource && !dst.memory &&
        admitSoleReadTiming(i, srcAccess, unsigned(cycles));
    if (unsigned(cycles) != tracedMove && !soleReadTiming) return false;

    const bool movea = (dst.idx == EA_AN);
    const bool cachePair = memory.proof.atomicCachePair();
    // The destination of Speedometer's 3F5F/2F5F uses A7 after the source
    // postincrement. Compute that value speculatively for the second probe,
    // but publish A7 only after both direct-RAM accesses have succeeded. Any
    // MMIO, code guard, crossing or /BERR mapping therefore reaches Moira
    // with a pristine instruction boundary.
    if (dependentPostincDestination) {
        if (memory.proof.protocol != MemoryProofProtocol::PreflightAll ||
            !srcAccess.preflight || !dstAccess.preflight ||
            in.memory.order != MemoryOrder::SourceThenDestination)
            return false;
        const int step = (src.reg == 7 && szIdx == 0) ? 2 : sizeBytes(szIdx);
        addrOf(src, RAX, szIdx);
        memProbe(RAX, sizeBytes(szIdx), false, runtimeStub(i));
        a_.movMR(Sz::Q, F(kFValue), RSI);

        a_.movRM(Sz::L, RAX, A(src.reg));
        a_.aluRI(Asm::Op::ADD, Sz::L, RAX, step);
        if (dst.idx == EA_DI && dst.value)
            a_.aluRI(Asm::Op::ADD, Sz::L, RAX, dst.value);
        memProbe(RAX, sizeBytes(szIdx), true, runtimeStub(i));
        a_.movRR(Sz::Q, R11, RSI);
        a_.movRM(Sz::Q, R10, F(kFValue));

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
        return true;
    }
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
    if (indirectSource) {
        addrOfFullIndirectPointer(src, RAX);
        memLoad(RAX, 2, RDI, pointerAccess);
        finishFullIndirect(src, RDI, RAX);
        memLoad(RAX, szIdx, RDI, srcAccess);
    } else {
        load(src, szIdx, RDI, srcAccess, movea);
    }
    if (movea) {
        a_.movMR(Sz::L, A(dst.reg), RDI);
    } else {
        // The flags come from the value itself; test it at its own width.
        // On the 030 this order is load-bearing: Moira publishes N/Z/V/C
        // BEFORE writeOp's LASTWRITE access, so an exact MMIO callback and
        // a fault frame both observe the final CCR (a64:1377).
        a_.testRR(hostSz(szIdx), RDI, RDI);
        flagsLogic(hostSz(szIdx));
        // (An)+ under an exact-access contract is CONSERVATIVE: its update
        // is architecturally before the access, no rollback door can make
        // the thunk's replay pristine for a POST-incremented register the
        // handler already observed, so the instruction is not emitted at
        // all — the in-block stub hands it to Moira, which owns MMIO and
        // /BERR alike with perfect mid-write state (the oracle pins
        // writeFaults == 1 for this form, == 2 for the thunked ones).
        if (L_.is030 && dst.idx == EA_PI && dstAccess.exactThunk)
            return false;
        if (restartWrite && dstAccess.exactThunk && dst.idx != EA_PI) {
            // At Moira's last-write point reg.pc has consumed every
            // extension while pc0 still names this instruction, and the
            // queue holds the tracer's terminal capture. An exact MMIO
            // thunk exposes all of it mid-block, so materialise the
            // running boundary before the store (a64:1385-1397).
            const uint32_t nextPc = in.pc + uint32_t(in.words) * 2;
            a_.movMI(Sz::L, at(L_.pc), int32_t(nextPc));
            a_.movMI(Sz::L, at(L_.pc0), int32_t(in.pc));
            commitQueue(in.terminalQueueValid ? in.terminalIrd : in.opcode,
                        in.terminalQueueValid ? in.terminalIrc
                                              : ir_.prefetchWord(nextPc));
        }
        // -(An) deliberately takes the SAME door as every other memory
        // destination: thunk on exact pages, tail commit after. The a64
        // commits the predecrement BEFORE the access so the device handler
        // observes it moved; two x64 ports of that contract (pre-commit,
        // then commit-on-both-doors) each diverged the real LC II lockstep
        // at an 8192-cycle peripheral boundary (~24k steps, 2026-08-18)
        // while the fault-frame oracle pins nothing about a PD callback's
        // A6 — its PD case checks fault count and frame bytes, both of
        // which the pristine replay already satisfies. Reopen only with an
        // oracle check that a real device write observes the committed An,
        // and bring the peripheral-phase trace from that bisect with you.
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
    // The full-format DIRECT source opts in on the AddressAlu path only —
    // `ADDA.L (bd.L,ZAn,Xn),An`, the D1F0 form that is 53 % of all
    // fallbacks under SimCity 2000. Which sub-form is admitted and what it
    // costs beyond the brief price are the 68k's, so both live in
    // JitCost.h (`fullFormatReadExtra`), read here and by the a64 backend.
    if (!decode(i, mode, reg, szIdx, 0, src, /*allowFullDirect=*/isAddr))
        return false;
    if (src.idx == EA_AN && szIdx == 0) return false;
    int fullFormatExtra = 0;
    if (src.fullFormat) {
        fullFormatExtra = fullFormatReadExtra(src);
        if (fullFormatExtra < 0) return false;
    }
    const int rc = kEaRead[src.idx][szIdx] < 0
                 ? kEaRead[src.idx][szIdx]
                 : kEaRead[src.idx][szIdx] + fullFormatExtra;
    if (rc < 0) return false;
    if (!lengthOk(i, src.ext)) return false;
    // ADDA/SUBA charge exactly the register forms on the 68020 (execAdda's
    // 020 column IS kEaRead). CMPA does NOT: execCmpa's own column is
    // kEaRead + 2 in every mode, byte, word and long alike — it holds a
    // SYNC(2) that the ADDA path only takes for a word or a register source
    // (MoiraExec_cpp.h:2129 vs :421-423). Charging both alike refused every
    // single CMPA; on the idle Finder that was 1.04 M fallbacks, 12 % of all
    // of them, second only to the MOVE cost cell fixed above (2026-08-09).
    const int cost = (isAddr && isCmp) ? rc + kCmpaExtraCycles : rc;
    if (cost < 0 || unsigned(cost) != traced030(i)) return false;
    if (isAddr) {
        Ea an; an.idx = EA_AN; an.reg = dn;
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
    if (dst.idx == EA_AN || dst.idx == EA_IM || dst.idx == EA_DIPC) return false;
    if (!lengthOk(i, dst.ext)) return false;
    const int cycles = eaRmwCost(dst.idx, szIdx);
    if (cycles < 0 || unsigned(cycles) != traced030(i)) return false;

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

    if (dst.idx == EA_DN) {
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
    if (dst.idx == EA_IM || dst.idx == EA_DIPC) return false;
    if (!lengthOk(i, dst.ext)) return false;
    const int cycles = eaRmwCost(dst.idx, szIdx);
    if (cycles < 0 || unsigned(cycles) != traced030(i)) return false;

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

    if (dst.idx == EA_AN) {
        // Address-register form: full width whatever the size, no flags.
        a_.aluMI(x86op, Sz::L, A(dst.reg), imm);
        return true;
    }
    if (dst.idx == EA_DN) {
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

// ADDX/SUBX Dn,Dn. NEG of the canonical 0/1 X value seeds x86 CF exactly;
// ADC/SBB then provide the guest-width N/V/C result for byte, word and long.
bool Emitter::emitAddSubExtend(size_t i) {
    const Instr& in = ir_.instrs[i];
    const InstructionSemantics& sem = in.semantics;
    if (sem.operation != SemanticOp::AddSubExtend || sem.sizeIndex > 2 ||
        in.words != 1 || traced030(i) != 2 ||
        (sem.alu != AluOperation::Add && sem.alu != AluOperation::Sub))
        return false;
    auto memory = instructionMemoryPlan(in.memory, proofOptions(L_));
    if (!memory.complete()) return false;

    if (packedCcr_) {
        a_.movRR(Sz::L, R10, kCnt);
        a_.shiftRI(Sz::L, R10, 5, 2);
        a_.aluRI(Asm::Op::AND, Sz::L, R10, 1); // old cumulative Z
        a_.movRR(Sz::L, RCX, kCnt);
        a_.shiftRI(Sz::L, RCX, 5, 4);
        a_.aluRI(Asm::Op::AND, Sz::L, RCX, 1); // input X
    } else {
        a_.movzx(Sz::B, R10, at(L_.srZ));
        a_.movzx(Sz::B, RCX, at(L_.srX));
    }
    a_.movRM(Sz::L, RDI, D(sem.registerIndex));
    a_.movRM(Sz::L, RDX, D(sem.eaReg));
    a_.movRR(Sz::L, RAX, RCX);
    a_.negR(Sz::L, RAX);                  // CF = (X != 0)
    const Asm::Op op = sem.alu == AluOperation::Sub
        ? Asm::Op::SBB : Asm::Op::ADC;
    a_.aluRR(op, hostSz(sem.sizeIndex), RDI, RDX);
    a_.movMR(hostSz(sem.sizeIndex), D(sem.registerIndex), RDI);
    flagsAddSubExtend(R10);
    return true;
}

bool Emitter::emitMoveq(size_t i) {
    const Instr& in = ir_.instrs[i];
    const uint16_t op = in.opcode;
    const InstructionSemantics& sem = in.semantics;
    if (sem.operation != SemanticOp::MoveQuick || traced030(i) != 2)
        return false;
    const int dn = sem.registerIndex;
    const int32_t v = int8_t(op & 0xFF);
    a_.movMI(Sz::L, D(dn), v);
    if (packedCcr_) {
        const int32_t nz = (v < 0 ? 8 : 0) | (v == 0 ? 4 : 0);
        a_.aluRI(Asm::Op::AND, Sz::Q, kCnt, ~0x0F);
        if (nz) a_.aluRI(Asm::Op::OR, Sz::Q, kCnt, nz);
    } else {
        a_.movMI(Sz::B, at(L_.srN), v < 0 ? 1 : 0);
        a_.movMI(Sz::B, at(L_.srZ), v == 0 ? 1 : 0);
        clearVC();
    }
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
    if (dst.idx == EA_AN || dst.idx == EA_IM || dst.idx == EA_DIPC) return false;
    if (!lengthOk(i, imm.ext + dst.ext)) return false;

    const int cycles = isCmp ? kEaRead[dst.idx][szIdx] : eaRmwCost(dst.idx, szIdx);
    if (cycles < 0 || unsigned(cycles) != traced030(i)) return false;

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

    if (dst.idx == EA_DN) {
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
    if (dst.idx == EA_AN || dst.idx == EA_IM) return false;
    if (!lengthOk(i, extUsed + dst.ext)) return false;

    const int cycles = toReg ? 4 : eaRmwCost(dst.idx, szIdx);
    if (cycles < 0) return false;

    MemoryAccessPlan read;
    if (dst.memory) {
        read = memory.access(MemoryDirection::Read, MemoryOperand::Operand,
                             1, uint8_t(mode), uint8_t(reg));
        if (!read.valid()) return false;
    }
    if (!memory.complete()) return false;
    const bool soleReadTiming = dst.memory &&
        admitSoleReadTiming(i, read, unsigned(cycles));
    if (unsigned(cycles) != traced030(i) && !soleReadTiming) return false;

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
    flagZFromEflags();
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
        return traced030(i) == 2;
    }

    if (sem.operation == SemanticOp::Link) {         // LINK.W An,#d16
        if (traced030(i) != 5 || !lengthOk(i, 1)) return false;
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
        if (traced030(i) != 6 || !lengthOk(i, 0)) return false;
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
        if (!decode(i, mode, reg, 2, 0, src, true, true)) return false;
        if (!src.memory) return false;               // LEA needs an address
        if (src.idx == EA_PI || src.idx == EA_PD) return false;  // not encodable
        if (!lengthOk(i, src.ext)) return false;
        const int fullPenalty = !src.fullFormat ? 0 : fullIndexPenalty(src);
        const int cycles = kLea[src.idx] < 0 ? -1 :
            kLea[src.idx] + fullPenalty;
        if (cycles < 0 || unsigned(cycles) != traced030(i)) return false;
        auto memory = instructionMemoryPlan(in.memory, proofOptions(L_));
        MemoryAccessPlan pointerRead;
        if (src.indirect != IndexIndirect::None)
            pointerRead = memory.access(
                MemoryDirection::Read, MemoryOperand::Control, 4,
                uint8_t(mode), uint8_t(reg));
        if (!memory.complete()) return false;
        if (src.indirect != IndexIndirect::None) {
            // New only for the cacheless 030: prove a direct RAM pointer
            // mapping before reading it. A miss/MMIO address replays the
            // pristine instruction, so no exact-device timing is guessed.
            if (!L_.is030 || !pointerRead.valid() ||
                !pointerRead.preflight || pointerRead.exactRequired)
                return false;
            pointerRead.exactThunk = false;
            pointerRead.cache = false;
            addrOfFullIndirectPointer(src, RAX);
            memLoad(RAX, 2, RDI, pointerRead);
            finishFullIndirect(src, RDI, RAX);
        } else {
            addrOf(src, RAX, 2);
        }
        a_.movMR(Sz::L, A(an), RAX);
        return true;
    }

    if (sem.operation == SemanticOp::Extend) {       // EXT.W / EXT.L
        if (traced030(i) != 4) return false;
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
        if (traced030(i) != 4) return false;
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
        if (src.idx == EA_PI || src.idx == EA_PD) return false;  // not encodable
        if (!lengthOk(i, src.ext)) return false;
        const int cycles = kPea[src.idx];    // execPea column, JitCost.h
        if (cycles < 0 || unsigned(cycles) != traced030(i)) return false;
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
        if (src.idx == EA_IM) return false;
        if (src.idx == EA_AN && szIdx == 0) return false;
        if (!lengthOk(i, src.ext)) return false;
        const int cycles = kEaRead[src.idx][szIdx];
        if (cycles < 0) return false;
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
        const bool soleReadTiming = src.memory &&
            admitSoleReadTiming(i, read, unsigned(cycles));
        if (unsigned(cycles) != traced030(i) && !soleReadTiming) return false;
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
    if (dst.idx == EA_AN || dst.idx == EA_IM || dst.idx == EA_DIPC) return false;
    if (!lengthOk(i, dst.ext)) return false;
    const int cycles = eaRmwCost(dst.idx, szIdx);
    if (cycles < 0 || unsigned(cycles) != traced030(i)) return false;

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
        if (dst.idx == EA_DN) {
            a_.movMI(hostSz(szIdx), D(dst.reg), 0);
        } else {
            addrOf(dst, RAX, szIdx);
            a_.aluRR(Asm::Op::XOR, Sz::L, RDI, RDI);
            memStore(RAX, szIdx, RDI, write);
            commitEa(dst, szIdx, write);
        }
        if (packedCcr_) {
            a_.aluRI(Asm::Op::AND, Sz::Q, kCnt, ~0x0F);
            a_.aluRI(Asm::Op::OR, Sz::Q, kCnt, 4);  // Z=1
        } else {
            a_.movMI(Sz::B, at(L_.srN), 0);
            a_.movMI(Sz::B, at(L_.srZ), 1);
            clearVC();
        }
        return true;
    }

    // NOT ($4600) and NEG ($4400)
    const bool isNeg = sem.operation == SemanticOp::Negate;
    if (dst.idx == EA_DN) {
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
    // Scc's store carries no restartable-write proof: through an exact
    // thunk it would reach the bus once from the thunk and once from
    // Moira's replay (the shared oracle pins writeFaults == 1 for
    // ST (A6)+ — "conservative", one attempt). Strip the thunk the way
    // a64's global restartableWriteRequired does, SCOPED to this
    // instruction: the global option is what diverged the LC II lockstep
    // on 2026-08-18 (see proofOptions' note).
    MemoryProofOptions sccOpts = proofOptions(L_);
    if (L_.is030) sccOpts.restartableWriteRequired = true;
    auto memory = instructionMemoryPlan(in.memory, sccOpts);
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
        if (traced030(i) != 4) return false;
        if (!lengthOk(i, 0)) return false;
        if (!memory.complete()) return false;
        materialise(RDI);
        a_.movMR(Sz::B, D(reg), RDI);                 // byte only; D(n) upper
        return true;                                  // bits are left alone
    }

    Ea dst;
    if (!decode(i, mode, reg, 0, 0, dst)) return false;
    if (!dst.memory) return false;                    // An is not a Scc target
    if (dst.idx == EA_DIPC || dst.idx == EA_IM) return false;   // not writable
    if (!lengthOk(i, dst.ext)) return false;
    const int cycles = kScc[dst.idx];    // execSccEa column, JitCost.h
    if (cycles < 0 || unsigned(cycles) != traced030(i)) return false;
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

// EXG — a pure register swap: two loads, two stores, no CCR. The bank of
// each side comes from the decoder's action field (0 = Dx,Dy; 1 = Ax,Ay;
// 2 = Dx,Ay), the a64 lowering verbatim (JitBackendA64.cpp:1428).
bool Emitter::emitExg(size_t i) {
    const Instr& in = ir_.instrs[i];
    const InstructionSemantics& sem = in.semantics;
    if (sem.operation != SemanticOp::Exchange) return false;
    auto memory = instructionMemoryPlan(in.memory, proofOptions(L_));
    if (in.words != 1 || traced030(i) != 2 || !lengthOk(i, 0) ||
        !memory.complete()) return false;
    const int rx = sem.registerIndex, ry = sem.eaReg;
    const bool leftAddress = sem.action == 1;
    const bool rightAddress = sem.action != 0;
    const auto slot = [&](bool addr, int r) { return addr ? A(r) : D(r); };
    a_.movRM(Sz::L, RDI, slot(leftAddress, rx));
    a_.movRM(Sz::L, RSI, slot(rightAddress, ry));
    a_.movMR(Sz::L, slot(leftAddress, rx), RSI);
    a_.movMR(Sz::L, slot(rightAddress, ry), RDI);
    return true;
}

// ASd/LSd/ROd #q,Dn and Dn,Dn — the register shift/rotate family, a64's
// step-exact lowering translated (JitBackendA64.cpp:3125) rather than
// mapped onto x86's native shifts: x86 leaves OF undefined past count 1
// and its CF at count >= width differs from the 68k's, so the unrolled
// single-bit steps that a64's 120k lockstep proved are kept as-is and the
// two backends agree by construction. ROXd stays interpreted except for
// Speedometer's exact immediate ROXR.B #2,D0 witness.
// The dynamic form is specialized for the traced count, guarded on Dn
// BEFORE any state changes, and replays through Moira when Dn moved —
// a64's rule verbatim, including the cycle arithmetic that recovers the
// count from the traced base cycles.
bool Emitter::emitShiftRegister(size_t i) {
    const Instr& in = ir_.instrs[i];
    const InstructionSemantics& sem = in.semantics;
    if (sem.operation != SemanticOp::ShiftRegister) return false;
    if (packedCcr_) return false;
    const int sz = sem.sizeIndex, type = sem.action;
    if (sz > 2 || (type == 2 && (in.opcode != 0xE410 || sem.dynamic)) ||
        in.words != 1)
        return false;
    const int bits = sz == 0 ? 8 : sz == 1 ? 16 : 32;
    const uint32_t mask = bits == 32 ? 0xFFFFFFFFu : (1u << bits) - 1u;
    const bool left = sem.left;
    int count = sem.registerIndex;
    if (sem.dynamic) {
        const unsigned dynamicBase =
            type == 3 || (type == 0 && left) ? 8u : 6u;
        const unsigned traced = traced030(i);
        const unsigned maxCount = type == 1
            ? kMaxSpecializedLogicalShiftCount
            : kMaxSpecializedShiftCount;
        if (traced < dynamicBase ||
            traced > dynamicBase + maxCount) return false;
        count = int(traced - dynamicBase);
        a_.movRM(Sz::L, RCX, D(sem.registerIndex));
        a_.aluRI(Asm::Op::AND, Sz::L, RCX, 63);
        a_.aluRI(Asm::Op::CMP, Sz::L, RCX, count);
        a_.jcc(Cc::NE, *slow_[i]);
    } else if (!count) {
        count = 8;
    }
    const int dn = sem.eaReg;
    // Value in EDI, masked to width. RSI = last outgoing bit (0/1),
    // RDX = sticky ASL overflow, RCX scratch.
    a_.movRM(Sz::L, RDI, D(dn));
    if (bits != 32) a_.aluRI(Asm::Op::AND, Sz::L, RDI, int32_t(mask));
    if (type == 0 && left) a_.aluRR(Asm::Op::XOR, Sz::L, RDX, RDX);
    if (type == 2) {
        a_.movzx(Sz::B, R8, at(L_.srX));            // ROX ring's ninth bit
    }
    if (!count) a_.aluRR(Asm::Op::XOR, Sz::L, RSI, RSI);   // zero count: C=0
    for (int k = 0; k < count; k++) {
        if (left) {
            a_.movRR(Sz::L, RSI, RDI);
            a_.shiftRI(Sz::L, RSI, 5, uint8_t(bits - 1));  // outgoing bit
            a_.shiftRI(Sz::L, RDI, 4, 1);                  // << 1
            if (bits != 32) a_.aluRI(Asm::Op::AND, Sz::L, RDI, int32_t(mask));
            if (type == 3) a_.aluRR(Asm::Op::OR, Sz::L, RDI, RSI);  // ROL
            if (type == 0) {                               // ASL: sign flip?
                a_.movRR(Sz::L, RCX, RDI);
                a_.shiftRI(Sz::L, RCX, 5, uint8_t(bits - 1));
                a_.aluRR(Asm::Op::XOR, Sz::L, RCX, RSI);
                a_.aluRR(Asm::Op::OR, Sz::L, RDX, RCX);
            }
        } else {
            a_.movRR(Sz::L, RSI, RDI);
            a_.aluRI(Asm::Op::AND, Sz::L, RSI, 1);         // outgoing bit
            if (type == 0) {                               // ASR at width
                if (bits == 8) a_.movsxRR(Sz::B, RCX, RDI);
                else if (bits == 16) a_.movsxRR(Sz::W, RCX, RDI);
                else a_.movRR(Sz::L, RCX, RDI);
                a_.shiftRI(Sz::L, RCX, 7, 1);              // SAR 1
                if (bits != 32) a_.aluRI(Asm::Op::AND, Sz::L, RCX, int32_t(mask));
                a_.movRR(Sz::L, RDI, RCX);
            } else {
                a_.shiftRI(Sz::L, RDI, 5, 1);              // >> 1
                if (type == 3) {                           // ROR wrap to top
                    a_.movRR(Sz::L, RCX, RSI);
                    a_.shiftRI(Sz::L, RCX, 4, uint8_t(bits - 1));
                    a_.aluRR(Asm::Op::OR, Sz::L, RDI, RCX);
                } else if (type == 2) {
                    a_.movRR(Sz::L, RCX, R8);
                    a_.shiftRI(Sz::L, RCX, 4, uint8_t(bits - 1));
                    a_.aluRR(Asm::Op::OR, Sz::L, RDI, RCX);
                    a_.movRR(Sz::L, R8, RSI);        // outgoing becomes X
                }
            }
        }
    }
    // Only the sized part of Dn is written, like every 68k Dn operation.
    if (bits == 32)      a_.movMR(Sz::L, D(dn), RDI);
    else if (bits == 16) a_.movMR(Sz::W, D(dn), RDI);
    else                 a_.movMR(Sz::B, D(dn), RDI);
    // CCR, in a64's store order: N/Z from the masked result, C from the
    // last outgoing bit, X follows C except for ROd and a zero dynamic
    // count, V sticky-set by ASL alone.
    a_.movRR(Sz::L, RCX, RDI);
    a_.shiftRI(Sz::L, RCX, 5, uint8_t(bits - 1));
    a_.movMR(Sz::B, at(L_.srN), RCX);
    a_.aluRI(Asm::Op::CMP, Sz::L, RDI, 0);
    a_.setccM(Cc::E, at(L_.srZ));
    a_.movMR(Sz::B, at(L_.srC), RSI);
    if (type != 3 && count) a_.movMR(Sz::B, at(L_.srX), RSI);
    if (type == 0 && left) a_.movMR(Sz::B, at(L_.srV), RDX);
    else a_.movMI(Sz::B, at(L_.srV), 0);
    return true;
}

// MULU.W / MULS.W. The 68020/030 timing is fixed by EA (cyclesMul is a
// 68000/010 concern), and there is no arithmetic trap. A successful sole
// exact read can therefore commit its EA immediately and always retire.
bool Emitter::emitMultiplyWord(size_t i) {
    const Instr& in = ir_.instrs[i];
    const InstructionSemantics& sem = in.semantics;
    if (sem.operation != SemanticOp::MultiplyWord) return false;
    auto memory = instructionMemoryPlan(in.memory, proofOptions(L_));
    Ea src;
    if (!decode(i, sem.eaMode, sem.eaReg, 1, 0, src) ||
        src.idx == EA_AN || !lengthOk(i, src.ext))
        return false;
    MemoryAccessPlan read;
    if (src.memory)
        read = memory.access(MemoryDirection::Read, MemoryOperand::Source,
                             2, uint8_t(sem.eaMode), sem.eaReg);
    if (!memory.complete()) return false;
    const int cycles = kMulWord[src.idx];
    if (cycles < 0 || traced030(i) != unsigned(cycles)) return false;

    const bool sign = sem.action != 0;
    if (src.idx == EA_DN) {
        a_.movzx(Sz::W, RCX, D(src.reg));
    } else if (src.idx == EA_IM) {
        a_.movRI(RCX, uint16_t(src.value));
    } else {
        addrOf(src, RAX, 1);
        memLoad(RAX, 1, RCX, read);
        commitEa(src, 1, read);
    }
    a_.movzx(Sz::W, RDI, D(sem.registerIndex));
    if (sign) {
        a_.movsxRR(Sz::W, RCX, RCX);
        a_.movsxRR(Sz::W, RDI, RDI);
    }
    a_.imulRR(RDI, RCX);
    a_.movMR(Sz::L, D(sem.registerIndex), RDI);
    a_.testRR(Sz::L, RDI, RDI);
    flagsLogic(Sz::L);                              // V=C=0, X preserved
    return true;
}

// Speedometer 4's observed MULL selector, MULU.L D0,D4. Deliberately do not
// infer support for the signed or two-register 64-bit extension actions.
bool Emitter::emitMultiplyLong(size_t i) {
    const Instr& in = ir_.instrs[i];
    const InstructionSemantics& sem = in.semantics;
    if (sem.operation != SemanticOp::MultiplyLong ||
        in.extensionCount < 1 || in.extensionWord(0) != 0x4004u)
        return false;
    Ea src;
    if (!decode(i, sem.eaMode, sem.eaReg, 2, 1, src) ||
        src.idx != EA_DN || src.reg != 0 || !lengthOk(i, 1 + src.ext))
        return false;
    const int cycles = kMulLong[src.idx];
    if (cycles < 0 || traced030(i) != unsigned(cycles)) return false;

    a_.movRM(Sz::L, RCX, D(0));
    a_.movRM(Sz::L, RDI, D(4));
    a_.imulRR64(RDI, RCX);                          // full unsigned product
    a_.movMR(Sz::L, D(4), RDI);
    a_.movRR(Sz::Q, R8, RDI);
    a_.shiftRI(Sz::Q, R8, 5, 32);
    a_.testRR(Sz::Q, R8, R8);
    a_.setccR(Cc::NE, R8);
    a_.movzxRR(Sz::B, R8, R8);                     // high half != 0
    a_.testRR(Sz::L, RDI, RDI);
    flagsLogic(Sz::L);                              // N/Z low, C=0, X survives
    if (packedCcr_) {
        a_.shiftRI(Sz::L, R8, 4, 1);
        a_.aluRI(Asm::Op::AND, Sz::Q, kCnt, ~2);
        a_.aluRR(Asm::Op::OR, Sz::Q, kCnt, R8);
    } else {
        a_.movMR(Sz::B, at(L_.srV), R8);
    }
    return true;
}

// DIVU.W / DIVS.W. Memory operands use a side-effect-free speculative probe,
// so trap and overflow paths still reach Moira with pristine guest state.
bool Emitter::emitDivideWord(size_t i) {
    const Instr& in = ir_.instrs[i];
    const InstructionSemantics& sem = in.semantics;
    if (sem.operation != SemanticOp::DivideWord) return false;
    auto memory = instructionMemoryPlan(in.memory, proofOptions(L_));
    Ea src;
    if (!decode(i, sem.eaMode, sem.eaReg, 1, 0, src) ||
        src.idx == EA_AN || !lengthOk(i, src.ext))
        return false;
    MemoryAccessPlan read;
    if (src.memory)
        read = memory.access(MemoryDirection::Read, MemoryOperand::Source,
                             2, uint8_t(sem.eaMode), sem.eaReg);
    if (!memory.complete()) return false;
    const bool sign = sem.action != 0;
    const int cycles = sign ? kDivsWord[src.idx] : kDivuWord[src.idx];
    if (cycles < 0 || traced030(i) != unsigned(cycles)) return false;

    if (src.idx == EA_DN) {
        a_.movzx(Sz::W, RCX, D(src.reg));
    } else if (src.idx == EA_IM) {
        const uint32_t divisor = sign
            ? uint32_t(int32_t(src.value)) : uint32_t(uint16_t(src.value));
        a_.movRI(RCX, divisor);
    } else if (!loadReplayableMemory(i, src, 1, RCX, read)) return false;
    if (sign) a_.movsxRR(Sz::W, RCX, RCX);
    a_.testRR(Sz::L, RCX, RCX);
    a_.jcc(Cc::E, runtimeStub(i));                 // divide-by-zero trap
    a_.movRM(Sz::L, RAX, D(sem.registerIndex));   // dividend

    if (sign) {
        // x86 IDIV alone would raise #DE for INT_MIN / -1. That quotient is
        // a 68k word overflow anyway, so hand it back before executing IDIV.
        Label& safe = *a_.fresh();
        a_.aluRI(Asm::Op::CMP, Sz::L, RCX, -1);
        a_.jccShort(Cc::NE, safe);
        a_.aluRI(Asm::Op::CMP, Sz::L, RAX, int32_t(0x80000000u));
        a_.jcc(Cc::E, runtimeStub(i));
        a_.bind(safe);
        a_.cdq();
        a_.divR(true, RCX);                        // EAX quotient, EDX remainder
        a_.aluRI(Asm::Op::CMP, Sz::L, RAX, -32768);
        a_.jcc(Cc::L, runtimeStub(i));
        a_.aluRI(Asm::Op::CMP, Sz::L, RAX, 32767);
        a_.jcc(Cc::G, runtimeStub(i));
    } else {
        a_.aluRR(Asm::Op::XOR, Sz::L, RDX, RDX);
        a_.divR(false, RCX);
        a_.aluRI(Asm::Op::CMP, Sz::L, RAX, 65535);
        a_.jcc(Cc::A, runtimeStub(i));
    }

    // 68k packs remainder:quotient into Dn and derives N/Z from the
    // 16-bit quotient; V=C=0 and X survives.
    a_.movRR(Sz::L, RDI, RDX);
    a_.shiftRI(Sz::L, RDI, 4, 16);
    a_.aluRI(Asm::Op::AND, Sz::L, RAX, 0xFFFF);
    a_.aluRR(Asm::Op::OR, Sz::L, RDI, RAX);
    if (src.memory) commitReplayableMemory(src, 1, read);
    a_.testRR(Sz::W, RAX, RAX);
    flagsLogic(Sz::W);
    a_.movMR(Sz::L, D(sem.registerIndex), RDI);
    return true;
}

// All four legal DIVL extension actions. The divide is
// deliberately performed at host width 64 even for a 32-bit dividend: x86
// can then represent the signed INT_MIN/-1 quotient and the common range
// guard hands the 68k overflow to Moira instead of raising host #DE.
bool Emitter::emitDivideLong(size_t i) {
    const Instr& in = ir_.instrs[i];
    const InstructionSemantics& sem = in.semantics;
    if (sem.operation != SemanticOp::DivideLong || in.extensionCount < 1)
        return false;
    const uint16_t ext = in.extensionWord(0);
    if ((ext & 0x83F8u) != 0) return false;          // reserved extension bits
    auto memory = instructionMemoryPlan(in.memory, proofOptions(L_));
    Ea src;
    if (!decode(i, sem.eaMode, sem.eaReg, 2, 1, src) ||
        src.idx == EA_AN || !lengthOk(i, 1 + src.ext))
        return false;
    MemoryAccessPlan read;
    if (src.memory)
        read = memory.access(MemoryDirection::Read, MemoryOperand::Source,
                             4, uint8_t(sem.eaMode), sem.eaReg);
    if (!memory.complete()) return false;
    const int cycles = kDivLong[src.idx];
    if (cycles < 0 || traced030(i) != unsigned(cycles)) return false;

    const bool sign = (ext & 0x0800u) != 0;
    const bool wideDividend = (ext & 0x0400u) != 0;
    const unsigned dl = (ext >> 12) & 7, dh = ext & 7;

    if (src.idx == EA_DN) a_.movRM(Sz::L, RCX, D(src.reg));
    else if (src.idx == EA_IM) a_.movRI(RCX, uint32_t(src.value));
    else if (!loadReplayableMemory(i, src, 2, RCX, read)) return false;
    a_.testRR(Sz::L, RCX, RCX);
    a_.jcc(Cc::E, runtimeStub(i));                   // vector 5
    if (sign) a_.movsxd(RCX, RCX);

    if (wideDividend) {
        a_.movRM(Sz::L, RAX, D(dh));
        a_.shiftRI(Sz::Q, RAX, 4, 32);
        a_.movRM(Sz::L, RDX, D(dl));
        a_.aluRR(Asm::Op::OR, Sz::Q, RAX, RDX);     // RAX = Dh:Dl
    } else {
        a_.movRM(Sz::L, RAX, D(dl));
        if (sign) a_.movsxd(RAX, RAX);
    }

    if (sign) {
        // The sole 64-bit host overflow is also a 68k quotient overflow.
        Label& safe = *a_.fresh();
        a_.aluRI(Asm::Op::CMP, Sz::Q, RCX, -1);
        a_.jccShort(Cc::NE, safe);
        a_.movRI64(RDI, 0x8000'0000'0000'0000ull);
        a_.aluRR(Asm::Op::CMP, Sz::Q, RAX, RDI);
        a_.jcc(Cc::E, runtimeStub(i));
        a_.bind(safe);
        a_.cqo();
        a_.divR64(true, RCX);
    } else {
        a_.aluRR(Asm::Op::XOR, Sz::L, RDX, RDX);
        a_.divR64(false, RCX);
    }

    if (sign) {
        a_.movRR(Sz::L, RDI, RAX);
        a_.movsxd(RDI, RDI);
        a_.aluRR(Asm::Op::CMP, Sz::Q, RDI, RAX);
        a_.jcc(Cc::NE, runtimeStub(i));              // outside int32_t
    } else {
        a_.movRR(Sz::Q, RDI, RAX);
        a_.shiftRI(Sz::Q, RDI, 5, 32);
        a_.testRR(Sz::L, RDI, RDI);
        a_.jcc(Cc::NE, runtimeStub(i));              // outside uint32_t
    }

    if (src.memory) commitReplayableMemory(src, 2, read);
    // Match Moira's remainder-then-quotient write order. Dh==Dl therefore
    // leaves the quotient, and no architectural state changed before here.
    a_.movMR(Sz::L, D(dh), RDX);
    a_.movMR(Sz::L, D(dl), RAX);
    a_.testRR(Sz::L, RAX, RAX);
    flagsLogic(Sz::L);                              // V=C=0, X preserved
    return true;
}

// The 68020 bitfield family — a64's lowerings (a64:2891 memory, a64:3040
// register) translated. Register forms fold the constants: with o and w
// known at compile time the rotate, the extraction shift and the
// destination mask are all immediates. Read-only memory forms cover both a
// field contained in one longword and the optional fifth-byte tail after the
// signed byte-displacement adjust, with static or dynamic o/w. Writes remain
// TAILLESS and consume the shared read4/write4 RMW contract, proving the
// writable translation before exposing the read.
// Dynamic register forms use CL-counted shifts and keep their entire body
// call-free. What refuses here, each with the door it leaves open: the
// five-byte writes need a four-slot transactional contract.
bool Emitter::emitBitfield(size_t i) {
    const Instr& in = ir_.instrs[i];
    const InstructionSemantics& sem = in.semantics;
    if (sem.operation != SemanticOp::Bitfield) return false;
    // a64:2874's rule verbatim: these forms publish several independently
    // computed flag bits; refuse under the packed CCR.
    if (packedCcr_) return false;
    const int kind = sem.action;
    const uint16_t memExt = in.extensionWord(0);
    if (sem.eaMode != 0) {
        const bool readOnly = kind == 0 || kind == 1 || kind == 3 ||
                              kind == 5;
        if (L_.is030 && !memBitfield030Admission()) return false;
        if (sem.eaMode != 2 && sem.eaMode != 5) return false;
        const bool possibleTail = !memoryBitfieldFitsLongword(memExt);
        if (!readOnly && possibleTail) return false;
        // PreflightAll proves plain mappings, not a two-line 040 D-cache
        // transaction. The promoted tail path is the cacheless 030; a live
        // 040 cache keeps replaying until the IR can publish that protocol.
        if (possibleTail && L_.cache040Live) return false;
        const bool dynOffset = (memExt & 0x0800) != 0;
        const bool dynWidth = (memExt & 0x0020) != 0;
        if ((dynOffset || dynWidth) && !dynamicRegisterBitfieldEnabled())
            return false;
        auto memory = instructionMemoryPlan(in.memory, proofOptions(L_));
        Ea src;
        if (!decode(i, sem.eaMode, sem.eaReg, 2, 1, src) ||
            !lengthOk(i, 1 + src.ext)) return false;
        const MemoryAccessPlan read = memory.access(
            MemoryDirection::Read, MemoryOperand::Operand, 4,
            uint8_t(sem.eaMode), uint8_t(sem.eaReg));
        MemoryAccessPlan tail;
        if (readOnly && possibleTail)
            tail = memory.access(MemoryDirection::Read,
                                 MemoryOperand::Operand, 1,
                                 uint8_t(sem.eaMode), uint8_t(sem.eaReg));
        MemoryAccessPlan write;
        if (!readOnly)
            write = memory.access(MemoryDirection::Write,
                                  MemoryOperand::Operand, 4,
                                  uint8_t(sem.eaMode), uint8_t(sem.eaReg));
        if (!read.valid() || (!readOnly &&
                              !memoryRmwAccessPair(read, write)) ||
            (readOnly && possibleTail &&
             (!tail.valid() || !read.preflight || !tail.preflight ||
              read.protocol != MemoryProofProtocol::PreflightAll ||
              tail.protocol != MemoryProofProtocol::PreflightAll ||
              read.exactThunk || tail.exactThunk || read.cache || tail.cache)) ||
            !memory.complete()) return false;

        // Same fixed 68020/68030 table as A64. A sole exact-required read may
        // carry live device delay above it; a two-read tail is plain-memory
        // preflight only and must match the fixed cost exactly.
        static const int8_t memoryCycles[8][2] = {
            {17,18}, {19,20}, {24,25}, {19,20},
            {24,25}, {32,33}, {24,25}, {21,22}
        };
        const int eaColumn = sem.eaMode == 2 ? 0 : 1;
        const unsigned opcodeCycles =
            unsigned(memoryCycles[kind][eaColumn]);
        MemoryAccessPlan timedRead = read;
        const bool soleReadTiming = readOnly && !possibleTail &&
            admitSoleReadTiming(i, timedRead, opcodeCycles);
        if (traced030(i) != opcodeCycles && !soleReadTiming) return false;
        const unsigned oImm = (memExt >> 6) & 31, oReg = (memExt >> 6) & 7;
        const unsigned wImm = ((unsigned(memExt & 31) - 1u) & 31u) + 1u;
        const unsigned wReg = memExt & 7;

        // The signed byte displacement moves the base address by
        // floor(rawOffset/8); the low three bits select within the loaded
        // longword (a64:2956 — SAR, not SHR: a Dn offset is signed).
        addrOf(src, RAX, 2);
        if (dynOffset) {
            a_.movRM(Sz::L, RCX, D(int(oReg)));
            a_.shiftRI(Sz::L, RCX, 7, 3);
            a_.aluRR(Asm::Op::ADD, Sz::L, RAX, RCX);
        } else if (oImm >> 3) {
            a_.aluRI(Asm::Op::ADD, Sz::L, RAX, int32_t(oImm >> 3));
        }
        if (readOnly && possibleTail) {
            // Prove both possible mappings before the first guest byte is
            // observed. A failure therefore replays an untouched instruction
            // and cannot duplicate an MMIO side effect. The second
            // probe is skipped at run time when offset+width fits bit 31.
            a_.movMR(Sz::L, F(kFSaveA), RAX);       // adjusted guest address
            if (dynWidth) {
                a_.movRM(Sz::L, RDX, D(int(wReg)));
                a_.aluRI(Asm::Op::SUB, Sz::L, RDX, 1);
                a_.aluRI(Asm::Op::AND, Sz::L, RDX, 31);
                a_.aluRI(Asm::Op::ADD, Sz::L, RDX, 1);
            } else {
                a_.movRI(RDX, wImm);
            }
            a_.movMR(Sz::L, F(kFValue), RDX);       // normalized width

            memProbe(RAX, 4, false, runtimeStub(i));
            a_.movMR(Sz::Q, F(kFPointerHost), RSI); // first host pointer

            if (dynOffset) {
                a_.movRM(Sz::L, RCX, D(int(oReg)));
                a_.aluRI(Asm::Op::AND, Sz::L, RCX, 7);
            } else {
                a_.movRI(RCX, oImm & 7);
            }
            a_.aluRM(Asm::Op::ADD, Sz::L, RCX, F(kFValue));
            a_.aluRI(Asm::Op::CMP, Sz::L, RCX, 32);
            Label& noTail = *a_.fresh();
            Label& haveData = *a_.fresh();
            a_.jcc(Cc::BE, noTail);

            a_.movRM(Sz::L, RAX, F(kFSaveA));
            a_.aluRI(Asm::Op::ADD, Sz::L, RAX, 4);
            memProbe(RAX, 1, false, runtimeStub(i));
            a_.movzx(Sz::B, R8, mem(RSI, 0));       // optional fifth byte
            a_.movRM(Sz::Q, RSI, F(kFPointerHost));
            a_.movRM(Sz::L, RDI, mem(RSI, 0));
            a_.bswap(RDI);
            a_.jmp(haveData);

            a_.bind(noTail);
            a_.movRI(R8, 0);
            a_.movRM(Sz::Q, RSI, F(kFPointerHost));
            a_.movRM(Sz::L, RDI, mem(RSI, 0));
            a_.bswap(RDI);
            a_.bind(haveData);
        } else if (readOnly) {
            memLoad(RAX, 2, RDI, timedRead);        // RDI = the longword
        } else {
            memRmwLoad(RAX, 2, RDI, read, write);
            a_.movMR(Sz::L, F(kFSaveV), RDI);       // pristine destination
        }

        // A miss called out and clobbered every caller-saved register, so
        // every extension-derived value is (re)built from guest state AFTER
        // the access (a64:2999's rule). RDX carries the normalized width
        // for the rest of the body when it is dynamic.
        if (dynWidth) {
            a_.movRM(Sz::L, RDX, D(int(wReg)));
            a_.aluRI(Asm::Op::SUB, Sz::L, RDX, 1);
            a_.aluRI(Asm::Op::AND, Sz::L, RDX, 31);
            a_.aluRI(Asm::Op::ADD, Sz::L, RDX, 1);  // 32,1..31
        }
        // field = (value << (raw & 7)) >> (32 - w), both zero-fill.
        if (dynOffset) {
            a_.movRM(Sz::L, RCX, D(int(oReg)));
            a_.aluRI(Asm::Op::AND, Sz::L, RCX, 7);
            a_.shiftRCl(Sz::L, RDI, 4);             // SHL by CL
            if (possibleTail) a_.shiftRCl(Sz::L, R8, 4);
        } else if (oImm & 7) {
            a_.shiftRI(Sz::L, RDI, 4, uint8_t(oImm & 7));
            if (possibleTail)
                a_.shiftRI(Sz::L, R8, 4, uint8_t(oImm & 7));
        }
        if (possibleTail) {
            a_.shiftRI(Sz::L, R8, 5, 8);
            a_.aluRR(Asm::Op::OR, Sz::L, RDI, R8);
        }
        if (dynWidth) {
            a_.movRI(RCX, 32);
            a_.aluRR(Asm::Op::SUB, Sz::L, RCX, RDX);
            a_.shiftRCl(Sz::L, RDI, 5);             // SHR by CL (0..31)
        } else if (wImm != 32) {
            a_.shiftRI(Sz::L, RDI, 5, uint8_t(32 - wImm));
        }
        if (kind != 7) {                            // BFINS uses source flags
            // N = bit (width-1) of the field, Z over the whole field,
            // V=C=0.
            a_.movRR(Sz::L, RSI, RDI);
            if (dynWidth) {
                a_.movRR(Sz::L, RCX, RDX);
                a_.aluRI(Asm::Op::SUB, Sz::L, RCX, 1);
                a_.shiftRCl(Sz::L, RSI, 5);
            } else if (wImm > 1) {
                a_.shiftRI(Sz::L, RSI, 5, uint8_t(wImm - 1));
            }
            a_.movMR(Sz::B, at(L_.srN), RSI);
            a_.aluRI(Asm::Op::CMP, Sz::L, RDI, 0);
            a_.setccM(Cc::E, at(L_.srZ));
            a_.movMI(Sz::B, at(L_.srV), 0);
            a_.movMI(Sz::B, at(L_.srC), 0);
        }
        if (!readOnly) {
            // R8 is the aligned BFINS source when needed. Width remains in
            // EDX for dynamic forms; offset is rebuilt from guest state so
            // no value crosses the writable DTLB fill call above.
            if (kind == 7) {
                a_.movRM(Sz::L, R8, D((memExt >> 12) & 7));
                if (dynWidth) {
                    a_.movRI(RCX, 32);
                    a_.aluRR(Asm::Op::SUB, Sz::L, RCX, RDX);
                    a_.shiftRCl(Sz::L, R8, 4);
                } else if (wImm != 32) {
                    a_.shiftRI(Sz::L, R8, 4, uint8_t(32 - wImm));
                }
                a_.movRR(Sz::L, RCX, R8);
                a_.shiftRI(Sz::L, RCX, 5, 31);
                a_.movMR(Sz::B, at(L_.srN), RCX);
                a_.aluRI(Asm::Op::CMP, Sz::L, R8, 0);
                a_.setccM(Cc::E, at(L_.srZ));
                a_.movMI(Sz::B, at(L_.srV), 0);
                a_.movMI(Sz::B, at(L_.srC), 0);
                if (dynOffset) {
                    a_.movRM(Sz::L, RCX, D(int(oReg)));
                    a_.aluRI(Asm::Op::AND, Sz::L, RCX, 7);
                    a_.shiftRCl(Sz::L, R8, 5);
                } else if (oImm & 7) {
                    a_.shiftRI(Sz::L, R8, 5, uint8_t(oImm & 7));
                }
            }

            // ESI = destination mask: top-width ones shifted right by the
            // residual memory offset. A tailless memory field never wraps.
            a_.movRI(RSI, 0xFFFFFFFFu);
            if (dynWidth) {
                a_.movRI(RCX, 32);
                a_.aluRR(Asm::Op::SUB, Sz::L, RCX, RDX);
                a_.shiftRCl(Sz::L, RSI, 4);
            } else if (wImm != 32) {
                a_.shiftRI(Sz::L, RSI, 4, uint8_t(32 - wImm));
            }
            if (dynOffset) {
                a_.movRM(Sz::L, RCX, D(int(oReg)));
                a_.aluRI(Asm::Op::AND, Sz::L, RCX, 7);
                a_.shiftRCl(Sz::L, RSI, 5);
            } else if (oImm & 7) {
                a_.shiftRI(Sz::L, RSI, 5, uint8_t(oImm & 7));
            }
            a_.movRM(Sz::L, RDI, F(kFSaveV));
            if (kind == 2) a_.aluRR(Asm::Op::XOR, Sz::L, RDI, RSI);
            else if (kind == 4) {
                a_.aluRI(Asm::Op::XOR, Sz::L, RSI, -1);
                a_.aluRR(Asm::Op::AND, Sz::L, RDI, RSI);
            } else if (kind == 6) {
                a_.aluRR(Asm::Op::OR, Sz::L, RDI, RSI);
            } else {                               // BFINS
                a_.aluRI(Asm::Op::XOR, Sz::L, RSI, -1);
                a_.aluRR(Asm::Op::AND, Sz::L, RDI, RSI);
                a_.aluRR(Asm::Op::OR, Sz::L, RDI, R8);
            }
            memRmwStore(2, RDI, read, write);
            commitEa(src, 2, write);
            return true;
        }
        switch (kind) {
        case 0:                                     // BFTST
            return true;
        case 1:                                     // BFEXTU
            a_.movMR(Sz::L, D((memExt >> 12) & 7), RDI);
            return true;
        case 3:                                     // BFEXTS
            if (dynWidth) {
                a_.movRI(RCX, 32);
                a_.aluRR(Asm::Op::SUB, Sz::L, RCX, RDX);
                a_.shiftRCl(Sz::L, RDI, 4);
                a_.shiftRCl(Sz::L, RDI, 7);         // SAR by the same CL
            } else if (wImm != 32) {
                a_.shiftRI(Sz::L, RDI, 4, uint8_t(32 - wImm));
                a_.shiftRI(Sz::L, RDI, 7, uint8_t(32 - wImm));
            }
            a_.movMR(Sz::L, D((memExt >> 12) & 7), RDI);
            return true;
        case 5: {                                   // BFFFO
            // raw offset (UNCROPPED, signed for a Dn) + width when the
            // field is empty, else raw + width - 1 - bsr(field).
            if (dynOffset) a_.movRM(Sz::L, RSI, D(int(oReg)));
            else a_.movRI(RSI, oImm);
            if (dynWidth) a_.aluRR(Asm::Op::ADD, Sz::L, RSI, RDX);
            else a_.aluRI(Asm::Op::ADD, Sz::L, RSI, int32_t(wImm));
            Label& ffoDone = *a_.fresh();
            a_.testRR(Sz::L, RDI, RDI);
            a_.jccShort(Cc::E, ffoDone);
            a_.bsrRR(Sz::L, RCX, RDI);
            a_.aluRI(Asm::Op::SUB, Sz::L, RSI, 1);
            a_.aluRR(Asm::Op::SUB, Sz::L, RSI, RCX);
            a_.bind(ffoDone);
            a_.movMR(Sz::L, D((memExt >> 12) & 7), RSI);
            return true;
        }
        default:
            return false;
        }
    }
    const uint16_t ext = memExt;
    if (in.words != 2) return false;
    // Per-action traced base cycles, a64:3041's table verbatim; a form
    // whose trace disagrees refuses rather than mischarges.
    static const uint8_t cycles[8] = {6, 8, 12, 8, 12, 18, 12, 10};
    if (traced030(i) != cycles[kind]) return false;
    const int dst = sem.eaReg, out = (ext >> 12) & 7;
    const bool dynOffset = (ext & 0x0800) != 0;
    const bool dynWidth = (ext & 0x0020) != 0;
    if (dynOffset || dynWidth) {
        if (!dynamicRegisterBitfieldEnabled()) return false;
        // Runtime register bitfields have fixed 68k timing. Keep the values
        // in caller-saved host registers: this body never calls out.
        //   EDI original destination, ESI selected field
        //   R8D cropped offset, R9D raw offset (BFFFO)
        //   R10D normalized width, R11D = 32-width
        a_.movRM(Sz::L, RDI, D(dst));
        if (dynOffset) {
            a_.movRM(Sz::L, R8, D((ext >> 6) & 7));
            a_.movRR(Sz::L, R9, R8);
            a_.aluRI(Asm::Op::AND, Sz::L, R8, 31);
        } else {
            a_.movRI(R8, (ext >> 6) & 31);
            a_.movRR(Sz::L, R9, R8);
        }
        if (dynWidth) a_.movRM(Sz::L, R10, D(ext & 7));
        else a_.movRI(R10, ext & 31);
        a_.aluRI(Asm::Op::SUB, Sz::L, R10, 1);
        a_.aluRI(Asm::Op::AND, Sz::L, R10, 31);
        a_.aluRI(Asm::Op::ADD, Sz::L, R10, 1);     // 32,1..31
        a_.movRI(R11, 32);
        a_.aluRR(Asm::Op::SUB, Sz::L, R11, R10);   // 32-width

        // rotl(original,offset), then right-justify the field. x86 variable
        // shifts mask CL to five bits, exactly the register-bitfield rule.
        a_.movRR(Sz::L, RSI, RDI);
        a_.movRR(Sz::L, RCX, R8);
        a_.shiftRCl(Sz::L, RSI, 0);                // ROL by offset
        a_.movRR(Sz::L, RCX, R11);
        a_.shiftRCl(Sz::L, RSI, 5);                // SHR by 32-width

        if (kind != 7) {                           // BFINS uses source flags
            a_.movRR(Sz::L, RDX, RSI);
            a_.movRR(Sz::L, RCX, R10);
            a_.aluRI(Asm::Op::SUB, Sz::L, RCX, 1);
            a_.shiftRCl(Sz::L, RDX, 5);
            a_.movMR(Sz::B, at(L_.srN), RDX);
            a_.aluRI(Asm::Op::CMP, Sz::L, RSI, 0);
            a_.setccM(Cc::E, at(L_.srZ));
            a_.movMI(Sz::B, at(L_.srV), 0);
            a_.movMI(Sz::B, at(L_.srC), 0);
        }
        if (kind == 0) return true;                // BFTST
        if (kind == 1 || kind == 3) {              // BFEXTU / BFEXTS
            if (kind == 3) {
                a_.movRR(Sz::L, RCX, R11);
                a_.shiftRCl(Sz::L, RSI, 4);
                a_.shiftRCl(Sz::L, RSI, 7);        // SAR by same CL
            }
            a_.movMR(Sz::L, D(out), RSI);
            return true;
        }
        if (kind == 5) {                           // BFFFO
            a_.movRR(Sz::L, RDX, R9);
            a_.aluRR(Asm::Op::ADD, Sz::L, RDX, R10);
            Label& done = *a_.fresh();
            a_.testRR(Sz::L, RSI, RSI);
            a_.jccShort(Cc::E, done);
            a_.bsrRR(Sz::L, RCX, RSI);
            a_.aluRI(Asm::Op::SUB, Sz::L, RDX, 1);
            a_.aluRR(Asm::Op::SUB, Sz::L, RDX, RCX);
            a_.bind(done);
            a_.movMR(Sz::L, D(out), RDX);
            return true;
        }

        // Top-width mask rotated right by the cropped offset.
        a_.movRI(RDX, 0xFFFFFFFFu);
        a_.movRR(Sz::L, RCX, R11);
        a_.shiftRCl(Sz::L, RDX, 4);
        a_.movRR(Sz::L, RCX, R8);
        a_.shiftRCl(Sz::L, RDX, 1);                // ROR by offset
        if (kind == 2) a_.aluRR(Asm::Op::XOR, Sz::L, RDI, RDX);
        else if (kind == 4) {
            a_.aluRI(Asm::Op::XOR, Sz::L, RDX, -1);
            a_.aluRR(Asm::Op::AND, Sz::L, RDI, RDX);
        } else if (kind == 6) {
            a_.aluRR(Asm::Op::OR, Sz::L, RDI, RDX);
        } else {                                   // BFINS
            a_.movRM(Sz::L, RSI, D(out));
            a_.movRR(Sz::L, RCX, R11);
            a_.shiftRCl(Sz::L, RSI, 4);            // crop + top-align source
            a_.movRR(Sz::L, RCX, RSI);
            a_.shiftRI(Sz::L, RCX, 5, 31);
            a_.movMR(Sz::B, at(L_.srN), RCX);
            a_.aluRI(Asm::Op::CMP, Sz::L, RSI, 0);
            a_.setccM(Cc::E, at(L_.srZ));
            a_.movMI(Sz::B, at(L_.srV), 0);
            a_.movMI(Sz::B, at(L_.srC), 0);
            a_.movRR(Sz::L, RCX, R8);
            a_.shiftRCl(Sz::L, RSI, 1);            // ROR into destination
            a_.aluRI(Asm::Op::XOR, Sz::L, RDX, -1);
            a_.aluRR(Asm::Op::AND, Sz::L, RDI, RDX);
            a_.aluRR(Asm::Op::OR, Sz::L, RDI, RSI);
        }
        a_.movMR(Sz::L, D(dst), RDI);
        return true;
    }
    const unsigned offset = (ext >> 6) & 31;
    const unsigned width = ((unsigned(ext & 31) - 1u) & 31u) + 1u; // 32,1..31
    const uint32_t topMask =
        width == 32 ? 0xFFFFFFFFu : 0xFFFFFFFFu << (32 - width);
    const uint32_t mask = offset
        ? (topMask >> offset) | (topMask << (32 - offset))
        : topMask;                                  // field mask at offset

    // EDI = original destination; ESI = extracted field (zero-extended).
    a_.movRM(Sz::L, RDI, D(dst));
    if (kind != 7) {                                // BFINS uses source flags
        a_.movRR(Sz::L, RSI, RDI);
        if (offset) a_.shiftRI(Sz::L, RSI, 0, uint8_t(offset));        // ROL
        if (width != 32) a_.shiftRI(Sz::L, RSI, 5, uint8_t(32 - width)); // SHR
        // N = bit (width-1) of the field, Z over the whole field, V=C=0 —
        // a64:3078-3081's store order.
        a_.movRR(Sz::L, RCX, RSI);
        if (width > 1) a_.shiftRI(Sz::L, RCX, 5, uint8_t(width - 1));
        a_.movMR(Sz::B, at(L_.srN), RCX);
        a_.aluRI(Asm::Op::CMP, Sz::L, RSI, 0);
        a_.setccM(Cc::E, at(L_.srZ));
        a_.movMI(Sz::B, at(L_.srV), 0);
        a_.movMI(Sz::B, at(L_.srC), 0);
    }
    switch (kind) {
    case 0:                                         // BFTST
        return true;
    case 1:                                         // BFEXTU
        a_.movMR(Sz::L, D(out), RSI);
        return true;
    case 3:                                         // BFEXTS
        if (width != 32) {
            a_.shiftRI(Sz::L, RSI, 4, uint8_t(32 - width));            // SHL
            a_.shiftRI(Sz::L, RSI, 7, uint8_t(32 - width));            // SAR
        }
        a_.movMR(Sz::L, D(out), RSI);
        return true;
    case 5: {                                       // BFFFO
        // offset + leading zeros inside the field: offset+width when the
        // field is zero (BSR is undefined there), else
        // offset + width - 1 - bsr(field) — the a64 clz math rearranged.
        // The label MUST be arena-allocated (a_.fresh()): finish() patches
        // fixups through Label POINTERS after this frame is gone, and a
        // stack-local Label here read freed stack at patch time — green in
        // the shallow lockstep by luck, SIGSEGV 19 s into the barefpu boot.
        Label& done = *a_.fresh();
        a_.movRI(RCX, offset + width);
        a_.testRR(Sz::L, RSI, RSI);
        a_.jccShort(Cc::E, done);
        a_.bsrRR(Sz::L, RDX, RSI);
        a_.movRI(RCX, offset + width - 1);
        a_.aluRR(Asm::Op::SUB, Sz::L, RCX, RDX);
        a_.bind(done);
        a_.movMR(Sz::L, D(out), RCX);
        return true;
    }
    case 2:                                         // BFCHG
        a_.aluRI(Asm::Op::XOR, Sz::L, RDI, int32_t(mask));
        a_.movMR(Sz::L, D(dst), RDI);
        return true;
    case 4:                                         // BFCLR
        a_.aluRI(Asm::Op::AND, Sz::L, RDI, int32_t(~mask));
        a_.movMR(Sz::L, D(dst), RDI);
        return true;
    case 6:                                         // BFSET
        a_.aluRI(Asm::Op::OR, Sz::L, RDI, int32_t(mask));
        a_.movMR(Sz::L, D(dst), RDI);
        return true;
    case 7: {                                       // BFINS
        a_.movRM(Sz::L, RSI, D(out));               // source Dn
        if (width != 32) a_.shiftRI(Sz::L, RSI, 4, uint8_t(32 - width));
        // Flags from the cropped, top-aligned source (a64:3110-3113).
        a_.movRR(Sz::L, RCX, RSI);
        a_.shiftRI(Sz::L, RCX, 5, 31);
        a_.movMR(Sz::B, at(L_.srN), RCX);
        a_.aluRI(Asm::Op::CMP, Sz::L, RSI, 0);
        a_.setccM(Cc::E, at(L_.srZ));
        a_.movMI(Sz::B, at(L_.srV), 0);
        a_.movMI(Sz::B, at(L_.srC), 0);
        if (offset) a_.shiftRI(Sz::L, RSI, 1, uint8_t(offset));        // ROR
        a_.aluRI(Asm::Op::AND, Sz::L, RDI, int32_t(~mask));
        a_.aluRR(Asm::Op::OR, Sz::L, RDI, RSI);
        a_.movMR(Sz::L, D(dst), RDI);
        return true;
    }
    default:
        return false;
    }
}

// CMPM.<s> (Ay)+,(Ax)+ — two postincremented reads, no write. Distinct
// address registers let both DTLB mappings be proved while the entry state
// is pristine (the x64 expansion of PreflightAll, exactly as the
// memory-to-memory MOVE above); once both probes hit, direct RAM loads
// cannot fault and the architectural source-read / source-commit /
// destination-read / destination-commit order is reproduced. The
// same-register form depends on the first increment for its second EA and
// stays on the precise interpreter path (a64:1441).
bool Emitter::emitCmpm(size_t i) {
    const Instr& in = ir_.instrs[i];
    const InstructionSemantics& sem = in.semantics;
    if (sem.operation != SemanticOp::CompareMemory) return false;
    auto memory = instructionMemoryPlan(in.memory, proofOptions(L_));
    const int szIdx = sem.sizeIndex;
    const uint8_t srcReg = sem.eaReg, dstReg = sem.destinationReg;
    if (srcReg == dstReg || in.words != 1 || traced030(i) != 9 ||
        !lengthOk(i, 0)) return false;
    Ea src, dst;
    if (!decode(i, 3, srcReg, szIdx, 0, src) ||
        !decode(i, 3, dstReg, szIdx, 0, dst)) return false;
    const MemoryAccessPlan srcRead = memory.access(
        MemoryDirection::Read, MemoryOperand::Source,
        uint8_t(sizeBytes(szIdx)), 3, srcReg);
    const MemoryAccessPlan dstRead = memory.access(
        MemoryDirection::Read, MemoryOperand::Destination,
        uint8_t(sizeBytes(szIdx)), 3, dstReg);
    if (!srcRead.valid() || !dstRead.valid() || !srcRead.preflight ||
        !dstRead.preflight ||
        memory.proof.protocol != MemoryProofProtocol::PreflightAll ||
        in.memory.order != MemoryOrder::SourceThenDestination ||
        !memory.complete()) return false;

    // Prove both mappings before either register moves or a flag changes.
    addrOf(src, RAX, szIdx);
    memProbe(RAX, sizeBytes(szIdx), false, runtimeStub(i));
    a_.movMR(Sz::Q, F(kFValue), RSI);               // source host pointer
    addrOf(dst, RAX, szIdx);
    memProbe(RAX, sizeBytes(szIdx), false, runtimeStub(i));
    a_.movRR(Sz::Q, R11, RSI);                      // destination bytes
    a_.movRM(Sz::Q, R10, F(kFValue));               // source bytes

    const auto loadGuest = [&](Reg host, Reg out) {
        if (szIdx == 0) {
            a_.movzx(Sz::B, out, mem(host, 0));
        } else if (szIdx == 1) {
            a_.movzx(Sz::W, out, mem(host, 0));
            a_.rolR16(out, 8);
        } else {
            a_.movRM(Sz::L, out, mem(host, 0));
            a_.bswap(out);
        }
    };
    loadGuest(R10, RDI);                            // source value
    commitEa(src, szIdx, srcRead);
    loadGuest(R11, RDX);                            // destination value
    commitEa(dst, szIdx, dstRead);
    a_.aluRR(Asm::Op::CMP, hostSz(szIdx), RDX, RDI);   // dst - src
    flagsAddSub(false);                             // CMP leaves X alone
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

    if (in.words > 3) return false;                  // 1 (.S), 2 (.W), 3 (.L)

    const uint32_t target = control.target;
    const uint32_t fall = control.fallthrough;
    if (target & 1) return false;                    // 68040 raises before cond

    const bool always = (cc == 0);
    const int takenCycles = always ? 10 : 6;         // execBra / execBcc
    const int fallCycles = (in.words == 1) ? 4 : 6;
    // A branch charges different amounts on its two paths, so the tracer's
    // single measurement can only confirm the path it took.
    const unsigned traced = traced030(i);
    if (traced != unsigned(takenCycles) && traced != unsigned(fallCycles))
        return false;

    // Is the target inside this block? Only then can it stay in host code.
    int targetIdx = -1;
    for (size_t k = 0; k < ir_.instrs.size(); k++)
        if (ir_.instrs[k].pc == target) { targetIdx = int(k); break; }

    // The i-cache charge — before the condition split so it covers both
    // paths. One-word forms fetch pc and pc+2 on either path. A two-word
    // conditional Bcc fetches those same two words on both paths and ONE
    // more, pc+4, on the fall-through only (readExt consumes the
    // displacement there; the taken path reads it out of queue.irc) — so
    // the common charge here is exactly 2 words and the fall-through path
    // below adds its own. Placed after this emitter's last compile-time
    // refusal; it has no runtime bail, so this is the success path. The
    // traced-queue refusal below is emitted-code-agnostic: emit()'s rewind
    // covers it.
    const bool bccWord = L_.is030 && ic_ && in.words == 2 && !always;
    chargeIcache(i, bccWord ? 2u : 0u);

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
    const uint16_t takenIrc = heldIrc(i);
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
    retire();
    loopTo_ = targetIdx;
    if (targetIdx >= 0) {
        a_.jmp(*entry_[size_t(targetIdx)]);   // the loop stays in host code
    } else {
        leaveTo(target);
    }

    if (!always) {
        a_.bind(notTaken);
        // The fall-through's extra fetch: readExt consumed the displacement
        // and refilled the lookahead at pc+4.
        if (bccWord) chargeIcacheExtraWord(in.pc + 4);
        a_.movMI(Sz::L, at(L_.pc), int32_t(fall));
        a_.movMI(Sz::L, at(L_.pc0), int32_t(fall));
        commitQueue(op, fallIrcQ);
        chargeCycles(fallCycles);
        retire();
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
    if (traced030(i) != 6 && traced030(i) != 10)
        return refuse("cycle cross-check");
    const uint16_t ircAfter = in.extensionWord(0);
    // Every DBcc path leaves the displacement in queue.irc (no readExt, no
    // mode-5 refill); the tracer's terminal capture must agree — a64:2398.
    if (!tracedQueueIs(i, ircAfter)) return refuse("traced queue");

    // The i-cache charge, HERE because a DBcc has no runtime bail: every
    // refusal above is compile-time, so from this point the instruction
    // always executes and the charge is on the success path by
    // construction. Exactly 2 words on every path — all three fetch pc and
    // pc+2 through mmuFetchWord and nothing else (the expired path's extra
    // pc+4 word goes through read<PROG> -> mmuRead, outside PomIcache, and
    // the mode-5 fullPrefetch is a no-op) — a64:2574's finding.
    chargeIcache(i, 2);

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
    retire();
    loopTo_ = targetIdx;
    if (targetIdx >= 0) a_.jmp(*entry_[size_t(targetIdx)]);
    else                leaveTo(target);

    a_.bind(expired);                    // Dn hit -1: fall through, 10 cycles
    a_.movMI(Sz::L, at(L_.pc), int32_t(fall));
    a_.movMI(Sz::L, at(L_.pc0), int32_t(fall));
    commitQueue(op, ircAfter);
    chargeCycles(10);
    retire();
    leaveTo(fall);

    if (cc != 1) {                       // condition true: no decrement, 6
        a_.bind(condTrue);
        a_.movMI(Sz::L, at(L_.pc), int32_t(fall));
        a_.movMI(Sz::L, at(L_.pc0), int32_t(fall));
        commitQueue(op, ircAfter);
        chargeCycles(6);
        retire();
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
    if (ea.idx == EA_PI || ea.idx == EA_PD) return false;    // not encodable
    if (!lengthOk(i, ea.ext)) return false;
    static const int8_t kJmp[EA_MODE_COUNT] =
        { -1, -1, 4, -1, -1, 5, -1, 4, 4, 5, -1, -1 };
    if (kJmp[ea.idx] < 0 || unsigned(kJmp[ea.idx]) != traced030(i)) return false;
    const uint16_t ircAfter = heldIrc(i);      // JMP (xxx).L holds the LOW half
    if (!tracedQueueIs(i, ircAfter)) return false;

    const bool constant = control.targetKnown;
    if (constant && (control.target & 1)) return false;

    addrOf(ea, RDI, 2);
    if (!constant) {                     // odd target: 040 address error
        a_.testRI(Sz::L, RDI, 1);
        a_.jcc(Cc::NE, runtimeStub(i));
    }
    chargeIcache(i);                     // success path: past the last bail
    a_.movMR(Sz::L, at(L_.pc), RDI);
    a_.movMR(Sz::L, at(L_.pc0), RDI);
    commitQueue(op, ircAfter);
    chargeCycles(kJmp[ea.idx]);          // table/base cost; the emitted
                                         // i-cache charge owns the misses
    retire();
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
    // A 030 fault in flight normally leaves a format-$B MOVEM resume state.
    // Native MOVEM cannot reach such a partial state: the OrderedSpan path
    // below proves every byte before the first access, and an unprovable span
    // bails to the untouched instruction.  This is the same proof that has
    // admitted the A64 emitter since 2026-08-23; x64 kept the old guard only
    // until its native 030 lockstep could be run on an x86-64 host.
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
        if (ea.idx == EA_PD) return false;           // not encodable
    } else {
        if (ea.idx == EA_PI || ea.idx == EA_DIPC) return false;
    }

    int n = 0;
    for (int b = 0; b < 16; b++) if (mask & (1 << b)) n++;

    const int base = toRegs ? kMovemToRegs[ea.idx] : kMovemToMem[ea.idx];
    if (base < 0 || unsigned(base + 4 * n) != traced030(i)) return false;
    const MemoryAccessPlan span = memory.access(
        toRegs ? MemoryDirection::Read : MemoryDirection::Write,
        MemoryOperand::RegisterList, uint8_t(size), uint8_t(mode),
        uint8_t(reg));
    const MemoryOrder emittedOrder = ea.idx == EA_PD
        ? MemoryOrder::RegisterDescending : MemoryOrder::RegisterAscending;
    if (!span.valid() || !span.preflight || !memory.complete() ||
        memory.proof.protocol != MemoryProofProtocol::OrderedSpan ||
        in.memory.order != emittedOrder || span.eaCommit != EaCommit::PerElement)
        return false;

    // The restart latch: armed between a faulted MOVEM and its completed
    // re-run. Cold in every normal execution, so one byte test.
    a_.aluMI(Asm::Op::CMP, Sz::B, at(L_.movemArmed), 0);
    a_.jcc(Cc::NE, runtimeStub(i));

    if (ea.idx == EA_PD) {
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
    if (ea.idx == EA_PI) {
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
        case SemanticOp::Exchange: return emitExg(i);
        case SemanticOp::ShiftRegister: return emitShiftRegister(i);
        case SemanticOp::Bitfield: return emitBitfield(i);
        case SemanticOp::MultiplyWord: return emitMultiplyWord(i);
        case SemanticOp::MultiplyLong: return emitMultiplyLong(i);
        case SemanticOp::DivideWord: return emitDivideWord(i);
        case SemanticOp::DivideLong: return emitDivideLong(i);
        case SemanticOp::CompareMemory: return emitCmpm(i);
        case SemanticOp::AddSubQuick: return emitAddSubQ(i);
        case SemanticOp::AddSubExtend: return emitAddSubExtend(i);
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
    if (packedCcr_) loadPackedCcr();
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
        //
        // DBcc is NOT such a branch — a64's finding (JitBackendA64.cpp:2574),
        // and it was 95.68 % of this census's block fallbacks there. All
        // THREE of its mode-5 paths fetch exactly pc and pc+2 through
        // mmuFetchWord (mmuExecuteStart), and nothing more: the displacement
        // is consumed from queue.irc without a readExt on every path, the
        // taken path's fullPrefetch is a no-op on the mode-5 030
        // (MoiraDataflow_cpp.h:865), and the expired path's extra word read
        // at pc+4 goes through read<PROG> -> mmuRead — the DATA-side funnel,
        // which never touches PomIcache. So a charge of exactly 2 words,
        // emitted before the condition, is path-independent and exact.
        const bool dbcc030 = ic_ &&
            ir_.instrs[i].semantics.operation == SemanticOp::DecrementBranch;
        // Widening this exemption to single-path transfers used to diverge
        // at step 16097; that was the peripheral-phase class, closed
        // 2026-08-21 (JIT_BRINGUP § C.4nonies). BRA.W/L and simple JSR/JMP
        // now share `provedLinearControlFetch030`; indexed JSR additionally
        // proves computeEA's one final refill. BSR.W keeps its independently
        // gated proof (fetchWords=2, no readExt). Indexed JMP remains out.
        //
        // A CONDITIONAL two-word Bcc is different: its fetch model is
        // proved against the mode-5 source — both paths fetch pc and
        // pc+2 (mmuExecuteStart), and only the fall-through consumes the
        // displacement through readExt, fetching pc+4 (execBcc). So
        // emitBranch charges the common two words before the condition
        // and the fall-through path adds its own extra word; nothing is
        // path-ambiguous. Only longer conditional forms stay refused.
        const bool bccW030 = ic_ && ir_.instrs[i].words == 2 &&
            ir_.instrs[i].semantics.operation == SemanticOp::Branch &&
            ir_.instrs[i].semantics.condition != 0;
        // JSR d16(PC) ($4EBA): one path, two words, both fetched at pc and
        // pc+2 through mmuFetchWord and nothing else — the displacement is
        // consumed from queue.irc, the traced fetch count is 2, and
        // tracedQueueIs pins the held displacement. 6.8 % of the LC II
        // fallback census, 120k-gated. BSR.W ($6100) rides the reproducer
        // knob: its traced linear charge is CORRECT (fetchWords=2, proved
        // 2026-08-21 — mode-5 execBsr consumes the displacement from
        // queue.irc with no readExt), and its historical step-16097
        // divergence was the peripheral-phase class, closed by the
        // access-thunk clock alignment (JIT_BRINGUP § C.4nonies;
        // jit_lockstep_030_x64_alignment_test runs this admission ON).
        // Default rides the backend's accessClockBias declaration (ON
        // here since 2026-08-22, −2.3 % alone, −8.0 % with restart-base).
        const bool controlLinear030 = ic_ &&
            provedLinearControlFetch030(ir_.instrs[i]);
        const bool bsrW030 = ic_ && ir_.instrs[i].words == 2 &&
            ir_.instrs[i].opcode == 0x6100 && bsrWideAdmission() &&
            ir_.instrs[i].semantics.operation == SemanticOp::BranchSubroutine;
        if (ic_ && ir_.instrs[i].kind == Kind::Branch &&
            ir_.instrs[i].words > 1 && !dbcc030 && !bccW030 &&
            !controlLinear030 && !bsrW030) {
            a_.jmp(staticStub(i));
            emitted = i + 1;
            continue;
        }
        // No traced fetch count, no charge: an emitted charge that guesses
        // words + 1 over-fetches every SKIP_LAST_RD form. IRs recorded by
        // this tree always carry the count on an 030; this is the guard
        // that keeps a stale or foreign IR honest.
        if (ic_ && !dbcc030 && !ir_.instrs[i].fetchWords) {
            a_.jmp(staticStub(i));
            emitted = i + 1;
            continue;
        }

        const Asm::Mark mark = a_.mark();
        pollIpl();
        instructionCycles_ = uint16_t(
            L_.is030 ? ir_.instrs[i].baseCycles : ir_.instrs[i].cycles);
        const bool native = emitInstr(i);
        emitted = i + 1;

        if (!native) {
            // Leave no trace of a family that gave up half-way — including
            // the IPL sample, which the fallback's own mmu040InstrStart
            // will perform. Then send
            // the instruction to its stub in the cold half. The i-cache
            // SHADOW still advances: the interpreter's run of this very
            // instruction performs the same line transitions.
            a_.rewind(mark);
            shadowIcache(i, dbcc030 ? 2u : 0u);
            a_.jmp(staticStub(i));
            continue;
        }
        native_++;
        if (ir_.instrs[i].kind == Kind::Branch) break;   // it committed its own

        // The 68030 i-cache charge — on the SUCCESS path only, after every
        // runtime bail the body can take, so no exit can ever carry a
        // charge for an instruction that did not run (see shadowIcache's
        // header comment for the divergence the old charge-first order
        // cost). Branch-kind terminals charge inside their own emitters, at
        // the first point past their last possible bail.
        chargeIcache(i);

        // No pc/pc0 here: nothing downstream reads them until the block
        // exits, and every exit commits them on the way out. The queue does
        // stay — see emitBoundary.
        // a64:2612's rule: the tracer's terminal capture wins when it
        // exists (SKIP_LAST_RD forms hold their last extension, not the
        // next word); the linear-next word is the fallback for mid-block
        // instructions, whose commit the next instruction overwrites.
        commitQueue(ir_.instrs[i].opcode,
                    ir_.instrs[i].terminalQueueValid
                        ? ir_.instrs[i].terminalIrc
                        : ir_.prefetchWord(ir_.instrs[i].pc +
                                           uint32_t(ir_.instrs[i].words) * 2));
        // On the 030 the emitted i-cache model (chargeIcache) has already
        // reproduced the fetch component, so a native instruction owes only
        // the tracer's BASE cost. Charging the traced total pays the
        // trace-time miss a SECOND time whenever the current run reproduces
        // it: +4 per miss, the 5956-step lockstep divergence that shelved
        // the first split admissions. Instructions traced on hits have
        // base == cycles, so this changes behaviour only for the admitted
        // split forms — a64's rule verbatim (JitBackendA64.cpp:2622).
        chargeCycles(int(instructionCycles_));
        retire();
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
        retire();
        a_.aluMI(Asm::Op::ADD, Sz::L, F(kFSlow), 1);
        // Moira may have written guest memory, and a write into this very
        // block's page leaves everything compiled after it stale. The
        // engine's write guard is the only thing that can see that.
        if (ir_.instrs[i].flags & FlagMayTrap) {
            // Internal DIV/CHK exceptions return "retired" from Moira but
            // redirect PC. Keep that vector boundary instead of continuing
            // at the next generated entry and overwriting it.
            Label& straight = *a_.fresh();
            a_.movRM(Sz::L, RAX, at(L_.pc));
            a_.aluRI(Asm::Op::CMP, Sz::L, RAX,
                     int32_t(ir_.instrs[i].pc +
                             uint32_t(ir_.instrs[i].words) * 2));
            a_.jcc(Cc::E, straight);
            a_.movMI(Sz::L, F(kFExit), int32_t(Exit::BlockEnd));
            a_.jmp(*epilogue_);
            a_.bind(straight);
        }
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
    retire();                                        // it ran, it just faulted
    a_.movMI(Sz::L, F(kFExit), int32_t(Exit::Fault));

    a_.bind(*epilogue_);
    if (packedCcr_) spillPackedCcr();
    spillRetiredCount();
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

    bool usable() const override {
#ifdef _WIN32
        // Every emitted prologue and thunk call in this file follows the
        // System V AMD64 ABI: arguments in RDI/RSI, RSI/RDI treated as
        // scratch, no 32-byte shadow space. Win64 places arguments in
        // RCX/RDX, makes RSI/RDI callee-saved and requires the shadow
        // space, so generated code would dereference an arbitrary pointer
        // on its first block and corrupt the caller's registers. The build
        // already refuses `auto` on Windows (CMakeLists.txt, JIT backend
        // block); this guard closes the explicit `-DPOM68K_JIT_BACKENDS=x64`
        // bypass. Remove both together, behind a gate, the day this file
        // learns the Win64 ABI.
        return false;
#else
        return CodeBuffer::supported();
#endif
    }

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
        // Widening was real work, not a flag flip — and it is DONE for
        // correctness (2026-08-18): the 030 branch in pomJitProbeData, the
        // split-timing consumption, the taken-short-branch IRC fix, the
        // write-block chain-boundary contract, and the
        // jit_lockstep_030_x64_experimental_test gate that holds all of it
        // to 120 000 identical steps. The declaration below is therefore
        // CORRECTNESS scope: an explicit POM68K_JIT_BACKEND=x64 on a 68030
        // is honoured without the unsafe override.
        c.guestFamilies = kGuest68040 | kGuest68030;
        // SPEED scope. The 68030 entered on 2026-08-21, on D.1 evidence:
        // bench −12 % vs `threaded` at the default budget, fingerprints
        // identical (JIT_BRINGUP § C.4sexies); the 120k lockstep gate; and
        // the IIsi — whose SIGSEGV ~5 s into boot blocked this line for two
        // days (§ C.4septies) — now boots the generator green natively on
        // the host that produced the crash, under the hardened
        // POM68K_JIT_REQUIRE_NATIVE gate that can no longer silently pass
        // on `threaded`. The crash did not survive the 2026-08-19/21
        // engine hardening; § C.4septies keeps the reproducer and the
        // triage order in case it returns.
        //
        // **68030 WITHDRAWN 2026-08-29.** The line above is kept because
        // its reasoning is still the record of how the pair entered; what
        // it did not survive is the first 68030 tier to run since the day
        // it entered. `ctest -L m030` hung EVERY 68030 gate that reaches
        // this backend — 23 `Timeout`, 20 more still running after 11 h —
        // while the six `interp_*` references, the two 68020 Mac LC gates
        // and the unit tests passed. Measured, not attributed: two arms
        // built from `d4a18b6` WITHOUT that day's Moira patch 31 wedge
        // `lcii_boot_etalon` identically (x64 pinned 900.08 s, HEAD's own
        // `auto` default 900.06 s), so this is a REGRESSION of the eight
        // days since 2026-08-21, in ordinary post-MMU generated code, and
        // its product consequence is that no 68030 machine boots under the
        // shipping default on an x86-64 host. So SPEED scope drops back to
        // the 68040 and `auto` resolves an 030 to `threaded` — proven
        // identical over 120 000 lockstep steps. CORRECTNESS scope above
        // is untouched, so the pinned `jit_*_boot_etalon` and lockstep
        // gates keep exercising this generator and keep saying what it is
        // worth; the lockstep also carries a deterministic
        // i-cache-accounting divergence at step 5 956, localised to the
        // block at $00A416AE. Restore the 030 here only with a green m030
        // tier behind it, not with a bench number (CHANGELOG.md
        // 2026-08-29).
        c.autoFamilies = kGuest68040;
        // The access thunks bias the clock by the not-yet-charged i-cache
        // fetch penalty for the access alone (pom68kJitRead/Write, JIT_BRINGUP
        // § C.4nonies) — the declaration that turns the restart-base and
        // BSR.W admission DEFAULTS on for this backend. Flipped 2026-08-22
        // on the measured evidence: −4.3 % and −2.3 % alone, −8.0 % for the
        // pair at 6000 frames, fingerprint identical, and the 120k
        // alignment gate holding both admissions to identical state. a64
        // declares it too since the same afternoon (its thunks carry the
        // same bias, replacing the guardIcacheHits replay).
        c.accessClockBias = true;
        // Exact WRITES (thunk mode 2) are withdrawn as a 68030 default,
        // 2026-08-29: the first x86-64 boot to reach post-MMU-enable 030
        // code under them never finishes — the guest advances, but the
        // engine burns its wall in serviceGuard() (SIGABRT core taken at
        // 90 s into a 2.7 s workload sits in the slice-index hash walk).
        // Mode 1 boots the LC II to the Finder in 53 s pinned (threaded:
        // 119 s) and prints fingerprints identical to threaded at every
        // budget tried. The mechanism of the mode-2 storm is NOT yet run
        // to ground — this cap is a default, not a fix, and the explicit
        // env override still selects mode 2 for the hunt.
        c.maxAccessThunk030 = 1;
        return c;
    }

    bool canEmit(uint16_t op) const override;

    CompileResult compile(const BlockIr& ir, const Context& ctx) override;
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
    const bool eaOk = eaCostIndex(mode, reg) >= 0;
    const auto controlEa = [](int index) {
        return index == EA_AI || index == EA_DI || index == EA_AW ||
               index == EA_AL || index == EA_DIPC;
    };
    switch (sem.operation) {
        case SemanticOp::ImmediateAlu:
            return eaOk;
        case SemanticOp::Bit:
            return sem.action == 0 && eaOk && (!sem.dynamic || mode != 1);
        case SemanticOp::Move: {
            const int dm = sem.destinationMode, dr = sem.destinationReg;
            return eaOk && eaCostIndex(dm, dr) >= 0 && kMoveDst[eaCostIndex(dm, dr)] >= 0;
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
        case SemanticOp::AddSubExtend:
            return true;
        case SemanticOp::JumpSubroutine:
            return controlEa(eaCostIndex(mode, reg)) ||
                   eaCostIndex(mode, reg) == EA_IX ||
                   eaCostIndex(mode, reg) == EA_IXPC;
        case SemanticOp::Jump:
            return controlEa(eaCostIndex(mode, reg));
        case SemanticOp::ShiftRegister:
            return sem.action != 2 || op == 0xE410;
        case SemanticOp::Bitfield:
            // a64:1100's rule verbatim: register, (An) and d16(An)
            // operands. The emitter then refuses the forms it cannot lower
            // yet (memory, dynamic o/w) — the a64 -1-row pattern, so the
            // two backends' admission stays comparable at this level.
            return mode == 0 || mode == 2 || mode == 5;
        case SemanticOp::DivideWord:
            return eaCostIndex(mode, reg) >= 0 &&
                   eaCostIndex(mode, reg) != EA_AN;
        case SemanticOp::MultiplyWord:
            return eaCostIndex(mode, reg) >= 0 &&
                   eaCostIndex(mode, reg) != EA_AN;
        case SemanticOp::MultiplyLong:
            return op == 0x4C00;
        case SemanticOp::DivideLong:
            return eaCostIndex(mode, reg) >= 0 &&
                   eaCostIndex(mode, reg) != EA_AN;
        case SemanticOp::Lea:
            return controlEa(eaCostIndex(mode, reg)) ||
                   eaCostIndex(mode, reg) == EA_IX ||
                   eaCostIndex(mode, reg) == EA_IXPC;
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
                                 eaCostIndex(mode, reg) != EA_DIPC &&
                                 eaCostIndex(mode, reg) != EA_IM);
        case SemanticOp::Exchange:
            return true;
        case SemanticOp::CompareMemory:
            // Same-register CMPM: the second EA depends on the first
            // increment; the preflight-all lowering is the independent-EA
            // subset (a64's canEmit rule verbatim).
            return sem.eaReg != sem.destinationReg;
        default: return false;
    }
}

CompileResult X64Backend::compile(const BlockIr& ir, const Context& ctx) {
    if (diagLeft_ < 0) diagLeft_ = verboseBlocks();
    if (!ctx.cpu) return {nullptr, CompileReject::Context};
    // Generated code models POLL_IPL as a plain assignment. If the deferred
    // IPL-recognition feature is ever armed, this backend has nothing to say.
    if (!ctx.cpu->pomJitSimpleIpl()) return {nullptr, CompileReject::Context};
    if (!haveLayout_) {
        layout_ = ctx.cpu->pomJitLayout();
        haveLayout_ = true;
    }
    if (!buf_.valid() && !buf_.reserve(kCodeBufferBytes))
        return {nullptr, CompileReject::CodeMemory};
    if (ir.code.empty()) return {nullptr, CompileReject::Context};

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
    if (!ok) return {nullptr, CompileReject::Emit};

    // A block of nothing but fallbacks is strictly worse than the fetch
    // window loop: same interpreter work, plus a call and a frame. Refuse
    // it and let the engine run the window instead. The bar is
    // POM68K_JIT_MIN_NATIVE percent (JitConfig.h owns the rationale and the
    // 68030 measurement that made it a knob).
    if (e.nativeCount() * 100 < int(ir.instrs.size()) * minNativePercent()) {
        if (verbose() && diagLeft_ > 0) std::fprintf(stderr, "[jit]   ^ REFUSED(coverage)\n");
        return {nullptr, CompileReject::Coverage};
    }

    if (!buf_.makeWritable()) return {nullptr, CompileReject::CodeMemory};
    uint8_t* dst = buf_.alloc(a.size(), 16);
    if (!dst) return {nullptr, CompileReject::CodeCapacity}; // engine recycles
    // Every branch the block contains is internal and rel32-encoded, and
    // the only absolute addresses baked in are the thunks', so the finished
    // bytes are position independent and a plain copy is a valid move.
    std::memcpy(dst, scratch_.data(), a.size());
    if (!buf_.makeExecutable()) return {nullptr, CompileReject::CodeMemory};

    X64Compiled* c = new X64Compiled();
    c->entry = dst;
    // The write-block chain-boundary contract, second half (see the
    // Emitter ctor): the fault-frame gate proves REPLAY, not transparent
    // links, so nothing may jump into this block without the Engine
    // between them.
    // Blocks with restartable 030 writes take links again since
    // 2026-08-19: the a64 divergence that suppressed them (lockstep step
    // 10455 — a peripheral delivery at identical clocks with differing
    // PCs) is the exact signature of the i-cache uncharge hole fixed the
    // same day (see shadowIcache), and with the charge on the success
    // path the full 120k x64 gate holds with links restored.
    c->linked = dst + e.linkEntryOffset();
    return {c, CompileReject::None};
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
