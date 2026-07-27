// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── JIT engine (host-agnostic layer 1) ──
// The second execution engine, sitting BESIDE the Moira interpreter and
// never in front of it: off by default, switchable at run time, and always
// able to hand a program counter back to the interpreter at an instruction
// boundary with exact guest state.
//
// What the engine owns:
//   * the instruction-fetch code window (arming, validating, dropping it);
//   * basic-block discovery by tracing, and the block cache;
//   * the fallback policy — anything unusual is the interpreter's job;
//   * the gauges.
// What it does NOT own: any knowledge of the host architecture. That lives
// entirely behind jit::Backend (src/jit/JitBackend.h).
//
// See src/jit/POM68K_JIT.md for the invariants this file is required to
// uphold, and extern/moira/POM68K_VENDOR.md for the three-point seam it
// relies on inside the vendored core.

#pragma once
#include "JitBackend.h"
#include "JitConfig.h"
#include "JitGuard.h"
#include "JitIr.h"
#include "JitStats.h"

#include <cstdint>
#include <vector>
#include <unordered_map>

namespace moira { class Moira; }

namespace jit {

// How the engine reaches the machine's memory map without knowing which
// machine it is. Bound once, by the CPU wrapper, with captureless lambdas —
// no virtual dispatch, no template instantiation of the engine per machine.
struct MemoryHooks {
    void* self = nullptr;
    // Host pointer to readable bytes at PHYSICAL `phys`; `len` receives how
    // many contiguous bytes are valid. Must return nullptr for anything that
    // is not plain RAM or plain ROM — I/O, VRAM, anything with a read side
    // effect, and the whole map while the boot overlay is still up.
    const uint8_t* (*codeSpan)(void* self, uint32_t phys, uint32_t& len) = nullptr;
    // Attaches (or detaches, with nullptr) the write guard.
    void (*setGuard)(void* self, CodeGuard* guard) = nullptr;
    // Physical RAM size, for sizing the guard's page map.
    uint32_t (*ramBytes)(void* self) = nullptr;
};

class Engine {
public:
    Engine(moira::Moira& cpu, const MemoryHooks& mem);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    bool enabled() const { return enabled_; }
    // Switching engines is only ever done between two instructions (the GUI
    // routes it through the machine thread's command queue), and it always
    // drops every cached artefact.
    void setEnabled(bool on);

    const char* backendName() const;
    const char* backendDescription() const;

    // The single entry point the CPU wrappers call instead of
    // Moira::executeUntil() when the engine is on.
    void executeUntil(int64_t clockTarget);

    // Everything cached is dropped: the code window, the block cache and
    // whatever the backend is holding. Called on hard reset, on cache
    // control writes, and whenever the memory map moves.
    void flushAll();

    const Stats& stats() const { return stats_; }
    Stats& stats() { return stats_; }

    // Gauge helper for the GUI: guest instructions per second, measured over
    // the caller's own wall clock (the engine does not read the host clock).
    uint64_t retired() const;

private:
    struct Block {
        BlockIr   ir;
        Compiled* code = nullptr;
    };

    // Runs instructions with the window armed but without recording or
    // consulting a block. This is J1a in isolation (POM68K_JIT_BLOCKS=0):
    // it isolates the contribution of the fetch window from the block cache.
    void runWindow(int64_t clockTarget);

    // Traces a straight line of instructions from `pc`, recording what
    // actually executed. Tracing IS execution: the instructions run through
    // Moira exactly as the interpreter would, so a recorded block can never
    // describe something that did not happen.
    Block* record(uint32_t pc, bool super, int64_t clockTarget);

    // Validates the translation of `pc`'s page and points the window at the
    // host bytes behind it. False = this pc cannot be fetched from a plain
    // memory span (I/O, unmapped, would fault) — the interpreter takes over.
    bool armWindow(uint32_t pc, bool super);
    void disarmWindow();

    // Drops every cached block when the guard reports that a write landed
    // in translated code, or that the address map itself moved.
    void serviceGuard();

    // Marks the physical pages a freshly recorded block occupies, so a
    // later write into them trips the guard.
    void markPages(uint32_t physBase, uint32_t physLen);

    static uint64_t key(uint32_t pc, bool super) {
        return uint64_t(pc) | (super ? (uint64_t(1) << 32) : 0);
    }

    moira::Moira& cpu_;
    MemoryHooks   mem_;
    Backend*      backend_ = nullptr;
    Context       ctx_{};
    Stats         stats_;
    CodeGuard     guard_;
    std::vector<uint8_t> pageMap_;   // one byte per 4 KB of physical RAM

    std::unordered_map<uint64_t, Block> blocks_;

    // Physical footprint of the currently armed code window.
    uint32_t winPhys_ = 0, winLen_ = 0;
    // Instructions the last trace actually retired (it executes as it
    // records, so the caller must not run one more on top).
    uint32_t traceRetired_ = 0;

    // Re-entrancy guard. A guest instruction running inside a block can
    // reach flushAll() (MOVEC to CACR -> didChangeCACR); freeing the cache
    // there would pull the BlockIr out from under the replay loop.
    bool running_ = false;
    bool pendingFlush_ = false;
    // Moira::pomJitMmuGen the cached blocks were recorded under. A recorded
    // block is a script of LOGICAL addresses and does not survive a change
    // of translation.
    uint32_t blocksGen_ = 0;

    bool enabled_ = false;
    bool useBlocks_ = true;
    bool useWindow_ = true;
    bool paranoid_ = false;      // POM68K_JIT_PARANOID: revalidate every step
    int  maxInstrs_ = 64;
    int  maxBlocks_ = 16384;
};

}  // namespace jit
