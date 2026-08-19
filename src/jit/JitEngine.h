// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── JIT engine (host-agnostic layer 1) ──
// The second execution engine, sitting BESIDE the Moira interpreter and
// never in front of it: default on validated 68040 guests, switchable at run
// time, and always
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

#include <array>
#include <cstdint>
#include <memory>
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
    // Host pointer to DATA bytes at PHYSICAL `phys`, for the JIT data TLB.
    // Same contract as codeSpan and one more: with `write` set it must also
    // refuse anything a store cannot simply land in — ROM, a region with a
    // write side effect, or a map with a debug write-watch armed.
    uint8_t* (*dataSpan)(void* self, uint32_t phys, uint32_t& len, int write) = nullptr;
    // Adds per-256-byte write-mask bits for bus aliases of `physSlice`.
    // The engine accounts for the direct view; machine maps return only
    // aliases that reach the same backing bytes through another address.
    uint32_t (*aliasCodeMask)(void* self, uint32_t physSlice,
                              const uint8_t* pageMap, uint32_t pages) = nullptr;
    // Attaches (or detaches, with nullptr) the write guard.
    void (*setGuard)(void* self, CodeGuard* guard) = nullptr;
    // Physical RAM size, for sizing the guard's page map.
    uint32_t (*ramBytes)(void* self) = nullptr;
};

class Engine {
public:
    // `guestFamily` is one GuestFamily bit and is REQUIRED — no default, on
    // purpose. Backend selection needs the guest CPU family (a code
    // generator written for one family is wrong on another, not slow), and
    // it cannot read it off `cpu`: an Engine is a MEMBER of every CPU
    // wrapper, so it is constructed before the wrapper's body reaches
    // setModel() and `cpu.getModel()` still answers the Moira default here.
    // That is not a theoretical ordering worry — it was measured: sampling
    // the model in this constructor reported "68000/68010" for the 68030
    // LC II and quietly cost the Quadra its x86-64 backend. A required
    // parameter makes a new wrapper that forgets it a COMPILE error instead.
    Engine(moira::Moira& cpu, const MemoryHooks& mem, uint32_t guestFamily);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    bool enabled() const { return enabled_; }
    // Switching engines is only ever done between two instructions (the GUI
    // routes it through the machine thread's command queue), and it always
    // drops every cached artefact.
    void setEnabled(bool on);

    // Told once by the CPU wrapper: where its peripheral-catch-up baseline
    // lives and how many cycles it batches. See Context::periphClock.
    void setPeriphPacing(const void* clock, int batch) {
        ctx_.periphClock = clock;
        ctx_.periphBatch = batch;
    }
    // Event-driven wrappers expose their absolute next deadline instead of
    // a fixed batch. A negative batch is the backend-neutral discriminator.
    void setPeriphDeadline(const void* deadline) {
        ctx_.periphClock = deadline;
        ctx_.periphBatch = -1;
    }
    void setWriteObserver(void* opaque, WriteObserver fn) {
        ctx_.observeWriteSelf = opaque;
        ctx_.observeWrite = fn;
    }

    const char* backendName() const;
    const char* backendDescription() const;
    // True when the active backend generates host code. Gates and the GUI
    // use it to know which defaults are in force (the block cache follows
    // this, see JitConfig.h).
    bool nativeBackend() const;

    // The single entry point the CPU wrappers call instead of
    // Moira::executeUntil() when the engine is on.
    void executeUntil(int64_t clockTarget);

    // Everything cached is dropped: the code window, the block cache and
    // whatever the backend is holding. Called on hard reset, on cache
    // control writes, and whenever the memory map moves. The cause is only
    // ever a gauge (`Stats::flushCauses`); it changes nothing the flush
    // does. The twelve CPU wrappers call it without one on purpose — from
    // the engine's side "a wrapper asked" IS the cause, and the two kinds
    // it covers (hard reset, SMC hint) are orders of magnitude apart in
    // frequency, so the counter reads as the hint.
    void flushAll(Flush cause = Flush::External);

    const Stats& stats() const { return stats_; }
    Stats& stats() { return stats_; }

    // Gauge helper for the GUI: guest instructions per second, measured over
    // the caller's own wall clock (the engine does not read the host clock).
    uint64_t retired() const;

    // POM68K_JIT_DISPATCH_RING=1 — the last 64 dispatch decisions of
    // executeUntil (which path ran, from which pc, at which clock, exiting
    // how). The 2026-08-19 retained-cache divergence could not be read from
    // end states: two machines agreed on every counter and differed only in
    // WHERE the final budget boundary cut, which only the dispatch sequence
    // can show. Null when off (one predictable branch per dispatch).
    struct DispatchEv {
        uint32_t pc = 0;
        int64_t  clock = 0;
        int64_t  target = 0;
        uint8_t  kind = 0;      // 0 flags,1 backoff,2 armfail,3 trace,
                                // 4 window,5 block,6 cacheline,7 notready
        uint8_t  exit = 0;      // block runs: RunResult::exit
        uint32_t instrs = 0;    // instructions the dispatch retired
    };
    static constexpr unsigned kDispatchRing = 8192;
    const DispatchEv* dispatchRing(unsigned& count, unsigned& head) const {
        count = dispatchCount_; head = dispatchHead_;
        return dispatchEv_.data();
    }

    // POM68K_JIT_HISTO phase instrument: dump the census accumulated so far
    // under `label`, then zero every census counter, so one run can report
    // boot, idle-Finder and a drawing phase as separate censuses instead of
    // one cumulative blur (§ 3.5 refuses idle-Finder numbers for the
    // indexed-mode question). No-op when the census is off.
    void censusPhase(const char* label);

private:
    struct Block {
        BlockIr   ir;
        Compiled* code = nullptr;
        // pomJitMmuGen this block was last proved under. A block is a
        // script of LOGICAL addresses; when the translation generation
        // moves, it may run again only after the dispatch loop re-proves
        // that its recorded (logical page, physical page, length) triple
        // still holds — the freshly re-armed window supplies the current
        // triple for free.
        uint32_t  gen = 0;
        // Visits before this block is worth generating code for. A boot
        // touches a very large amount of code exactly once — compiling all
        // of it costs more than it can ever return, so a block earns its
        // translation by being executed (see POM68K_JIT_HOT).
        uint32_t  visits = 0;
        bool      rejected = false;      // the backend declined it; do not retry
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
    void markPages(uint64_t key, uint32_t physBase, uint32_t physLen);

    // Fills Moira's data TLB for `addr` and hands generated code the host
    // page behind it, or nullptr when the access must go the long way. This
    // is the ONLY door into that TLB, and the refusals are the safety
    // argument for the whole inline data path (src/jit/POM68K_JIT.md § 8).
    uint8_t* fillDtlb(uint32_t addr, int write);
    static uint8_t* fillDtlbThunk(void* self, uint32_t addr, int write) {
        return static_cast<Engine*>(self)->fillDtlb(addr, write);
    }

    // ── the block-link table (Context::linkTable) ────────────────────────
    // Direct-mapped on the guest pc, tagged with pc|super — pc is always
    // even, so the privilege bit rides in bit 0 for free and a user-mode
    // and a supervisor block at one address cannot be confused.
    struct LinkSlot {
        uint32_t tag;                  // pc | super, or kNoLink
        uint32_t pad;
        void*    entry;
    };
    // A full Q605 run compiles well over 100k blocks. With 4096 direct-mapped
    // slots, unrelated hot PCs constantly displaced one another and turned
    // otherwise linkable transfers back into Engine round trips. 64k slots
    // cost 1 MiB per CPU and keep the O(1), non-dangling invalidation model.
    static constexpr uint32_t kLinkSlots = 65536;
    static constexpr uint32_t kNoLink = 0xFFFFFFFF;
    static uint32_t linkIndex(uint32_t pc) { return (pc >> 1) & (kLinkSlots - 1); }

    void publishLink(uint32_t pc, bool super, void* entry) {
        LinkSlot& s = linkTable_[linkIndex(pc)];
        // First occupancy of the slot since the last wipe: remember it, so
        // clearLinks() wipes exactly the slots that carry anything. A flush
        // used to memset the whole megabyte table — 4 527 translation
        // flushes per 2000-frame LC II boot made that 3.3 % of the run in
        // __memset_avx2 (callgrind, 2026-08-19) for slots that were ~99.9 %
        // already empty.
        if (s.tag == kNoLink) published_.push_back(linkIndex(pc));
        s.tag = pc | uint32_t(super);
        s.entry = entry;
    }
    // A block that is going away must stop being a jump target. Exact,
    // because a block's slot is a function of its pc: if the slot still
    // carries this block's tag it is this block, and if it does not then
    // some other block took the slot and this one was already unreachable.
    void retractLink(uint32_t pc, bool super) {
        LinkSlot& s = linkTable_[linkIndex(pc)];
        if (s.tag == (pc | uint32_t(super))) { s.tag = kNoLink; s.entry = nullptr; }
    }
    void clearLinks() {
        for (uint32_t idx : published_) {
            linkTable_[idx].tag = kNoLink;
            linkTable_[idx].entry = nullptr;
        }
        published_.clear();
    }
    std::vector<LinkSlot> linkTable_{ kLinkSlots };
    // Slots holding a live entry — what clearLinks() must visit. Bounded by
    // the blocks compiled since the last flush; duplicates are impossible
    // because publishLink records only the empty->occupied transition and
    // retractLink leaves the slot on the list (cleared twice is harmless).
    std::vector<uint32_t> published_;

    static uint64_t key(uint32_t pc, bool super) {
        return uint64_t(pc) | (super ? (uint64_t(1) << 32) : 0);
    }

    moira::Moira& cpu_;
    MemoryHooks   mem_;
    uint32_t      guestFamily_ = 0; // fixes model-specific IR memory semantics
    ResolvedConfig config_;         // environment resolved once per Engine
    std::unique_ptr<Backend> ownedBackend_;
    Backend*      backend_ = nullptr;
    Context       ctx_{};
    Stats         stats_;
    CodeGuard     guard_;
    // One byte per CodeGuard::kUnit of physical RAM: does any cached block
    // have code here? Fine enough that ordinary data writes do not trip it.
    std::vector<uint8_t> pageMap_;
    // …and the same question at 4 KB, which is the granularity the data TLB
    // hands out. A store through a TLB entry can land anywhere in its page,
    // so a page holding ANY translated code must never become a TLB write
    // entry — the write guard would never see the store.
    std::vector<uint8_t> codePage_;

    // POM68K_JIT_VERBOSE diagnostics: WHY fillDtlb refused, not just how
    // often. `dtlbRefused` alone cannot separate a TRANSIENT refusal (no ATC
    // entry yet, or a write to a page still owing its M bit — the
    // interpreter fixes both and the next probe succeeds) from a REMEMBERED
    // one, which is written into the table as a null host and then bails
    // every later access to that page without ever calling back. Those are
    // what a runtime-fallback census actually sees, and the two kinds want
    // completely different fixes.
    enum RefuseWhy {
        kWhyProbe, kWhyPageLen, kWhyCodePage, kWhyNotRam, kWhyCache040,
        kWhyCount
    };
    uint64_t dtlbWhy_[kWhyCount] = {};
    uint32_t dtlbLastReason_ = RuntimeFillTag;

    // BackendCaps::dtlbCodeMask, cached: may fillDtlb hand out a write entry
    // for a page holding translated code in some slice? Only if the backend
    // tests the per-slice mask before storing.
    bool maskAware_ = false;

    std::unordered_map<uint64_t, Block> blocks_;
    // slice -> the blocks translated from it. Servicing a guard trip by
    // scanning the whole cache is O(blocks) per write, and the writes are
    // frequent: on a full boot that cost more than everything the code
    // generator saved (436 s against the fetch window's 127 s). This makes
    // it O(blocks in the slice that was actually written).
    // Do not use unordered_multimap here. Thousands of blocks can share one
    // 4 KB code page (and therefore the same 256-byte slice); libc++ keeps
    // equivalent keys contiguous by scanning that whole group on insertion,
    // turning a long boot into quadratic work. One hash lookup followed by
    // append-only storage is amortized O(1).
    std::unordered_map<uint32_t, std::vector<uint64_t>> sliceIndex_;

    // Physical footprint of the currently armed code window.
    uint32_t winPhys_ = 0, winLen_ = 0;
    // Arm-failure backoff. When the window cannot be armed — code in a
    // region codeSpan refuses, or a translation the probe cannot confirm —
    // retrying on the VERY NEXT instruction charges a probe per
    // instruction, and on the 030 machines (22-entry ATC, working sets
    // that churn it) that measured the JIT at HALF the interpreter's
    // speed. After a failure the engine runs this many instructions
    // interpreted before probing again; the guest is untouched either way
    // (the ATC-eviction hook makes window and no-window walk-identical).
    int armBackoff_ = 0;
    // POM68K_JIT_ARM_BACKOFF — how many instructions the interpreter takes
    // alone after a refused arm. Sampled once (JitConfig.h owns the why).
    int armBackoff_steps_ = 32;
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

    // Allocated only when POM68K_JIT_DISPATCH_RING=1 — an Engine lives
    // inside every CPU wrapper, and a quarter-megabyte of always-present
    // ring would be paid twelve times over for an instrument that is off.
    std::vector<DispatchEv> dispatchEv_;
    unsigned dispatchHead_ = 0;
    unsigned dispatchCount_ = 0;
    bool dispatchRingOn_ = false;
    void ring(uint8_t kind, uint32_t pc, int64_t target,
              uint8_t exit, uint32_t instrs);

    bool enabled_ = false;
    bool useBlocks_ = true;
    bool useWindow_ = true;
    bool paranoid_ = false;      // POM68K_JIT_PARANOID: revalidate every step
    int  maxInstrs_ = 64;
    int  maxBlocks_ = 16384;
    int  hotAt_ = 8;             // POM68K_JIT_HOT: visits before compiling

    // POM68K_JIT_WINDOW_KILL — bench instrument (JitConfig.h § windowKillEvery).
    // Forces a window-lost exit every N retired instructions so the COST of
    // one exit can be read off the slope of wall time against exit count.
    // 0 disables it and the hot paths pay one always-predicted branch.
    int  windowKill_ = 0;
    int  killCountdown_ = 0;

    // POM68K_JIT_HISTO=1 — dynamic opcode census, dumped on destruction.
    // A code generator is only worth the opcodes it actually meets: this is
    // what decided which forms JitBackendX64 emits natively and which it
    // hands back to Moira (src/jit/POM68K_JIT.md § 7). Null when off, so
    // the hot loop pays one always-predicted branch.
    std::vector<uint64_t> histo_;
    std::vector<uint64_t> slowStaticHisto_;
    std::vector<uint64_t> slowRuntimeHisto_;
    std::vector<uint64_t> slowRuntimeReasonHisto_;

    // Which 68020 indexed EXTENSION form each opcode was compiled with —
    // [opcode][0] = brief (d8,An/PC,Xn), [1] = the full format. The static
    // fallback census counts an opcode, and an opcode does not say which
    // form its extension word carried; that word is per SITE. Deciding
    // whether the brief decoder alone is worth writing needs the split, so
    // the engine tallies it where it holds the IR — which also means both
    // backends report it, instead of whichever one had the counter wired.
    // Deliberately NOT cleared by censusPhase(): a block compiled during
    // boot still executes during a later phase, and clearing would leave
    // its opcodes unattributed exactly where they matter most.
    std::vector<std::array<uint32_t, 2>> indexFormSites_;
    void recordIndexForms(const BlockIr& ir);
    struct RuntimeAddressKey {
        uint32_t reason, opcode, address, bytes, write, codeMask;
        bool operator==(const RuntimeAddressKey&) const = default;
    };
    struct RuntimeAddressHash {
        size_t operator()(const RuntimeAddressKey& k) const noexcept {
            uint64_t h = uint64_t(k.address) * 0x9E3779B185EBCA87ull;
            h ^= uint64_t(k.opcode | (k.reason << 16)) * 0xC2B2AE3D27D4EB4Full;
            h ^= uint64_t(k.bytes | (k.write << 8) | (k.codeMask << 16));
            return size_t(h ^ (h >> 32));
        }
    };
    std::unordered_map<RuntimeAddressKey, uint64_t, RuntimeAddressHash>
        runtimeAddressHisto_;
    RuntimeAddressKey lastRuntimeAddress_{};
    uint64_t* lastRuntimeAddressCount_ = nullptr;
    static void runtimeAddressThunk(void* self, uint32_t reason,
                                    uint32_t opcode, uint32_t address,
                                    uint32_t bytes, uint32_t write,
                                    uint32_t codeMask) {
        static_cast<Engine*>(self)->recordRuntimeAddress(
            reason, opcode, address, bytes, write, codeMask);
    }
    void recordRuntimeAddress(uint32_t reason, uint32_t opcode,
                              uint32_t address, uint32_t bytes,
                              uint32_t write, uint32_t codeMask);
    void dumpHisto() const;
};

}  // namespace jit
