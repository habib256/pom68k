// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "JitCodeBuffer.h"

#include <cstdio>
#include <cstdlib>

#if defined(__EMSCRIPTEN__)
  // WebAssembly has no writable-then-executable memory: generated code is
  // not a thing here, and pretending otherwise would fail at run time
  // instead of at selection time.
  #define POM68K_JIT_MEM_NONE 1
#elif defined(_WIN32)
  #define POM68K_JIT_MEM_WIN32 1
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
#else
  #define POM68K_JIT_MEM_POSIX 1
  #include <sys/mman.h>
  #include <unistd.h>
  #if defined(__APPLE__)
    #include <TargetConditionals.h>
    #include <libkern/OSCacheControl.h>
    #if defined(__aarch64__) || defined(__arm64__)
      #include <pthread.h>
      #define POM68K_JIT_MEM_APPLE_JIT 1
    #endif
  #endif
#endif

namespace jit {

namespace {

std::size_t pageSize() {
#if defined(POM68K_JIT_MEM_WIN32)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return std::size_t(si.dwPageSize);
#elif defined(POM68K_JIT_MEM_POSIX)
    long p = sysconf(_SC_PAGESIZE);
    return p > 0 ? std::size_t(p) : std::size_t(4096);
#else
    return 4096;
#endif
}

std::size_t roundUp(std::size_t n, std::size_t to) {
    return (n + to - 1) / to * to;
}

// Non-x86 hosts do not snoop stores against the instruction cache: code
// written through a data mapping is invisible to the fetcher until the
// range is explicitly flushed. On x86-64 this is a no-op by architecture.
void flushICache(void* p, std::size_t n) {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    (void)p; (void)n;
#elif defined(POM68K_JIT_MEM_WIN32)
    FlushInstructionCache(GetCurrentProcess(), p, n);
#elif defined(__APPLE__)
    sys_icache_invalidate(p, n);
#elif defined(__GNUC__) || defined(__clang__)
    __builtin___clear_cache(static_cast<char*>(p), static_cast<char*>(p) + n);
#else
    (void)p; (void)n;
#endif
}

}  // namespace

bool CodeBuffer::supported() {
#if defined(POM68K_JIT_MEM_NONE)
    return false;
#else
    // Probe once: a hardened kernel can refuse an executable mapping even
    // though the API exists. Doing it for real is the only honest test.
    //
    // The probe MUST leave the calling thread exactly as it found it. On
    // Apple silicon makeExecutable() flips the per-thread JIT write-protect
    // flag, which is thread-global state — a "side-effect-free" query that
    // silently left the thread in execute-only mode would break whatever
    // buffer that thread was writing.
    static const bool ok = [] {
        CodeBuffer probe;
        if (!probe.reserve(pageSize())) return false;
        uint8_t* p = probe.alloc(16);
        if (!p) return false;
        p[0] = 0xC3;                       // whatever; never executed
        bool x = probe.makeExecutable();
        if (x) probe.makeWritable();       // restore the thread's W state
        probe.release();
        return x;
    }();
    return ok;
#endif
}

bool CodeBuffer::reserve(std::size_t bytes) {
    release();
    const std::size_t n = roundUp(bytes ? bytes : 1, pageSize());

#if defined(POM68K_JIT_MEM_NONE)
    (void)n;
    return false;
#elif defined(POM68K_JIT_MEM_WIN32)
    void* p = VirtualAlloc(nullptr, n, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!p) return false;
#else
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
  #if defined(POM68K_JIT_MEM_APPLE_JIT)
    flags |= MAP_JIT;
  #endif
    // Ask for RWX first (see CodeBuffer::unified). A kernel that refuses it
    // is the normal case this class was written for, not an error — fall
    // back to the strict W^X mapping and the mprotect flipping.
    void* p = MAP_FAILED;
  #if !defined(POM68K_JIT_MEM_APPLE_JIT)
    p = mmap(nullptr, n, PROT_READ | PROT_WRITE | PROT_EXEC, flags, -1, 0);
    unified_ = (p != MAP_FAILED);
  #endif
    if (p == MAP_FAILED) {
        unified_ = false;
        p = mmap(nullptr, n, PROT_READ | PROT_WRITE, flags, -1, 0);
    }
    if (p == MAP_FAILED) return false;
#endif

#if !defined(POM68K_JIT_MEM_NONE)
  #if defined(POM68K_JIT_MEM_APPLE_JIT)
    // MAP_JIT pages follow a PER-THREAD write-protect state; a fresh
    // mapping is not necessarily writable in this thread.
    pthread_jit_write_protect_np(0);
  #endif
    base_ = static_cast<uint8_t*>(p);
    size_ = n;
    used_ = 0;
    icacheSynced_ = 0;
    writable_ = true;
    execMapped_ = false;
    return true;
#endif
}

void CodeBuffer::release() {
    if (!base_) return;
#if defined(POM68K_JIT_MEM_WIN32)
    VirtualFree(base_, 0, MEM_RELEASE);
#elif defined(POM68K_JIT_MEM_POSIX)
    munmap(base_, size_);
#endif
    base_ = nullptr;
    size_ = 0;
    used_ = 0;
    icacheSynced_ = 0;
    writable_ = true;
    unified_ = false;
    execMapped_ = false;
}

CodeBuffer::~CodeBuffer() { release(); }

uint8_t* CodeBuffer::alloc(std::size_t n, std::size_t align) {
    if (!base_ || !writable_) return nullptr;
    const std::size_t at = roundUp(used_, align ? align : 1);
    if (at + n > size_) return nullptr;
    used_ = at + n;
    return base_ + at;
}

bool CodeBuffer::makeExecutable() {
    if (!base_) return false;
    const auto syncNewCode = [this] {
        if (used_ > icacheSynced_)
            flushICache(base_ + icacheSynced_, used_ - icacheSynced_);
        icacheSynced_ = used_;
    };
    if (unified_) { syncNewCode(); return true; }
    if (!writable_) return true;
#if defined(POM68K_JIT_MEM_APPLE_JIT)
    // MAP_JIT pages are mapped PROT_READ|PROT_WRITE|PROT_EXEC once and the
    // W/X split is enforced by the per-thread write-protect flag, not by
    // mprotect — but the mapping above only asked for READ|WRITE, so the
    // pages would never actually be executable. Ask for EXEC too, then flip
    // the thread out of write mode.
    if (!execMapped_) {
        if (mprotect(base_, size_, PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
            return false;
        execMapped_ = true;
    }
    pthread_jit_write_protect_np(1);
#elif defined(POM68K_JIT_MEM_WIN32)
    DWORD old = 0;
    if (!VirtualProtect(base_, size_, PAGE_EXECUTE_READ, &old)) return false;
#elif defined(POM68K_JIT_MEM_POSIX)
    if (mprotect(base_, size_, PROT_READ | PROT_EXEC) != 0) return false;
#else
    return false;
#endif
    syncNewCode();
    writable_ = false;
    return true;
}

bool CodeBuffer::makeWritable() {
    if (!base_) return false;
    if (unified_) return true;
    if (writable_) return true;
#if defined(POM68K_JIT_MEM_APPLE_JIT)
    pthread_jit_write_protect_np(0);
#elif defined(POM68K_JIT_MEM_WIN32)
    DWORD old = 0;
    if (!VirtualProtect(base_, size_, PAGE_READWRITE, &old)) return false;
#elif defined(POM68K_JIT_MEM_POSIX)
    if (mprotect(base_, size_, PROT_READ | PROT_WRITE) != 0) return false;
#else
    return false;
#endif
    writable_ = true;
    return true;
}

}  // namespace jit
