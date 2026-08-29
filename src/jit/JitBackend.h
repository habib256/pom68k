// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── JIT backend interface (the host-architecture seam) ──
// Everything above this header is host-agnostic; everything that knows what
// an x86-64 or an AArch64 instruction looks like lives below it, in
// src/jit/backends/. POM68K is multiplatform, so the JIT is multi-target by
// construction and NOT by later refactoring:
//
//   threaded  — no code generation at all. Replays a recorded block through
//               Moira's own instruction handlers with the code window armed.
//               Builds and runs on every host POM68K compiles for, including
//               Emscripten. This is the floor: `auto` always lands here when
//               nothing better is available.
//   x86-64    — native code generation (J2).
//   aarch64   — native 68040 generator and the automatic arm64 choice; see
//               src/jit/backends/JitBackendA64.md.
//
// A backend advertises what it can do through caps(); the block builder
// consults that and simply stops a block before anything the backend cannot
// handle. A new backend can therefore start with a handful of opcodes and
// grow without the engine changing at all.

#pragma once
#include "JitGuard.h"
#include "JitIr.h"
#include "JitStats.h"

#include <cstdint>

namespace moira { class Moira; }

namespace jit {

struct ResolvedConfig;

using WriteObserver = void (*)(void*, moira::Moira*, uint32_t, uint32_t,
                               uint32_t, uint32_t, int);
using PeriphDue = void (*)(moira::Moira*);
// Returns the address of a stable cell whose first machine word is either a
// compiled linked entry or null. The engine owns and invalidates the cell;
// generated code may embed its address but never the entry itself.
using LinkCellLookup = void* (*)(void* self, uint32_t pc, bool super);

// Which GUEST CPU families a backend's compiled form is semantically valid
// for. This is not a performance hint and not a capability ranking: Moira's
// own dataflow branches on the model, so a generator that bakes in one
// family's conventions is WRONG on another, not merely slower.
//
//   * `(An)+` updates the register BEFORE the access on a 68030 and AFTER it
//     on a 68040 (MoiraDataflow_cpp.h:326-332);
//   * the 68030 marks its last write restartable and stacks a format $A
//     frame (:355-361), the 68040 does not;
//   * both mode-5 cores suppress the tail refill, so a block must preserve
//     the exact held word (lookahead, extension or displacement) rather than
//     manufacture `queue.irc` from the exit PC.
//
// A backend that REPLAYS through Moira's own handlers inherits whatever the
// model does and therefore covers everything; a code generator has to
// declare, honestly, the families it was written and measured against.
//
// History: the x86-64 generator was written entirely against the 68040 (its
// cost tables are the 68020 column "which is what the 68040 core uses") and
// carried no such declaration. The moment `auto` started choosing it
// (2026-07-29) it was handed the 68030 LC II, where it wedged the guest in
// the ROM's Egret handshake loop — `jit_lcii_boot_etalon` timed out at an
// hour while the same machine booted in 2 min 21 s on `threaded`.
enum GuestFamily : uint32_t {
    kGuest68000 = 1u << 0,      // 68000 / 68010
    kGuest68020 = 1u << 1,      // 68020 / 68EC020
    kGuest68030 = 1u << 2,
    kGuest68040 = 1u << 3,      // 68040 / 68LC040
    kGuestAny   = kGuest68000 | kGuest68020 | kGuest68030 | kGuest68040,
};

// What a backend is able to turn into its own executable form. Anything not
// covered here is left to the interpreter — never emulated approximately.
struct BackendCaps {
    bool nativeCode   = false;  // emits host machine code (needs W^X memory)
    bool aluReg       = false;  // register-to-register ALU
    bool aluMem       = false;  // ALU with a plain-RAM operand
    bool moves        = false;
    bool branches     = false;  // internal branches (block-local)
    bool addrModes    = false;  // indexed / displacement modes
    int  maxBlockInstrs = 64;

    // Generated code tests PomJitDtlbEntry::codeMask before a store, so the
    // engine may hand it a write entry for a page that holds translated code
    // in SOME slice. A backend that does not (or that emits no code at all)
    // gets the older, coarser answer: no write entry for such a page, ever.
    //
    // This is a SAFETY declaration, like guestFamilies: a backend that stores
    // through an entry without testing the mask bypasses the memory map, so
    // jit::CodeGuard never sees the store and a block translated from that
    // page is never evicted — self-modifying code, undetected. Defaulting to
    // false means a new backend is conservative until it says otherwise.
    bool dtlbCodeMask = false;

    // The exact access thunks carry the peripheral-phase access-clock bias
    // (JIT_BRINGUP § C.4nonies): a device access flushes peripheral time
    // at the clock the interpreter's same access would see, because the
    // thunk biases it by the instruction's not-yet-charged i-cache fetch
    // penalty for the access alone. Declaring it is what turns the
    // § C.4nonies admission DEFAULTS on for this backend (restart-base,
    // BSR.W) — the coupling is deliberate: an admission default must never
    // outrun the alignment on the ISA that runs it. Both native backends
    // declare it since 2026-08-22 (x64 in the morning, a64 the same
    // afternoon — pom68kA64Read/Write replaced the guardIcacheHits replay
    // with the bias); `threaded` does not, and does not need to: it
    // replays through Moira's own handlers, which charge at fetch time.
    bool accessClockBias = false;

    // Bitmask of GuestFamily. Deliberately defaults to 0 = "not declared",
    // which selection treats as "do not use": a new backend that forgets to
    // state its scope gets a diagnostic, not a silent wedge on the first
    // machine nobody tested it against. That is the failure this field
    // exists to prevent.
    uint32_t guestFamilies = 0;

    // Bitmask of GuestFamily `auto` may RESOLVE to this backend — the SPEED
    // declaration, where `guestFamilies` above is the correctness one, and
    // the two are deliberately separate (JIT_BRINGUP § C.5): a family
    // enters this mask per (family, backend) pair on D.1 evidence — the
    // lockstep gate, the boot etalons, a fixed-budget bench win and the
    // full etalon tier — never as a side effect of an emitter growing.
    // Defaults to 0 like `guestFamilies`, and for the same reason: a new
    // backend earns its place in `auto` with a measurement. An explicit
    // POM68K_JIT_BACKEND=<name> consults `guestFamilies` only.
    //
    // History: x64 has carried the 68040 since J2. Its 68030 bench win is
    // measured (−12 % at the default budget, JIT_BRINGUP § C.4sexies) but
    // the family waits on the IIsi segfault under the generator
    // (§ C.4septies) — adding it is the C.5 flip. a64 independently added
    // 68030 on 2026-08-20 after native-state hardening, a matching-fingerprint
    // bench win, the long lockstep and native platform gates.
    uint32_t autoFamilies = 0;

    // Backend/family-specific cold-code admission. Zero preserves the
    // engine-wide default. The AArch64 68030 pays a substantial emitted
    // i-cache model per block and has measured better when a block first
    // earns `visits * potentially-native instructions >= 64`; other pairs
    // keep immediate native compilation until they independently earn a
    // different value. An explicit POM68K_JIT_PROFIT_SCORE always wins.
    uint32_t profitScore68030 = 0;
    // Highest access-thunk mode (JitConfig accessThunk: 0 none, 1 exact
    // reads, 2 exact reads+writes) this backend supports as a DEFAULT on a
    // 68030 guest. An explicit POM68K_JIT_ACCESS_THUNK still wins. Added
    // 2026-08-29: under exact writes (mode 2) the x64 generator wedges the
    // LC II boot right after MMU enable — 250 bench frames take >600 s
    // against 1.27 s at mode 1, the abort core is inside serviceGuard(),
    // and modes 0/1 print threaded's exact fingerprint. 2 = no cap.
    int maxAccessThunk030 = 2;
};

// A backend's compiled artefact. Opaque above this header: the threaded
// backend stores a verified script, a code generator stores a pointer into
// its executable buffer.
class Compiled {
public:
    virtual ~Compiled() = default;
    const BlockIr* ir = nullptr;   // owned by the engine's block cache
};

struct CompileResult {
    Compiled* code = nullptr;
    CompileReject reject = CompileReject::None;
};

// Diagnostic-only attribution of a native instruction's final runtime
// fallback. The overall runtime opcode census remains the source of truth;
// these buckets explain it and must sum back to it when enabled.
enum RuntimeFallbackReason : uint32_t {
    RuntimeFillTag = 0,       // tag miss whose exact ATC/data probe refused
    RuntimeNonPlain,          // MMIO, ROM write, hole, or remembered null host
    RuntimeCodeMask,          // store overlaps translated-code slice
    RuntimeCrossPage,         // one access straddles a 4 KiB DTLB slice
    RuntimeOther,             // CPU/queue/branch guard unrelated to data mapping
    RuntimeReasonCount
};

using RuntimeAddressObserver = void (*)(void* self, uint32_t reason,
                                        uint32_t opcode, uint32_t address,
                                        uint32_t bytes, uint32_t write,
                                        uint32_t codeMask);

// Everything a running block is allowed to touch.
struct Context {
    moira::Moira* cpu = nullptr;
    Stats*        stats = nullptr;
    const ResolvedConfig* config = nullptr; // immutable per-Engine policy
    int64_t       clockTarget = 0;   // stop before running past this

    // ── the data path a code generator needs ─────────────────────────────
    // Generated code translates guest data addresses through Moira's data
    // TLB inline, and calls this only on a miss. It never throws and never
    // performs a guest side effect: a nullptr answer means "this access is
    // not a plain memory access", and the block hands that one instruction
    // back to the interpreter.
    uint8_t* (*dtlbFill)(void* self, uint32_t addr, int write) = nullptr;
    void*    dtlbSelf = nullptr;
    WriteObserver observeWrite = nullptr;
    void* observeWriteSelf = nullptr;
    // ── block linking ────────────────────────────────────────────────────
    // A direct-mapped pc -> compiled-entry table, owned by the engine and
    // read by generated code at a block's exit. Without it, a block that
    // ends on a branch returns to the engine, which then does a hash lookup
    // and sets up a fresh frame — and on a real Mac OS workload a THIRD of
    // all instructions are control transfers, so blocks run five
    // instructions and that per-entry cost dominates everything else the
    // code generator does. With it, the exit is a tag compare and a jump.
    //
    // Deliberately a table and not patched call sites: a boot evicts blocks
    // hundreds of thousands of times, and un-patching every jump INTO an
    // evicted block means maintaining incoming/outgoing link lists that can
    // dangle. Invalidating one table slot is O(1) and cannot dangle.
    void*    linkTable = nullptr;
    uint32_t linkMask = 0;             // entries - 1, 0 = linking disabled
    LinkCellLookup linkCell = nullptr;  // exact constant-edge dependency cell
    void* linkCellSelf = nullptr;

    // Set by the memory map when a guest write lands in a page holding
    // translated code. Generated code re-checks it after any store it did
    // not perform itself.
    const CodeGuard* guard = nullptr;

    // ── peripheral pacing ────────────────────────────────────────────────
    // The CPU wrapper either batches VIA/ASC/SWIM/MCU time (`periphBatch`
    // positive, `periphClock` is the baseline) or exposes an absolute next
    // event deadline (`periphBatch == -1`). Generated code is given both so it
    // can make that test INLINE and call out only when it is actually due —
    // a call on every instruction costs more than most instructions do.
    // Null pointer = no pacing information, call out every time. Typed as
    // void because the only consumer is generated code, which just loads
    // 64 bits from it — and because the CPU wrappers spell that counter
    // moira::i64, which is not the same TYPE as int64_t on every platform
    // even when it is the same width.
    const void* periphClock = nullptr;
    int         periphBatch = 0;       // -1 = periphClock is an absolute deadline
    PeriphDue   periphDue = nullptr;   // native test already proved it due

    // Optional dynamic fallback census. Native backends increment the
    // opcode slot that led to a cold interpreter stub. Keeping the two
    // causes separate tells unsupported ISA coverage from an otherwise
    // native instruction whose runtime access/guard could not be inlined.
    uint64_t* slowStaticHisto = nullptr;
    uint64_t* slowRuntimeHisto = nullptr;
    uint64_t* slowRuntimeReasonHisto = nullptr; // [reason][opcode]
    const uint32_t* dtlbFillReason = nullptr;   // last fillDtlb refusal
    RuntimeAddressObserver runtimeAddressObserver = nullptr;
    void* runtimeAddressSelf = nullptr;
};

struct RunResult {
    uint32_t instrs = 0;             // guest instructions actually retired
    // …of which this many went through the backend's per-instruction
    // fallback rather than host code. The ratio is the gauge that says
    // whether a code generator is doing anything at all.
    uint32_t slowInstrs = 0;
    Exit     exit = Exit::BlockEnd;
};

class Backend {
public:
    virtual ~Backend() = default;

    // Native backends own mutable code buffers and therefore cannot be
    // shared by two emulated CPUs in one process. Registry entries are
    // prototypes; returning a clone gives each Engine an independent code
    // cache. Stateless backends keep the default nullptr and stay shared.
    virtual Backend* clone() const { return nullptr; }

    virtual const char* name() const = 0;          // "threaded" | "x86-64" | …
    virtual const char* description() const = 0;   // shown in the GUI

    // Runtime check, not a compile-time one: a backend can be compiled in
    // and still be unusable on this machine (no executable memory allowed,
    // missing CPU feature, hardened kernel). Returning false is a normal
    // outcome, not an error — selection falls through to the next candidate.
    virtual bool usable() const = 0;

    virtual BackendCaps caps() const = 0;

    // True when compile() will turn this opcode into host code rather than
    // hand it back to Moira. The block builder does not consult it (every
    // backend can always fall back), but the opcode census does — printing
    // the covered share of a real boot next to the census is how the native
    // instruction set was chosen and is how its growth is measured.
    virtual bool canEmit(uint16_t /*opcode*/) const { return false; }

    // `ctx` is the engine's live context: a code generator reads the
    // register-file layout and the data-TLB door from it at compile time.
    // A refusal names its whole-block cause so the engine can price rejected
    // work independently from fallbacks inside an accepted block.
    virtual CompileResult compile(const BlockIr& ir, const Context& ctx) = 0;

    // Where a LINKED jump enters this block: past the prologue, because the
    // callee-saved registers the block runs on were already set up by
    // whichever block the chain started in. Null = this backend does not
    // support being jumped into, and the engine will not publish it.
    virtual void* linkEntry(Compiled*) const { return nullptr; }
    virtual RunResult run(Compiled* c, Context& ctx) = 0;
    virtual void release(Compiled* c) = 0;
    virtual void flushAll() = 0;
};

// Picks a backend for `pref` ("auto", "threaded", "x64", "a64", …) that is
// usable on this host AND valid for `guestFamily` (one GuestFamily bit — the
// CPU model the engine is attached to). Never returns nullptr: `threaded` is
// always compiled in, always usable and valid for every guest.
Backend* selectBackend(const char* pref, uint32_t guestFamily,
                       bool unsafeBackend = false);

// Names of every backend compiled into this binary, most capable first.
// `count` receives the number of entries. For the GUI and for diagnostics.
const char* const* backendNames(int& count);

// The registry KEYS, in the same order — what POM68K_JIT_BACKEND accepts and
// what selectBackend() matches on. Deliberately separate from
// backendNames(): the key is "x64" while the display name is "x86-64", and
// feeding a display name to selectBackend() silently resolves to "unknown
// name -> auto". A scope check written that way tested nothing at all
// (caught 2026-07-30 by its own stderr).
const char* const* backendKeys(int& count);

}  // namespace jit
