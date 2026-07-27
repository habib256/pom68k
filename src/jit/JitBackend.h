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
};

struct RunResult {
    uint32_t instrs = 0;             // guest instructions actually retired
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

    virtual Compiled* compile(const BlockIr& ir) = 0;
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
