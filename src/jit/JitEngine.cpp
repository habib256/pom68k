// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "JitEngine.h"

#include "Moira.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstring>

namespace jit {

namespace {
// The code window must always be able to serve BOTH words mmu040InstrStart
// fetches (ird at pc, irc at pc+2) or neither of them — see the seam in
// extern/moira/Moira/MoiraExecMMU_cpp.h. Four bytes is therefore the
// minimum span, and Moira::pomJitFetch's bounds test assumes it.
constexpr uint32_t kMinWindow = 4;

constexpr int kEaBuckets = 12;

int eaBucket(int mode, int reg) {
    if (mode < 7) return mode;
    return reg <= 4 ? 7 + reg : -1;
}

const char* eaBucketName(int bucket) {
    static constexpr const char* names[kEaBuckets] = {
        "Dn", "An", "(An)", "(An)+", "-(An)", "d16(An)", "idx(An)",
        "abs.W", "abs.L", "d16(PC)", "idx(PC)", "imm"
    };
    return bucket >= 0 && bucket < kEaBuckets ? names[bucket] : "n/a";
}

void formatEa(char* out, size_t size, int mode, int reg) {
    switch (mode) {
        case 0: std::snprintf(out, size, "D%d", reg); break;
        case 1: std::snprintf(out, size, "A%d", reg); break;
        case 2: std::snprintf(out, size, "(A%d)", reg); break;
        case 3: std::snprintf(out, size, "(A%d)+", reg); break;
        case 4: std::snprintf(out, size, "-(A%d)", reg); break;
        case 5: std::snprintf(out, size, "d16(A%d)", reg); break;
        case 6: std::snprintf(out, size, "idx(A%d)", reg); break;
        case 7:
            std::snprintf(out, size, "%s", eaBucketName(eaBucket(mode, reg)));
            break;
        default: std::snprintf(out, size, "n/a"); break;
    }
}
}  // namespace

Engine::Engine(moira::Moira& cpu, const MemoryHooks& mem, uint32_t guestFamily,
               const ResolvedConfig& config)
    : cpu_(cpu), mem_(mem), guestFamily_(guestFamily),
      config_(config) {
    // The family comes from the wrapper, not from cpu.getModel(): see the
    // ordering note on the declaration in JitEngine.h.
    backend_ = selectBackend(config_.backend.c_str(), guestFamily,
                             config_.unsafeBackend);
    if (Backend* instance = backend_->clone()) {
        ownedBackend_.reset(instance);
        backend_ = instance;
    }
    // Gate-only hard requirement. An explicit native request normally falls
    // back to `threaded` when executable memory is unavailable; that is the
    // right product behaviour, but it made several `jit_*` gates report green
    // without executing a single generated instruction. Native proof must
    // fail loudly instead of validating the portable floor a second time.
    if (config_.requireNative && !backend_->caps().nativeCode) {
        std::fprintf(stderr,
                     "[jit] FAIL: a native backend is required, but selection "
                     "resolved to '%s'\n", backend_->name());
        std::abort();
    }
    config_.applyBackendDefaults(backend_->caps().nativeCode,
                                 backend_->caps().accessClockBias);
    useWindow_ = config_.fetchWindow;
    useBlocks_ = config_.blockCache;
    paranoid_ = config_.paranoid;
    maxInstrs_ = config_.maxBlockInstrs;
    if (maxInstrs_ > backend_->caps().maxBlockInstrs)
        maxInstrs_ = backend_->caps().maxBlockInstrs;
    maxBlocks_ = config_.maxBlocks;
    maskAware_ = backend_->caps().dtlbCodeMask;
    hotAt_ = config_.hot;
    const BackendCaps caps = backend_->caps();
    profitScore_ = caps.nativeCode ? config_.profitScore : 0;
    if (caps.nativeCode && !config_.profitScoreExplicit &&
        guestFamily_ == kGuest68030)
        profitScore_ = int(caps.profitScore68030);
    windowKill_ = killCountdown_ = config_.windowKill;
    armBackoff_steps_ = config_.armBackoff;
    // The virgin table must read as EMPTY (kNoLink), not as value-zero:
    // tag 0 is a legal user-mode pc-0 key. The old whole-table clearLinks
    // established this on the first flush; the lazy clear never sweeps the
    // full table, so establish it once here.
    for (LinkSlot& sl : linkTable_) { sl.tag = kNoLink; sl.entry = nullptr; }
    dispatchRingOn_ = config_.dispatchRing;
    if (dispatchRingOn_) dispatchEv_.resize(kDispatchRing);
    ctx_.cpu = &cpu_;
    ctx_.stats = &stats_;
    ctx_.config = &config_;
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
    if (config_.dataWindow) {
        cpu_.pomJitDtlbFillFn = &Engine::fillDtlbThunk;
        cpu_.pomJitDtlbFillCtx = this;
    }
    ctx_.guard = &guard_;
    clearLinks();
    if (config_.links) {
        ctx_.linkTable = linkTable_.data();
        ctx_.linkMask = kLinkSlots - 1;
        if (config_.edgeCells) {
            ctx_.linkCell = &Engine::linkCellThunk;
            ctx_.linkCellSelf = this;
        }
    }

    const uint32_t ram = mem_.ramBytes ? mem_.ramBytes(mem_.self) : 0;
    pageMap_.assign((ram + CodeGuard::kUnit - 1) >> CodeGuard::kShift, 0);
    codePage_.assign((ram + 4095) >> 12, 0);
    guard_.pageMap = pageMap_.empty() ? nullptr : pageMap_.data();
    guard_.pages = uint32_t(pageMap_.size());
    blocksGen_ = cpu_.pomJitMmuGen;

    if (config_.histogram) {
        histo_.assign(1 << 16, 0);
        slowStaticHisto_.assign(1 << 16, 0);
        slowRuntimeHisto_.assign(1 << 16, 0);
        slowRuntimeReasonHisto_.assign(RuntimeReasonCount << 16, 0);
        indexFormSites_.assign(1 << 16, {0, 0});
        ctx_.slowStaticHisto = slowStaticHisto_.data();
        ctx_.slowRuntimeHisto = slowRuntimeHisto_.data();
        ctx_.slowRuntimeReasonHisto = slowRuntimeReasonHisto_.data();
        ctx_.dtlbFillReason = &dtlbLastReason_;
        runtimeAddressHisto_.reserve(1 << 16);
        ctx_.runtimeAddressObserver = &Engine::runtimeAddressThunk;
        ctx_.runtimeAddressSelf = this;
    }

    if (config_.verbose) {
        std::fprintf(stderr,
                     "[jit] profile=%s backend=%s (%s) window=%d blocks=%d "
                     "max=%d ram=%uMB\n",
                     config_.profileName(),
                     backend_->name(), backend_->description(),
                     int(useWindow_), int(useBlocks_), maxInstrs_, ram >> 20);
    }
    // Phase D: native 68040 backends are lockstep- and full-boot-proved, and
    // the portable backend inherits Moira's handlers. An explicit
    // POM68K_CPU_ENGINE still overrides this policy in either direction.
    //
    // **68030 joined on 2026-08-18.** BackendCaps keeps correctness scope
    // (`guestFamilies`) separate from automatic speed policy
    // (`autoFamilies`). AArch64 has earned generated 030 code through the
    // long lockstep, platform gates and fixed-budget measurement; x86-64
    // still resolves an 030 to `threaded` because its IIsi native gate is
    // red. This per-(family, backend) admission is the property that stops
    // unfinished native work reaching a shipping default. On the portable
    // path, `threaded` changes the FETCH path, where the 68030's time is:
    // callgrind on the shipping LC II interpreter puts
    // `mmuFetchWord` + `Cpu030::read16` at 44.4 % of all instructions, and
    // the code window replaces exactly that with a host pointer. Measured
    // (jit_bench_lcii, 2000 frames, fingerprint `3de5c5ab62b4eca8` on every
    // engine): interpreter 17.90 s / x1.86, `threaded` 15.14 s / x2.20.
    // Conformance is not inherited, it is gated: `jit_lockstep_030_test`
    // and `_blocks_test` step two LC IIs from power-up comparing registers,
    // the three stacks, SR, `clock`, 2 KB of low RAM and the three PomIcache
    // counters — and the 030 i-cache overlay is charged inside
    // `mmuFetchWord` BEFORE the window hook, so the window is conformant on
    // it by construction (docs/JIT_BRINGUP.md line 32).
    const bool jitByDefault =
        (guestFamily & (kGuest68040 | kGuest68030)) != 0;
    setEnabled(config_.engineForGuest(jitByDefault) == EngineKind::Jit);
}

void Engine::recordRuntimeAddress(uint32_t reason, uint32_t opcode,
                                  uint32_t address, uint32_t bytes,
                                  uint32_t write, uint32_t codeMask) {
    RuntimeAddressKey key{reason, opcode, address, bytes, write, codeMask};
    if (lastRuntimeAddressCount_ && key == lastRuntimeAddress_) {
        ++*lastRuntimeAddressCount_;
        return;
    }
    auto [it, inserted] = runtimeAddressHisto_.try_emplace(key, 0);
    (void)inserted;
    lastRuntimeAddress_ = key;
    lastRuntimeAddressCount_ = &it->second;
    ++it->second;
}

Engine::~Engine() {
    // The one number that says whether the engine did anything at all:
    // how much of the run was actually fetched through the window, and how
    // often it had to be re-armed to keep that up. A family whose window
    // arms constantly and covers little is paying bookkeeping for nothing,
    // which is exactly what the compacts' numbers show (POM68K_JIT.md § 7).
    if (config_.verbose) {
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
        // Refusals BY REASON. The counts are of fillDtlb calls, so a
        // REMEMBERED refusal appears once here and then serves every later
        // access to that page silently — which is why this line has to be
        // read next to the runtime half of the fallback census, never on
        // its own. `codepage` is the one that is architecturally forced:
        // an entry maps a whole 4 KB slice, so a page holding ANY block's
        // code can never become a write entry, however far the store is
        // from the code (docs/JIT_BRINGUP.md § A.0).
        std::fprintf(stderr,
                     "[jit] dtlb refusals: probe=%llu pagelen=%llu codepage=%llu "
                     "notram=%llu cache040=%llu\n",
                     (unsigned long long)dtlbWhy_[kWhyProbe],
                     (unsigned long long)dtlbWhy_[kWhyPageLen],
                     (unsigned long long)dtlbWhy_[kWhyCodePage],
                     (unsigned long long)dtlbWhy_[kWhyNotRam],
                     (unsigned long long)dtlbWhy_[kWhyCache040]);
    }
    cpu_.pomJitCache040Consumer = false;
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
        char ea[64] = {};
        const uint16_t op = slow[i].op;
        const uint16_t hi = op & 0xF000;
        if (hi == 0x1000 || hi == 0x2000 || hi == 0x3000) {
            char src[24], dst[24];
            formatEa(src, sizeof src, (op >> 3) & 7, op & 7);
            formatEa(dst, sizeof dst, (op >> 6) & 7, (op >> 9) & 7);
            std::snprintf(ea, sizeof ea, "  %s -> %s", src, dst);
        } else if ((op & 0xFB80) == 0x4880) {
            char operand[24];
            formatEa(operand, sizeof operand, (op >> 3) & 7, op & 7);
            std::snprintf(ea, sizeof ea, "  EA=%s", operand);
        }
        std::fprintf(stderr, "  %04X %-8s %10llu unsupported %10llu runtime  %5.2f%%%s\n",
                     slow[i].op, kKind[int(classify(slow[i].op))],
                     (unsigned long long)slow[i].unsupported,
                     (unsigned long long)slow[i].runtime,
                     slowTotal ? 100.0 * double(n) / double(slowTotal) : 0.0,
                     ea);
    }

    if (!slowRuntimeReasonHisto_.empty()) {
        static constexpr const char* names[RuntimeReasonCount] = {
            "fill/tag MMU", "non-plain/MMIO", "codeMask",
            "cross-page", "other guard"
        };
        uint64_t attributed = 0;
        std::fprintf(stderr, "\n[jit] runtime fallback causes\n");
        for (uint32_t reason = 0; reason < RuntimeReasonCount; reason++) {
            struct ReasonRow { uint16_t op; uint64_t n; };
            std::vector<ReasonRow> top;
            uint64_t total = 0;
            const size_t base = size_t(reason) << 16;
            for (uint32_t op = 0; op < (1u << 16); op++) {
                const uint64_t n = slowRuntimeReasonHisto_[base + op];
                total += n;
                if (n) top.push_back({uint16_t(op), n});
            }
            attributed += total;
            std::sort(top.begin(), top.end(), [](const ReasonRow& a, const ReasonRow& b) {
                return a.n > b.n;
            });
            std::fprintf(stderr, "  %-16s %12llu  %6.2f%%",
                         names[reason], (unsigned long long)total,
                         runtimeTotal ? 100.0 * double(total) / double(runtimeTotal) : 0.0);
            for (size_t i = 0; i < top.size() && i < 8; i++)
                std::fprintf(stderr, "  %04X:%llu", top[i].op,
                             (unsigned long long)top[i].n);
            std::fputc('\n', stderr);
        }
        std::fprintf(stderr, "  %-16s %12llu / %12llu runtime%s\n",
                     "attributed", (unsigned long long)attributed,
                     (unsigned long long)runtimeTotal,
                     attributed == runtimeTotal ? "  exact" : "  MISMATCH");
    }

    if (!runtimeAddressHisto_.empty()) {
        struct AddressRow { RuntimeAddressKey key; uint64_t n; };
        for (uint32_t reason : {uint32_t(RuntimeCodeMask),
                                uint32_t(RuntimeCrossPage),
                                uint32_t(RuntimeOther)}) {
            std::vector<AddressRow> addressRows;
            uint64_t observed = 0, expected = 0;
            for (const auto& [key, n] : runtimeAddressHisto_) {
                if (key.reason != reason) continue;
                addressRows.push_back({key, n});
                observed += n;
            }
            const size_t base = size_t(reason) << 16;
            for (uint32_t op = 0; op < (1u << 16); op++)
                expected += slowRuntimeReasonHisto_[base + op];
            std::sort(addressRows.begin(), addressRows.end(),
                      [](const AddressRow& a, const AddressRow& b) {
                          return a.n > b.n;
                      });
            const char* title = reason == RuntimeCodeMask ? "codeMask"
                              : reason == RuntimeCrossPage ? "cross-page"
                              : "other-runtime-access";
            std::fprintf(stderr, "\n[jit] exact %s addresses — %llu / %llu%s\n",
                         title,
                         (unsigned long long)observed,
                         (unsigned long long)expected,
                         observed == expected ? " exact" : " MISMATCH");
            for (size_t i = 0; i < addressRows.size() && i < 60; i++) {
                const auto& row = addressRows[i];
                const auto& k = row.key;
                if (reason != RuntimeCrossPage) {
                    std::fprintf(stderr,
                        "  op=%04X %c addr=$%08X page=$%05X slice=%X "
                        "mask=$%04X bytes=%u  %12llu\n",
                        k.opcode, k.write ? 'W' : 'R', k.address,
                        k.address >> 12, (k.address >> 8) & 15,
                        k.codeMask & 0xFFFFu, k.bytes,
                        (unsigned long long)row.n);
                } else {
                    std::fprintf(stderr,
                        "  op=%04X %c addr=$%08X page=$%05X off=$%03X "
                        "bytes=%u  %12llu\n",
                        k.opcode, k.write ? 'W' : 'R', k.address,
                        k.address >> 12, k.address & 4095u, k.bytes,
                        (unsigned long long)row.n);
                }
            }
        }
    }

    struct ModeCount { uint64_t unsupported = 0, runtime = 0; };
    ModeCount moveSrc[kEaBuckets]{}, moveDst[kEaBuckets]{},
              movePair[kEaBuckets][kEaBuckets]{}, movemEa[kEaBuckets]{};
    for (const SlowRow& row : slow) {
        const uint16_t op = row.op;
        const uint16_t hi = op & 0xF000;
        if (hi == 0x1000 || hi == 0x2000 || hi == 0x3000) {
            const int src = eaBucket((op >> 3) & 7, op & 7);
            const int dst = eaBucket((op >> 6) & 7, (op >> 9) & 7);
            if (src >= 0 && dst >= 0) {
                moveSrc[src].unsupported += row.unsupported;
                moveSrc[src].runtime += row.runtime;
                moveDst[dst].unsupported += row.unsupported;
                moveDst[dst].runtime += row.runtime;
                movePair[src][dst].unsupported += row.unsupported;
                movePair[src][dst].runtime += row.runtime;
            }
        } else if ((op & 0xFB80) == 0x4880) {
            const int ea = eaBucket((op >> 3) & 7, op & 7);
            if (ea >= 0) {
                movemEa[ea].unsupported += row.unsupported;
                movemEa[ea].runtime += row.runtime;
            }
        }
    }

    auto dumpModes = [](const char* title, const ModeCount* counts) {
        struct ModeRow { int mode; uint64_t n; };
        std::vector<ModeRow> rows;
        for (int mode = 0; mode < kEaBuckets; mode++) {
            const uint64_t n = counts[mode].unsupported + counts[mode].runtime;
            if (n) rows.push_back({mode, n});
        }
        std::sort(rows.begin(), rows.end(), [](const ModeRow& a, const ModeRow& b) {
            return a.n > b.n;
        });
        std::fprintf(stderr, "\n[jit] fallback addressing — %s\n", title);
        for (const ModeRow& row : rows) {
            const ModeCount& c = counts[row.mode];
            std::fprintf(stderr, "  %-8s %12llu unsupported %12llu runtime\n",
                         eaBucketName(row.mode),
                         (unsigned long long)c.unsupported,
                         (unsigned long long)c.runtime);
        }
    };
    dumpModes("MOVE source", moveSrc);
    dumpModes("MOVE destination", moveDst);
    dumpModes("MOVEM operand", movemEa);

    // The indexed modes, split by EXTENSION form. `docs/JIT_BRINGUP.md`
    // scopes the work as "a brief extension-word decoder", so what decides
    // it is not how many fallbacks carry an indexed EA but how many carry
    // one that decoder would actually reach. Weight is the fallback count
    // of the opcode; the form comes from the compiled sites (see
    // indexFormSites_). An opcode compiled BOTH ways is reported as mixed
    // rather than assigned, because its counter cannot be divided.
    if (!indexFormSites_.empty()) {
        uint64_t brief = 0, full = 0, mixed = 0;
        double briefApportioned = 0;
        uint32_t briefSites = 0, fullSites = 0;
        for (const SlowRow& row : slow) {
            const auto& sites = indexFormSites_[row.op];
            if (!sites[0] && !sites[1]) continue;      // no indexed EA here
            const uint64_t n = row.unsupported + row.runtime;
            if (sites[0] && sites[1]) mixed += n;
            else if (sites[0]) brief += n;
            else full += n;
            briefApportioned += double(n) * double(sites[0]) /
                                double(sites[0] + sites[1]);
        }
        for (const auto& sites : indexFormSites_) {
            briefSites += sites[0];
            fullSites += sites[1];
        }
        const uint64_t idx = brief + full + mixed;
        std::fprintf(stderr,
                     "\n[jit] indexed-mode fallbacks by extension form — "
                     "%llu attributed (of %llu total fallbacks)\n",
                     (unsigned long long)idx, (unsigned long long)slowTotal);
        std::fprintf(stderr, "  brief (d8,An/PC,Xn) %12llu  %5.1f%%\n",
                     (unsigned long long)brief,
                     idx ? 100.0 * double(brief) / double(idx) : 0.0);
        std::fprintf(stderr, "  full 68020 format   %12llu  %5.1f%%\n",
                     (unsigned long long)full,
                     idx ? 100.0 * double(full) / double(idx) : 0.0);
        std::fprintf(stderr, "  mixed (both seen)   %12llu  %5.1f%%\n",
                     (unsigned long long)mixed,
                     idx ? 100.0 * double(mixed) / double(idx) : 0.0);
        std::fprintf(stderr, "  compiled slots: %u brief, %u full "
                     "(cumulative over the run, not per phase)\n",
                     briefSites, fullSites);
        // An ESTIMATE, and labelled one: the mixed mass is split by each
        // opcode's own brief/full slot ratio. Exactness would need the
        // census keyed per compiled SITE rather than per opcode — a change
        // inside both code generators' cold stubs, which this number is
        // meant to decide whether to bother with.
        std::fprintf(stderr, "  brief share if mixed is apportioned by slot "
                     "ratio: %5.1f%%  (ESTIMATE)\n",
                     idx ? 100.0 * briefApportioned / double(idx) : 0.0);
    }

    struct PairRow { int src, dst; uint64_t n; };
    std::vector<PairRow> pairs;
    for (int src = 0; src < kEaBuckets; src++) {
        for (int dst = 0; dst < kEaBuckets; dst++) {
            const ModeCount& c = movePair[src][dst];
            const uint64_t n = c.unsupported + c.runtime;
            if (n) pairs.push_back({src, dst, n});
        }
    }
    std::sort(pairs.begin(), pairs.end(), [](const PairRow& a, const PairRow& b) {
        return a.n > b.n;
    });
    std::fprintf(stderr, "\n[jit] fallback addressing — top MOVE source -> destination pairs\n");
    for (size_t i = 0; i < pairs.size() && i < 24; i++) {
        const PairRow& row = pairs[i];
        const ModeCount& c = movePair[row.src][row.dst];
        std::fprintf(stderr, "  %-8s -> %-8s %12llu unsupported %12llu runtime\n",
                     eaBucketName(row.src), eaBucketName(row.dst),
                     (unsigned long long)c.unsupported,
                     (unsigned long long)c.runtime);
    }
}

// One tally per COMPILED slot, not per execution: the question it answers
// is "what shape is the code", and the static census supplies the weight.
void Engine::recordIndexForms(const BlockIr& ir) {
    if (indexFormSites_.empty()) return;
    for (const Instr& in : ir.instrs) {
        for (uint8_t e = 0; e < in.effectiveAddressCount; e++) {
            const DecodedEffectiveAddress& ea = in.effectiveAddresses[e];
            switch (ea.kind) {
                case EffectiveAddressKind::BriefIndex:
                case EffectiveAddressKind::PcBriefIndex:
                    indexFormSites_[in.opcode][0]++;
                    break;
                case EffectiveAddressKind::FullIndex:
                case EffectiveAddressKind::PcFullIndex:
                    indexFormSites_[in.opcode][1]++;
                    break;
                default: break;
            }
        }
    }
}

void Engine::censusPhase(const char* label) {
    if (histo_.empty()) return;
    std::fprintf(stderr, "\n[jit] ════ census phase '%s' ════\n", label);
    dumpHisto();
    std::fill(histo_.begin(), histo_.end(), 0);
    std::fill(slowStaticHisto_.begin(), slowStaticHisto_.end(), 0);
    std::fill(slowRuntimeHisto_.begin(), slowRuntimeHisto_.end(), 0);
    std::fill(slowRuntimeReasonHisto_.begin(), slowRuntimeReasonHisto_.end(), 0);
    // lastRuntimeAddressCount_ points INTO this map; clearing without
    // resetting it would leave the fast-path cache writing through a
    // dangling pointer on the next runtime fallback.
    runtimeAddressHisto_.clear();
    lastRuntimeAddress_ = {};
    lastRuntimeAddressCount_ = nullptr;
}

void Engine::setEnabled(bool on) {
    // Unconditional, even when the state does not change: leaving a window
    // armed while the interpreter runs would let IT fetch from a host
    // pointer the engine is no longer maintaining.
    flushAll();
    cpu_.pomJitCache040Consumer = on;
    if (on == enabled_) return;
    enabled_ = on;
    if (mem_.setGuard) mem_.setGuard(mem_.self, on ? &guard_ : nullptr);
    if (config_.verbose) std::fprintf(stderr, "[jit] engine %s\n", on ? "ON" : "OFF");
}

const char* Engine::backendName() const { return backend_->name(); }
const char* Engine::backendDescription() const { return backend_->description(); }
bool Engine::nativeBackend() const { return backend_->caps().nativeCode; }

uint64_t Engine::retired() const {
    return stats_.instrs.load(std::memory_order_relaxed) +
           stats_.interpInstrs.load(std::memory_order_relaxed);
}

void* Engine::linkCell(uint32_t pc, bool super) {
    const uint64_t targetKey = key(pc, super);
    auto [it, inserted] = linkCells_.try_emplace(targetKey);
    if (inserted) it->second = std::make_unique<LinkCell>();
    LinkCell* cell = it->second.get();

    // A source may compile after its target was already published. Bind the
    // newly created dependency immediately; the later publishLink path owns
    // the opposite ordering (source first, target later).
    if (!cell->entry) {
        auto target = blocks_.find(targetKey);
        if (target != blocks_.end() && target->second.code &&
            target->second.gen == blocksGen_) {
            cell->entry = backend_->linkEntry(target->second.code);
            if (cell->entry && !cell->listed) {
                cell->listed = true;
                publishedCells_.push_back(cell);
            }
        }
    }
    return cell;
}

void Engine::ring(uint8_t kind, uint32_t pc, int64_t target,
                  uint8_t exit, uint32_t instrs) {
    DispatchEv& e = dispatchEv_[dispatchHead_];
    e.pc = pc; e.clock = cpu_.getClock(); e.target = target;
    e.kind = kind; e.exit = exit; e.instrs = instrs;
    dispatchHead_ = (dispatchHead_ + 1) % kDispatchRing;
    if (dispatchCount_ < kDispatchRing) dispatchCount_++;
}

void Engine::flushAll(Flush cause) {
    // RE-ENTRANCY. This is reachable from INSIDE a running block: a guest
    // MOVEC to CACR reaches Moira::setCACR -> Cpu040::didChangeCACR ->
    // flushAll() while the backend is iterating the very BlockIr we would
    // free here. Releasing it there is a use-after-free (and, once the
    // x86-64 backend lands, freeing host code that is mid-execution).
    // Defer instead; executeUntil() services the flush the moment the
    // block returns, before it touches the cache again.
    if (running_) { pendingFlush_ = true; return; }

    // A DISABLED engine holds nothing: no blocks, no links, no armed window,
    // and page maps that were zeroed when it was switched off. Clearing them
    // again costs two `memset`s over a map sized by guest RAM, and the CPU
    // wrappers call this on every guest instruction-cache strobe whether the
    // engine is running or not. On the shipping LC II — where the JIT is OFF
    // by default, `guestFamily == kGuest68040` being the only case that arms
    // it — the guest's ~26 500 CACR clears per 2000 frames put `flushAll` at
    // 1.0 % of the INTERPRETER's instruction count and a large share of the
    // 4.1 % spent in `__memset_avx2` (callgrind, 2026-08-18). Removing the
    // work is worth **5.8 % of wall clock** with the fingerprint unmoved.
    // The disarm stays unconditional: setEnabled()'s contract is that no
    // window survives a switch, and it is free when none is armed.
    if (!enabled_) { disarmWindow(); return; }

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
    stats_.bump(cause);
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

    if (guard_.mustFlush() || blocks_.empty()) { flushAll(Flush::Guard); return; }

    for (int k = 0; k < guard_.nHits; k++) {
        const CodeGuard::Hit& h = guard_.hits[k];
        // note() only records a hit when the write covers a sub-slice some
        // block has bytes in, so nearly every trip names a real victim; the
        // per-block intersection below is what makes it exact. Walk a copy:
        // unmarkPages() edits the vector being walked.
        std::vector<uint64_t> keys;
        if (auto indexed = sliceIndex_.find(h.slice); indexed != sliceIndex_.end())
            keys = indexed->second;
        for (uint64_t blockKey : keys) {
            auto b = blocks_.find(blockKey);
            if (b == blocks_.end()) continue;
            const BlockIr& ir = b->second.ir;
            uint32_t lo = 0, len = 0;
            blockSpan(ir, lo, len);
            if (!len || h.hi < lo || h.lo > lo + len - 1) continue;
            if (b->second.code) {
                retractLink(ir.entryPc, ir.super);
                backend_->release(b->second.code);
            }
            unmarkPages(blockKey, lo, len);
            blocks_.erase(b);
            stats_.add(stats_.evictions);
        }
    }
    guard_.clear();
    stats_.blocksLive.store(blocks_.size(), std::memory_order_relaxed);
}

void Engine::recomputeSliceMark(uint32_t slice) {
    if (slice >= pageMap_.size()) return;
    uint8_t mask = 0;
    if (auto it = sliceIndex_.find(slice); it != sliceIndex_.end()) {
        const uint32_t sliceLo = slice << CodeGuard::kShift;
        const uint32_t sliceHi = sliceLo + CodeGuard::kUnit - 1;
        for (uint64_t key : it->second) {
            auto b = blocks_.find(key);
            if (b == blocks_.end()) continue;
            uint32_t lo = 0, len = 0;
            blockSpan(b->second.ir, lo, len);
            if (!len) continue;
            const uint32_t hi = lo + len - 1;
            if (hi < sliceLo || lo > sliceHi) continue;
            mask |= CodeGuard::subMask(lo > sliceLo ? lo : sliceLo,
                                       hi < sliceHi ? hi : sliceHi);
        }
    }
    pageMap_[slice] = mask;
    if (mask) return;

    // codePage_ answers the same question at the 4 KB granularity the
    // data TLB hands out, so it may only be cleared once no slice in the
    // page is marked any more.
    const uint32_t page = (slice << CodeGuard::kShift) >> 12;
    if (page >= codePage_.size() || !codePage_[page]) return;
    const uint32_t first = (page << 12) >> CodeGuard::kShift;
    const uint32_t count = 4096 >> CodeGuard::kShift;
    for (uint32_t i = first; i < first + count && i < pageMap_.size(); i++)
        if (pageMap_[i]) return;
    codePage_[page] = 0;
    cpu_.pomJitDtlbFlush();
}

void Engine::unmarkPages(uint64_t blockKey, uint32_t lo, uint32_t len) {
    if (pageMap_.empty() || !len) return;
    uint32_t p = lo >> CodeGuard::kShift;
    const uint32_t last = (lo + len - 1) >> CodeGuard::kShift;
    for (; p <= last && p < pageMap_.size(); p++) {
        auto it = sliceIndex_.find(p);
        if (it == sliceIndex_.end()) continue;
        std::vector<uint64_t>& keys = it->second;
        keys.erase(std::remove(keys.begin(), keys.end(), blockKey), keys.end());
        if (keys.empty()) sliceIndex_.erase(it);
        recomputeSliceMark(p);
    }
}

void Engine::markPages(uint64_t blockKey, uint32_t lo, uint32_t len) {
    if (pageMap_.empty() || !len) return;
    const uint32_t hi = lo + len - 1;
    uint32_t p = lo >> CodeGuard::kShift;
    const uint32_t last = hi >> CodeGuard::kShift;
    bool gained = false;
    for (; p <= last && p < pageMap_.size(); p++) {
        const uint32_t sliceLo = p << CodeGuard::kShift;
        const uint32_t sliceHi = sliceLo + CodeGuard::kUnit - 1;
        if (!pageMap_[p]) gained = true;
        pageMap_[p] |= CodeGuard::subMask(lo > sliceLo ? lo : sliceLo,
                                          hi < sliceHi ? hi : sliceHi);
        sliceIndex_[p].push_back(blockKey);
    }
    uint32_t q = lo >> 12;
    const uint32_t qlast = hi >> 12;
    for (; q <= qlast && q < codePage_.size(); q++) codePage_[q] = 1;

    // ── the INVALIDATION this function owes, and never paid ──────────────
    // A data-TLB write entry filled for a page BEFORE it held any translated
    // code points straight at the host bytes and carries an empty code mask.
    // A store through it bypasses the memory map, so the write guard never
    // sees it and the block the page now carries is never evicted:
    // self-modifying code, undetected. POM68K_JIT.md § 8's invalidation
    // table has claimed since it was written that "markPages() flushes when
    // it marks"; it did not — only the 1 -> 0 direction in serviceGuard()
    // ever flushed (found and fixed 2026-08-10).
    //
    // The flush is on the 0 -> 1 TRANSITION of a SLICE, which is both what
    // makes the mask sound and what keeps this cheap: the engine compiles
    // tens of thousands of blocks per boot but they come from far fewer
    // distinct slices, and the count falls to nothing once the working set
    // is translated. An UNCONDITIONAL flush here would be the same 8 KB
    // memset whose arm-time cousin cost -23 to -33 % of wall clock (§ 8,
    // "the arm-time flush that owned nothing"). Do not promote it to one.
    if (gained) cpu_.pomJitDtlbFlush();
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

    cpu_.pomJitArm(host, pageBase, physPage, len, super);
    winPhys_ = physPage;
    winLen_ = len;
    // With an architectural 040 I-cache the translation can be known before
    // the line exists. Let one interpreter instruction perform the real
    // fill; the following arm can then expose byte-identical cached data.
    if (!cpu_.pomJitCovers(pc)) { disarmWindow(); return false; }
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
        uint16_t indexExtension = 0;
        if (terminator && (op & 0xFF80) == 0x4E80) {
            const int mode = (op >> 3) & 7, reg = op & 7;
            const bool indexed = mode == 6 || (mode == 7 && reg == 3);
            if (indexed) {
                if (!cpu_.pomJitCovers(at + 2)) {
                    why = EndReason::WindowEdge;
                    break;
                }
                indexExtension = cpu_.pomJitPeek(at + 2);
            }
        }
        const uint32_t words = terminator
            ? branchWords(op, indexExtension) : 0;
        if (terminator && !cpu_.pomJitCovers(at + words * 2 - 2)) {
            why = EndReason::WindowEdge;
            break;
        }

        // The interpreter's own cycle count for this instruction, recorded
        // rather than modelled: a backend has to agree with it before it may
        // emit the instruction (JitIr.h, Instr::cycles).
        const int64_t clk0 = cpu_.getClock();
        cpu_.pomJitBeginTiming();
        const bool didRetire = cpu_.pomJitExecOne();
        const auto timing = cpu_.pomJitEndTiming();
        if (!didRetire) { retired++; why = EndReason::Faulted; break; }
        retired++;
        const int64_t spent = cpu_.getClock() - clk0;
        const uint16_t cycles = uint16_t(spent >= 0 && spent < 0x10000 ? spent : 0);
        const auto narrowCycles = [](int64_t value) {
            return uint16_t(value >= 0 && value < 0x10000 ? value : 0);
        };
        // Never expose a partial or internally inconsistent split to a code
        // generator. Overflow likewise collapses to the legacy conservative
        // representation, in which the whole delta is `baseCycles`.
        const bool timingExact = timing.valid &&
            timing.baseCycles >= 0 && timing.icacheCycles >= 0 &&
            timing.postExceptionCycles >= 0 &&
            timing.baseCycles + timing.icacheCycles +
                timing.postExceptionCycles == spent;
        const uint16_t baseCycles = timingExact
            ? narrowCycles(timing.baseCycles) : cycles;
        const uint16_t icacheCycles = timingExact
            ? narrowCycles(timing.icacheCycles) : 0;
        const uint16_t postExceptionCycles = timingExact
            ? narrowCycles(timing.postExceptionCycles) : 0;
        // The real instruction-stream fetch count (SKIP_LAST_RD forms fetch
        // `words`, not `words + 1`) — what the emitted 030 i-cache charge
        // must reproduce. 0 = unknown; the backend then refuses to charge.
        const uint8_t fetchWords = uint8_t(
            timing.fetchWords > 0 && timing.fetchWords < 256
                ? timing.fetchWords : 0);
        const uint32_t observedNextPc = cpu_.getPC();
        const uint16_t terminalIrd = cpu_.getIRD();
        const uint16_t terminalIrc = cpu_.getIRC();

        if (terminator) {
            ir.instrs.push_back(Instr{ at, op, uint16_t(words), kind,
                                       instrFlags(op, kind), cycles, baseCycles,
                                       icacheCycles, postExceptionCycles,
                                       fetchWords,
                                       observedNextPc, terminalIrd, terminalIrc,
                                       true });
            ir.instrs.back().memory =
                describeMemory(op, guestFamily_ == kGuest68030);
            ir.instrs.back().semantics = describeInstruction(op);
            at += words * 2;
            why = EndReason::ControlFlow;
            break;
        }

        const uint32_t next = cpu_.getPC();
        // 22 bytes is the longest a 68020-family instruction can be. Landing
        // outside that window means the instruction transferred control —
        // a trap the classifier did not predict, or a fault redirect.
        if (next <= at || next - at > 22) {
            ir.instrs.push_back(Instr{ at, op, 1, kind, instrFlags(op, kind),
                                       cycles, baseCycles, icacheCycles,
                                       postExceptionCycles, fetchWords,
                                       observedNextPc,
                                       terminalIrd, terminalIrc, true });
            ir.instrs.back().memory =
                describeMemory(op, guestFamily_ == kGuest68030);
            ir.instrs.back().semantics = describeInstruction(op);
            why = EndReason::Discontinuity;
            break;
        }

        ir.instrs.push_back(Instr{ at, op, uint16_t((next - at) / 2),
                                   kind, instrFlags(op, kind), cycles, baseCycles,
                                   icacheCycles, postExceptionCycles,
                                   fetchWords,
                                   observedNextPc, terminalIrd, terminalIrc,
                                   true });
        ir.instrs.back().memory =
            describeMemory(op, guestFamily_ == kGuest68030);
        ir.instrs.back().semantics = describeInstruction(op);
        at = next;

        if (guard_.tripped()) { why = EndReason::WindowEdge; break; }
    }

    running_ = false;
    traceRetired_ = retired;
    if (retired) {
        stats_.add(stats_.instrs, retired);
        // Counted apart so the report's native share stops absorbing the
        // tracer's interpretation (JitStats.h owns the why).
        stats_.add(stats_.traceInstrs, retired);
    }

    // A single-instruction "block" that ended on a discontinuity or a fault
    // describes nothing reusable — do not cache it.
    if (ir.instrs.empty() || why == EndReason::Discontinuity ||
        why == EndReason::Faulted) {
        stats_.bump(Exit::NotCompilable);
        return nullptr;
    }

    // A flush requested from inside the trace was deferred; the block we
    // just recorded belongs to the world that flush is throwing away.
    if (pendingFlush_) { flushAll(Flush::Deferred); return nullptr; }

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
        for (Instr& in : ir.instrs) {
            in.extensionCount = uint8_t(std::min<unsigned>(
                in.words > 0 ? in.words - 1 : 0, Instr::MaxExtensionWords));
            for (unsigned i = 0; i < in.extensionCount; i++)
                in.extensions[i] = ir.word(in.pc + 2 + uint32_t(i) * 2);
            describeEffectiveAddresses(in);
            refineMemoryFromExtensions(in, guestFamily_ == kGuest68030);
            describeControlFlow(in);
        }
    }

    Block b;
    b.ir = std::move(ir);
    b.gen = blocksGen_;
    if (profitScore_) {
        const BackendCaps caps = backend_->caps();
        for (const Instr& in : b.ir.instrs)
            if (backend_->canEmit(in.opcode) ||
                (in.kind == Kind::Branch && caps.branches))
                b.nativePotential++;
    }
    auto [it, inserted] = blocks_.emplace(key(pc, super), std::move(b));
    if (!inserted) return &it->second;

    {
        uint32_t lo = 0, len = 0;
        blockSpan(it->second.ir, lo, len);
        markPages(key(pc, super), lo, len);
    }
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
    dtlbLastReason_ = RuntimeFillTag;

    // With the architectural 040 D-cache active, a native host mapping is
    // never legal: it would bypass resident/dirty cache data. Unlike an ATC
    // probe miss this refusal is not transient, so remember a tagged null
    // entry. The generated path then goes straight to the exact cache-aware
    // access thunk instead of calling fillDtlb and receiving the same refusal
    // on every load/store. A Q605 boot previously made 321 million such
    // redundant fill calls.
    if (cpu_.pomCache040Active()) {
        dtlbWhy_[kWhyCache040]++;
        dtlbLastReason_ = RuntimeNonPlain;
        moira::Moira::PomJitDtlb& tlb = write ? cpu_.pomJitDtlbW
                                              : cpu_.pomJitDtlbR;
        const uint32_t page = addr >> 12;
        moira::Moira::PomJitDtlbEntry& e =
            tlb.e[page & (moira::Moira::PomJitDtlb::kEntries - 1)];
        e.tag = moira::Moira::pomJitDataTag(addr, cpu_.pomJitSuper());
        e.host = nullptr;
        e.codeMask = 0;
        stats_.add(stats_.dtlbRefused);
        return nullptr;
    }

    if (!mem_.dataSpan) {
        dtlbLastReason_ = RuntimeNonPlain;
        return nullptr;
    }

    const bool super = cpu_.pomJitSuper();
    uint32_t phys = 0, pageBase = 0, pageLen = 0;
    // A failed PROBE is transient — no ATC entry yet, or a write to a page
    // whose descriptor still owes its M bit. The interpreter's next access
    // fixes both, so this one is not remembered.
    if (!cpu_.pomJitProbeData(addr, super, write != 0, phys, pageBase, pageLen)) {
        dtlbWhy_[kWhyProbe]++;
        return nullptr;
    }

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
    // …and that argument does not stop at 8 KB. Any page at least a slice
    // wide fills as independent 4 KB slices, because translation preserves
    // the in-page offset and pages are size-aligned. A page SMALLER than a
    // slice is a different matter and stays refused: one slice would then
    // span several pages with different translations, and the entry has room
    // for exactly one.
    //
    // The 68030 is where this stopped being academic. Its TC picks a page
    // size anywhere from 256 bytes to 32 KB, and the Mac LC II's System does
    // not pick 4 or 8 KB: with the old exact test, x86-64 on an LC II
    // refused **17 425 292** fills over one bring-up run — every single data
    // access paying a call, a probe and a rejection, which is the same
    // constant loss the 8 KB case cost before it was allowed (2026-08-10).
    if (pageLen < 4096) { dtlbWhy_[kWhyPageLen]++; return nullptr; }
    (void)pageBase;
    const uint32_t physSlice = phys & ~4095u;

    uint32_t span = 0;
    uint8_t* host = mem_.dataSpan(mem_.self, physSlice, span, write);
    if (host && span < 4096) host = nullptr;
    if (!host) {
        dtlbWhy_[kWhyNotRam]++;
        dtlbLastReason_ = RuntimeNonPlain;
    }

    // ── the per-slice code mask (Moira.h § PomJitDtlbEntry) ───────────────
    // A page holding translated code used to be refused WHOLE, because an
    // entry maps 4 KB while CodeGuard works at 256 bytes — a 16x mismatch,
    // and 68k code shares its page with its own stack constantly. That one
    // refusal was 95.6 % of every remembered refusal on an idle Finder
    // (63 998 of 66 922), and dropping it — unsafely, as a ceiling
    // measurement — was worth -9.8 % of wall clock. Generated code now
    // tests the mask and falls back per SLICE, so a store that could be
    // self-modifying still goes through the memory map and the guard still
    // sees it, while a store 3 KB away from the nearest block does not pay
    // for the coincidence of sharing a page with it.
    uint32_t mask = 0;
    if (write && host && !codePage_.empty() &&
        (physSlice >> 12) < codePage_.size() && codePage_[physSlice >> 12]) {
        static_assert(CodeGuard::kShift == moira::Moira::PomJitDtlb::kSliceShift,
                      "one mask bit per CodeGuard slice");
        const uint32_t first = physSlice >> CodeGuard::kShift;
        const uint32_t n = 4096u >> CodeGuard::kShift;
        for (uint32_t i = 0; i < n; i++) {
            const uint32_t s = first + i;
            if (s < pageMap_.size() && pageMap_[s]) mask |= 1u << i;
        }
    }
    if (write && host && mem_.aliasCodeMask)
        mask |= mem_.aliasCodeMask(mem_.self, physSlice, pageMap_.data(),
                                   uint32_t(pageMap_.size()));
    if (mask) {
        dtlbWhy_[kWhyCodePage]++;
        // A backend that does not test the mask must never be handed this
        // entry: its store would bypass the map and the guard would never
        // see it. That is the whole-page refusal this replaced, kept for
        // everyone who has not opted in (BackendCaps).
        if (!maskAware_) host = nullptr;
    }

    // The answer is cached either way. A REFUSAL is worth remembering — an
    // I/O register, the ROM window seen by a store — because a hardware poll
    // loop would otherwise ask again on every single iteration, and the ask
    // is a call. Every reason for refusing here needs a map change or a
    // block flush to stop being true, and both empty this cache.
    moira::Moira::PomJitDtlb& tlb = write ? cpu_.pomJitDtlbW : cpu_.pomJitDtlbR;
    const uint32_t page = addr >> 12;
    moira::Moira::PomJitDtlbEntry& e = tlb.e[page & (moira::Moira::PomJitDtlb::kEntries - 1)];
    // The privilege the entry was PROBED under rides in the tag (bit 31),
    // so a supervisor-only page filled from supervisor mode can never be
    // hit by user code — and nothing needs flushing on an RTE.
    e.tag = moira::Moira::pomJitDataTag(addr, super);
    e.host = host;
    e.codeMask = mask;
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
            const uint32_t fpc = cpu_.getPC();
            disarmWindow();
            cpu_.execute();
            stats_.add(stats_.interpInstrs);
            stats_.bump(Exit::CpuFlags);
            stats_.miss(Miss::CpuFlags);
            if (dispatchRingOn_) [[unlikely]] ring(0, fpc, clockTarget, 0, 1);
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
            stats_.miss(Miss::ArmBackoff);
            if (dispatchRingOn_) [[unlikely]] ring(1, pc, clockTarget, 0, 1);
            continue;
        }

        if (!armWindow(pc, super)) {
            // Code outside plain memory, or a translation the probe cannot
            // confirm. The interpreter runs it and stacks any fault itself
            // — and the next probes wait their turn.
            armBackoff_ = armBackoff_steps_;
            cpu_.execute();
            stats_.add(stats_.interpInstrs);
            stats_.miss(Miss::ArmFailed);
            if (dispatchRingOn_) [[unlikely]] ring(2, pc, clockTarget, 0, 1);
            continue;
        }
        armBackoff_ = 0;

        if (!useBlocks_) { runWindow(clockTarget); continue; }

        ctx_.clockTarget = clockTarget;

        // A recorded block is a script of LOGICAL addresses. When the
        // translation underneath changes — a PFLUSH, a write to TC/URP/SRP
        // or a TTR — the same logical pc can point at entirely different
        // code. Until 2026-08-19 the whole cache was dropped here, 4 527
        // times per 2000-frame LC II boot, and the retrace+recompile storm
        // was the largest x64-vs-threaded differential left once the CACR
        // hint had been retired. Nothing that binds logical to physical
        // survives the bump unrevalidated — the data TLB is flushed at the
        // source (Moira::pomJitMapMoved), pomJitCovers refuses the stale
        // window so armWindow has ALREADY re-proved this pc's page under
        // the new generation, and every published native link is retracted
        // below. The BLOCKS stay, each pinned by its recorded
        // (logical page, physical page, length) triple: the dispatch path
        // re-proves that triple against the fresh window before a stale-
        // generation block may run or publish again, and evicts it when
        // the mapping moved.
        if (blocksGen_ != cpu_.pomJitMmuGen) {
            cpu_.pomJitDtlbFlush();          // idempotent belt to the source
            clearLinks();
            blocksGen_ = cpu_.pomJitMmuGen;
            stats_.bump(Flush::MmuGen);
        }

        auto it = blocks_.find(key(pc, super));
        if (it != blocks_.end() && it->second.gen != blocksGen_) {
            Block& b = it->second;
            if (b.ir.codeBase == cpu_.pomJitWindow.base &&
                b.ir.physBase == winPhys_ && b.ir.physLen == winLen_) {
                b.gen = blocksGen_;
                if (b.code)
                    if (void* e = backend_->linkEntry(b.code))
                        publishLink(pc, super, e);
            } else {
                if (b.code) backend_->release(b.code);
                // Its key must leave the slice index with it: record() is
                // about to re-file the same key, and a stale one under the
                // MMU-generation churn of Mac OS VM grew the index by
                // gigabytes on the LC II soak (2026-08-22).
                {
                    uint32_t lo = 0, len = 0;
                    blockSpan(b.ir, lo, len);
                    unmarkPages(it->first, lo, len);
                }
                blocks_.erase(it);
                it = blocks_.end();
            }
        }
        if (it == blocks_.end()) {
            // First visit: tracing runs the instructions as it records them,
            // so there is normally nothing left to execute afterwards. Only
            // when the trace retired NOTHING (the very first opcode is a
            // branch, a trap or an MMU instruction) must the interpreter
            // step in — otherwise this loop would not advance.
            traceRetired_ = 0;
            record(pc, super, clockTarget);
            stats_.miss(Miss::Trace, traceRetired_);
            if (traceRetired_ == 0) {
                cpu_.execute();
                stats_.add(stats_.interpInstrs);
                stats_.miss(Miss::Trace);
            }
            if (dispatchRingOn_) [[unlikely]]
                ring(3, pc, clockTarget, 0, traceRetired_ ? traceRetired_ : 1);
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
            const uint32_t visits = ++b.visits;
            const bool hotReady = visits >= uint32_t(hotAt_);
            const bool profitReady = !profitScore_ ||
                uint64_t(visits) * b.nativePotential >=
                    uint64_t(profitScore_);
            if (!b.rejected && hotReady && profitReady) {
                // record() already indexed and marked this block's physical
                // footprint. Only the TLB flush is needed now so a writable
                // entry filled before that mark cannot survive compilation.
                cpu_.pomJitDtlbFlush();
                // A linked successor bypasses armWindow's I-cache residency
                // guard. Keep native blocks, but return between them while
                // the architectural 040 cache is active.
                const uint32_t savedLinkMask = ctx_.linkMask;
                if (cpu_.pomCache040Active()) ctx_.linkMask = 0;
                const bool cacheReady = !cpu_.pomCache040Active() ||
                    cpu_.pomCache040CodeMatches(
                        b.ir.entryPc, uint32_t(b.ir.code.size() * 2));
                // POM68K_JIT_DENY_FROM/_TO (hex): refuse to COMPILE any
                // block whose entry pc falls in [from, to). A bisection
                // instrument, not a mode: the 2026-08-19 retained-cache
                // divergence could only be pinned to one block by halving
                // the pc space, because every pacing/knob perturbation
                // moved the trajectory and healed the symptom.
                const bool denied = config_.denyFrom != config_.denyTo &&
                    pc >= config_.denyFrom && pc < config_.denyTo;
                if (cacheReady && !denied) {
                    ScopedResolvedConfig activeConfig(ctx_.config);
                    recordIndexForms(b.ir);
                    const auto compileStart = std::chrono::steady_clock::now();
                    stats_.add(stats_.compileAttempts);
                    const CompileResult compiled = backend_->compile(b.ir, ctx_);
                    b.code = compiled.code;
                    const auto compileEnd = std::chrono::steady_clock::now();
                    const uint64_t compileNs = uint64_t(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            compileEnd - compileStart).count());
                    stats_.add(stats_.compileNanos, compileNs);
                    if (!b.code) {
                        stats_.add(stats_.compileRejected);
                        CompileReject reason = compiled.reject;
                        // A backend returning {nullptr, None} would otherwise
                        // make the attribution silently fail its own sum.
                        if (reason == CompileReject::None)
                            reason = CompileReject::Emit;
                        stats_.add(stats_.compileRejects[int(reason)]);
                        stats_.add(stats_.compileRejectNanos[int(reason)],
                                   compileNs);
                    }
                    // CodeBuffer is deliberately a non-compacting bump
                    // allocator. Precise SMC eviction releases a Compiled
                    // wrapper but cannot reclaim its executable bytes, so a
                    // long session eventually reaches the end even while the
                    // live block map is below maxBlocks_. Capacity exhaustion
                    // is recoverable only by retracting every entry before
                    // rewinding the buffer. Do it here, with no generated code
                    // running and before touching `b` again: flushAll erases
                    // the Block that owns this reference. Reserve/W^X errors
                    // remain ordinary permanent rejections, avoiding a retry
                    // loop on hosts where executable memory is unavailable.
                    if (compiled.reject == CompileReject::CodeCapacity) {
                        ctx_.linkMask = savedLinkMask;
                        flushAll(Flush::CodeCapacity);
                        continue;
                    }
                }
                if (denied) { b.rejected = true; }
                ctx_.linkMask = savedLinkMask;
                if (b.code) {
                    b.code->ir = &b.ir;
                    stats_.add(stats_.blocksCompiled);
                    // From here on another block may jump straight into it
                    // instead of coming back through the engine.
                    if (void* e = backend_->linkEntry(b.code)) publishLink(pc, super, e);
                } else if (cacheReady) {
                    b.rejected = true;
                }
            }
            if (!b.code) {
                const uint64_t before = stats_.windowInstrs.load(
                    std::memory_order_relaxed);
                runWindow(clockTarget);
                const Miss miss = b.rejected ? Miss::Rejected
                    : hotReady && !profitReady ? Miss::NotProfitable
                                               : Miss::NotHot;
                stats_.miss(miss,
                            stats_.windowInstrs.load(
                                std::memory_order_relaxed) - before);
                if (dispatchRingOn_) [[unlikely]]
                    ring(4, pc, clockTarget, 0,
                         uint32_t(stats_.windowInstrs.load(
                             std::memory_order_relaxed) - before));
                continue;
            }
        }

        // The block may span several I-cache lines. armWindow guarded the
        // entry line; validate the whole embedded byte stream before native
        // execution. The window path refills any missing line naturally.
        if (cpu_.pomCache040Active() &&
            !cpu_.pomCache040CodeMatches(
                b.ir.entryPc, uint32_t(b.ir.code.size() * 2))) {
            const uint64_t before = stats_.windowInstrs.load(
                std::memory_order_relaxed);
            runWindow(clockTarget);
            stats_.miss(Miss::CacheLine,
                        stats_.windowInstrs.load(std::memory_order_relaxed) - before);
            continue;
        }

        running_ = true;
        RunResult r = backend_->run(b.code, ctx_);
        running_ = false;
        if (dispatchRingOn_) [[unlikely]]
            ring(5, pc, clockTarget, uint8_t(r.exit), r.instrs);

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
        if (pendingFlush_) flushAll(Flush::Deferred);
        else if (guard_.tripped()) serviceGuard();
    }
}

}  // namespace jit
