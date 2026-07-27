// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "JitEngine.h"

#include "Moira.h"

#include <cstdio>
#include <cstring>

namespace jit {

namespace {
// The code window must always be able to serve BOTH words mmu040InstrStart
// fetches (ird at pc, irc at pc+2) or neither of them — see the seam in
// extern/moira/Moira/MoiraExecMMU_cpp.h. Four bytes is therefore the
// minimum span, and Moira::pomJitFetch's bounds test assumes it.
constexpr uint32_t kMinWindow = 4;
}  // namespace

Engine::Engine(moira::Moira& cpu, const MemoryHooks& mem)
    : cpu_(cpu), mem_(mem) {
    backend_ = selectBackend(backendPreference());
    useWindow_ = fetchWindowEnabled();
    useBlocks_ = blockCacheEnabled();
    paranoid_ = detail::envBool("POM68K_JIT_PARANOID", false);
    maxInstrs_ = maxBlockInstrs();
    if (maxInstrs_ > backend_->caps().maxBlockInstrs)
        maxInstrs_ = backend_->caps().maxBlockInstrs;
    maxBlocks_ = maxBlocks();
    ctx_.cpu = &cpu_;
    ctx_.stats = &stats_;

    const uint32_t ram = mem_.ramBytes ? mem_.ramBytes(mem_.self) : 0;
    pageMap_.assign((ram + 4095) >> 12, 0);
    guard_.pageMap = pageMap_.empty() ? nullptr : pageMap_.data();
    guard_.pages = uint32_t(pageMap_.size());
    blocksGen_ = cpu_.pomJitMmuGen;

    if (verbose()) {
        std::fprintf(stderr,
                     "[jit] backend=%s (%s) window=%d blocks=%d max=%d ram=%uMB\n",
                     backend_->name(), backend_->description(),
                     int(useWindow_), int(useBlocks_), maxInstrs_, ram >> 20);
    }
    setEnabled(defaultEngine() == EngineKind::Jit);
}

Engine::~Engine() {
    if (mem_.setGuard) mem_.setGuard(mem_.self, nullptr);
    for (auto& kv : blocks_) if (kv.second.code) backend_->release(kv.second.code);
}

void Engine::setEnabled(bool on) {
    // Unconditional, even when the state does not change: leaving a window
    // armed while the interpreter runs would let IT fetch from a host
    // pointer the engine is no longer maintaining.
    flushAll();
    if (on == enabled_) return;
    enabled_ = on;
    if (mem_.setGuard) mem_.setGuard(mem_.self, on ? &guard_ : nullptr);
    if (verbose()) std::fprintf(stderr, "[jit] engine %s\n", on ? "ON" : "OFF");
}

const char* Engine::backendName() const { return backend_->name(); }
const char* Engine::backendDescription() const { return backend_->description(); }

uint64_t Engine::retired() const {
    return stats_.instrs.load(std::memory_order_relaxed) +
           stats_.interpInstrs.load(std::memory_order_relaxed);
}

void Engine::flushAll() {
    // RE-ENTRANCY. This is reachable from INSIDE a running block: a guest
    // MOVEC to CACR reaches Moira::setCACR -> Cpu040::didChangeCACR ->
    // flushAll() while the backend is iterating the very BlockIr we would
    // free here. Releasing it there is a use-after-free (and, once the
    // x86-64 backend lands, freeing host code that is mid-execution).
    // Defer instead; executeUntil() services the flush the moment the
    // block returns, before it touches the cache again.
    if (running_) { pendingFlush_ = true; return; }

    for (auto& kv : blocks_) if (kv.second.code) backend_->release(kv.second.code);
    blocks_.clear();
    backend_->flushAll();
    if (!pageMap_.empty()) std::memset(pageMap_.data(), 0, pageMap_.size());
    disarmWindow();
    guard_.clear();
    blocksGen_ = cpu_.pomJitMmuGen;
    pendingFlush_ = false;
    stats_.add(stats_.flushes);
    stats_.blocksLive.store(0, std::memory_order_relaxed);
}

void Engine::disarmWindow() {
    cpu_.pomJitDisarm();
    winPhys_ = winLen_ = 0;
}

void Engine::serviceGuard() {
    if (!guard_.tripped()) return;
    stats_.add(stats_.invalidations);
    // J1 drops the whole cache rather than evicting page by page. A write
    // into live code is rare (loaders, INIT patching, a trap patch) and the
    // recorded IR is cheap to rebuild; fine-grained eviction only starts to
    // pay when what gets thrown away is generated code — a J2 concern.
    flushAll();
}

void Engine::markPages(uint32_t physBase, uint32_t physLen) {
    if (pageMap_.empty() || !physLen) return;
    uint32_t p = physBase >> 12;
    const uint32_t last = (physBase + physLen - 1) >> 12;
    for (; p <= last && p < pageMap_.size(); p++) pageMap_[p] = 1;
}

bool Engine::armWindow(uint32_t pc, bool super) {
    if (!useWindow_) return false;

    // Already covered, nothing invalidated: the common case, and the whole
    // reason the window is worth having. pomJitCovers() also re-checks the
    // MMU generation and the privilege level, which an address range alone
    // cannot see.
    if (!paranoid_ && cpu_.pomJitCovers(pc) && !guard_.tripped()) return true;

    serviceGuard();
    stats_.add(stats_.windowArmed);

    uint32_t phys = 0, pageBase = 0, pageLen = 0;
    if (!cpu_.pomJitProbeCode(pc, super, phys, pageBase, pageLen)) {
        // No ATC entry yet, a supervisor-only page seen from user mode, or
        // walk-per-access mode. The interpreter fetches it — and in doing so
        // fills the ATC, so the next attempt usually succeeds.
        stats_.add(stats_.windowFailed);
        disarmWindow();
        return false;
    }

    // Arm the WHOLE page, not the tail from `pc`: a loop jumping backwards
    // inside its own page must stay covered, otherwise every iteration pays
    // a re-validation. The page base translates to `phys - offset` — same
    // page, same descriptor, and both RAM and the 1 MB ROM mirror are
    // 4 KB-aligned, so a page never straddles a region seam.
    const uint32_t offset = pc - pageBase;
    const uint32_t physPage = phys - offset;

    uint32_t span = 0;
    const uint8_t* host =
        mem_.codeSpan ? mem_.codeSpan(mem_.self, physPage, span) : nullptr;
    if (!host || span < kMinWindow) {
        // Not plain memory: an I/O aperture, VRAM, an unmapped hole, or the
        // whole map while the boot overlay is still up (clearing it is a
        // READ side effect the window would skip).
        stats_.add(stats_.windowFailed);
        disarmWindow();
        return false;
    }

    // Two bounds, both mandatory: the MMU page (past it the translation is a
    // different one) and the contiguous host span (past it the memory map
    // mirrors or changes region).
    uint32_t len = pageLen < span ? pageLen : span;
    if (len < kMinWindow || pc - pageBase > len - kMinWindow) {
        stats_.add(stats_.windowFailed);
        disarmWindow();
        return false;
    }

    cpu_.pomJitArm(host, pageBase, len, super);
    winPhys_ = physPage;
    winLen_ = len;
    return true;
}

void Engine::runWindow(int64_t clockTarget) {
    // J1a in isolation: the fetch window with no block bookkeeping at all.
    // Every instruction still goes through Moira; the only thing that
    // changed is where its opcode words come from.
    uint32_t n = 0;
    while (cpu_.getClock() < clockTarget) {
        if (!cpu_.pomJitIdle()) { stats_.bump(Exit::CpuFlags); break; }
        if (!cpu_.pomJitCovers(cpu_.getPC())) { stats_.bump(Exit::WindowLost); break; }
        if (!cpu_.pomJitExecOne()) { n++; stats_.bump(Exit::Fault); break; }
        n++;
    }
    if (n) stats_.add(stats_.instrs, n);
}

Engine::Block* Engine::record(uint32_t pc, bool super, int64_t clockTarget) {
    if (int(blocks_.size()) >= maxBlocks_) flushAll();

    BlockIr ir;
    ir.entryPc = pc;
    ir.super = super;
    ir.codeBase = cpu_.pomJitWindow.base;
    ir.codeLen = cpu_.pomJitWindow.len;
    ir.physBase = winPhys_;
    ir.physLen = winLen_;

    // Tracing IS executing: the recorded line is by construction the line
    // the interpreter just ran, so a block can never claim something that
    // did not happen. The cost of a trace is one run of the code; from the
    // second visit on, the block is free.
    uint32_t at = pc;
    uint32_t retired = 0;
    EndReason why = EndReason::LengthLimit;

    running_ = true;
    for (int i = 0; i < maxInstrs_; i++) {

        if (cpu_.getClock() >= clockTarget) { why = EndReason::LengthLimit; break; }
        if (!cpu_.pomJitIdle()) { why = EndReason::ControlFlow; break; }
        if (!cpu_.pomJitCovers(at)) { why = EndReason::WindowEdge; break; }

        const uint16_t op = cpu_.pomJitPeek(at);
        const Kind kind = classify(op);
        if (endsBlock(kind)) { why = EndReason::Unsafe; break; }

        if (!cpu_.pomJitExecOne()) { retired++; why = EndReason::Faulted; break; }
        retired++;

        const uint32_t next = cpu_.getPC();
        // 22 bytes is the longest a 68020-family instruction can be. Landing
        // outside that window means the instruction transferred control —
        // a trap the classifier did not predict, or a fault redirect.
        if (next <= at || next - at > 22) {
            ir.instrs.push_back(Instr{ at, op, 1, kind, instrFlags(op, kind) });
            why = EndReason::Discontinuity;
            break;
        }

        ir.instrs.push_back(Instr{ at, op, uint16_t((next - at) / 2),
                                   kind, instrFlags(op, kind) });
        at = next;

        if (guard_.tripped()) { why = EndReason::WindowEdge; break; }
    }

    running_ = false;
    traceRetired_ = retired;
    if (retired) stats_.add(stats_.instrs, retired);

    // A single-instruction "block" that ended on a discontinuity or a fault
    // describes nothing reusable — do not cache it.
    if (ir.instrs.empty() || why == EndReason::Discontinuity ||
        why == EndReason::Faulted) {
        stats_.bump(Exit::NotCompilable);
        return nullptr;
    }

    // A flush requested from inside the trace was deferred; the block we
    // just recorded belongs to the world that flush is throwing away.
    if (pendingFlush_) { flushAll(); return nullptr; }

    ir.endReason = why;
    Block b;
    b.ir = std::move(ir);
    auto [it, inserted] = blocks_.emplace(key(pc, super), std::move(b));
    if (!inserted) return &it->second;

    it->second.code = backend_->compile(it->second.ir);
    if (it->second.code) it->second.code->ir = &it->second.ir;
    markPages(it->second.ir.physBase, it->second.ir.physLen);
    stats_.add(stats_.blocksCompiled);
    stats_.blocksLive.store(blocks_.size(), std::memory_order_relaxed);
    return &it->second;
}

void Engine::executeUntil(int64_t clockTarget) {
    if (!enabled_) { cpu_.executeUntil(clockTarget); return; }

    while (cpu_.getClock() < clockTarget) {

        // Anything Moira has flagged — a pending interrupt, tracing, STOP,
        // HALT, a breakpoint, instruction logging — is the interpreter's
        // business, always. The engine never second-guesses those paths.
        if (!cpu_.pomJitIdle()) {
            disarmWindow();
            cpu_.execute();
            stats_.add(stats_.interpInstrs);
            stats_.bump(Exit::CpuFlags);
            continue;
        }

        const uint32_t pc = cpu_.getPC();
        const bool super = cpu_.pomJitSuper();

        if (!armWindow(pc, super)) {
            // Code outside plain memory, or a translation the probe cannot
            // confirm. The interpreter runs it and stacks any fault itself.
            cpu_.execute();
            stats_.add(stats_.interpInstrs);
            continue;
        }

        if (!useBlocks_) { runWindow(clockTarget); continue; }

        ctx_.clockTarget = clockTarget;

        // A recorded block is a script of LOGICAL addresses. When the
        // translation underneath changes — a PFLUSH, a write to TC/URP/SRP
        // or a TTR — the same logical pc can point at entirely different
        // code, and the cache would happily replay the old script against
        // it. The window is refused on a generation mismatch; the block
        // cache has to be dropped, because there is nothing in a (pc,super)
        // key that could notice.
        if (blocksGen_ != cpu_.pomJitMmuGen) flushAll();

        auto it = blocks_.find(key(pc, super));
        if (it == blocks_.end()) {
            // First visit: tracing runs the instructions as it records them,
            // so there is normally nothing left to execute afterwards. Only
            // when the trace retired NOTHING (the very first opcode is a
            // branch, a trap or an MMU instruction) must the interpreter
            // step in — otherwise this loop would not advance.
            traceRetired_ = 0;
            record(pc, super, clockTarget);
            if (traceRetired_ == 0) {
                cpu_.execute();
                stats_.add(stats_.interpInstrs);
            }
            continue;
        }

        Block& b = it->second;
        if (!b.code) { runWindow(clockTarget); continue; }

        running_ = true;
        RunResult r = backend_->run(b.code, ctx_);
        running_ = false;

        stats_.add(stats_.blocksRun);
        if (r.instrs) stats_.add(stats_.instrs, r.instrs);
        stats_.bump(r.exit);
        // Any flush the block itself asked for was deferred; honour it now,
        // with nothing of the cache in flight.
        if (pendingFlush_) flushAll();
        else if (guard_.tripped()) serviceGuard();
    }
}

}  // namespace jit
