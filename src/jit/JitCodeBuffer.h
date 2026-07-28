// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Executable memory for code-generating JIT backends (portable) ──
// Written at J0, before any backend generates a single byte, because this
// is where portability is actually decided. POM68K runs on Linux, macOS,
// Windows and (headless) Emscripten; those disagree about executable
// memory more than they disagree about anything else in this project.
//
// The model is W^X — writable OR executable, never both at once:
//   * hardened Linux (SELinux, PaX), OpenBSD and macOS all refuse or
//     penalise RWX pages;
//   * macOS on Apple Silicon needs MAP_JIT plus an explicit per-thread
//     write-protect toggle;
//   * ARM needs an explicit instruction-cache invalidation that x86 does
//     not, and forgetting it produces the worst class of bug there is —
//     one that only appears when the allocator happens to reuse an address.
//
// A platform that cannot provide executable memory at all (Emscripten,
// iOS) reports supported() == false; backend selection then falls through
// to `threaded`, which needs none of this. That is the whole reason the
// portable backend exists.

#pragma once
#include <cstddef>
#include <cstdint>

namespace jit {

class CodeBuffer {
public:
    CodeBuffer() = default;
    ~CodeBuffer();
    CodeBuffer(const CodeBuffer&) = delete;
    CodeBuffer& operator=(const CodeBuffer&) = delete;

    // Can this build, on this host, obtain executable memory at all?
    // Cheap and side-effect free: a code-generating backend calls it from
    // usable() before anything else.
    static bool supported();

    // Reserves `bytes` (rounded up to a page). Starts WRITABLE.
    bool reserve(std::size_t bytes);
    void release();

    bool     valid() const { return base_ != nullptr; }
    std::size_t capacity() const { return size_; }
    std::size_t used() const { return used_; }
    bool     writable() const { return writable_; }

    // Bump-allocates `n` bytes aligned to `align`. Only valid while
    // writable; returns nullptr when the buffer is full (the caller must
    // then flush the whole cache and start over — J1/J2 do not compact).
    uint8_t* alloc(std::size_t n, std::size_t align = 16);

    // W -> X. Also invalidates the instruction cache where that is not
    // automatic (every non-x86 host).
    bool makeExecutable();
    // X -> W, for appending more code.
    bool makeWritable();

    // True when the mapping is writable AND executable at the same time, so
    // the two calls above are no-ops. A backend that compiles one block at a
    // time cares a great deal: the alternative is an mprotect PAIR per
    // block, which measured as the single largest cost in the whole code
    // generator — more than the code generation. POM68K asks for RWX first
    // and keeps the strict W^X path as the fallback for the platforms and
    // kernels that refuse it (macOS/arm64, SELinux, PaX, OpenBSD).
    bool unified() const { return unified_; }

    // Rewinds the bump pointer. Callers must be certain no compiled block
    // is still reachable — the engine only does this from a full flush.
    void reset() { used_ = 0; }

private:
    uint8_t*    base_ = nullptr;
    std::size_t size_ = 0;
    std::size_t used_ = 0;
    bool        writable_ = true;
    bool        unified_ = false;
};

}  // namespace jit
