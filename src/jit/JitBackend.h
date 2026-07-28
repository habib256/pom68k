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
//   aarch64   — planned; see src/jit/backends/JitBackendA64.md.
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
};

// A backend's compiled artefact. Opaque above this header: the threaded
// backend stores a verified script, a code generator stores a pointer into
// its executable buffer.
class Compiled {
public:
    virtual ~Compiled() = default;
    const BlockIr* ir = nullptr;   // owned by the engine's block cache
};

// Everything a running block is allowed to touch.
struct Context {
    moira::Moira* cpu = nullptr;
    Stats*        stats = nullptr;
    int64_t       clockTarget = 0;   // stop before running past this

    // ── the data path a code generator needs ─────────────────────────────
    // Generated code translates guest data addresses through Moira's data
    // TLB inline, and calls this only on a miss. It never throws and never
    // performs a guest side effect: a nullptr answer means "this access is
    // not a plain memory access", and the block hands that one instruction
    // back to the interpreter.
    uint8_t* (*dtlbFill)(void* self, uint32_t addr, int write) = nullptr;
    void*    dtlbSelf = nullptr;
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

    // Set by the memory map when a guest write lands in a page holding
    // translated code. Generated code re-checks it after any store it did
    // not perform itself.
    const CodeGuard* guard = nullptr;

    // ── peripheral pacing ────────────────────────────────────────────────
    // The CPU wrapper batches VIA/ASC/SWIM/MCU time: sync() advances the
    // clock and only runs the machine forward once `periphBatch` cycles
    // have piled up since `periphClock`. Generated code is given both so it
    // can make that test INLINE and call out only when it is actually due —
    // a call on every instruction costs more than most instructions do.
    // Null pointer = no pacing information, call out every time. Typed as
    // void because the only consumer is generated code, which just loads
    // 64 bits from it — and because the CPU wrappers spell that counter
    // moira::i64, which is not the same TYPE as int64_t on every platform
    // even when it is the same width.
    const void* periphClock = nullptr;
    int         periphBatch = 0;
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
    virtual Compiled* compile(const BlockIr& ir, const Context& ctx) = 0;

    // Where a LINKED jump enters this block: past the prologue, because the
    // callee-saved registers the block runs on were already set up by
    // whichever block the chain started in. Null = this backend does not
    // support being jumped into, and the engine will not publish it.
    virtual void* linkEntry(Compiled*) const { return nullptr; }
    virtual RunResult run(Compiled* c, Context& ctx) = 0;
    virtual void release(Compiled* c) = 0;
    virtual void flushAll() = 0;
};

// Picks a backend for `pref` ("auto", "threaded", "x64", "a64", …).
// Never returns nullptr: `threaded` is always compiled in and always usable.
Backend* selectBackend(const char* pref);

// Names of every backend compiled into this binary, most capable first.
// `count` receives the number of entries. For the GUI and for diagnostics.
const char* const* backendNames(int& count);

}  // namespace jit
