// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "JitEngine.h"

#include "Moira.h"

#include <algorithm>
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

Engine::Engine(moira::Moira& cpu, const MemoryHooks& mem, uint32_t guestFamily)
    : cpu_(cpu), mem_(mem) {
    // The family comes from the wrapper, not from cpu.getModel(): see the
    // ordering note on the declaration in JitEngine.h.
    backend_ = selectBackend(backendPreference(), guestFamily);
    if (Backend* instance = backend_->clone()) {
        ownedBackend_.reset(instance);
        backend_ = instance;
    }
    useWindow_ = fetchWindowEnabled();
    useBlocks_ = blockCacheEnabled(backend_->caps().nativeCode);
    paranoid_ = detail::envBool("POM68K_JIT_PARANOID", false);
    maxInstrs_ = maxBlockInstrs();
    if (maxInstrs_ > backend_->caps().maxBlockInstrs)
        maxInstrs_ = backend_->caps().maxBlockInstrs;
    maxBlocks_ = maxBlocks();
    hotAt_ = hotThreshold(backend_->caps().nativeCode);
    windowKill_ = killCountdown_ = windowKillEvery();
    ctx_.cpu = &cpu_;
    ctx_.stats = &stats_;
    ctx_.dtlbFill = &Engine::fillDtlbThunk;
    ctx_.dtlbSelf = this;
    // J3: hand the fill door to the INTERPRETER's data window (Moira.h §
    // pomJitData) — OPT-IN, and the story of why is worth the two lines.
    // The window measured a real win (42 -> 37.6 s on the loading phase)
    // until the ATC-eviction hook (pomJitAtcEvict) made it bit-exact, and
    // bit-exactness is exactly what killed it: a TLB entry may not outlive
    // the ATC entry it derives from, so the window's coverage is capped at
    // the ATC's 32 pages — and under Mac OS VM the eviction/refill churn
    // costs more than the remaining hits save (73 s: WORSE than no window).
    // The x86-64 backend keeps its inline TLB (same cap, but it replaces a
    // C++ call chain, not a hot MRU probe), and invariant 3 gets its
    // interpreter back: byte-identical to the pre-JIT baseline.
    if (detail::envBool("POM68K_DATA_WINDOW", false)) {
        cpu_.pomJitDtlbFillFn = &Engine::fillDtlbThunk;
        cpu_.pomJitDtlbFillCtx = this;
    }
    ctx_.guard = &guard_;
    clearLinks();
    if (detail::envBool("POM68K_JIT_LINKS", true)) {
        ctx_.linkTable = linkTable_.data();
        ctx_.linkMask = kLinkSlots - 1;
    }

    const uint32_t ram = mem_.ramBytes ? mem_.ramBytes(mem_.self) : 0;
    pageMap_.assign((ram + CodeGuard::kUnit - 1) >> CodeGuard::kShift, 0);
    codePage_.assign((ram + 4095) >> 12, 0);
    guard_.pageMap = pageMap_.empty() ? nullptr : pageMap_.data();
    guard_.pages = uint32_t(pageMap_.size());
    blocksGen_ = cpu_.pomJitMmuGen;

    if (detail::envBool("POM68K_JIT_HISTO", false)) {
        histo_.assign(1 << 16, 0);
        slowStaticHisto_.assign(1 << 16, 0);
        slowRuntimeHisto_.assign(1 << 16, 0);
        ctx_.slowStaticHisto = slowStaticHisto_.data();
        ctx_.slowRuntimeHisto = slowRuntimeHisto_.data();
    }

    if (verbose()) {
        std::fprintf(stderr,
                     "[jit] backend=%s (%s) window=%d blocks=%d max=%d ram=%uMB\n",
                     backend_->name(), backend_->description(),
                     int(useWindow_), int(useBlocks_), maxInstrs_, ram >> 20);
    }
    setEnabled(defaultEngine() == EngineKind::Jit);
}

Engine::~Engine() {
    // The one number that says whether the engine did anything at all:
    // how much of the run was actually fetched through the window, and how
    // often it had to be re-armed to keep that up. A family whose window
    // arms constantly and covers little is paying bookkeeping for nothing,
    // which is exactly what the compacts' numbers show (POM68K_JIT.md § 7).
    if (verbose()) {
        const Stats::Snapshot s = stats_.snapshot();
        const double pct = s.instrs ? 100.0 * double(s.windowInstrs) / double(s.instrs) : 0.0;
        std::fprintf(stderr,
                     "[jit] retired=%llu window=%llu (%.1f%%) arms=%llu "
                     "failed=%llu blocks run=%llu compiled=%llu flushes=%llu\n",
                     (unsigned long long)s.instrs,
                     (unsigned long long)s.windowInstrs, pct,
                     (unsigned long long)s.windowArmed,
                     (unsigned long long)s.windowFailed,
                     (unsigned long long)s.blocksRun,
                     (unsigned long long)s.blocksCompiled,
                     (unsigned long long)s.flushes);
    }
    cpu_.pomJitDtlbFillFn = nullptr;
    cpu_.pomJitDtlbFillCtx = nullptr;
    cpu_.pomJitDtlbFlush();
    if (mem_.setGuard) mem_.setGuard(mem_.self, nullptr);
    for (auto& kv : blocks_) if (kv.second.code) backend_->release(kv.second.code);
    if (!histo_.empty()) dumpHisto();
}

// POM68K_JIT_HISTO. A code generator is worth exactly the opcodes it
// actually meets, and 68k static frequency tables are a poor guide to what
// a Mac ROM + Mac OS 8.1 boot executes. This census is what picked the
// forms JitBackendX64 emits natively; the `native` column is that backend's
// own answer, so the report also doubles as its coverage gauge.
void Engine::dumpHisto() const {
    struct Row { uint16_t op; uint64_t n; };
    std::vector<Row> rows;
    uint64_t total = 0, byKind[int(Kind::Count)] = {};
    for (uint32_t op = 0; op < histo_.size(); op++) {
        if (!histo_[op]) continue;
        rows.push_back({ uint16_t(op), histo_[op] });
        total += histo_[op];
        byKind[int(classify(uint16_t(op)))] += histo_[op];
    }
    std::sort(rows.begin(), rows.end(),
              [](const Row& a, const Row& b) { return a.n > b.n; });

    std::fprintf(stderr, "\n[jit] opcode census — %llu instructions, %zu distinct\n",
                 (unsigned long long)total, rows.size());
    static const char* kKind[] = { "move", "alu", "shift", "bitop", "muldiv",
                                   "addr", "multi", "cond", "branch", "UNSAFE" };
    for (int k = 0; k < int(Kind::Count); k++) {
        if (!byKind[k]) continue;
        std::fprintf(stderr, "  %-8s %12llu  %5.1f%%\n", kKind[k],
                     (unsigned long long)byKind[k], 100.0 * double(byKind[k]) / double(total));
    }
    uint64_t nat = 0;
    for (const Row& r : rows) if (backend_->canEmit(r.op)) nat += r.n;
    std::fprintf(stderr, "  %-8s %12llu  %5.1f%%  (backend '%s')\n", "native",
                 (unsigned long long)nat, 100.0 * double(nat) / double(total),
                 backend_->name());

    std::fprintf(stderr, "  ── top 60 opcodes ──\n");
    uint64_t acc = 0;
    for (size_t i = 0; i < rows.size() && i < 60; i++) {
        acc += rows[i].n;
        std::fprintf(stderr, "  %04X %-6s %c %10llu  %5.2f%%  cum %5.1f%%\n",
                     rows[i].op, kKind[int(classify(rows[i].op))],
                     backend_->canEmit(rows[i].op) ? '*' : ' ',
                     (unsigned long long)rows[i].n,
                     100.0 * double(rows[i].n) / double(total),
                     100.0 * double(acc) / double(total));
    }

    struct SlowRow { uint16_t op; uint64_t unsupported, runtime; };
    std::vector<SlowRow> slow;
    uint64_t staticTotal = 0, runtimeTotal = 0;
    for (uint32_t op = 0; op < slowStaticHisto_.size(); op++) {
        const uint64_t s = slowStaticHisto_[op];
        const uint64_t r = slowRuntimeHisto_[op];
        if (s || r) slow.push_back({uint16_t(op), s, r});
        staticTotal += s;
        runtimeTotal += r;
    }
    std::sort(slow.begin(), slow.end(), [](const SlowRow& a, const SlowRow& b) {
        return a.unsupported + a.runtime > b.unsupported + b.runtime;
    });
    const uint64_t slowTotal = staticTotal + runtimeTotal;
    std::fprintf(stderr,
                 "\n[jit] block fallback census — %llu unsupported + %llu "
                 "runtime guard/access = %llu\n",
                 (unsigned long long)staticTotal,
                 (unsigned long long)runtimeTotal,
                 (unsigned long long)slowTotal);
    for (size_t i = 0; i < slow.size() && i < 60; i++) {
        const uint64_t n = slow[i].unsupported + slow[i].runtime;
        std::fprintf(stderr, "  %04X %-8s %10llu unsupported %10llu runtime  %5.2f%%\n",
                     slow[i].op, kKind[int(classify(slow[i].op))],
                     (unsigned long long)slow[i].unsupported,
                     (unsigned long long)slow[i].runtime,
                     slowTotal ? 100.0 * double(n) / double(slowTotal) : 0.0);
    }
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
bool Engine::nativeBackend() const { return backend_->caps().nativeCode; }

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
    sliceIndex_.clear();
    clearLinks();
    backend_->flushAll();
    cpu_.pomJitDtlbFlush();
    if (!pageMap_.empty()) std::memset(pageMap_.data(), 0, pageMap_.size());
    if (!codePage_.empty()) std::memset(codePage_.data(), 0, codePage_.size());
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

// A write landed in memory some cached block was translated from. J1 dropped
// the whole cache here, on the reasoning that writes into live code are rare
// and a recorded IR is cheap to rebuild. Both halves of that turned out to
// be wrong once the cache held GENERATED CODE: the writes are not rare (68k
// code and its data share memory closely), and what gets thrown away is no
// longer cheap. So evict precisely, and keep the flush for the two cases
// that genuinely leave nothing to salvage.
void Engine::serviceGuard() {
    if (!guard_.tripped()) return;
    stats_.add(stats_.invalidations);

    if (guard_.mustFlush() || blocks_.empty()) { flushAll(); return; }

    for (int k = 0; k < guard_.nHits; k++) {
        const uint32_t slice = guard_.hits[k];
        // Every block filed under this slice was translated from memory the
        // write touched, so all of them go — and with them every index entry
        // for the slice, which is what makes clearing its mark correct.
        auto indexed = sliceIndex_.find(slice);
        if (indexed != sliceIndex_.end()) for (uint64_t blockKey : indexed->second) {
            auto b = blocks_.find(blockKey);
            if (b != blocks_.end()) {
                if (b->second.code) {
                    retractLink(b->second.ir.entryPc, b->second.ir.super);
                    backend_->release(b->second.code);
                }
                blocks_.erase(b);
                stats_.add(stats_.evictions);
            }
        }
        if (indexed != sliceIndex_.end()) sliceIndex_.erase(indexed);
        if (slice >= pageMap_.size()) continue;
        pageMap_[slice] = 0;

        // codePage_ answers the same question at the 4 KB granularity the
        // data TLB hands out, so it may only be cleared once no slice in the
        // page is marked any more.
        const uint32_t page = (slice << CodeGuard::kShift) >> 12;
        if (page >= codePage_.size() || !codePage_[page]) continue;
        const uint32_t first = (page << 12) >> CodeGuard::kShift;
        const uint32_t count = 4096 >> CodeGuard::kShift;
        bool any = false;
        for (uint32_t i = first; i < first + count && i < pageMap_.size(); i++)
            if (pageMap_[i]) { any = true; break; }
        if (!any) { codePage_[page] = 0; cpu_.pomJitDtlbFlush(); }
    }
    guard_.clear();
    stats_.blocksLive.store(blocks_.size(), std::memory_order_relaxed);
}

void Engine::markPages(uint64_t blockKey, uint32_t physBase, uint32_t physLen) {
    if (pageMap_.empty() || !physLen) return;
    uint32_t p = physBase >> CodeGuard::kShift;
    const uint32_t last = (physBase + physLen - 1) >> CodeGuard::kShift;
    for (; p <= last && p < pageMap_.size(); p++) {
        pageMap_[p] = 1;
        sliceIndex_[p].push_back(blockKey);
    }
    uint32_t q = physBase >> 12;
    const uint32_t qlast = (physBase + physLen - 1) >> 12;
    for (; q <= qlast && q < codePage_.size(); q++) codePage_[q] = 1;
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
    // NO DTLB flush here (churn fix, 2026-07-31). Re-arming used to flush
    // the data TLB every time, and at the idle Finder the window dies every
    // ~15 instructions (cross-page control transfers under a one-page
    // window), so the TLB was rebuilt from nothing about 30 M times per
    // boot — 942 M fills measured over one 60 M-step lockstep. Every
    // invalidation the flush was standing in for already has its own exact
    // owner: an MMU-generation bump flushes at the source
    // (Moira::pomJitMapMoved), an ATC eviction kills its derived entries
    // per page and per space (pomJitAtcEvict), privilege rides in each
    // entry's tag (bit 31), a page gaining translated code flushes when it
    // is marked (the hot path below), and one losing its last block
    // flushes when it is unmarked (serviceGuard()). What remains
    // between two arms is exactly the set of entries whose backing ATC
    // rows are still resident — which is the exactness contract itself.

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
        if (!histo_.empty()) [[unlikely]] histo_[cpu_.pomJitPeek(cpu_.getPC())]++;
        if (!cpu_.pomJitExecOne()) { n++; stats_.bump(Exit::Fault); break; }
        n++;
        // POM68K_JIT_WINDOW_KILL: a forced exit, indistinguishable from the
        // ATC-eviction one except that its rate is chosen. Off = one
        // always-predicted branch.
        if (windowKill_ && --killCountdown_ <= 0) [[unlikely]] {
            killCountdown_ = windowKill_;
            disarmWindow();
            stats_.bump(Exit::WindowLost);
            break;
        }
    }
    if (n) { stats_.add(stats_.instrs, n); stats_.add(stats_.windowInstrs, n); }
}

Engine::Block* Engine::record(uint32_t pc, bool super, int64_t clockTarget) {
    // Full cache: STOP RECORDING, do not throw the cache away. Flushing
    // here is what a code generator cannot afford — a Mac OS 8.1 boot
    // touches far more hot code than any cache holds, and dropping the lot
    // on every overflow had the engine recompile 1.8 million blocks in one
    // boot, which cost more than everything translation saved. Whatever is
    // not cached simply runs on the fetch-window path, which is what the
    // engine would have done anyway.
    if (int(blocks_.size()) >= maxBlocks_) return nullptr;

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

        // A branch ENDS the block and is part of it. Its length cannot be
        // read off the pc delta the way every other instruction's can — it
        // jumps — so it comes from the encoding, and the block's footprint
        // ends after the displacement rather than where control went.
        const bool terminator = endsBlockAfter(kind);
        const uint32_t words = terminator ? branchWords(op) : 0;
        if (terminator && !cpu_.pomJitCovers(at + words * 2 - 2)) {
            why = EndReason::WindowEdge;
            break;
        }

        // The interpreter's own cycle count for this instruction, recorded
        // rather than modelled: a backend has to agree with it before it may
        // emit the instruction (JitIr.h, Instr::cycles).
        const int64_t clk0 = cpu_.getClock();
        if (!cpu_.pomJitExecOne()) { retired++; why = EndReason::Faulted; break; }
        retired++;
        const int64_t spent = cpu_.getClock() - clk0;
        const uint16_t cycles = uint16_t(spent >= 0 && spent < 0x10000 ? spent : 0);

        if (terminator) {
            ir.instrs.push_back(Instr{ at, op, uint16_t(words), kind,
                                       instrFlags(op, kind), cycles });
            at += words * 2;
            why = EndReason::ControlFlow;
            break;
        }

        const uint32_t next = cpu_.getPC();
        // 22 bytes is the longest a 68020-family instruction can be. Landing
        // outside that window means the instruction transferred control —
        // a trap the classifier did not predict, or a fault redirect.
        if (next <= at || next - at > 22) {
            ir.instrs.push_back(Instr{ at, op, 1, kind, instrFlags(op, kind), cycles });
            why = EndReason::Discontinuity;
            break;
        }

        ir.instrs.push_back(Instr{ at, op, uint16_t((next - at) / 2),
                                   kind, instrFlags(op, kind), cycles });
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

    // The block's own copy of its instruction words. A code generator needs
    // the displacements, immediates and absolute addresses embedded in the
    // stream, and it cannot read them out of the code window: the window is
    // engine state and may point somewhere else by the time the block runs.
    // Strict bounds: an instruction can START inside the window and have an
    // extension word outside it (readExt falls back to the bus there), and
    // pomJitPeek does no checking of its own. Anything not wholly inside
    // leaves `code` empty, which a code generator reads as "do not compile".
    // Two words PAST the footprint as well: the prefetch queue an
    // instruction leaves behind holds the word after it, and for the last
    // instruction of a block that word is outside the block.
    const auto& win = cpu_.pomJitWindow;
    const uint32_t want = at + 4;
    if (at > pc && win.armed && pc >= win.base && want <= win.base + win.len) {
        ir.code.reserve((want - pc) / 2);
        for (uint32_t w = pc; w < want; w += 2) ir.code.push_back(cpu_.pomJitPeek(w));
    }

    Block b;
    b.ir = std::move(ir);
    auto [it, inserted] = blocks_.emplace(key(pc, super), std::move(b));
    if (!inserted) return &it->second;

    markPages(key(pc, super), it->second.ir.physBase, it->second.ir.physLen);
    stats_.blocksLive.store(blocks_.size(), std::memory_order_relaxed);
    return &it->second;
}

// The only door into Moira's data TLB. Everything a JIT-compiled store or
// load may NOT quietly do is refused here, once, at fill time — after which
// generated code can hit the entry with nothing but a tag compare:
//
//   * no page-table walk and no descriptor U/M write-back (pomJitProbeData
//     refuses anything not already resident and already marked);
//   * no I/O, no VRAM register aperture, no unmapped hole (dataSpan hands
//     back plain RAM/ROM/VRAM bytes and nothing else);
//   * no store into a page holding translated code — that one has to go
//     through the memory map so the write guard sees it.
uint8_t* Engine::fillDtlb(uint32_t addr, int write) {
    if (!mem_.dataSpan) return nullptr;

    const bool super = cpu_.pomJitSuper();
    uint32_t phys = 0, pageBase = 0, pageLen = 0;
    // A failed PROBE is transient — no ATC entry yet, or a write to a page
    // whose descriptor still owes its M bit. The interpreter's next access
    // fixes both, so this one is not remembered.
    if (!cpu_.pomJitProbeData(addr, super, write != 0, phys, pageBase, pageLen))
        return nullptr;

    // An entry maps a 4 KB SLICE, whatever the MMU's page size: the first
    // cut refused anything but 4 KB pages — "the 68040 boots with 4 KB on
    // every Mac" — and that assumption is false the moment the System arms
    // paging, which uses 8 KB pages on the 040. The refusal was also not
    // remembered (a probe result is transient by design), so once the MMU
    // was up EVERY data access paid a call, a probe and a refusal: the
    // "fast" path measured a constant ~7 s SLOWER across engines. An 8 KB
    // page simply fills as two independent slices; translation preserves
    // the in-page offset and pages are size-aligned, so the slice's
    // physical base is just the translated address rounded down.
    if (pageLen != 4096 && pageLen != 8192) return nullptr;
    (void)pageBase;
    const uint32_t physSlice = phys & ~4095u;

    const bool codePage = write && !codePage_.empty() &&
                          (physSlice >> 12) < codePage_.size() &&
                          codePage_[physSlice >> 12];

    uint32_t span = 0;
    uint8_t* host = codePage ? nullptr
                             : mem_.dataSpan(mem_.self, physSlice, span, write);
    if (host && span < 4096) host = nullptr;

    // The answer is cached either way. A REFUSAL is worth remembering — an
    // I/O register, the ROM window seen by a store, a page holding
    // translated code — because a hardware poll loop would otherwise ask
    // again on every single iteration, and the ask is a call. Every reason
    // for refusing here needs a map change or a block flush to stop being
    // true, and both empty this cache.
    moira::Moira::PomJitDtlb& tlb = write ? cpu_.pomJitDtlbW : cpu_.pomJitDtlbR;
    const uint32_t page = addr >> 12;
    moira::Moira::PomJitDtlbEntry& e = tlb.e[page & (moira::Moira::PomJitDtlb::kEntries - 1)];
    // The privilege the entry was PROBED under rides in the tag (bit 31),
    // so a supervisor-only page filled from supervisor mode can never be
    // hit by user code — and nothing needs flushing on an RTE.
    e.tag = moira::Moira::pomJitDataTag(addr, super);
    e.host = host;
    stats_.add(host ? stats_.dtlbFills : stats_.dtlbRefused);
    return host;
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

        // Backoff after a failed arm: probing on every instruction is what
        // made the 030 machines SLOWER under the JIT than interpreted.
        if (armBackoff_ > 0 && !cpu_.pomJitCovers(pc)) {
            armBackoff_--;
            cpu_.execute();
            stats_.add(stats_.interpInstrs);
            continue;
        }

        if (!armWindow(pc, super)) {
            // Code outside plain memory, or a translation the probe cannot
            // confirm. The interpreter runs it and stacks any fault itself
            // — and the next probes wait their turn.
            armBackoff_ = 32;
            cpu_.execute();
            stats_.add(stats_.interpInstrs);
            continue;
        }
        armBackoff_ = 0;

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
        if (!b.code) {
            // Not (yet) translated. Run the window path — which is what the
            // engine would do anyway — and let the block earn its code by
            // coming back. Marking its pages before compiling is required:
            // fillDtlb refuses write access to a page holding translated
            // code, and a block that modified its own page through the
            // inline store path would leave the guard none the wiser.
            if (!b.rejected && ++b.visits >= uint32_t(hotAt_)) {
                // record() already indexed and marked this block's physical
                // footprint. Only the TLB flush is needed now so a writable
                // entry filled before that mark cannot survive compilation.
                cpu_.pomJitDtlbFlush();
                b.code = backend_->compile(b.ir, ctx_);
                if (b.code) {
                    b.code->ir = &b.ir;
                    stats_.add(stats_.blocksCompiled);
                    // From here on another block may jump straight into it
                    // instead of coming back through the engine.
                    if (void* e = backend_->linkEntry(b.code)) publishLink(pc, super, e);
                } else {
                    b.rejected = true;
                }
            }
            if (!b.code) { runWindow(clockTarget); continue; }
        }

        running_ = true;
        RunResult r = backend_->run(b.code, ctx_);
        running_ = false;

        stats_.add(stats_.blocksRun);
        if (r.instrs) stats_.add(stats_.instrs, r.instrs);
        if (r.slowInstrs) stats_.add(stats_.slowInstrs, r.slowInstrs);
        stats_.bump(r.exit);
        // Same forced exit on the block path. A generated block checks the
        // window from inside its own code (and, once linked, jumps to the
        // next block without coming back), so the kill can only be applied
        // where a block RETURNS — the realized rate is therefore at most the
        // requested one. That is why the regression is fitted against the
        // exit count the run actually reports, never against N.
        if (windowKill_) [[unlikely]] {
            killCountdown_ -= int(r.instrs > uint32_t(windowKill_) ? windowKill_ : r.instrs);
            if (killCountdown_ <= 0) {
                killCountdown_ = windowKill_;
                disarmWindow();
                stats_.bump(Exit::WindowLost);
            }
        }
        // Any flush the block itself asked for was deferred; honour it now,
        // with nothing of the cache in flight.
        if (pendingFlush_) flushAll();
        else if (guard_.tripped()) serviceGuard();
    }
}

}  // namespace jit
