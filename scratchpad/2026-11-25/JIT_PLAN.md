# Phase 2 — bringing the JIT back on Dirk's Moira: staged plan (DRAFT)

Status: **draft for the owner's approval, nothing decided.** Written on
2026-11-25 from a read-only study of `main` (Dirk's `bf98107f` + 15 patches)
and `origin/main_old` (`0139930a`). No build or test backs any claim below;
line numbers are those of the tree on that day and will move. Every
performance figure from main_old is "not determined on this core" until it
is measured again. Phase 2 opens only after Phase 1 exits (TODO § Objectif
principal): first the full machine tier on the final tree, on the M4.

## 0. Findings that change the TODO § Phase 2 skeleton

- **F1 — the JIT code is already in the tree.** `src/jit` on `main` equals
  main_old's except `JitConfig.h` (−8 lines), `POM68K_JIT.md` and the new
  `JitEngineStub.cpp`; all JIT tests are present. What "comes back" is the
  core seam (~66 `pomJit*` identifiers, ~1.3–1.5k lines in the fork's
  `Moira.h`, `MoiraExecMMU_cpp.h`, `Moira.cpp`, `MoiraDataflow_cpp.h`) and
  the gate registrations (present behind `if(POM68K_JIT)`). The real work is
  rewriting what Dirk's boundary contract invalidates (F2).
- **F2 — Dirk's instruction-boundary contract differs in five places** the
  IR, the cost model and both emitters encode:
  - PC convention: at a boundary `reg.pc == reg.pc0 + 2` (Moira.cpp:193;
    `advancePC0` also sets `prev_pc0`); the fork had `pc0 == pc`. Every
    emitter PC commit, `ControlFlowPlan` and the dispatch keys.
  - Prefetch queue: an `ird`/`irc` queue on 020/030/040, no mode-5 loop
    heads (`mmu040InstrStart`, `mmuExecuteStart`, `mmuFetchWord` gone). The
    IR's terminal-queue assumptions (POM68K_JIT.md § 7) and the JSR
    target-fetch helpers no longer apply.
  - Fault model: restart from a Registers snapshot whenever
    `flags & (SAVE_REGS|MMU)` (Moira.cpp:264-272) plus the `completed`
    cycle record (MoiraDataflow_cpp.h:785-804), not 030/040 continuation.
    The fault phases of `Instr::memory` and `jit_restart_write_030_test`
    must be re-derived; latches `mmu040MovemArmed`, `mmuFixupReg`, `mmuRmw`,
    `mmu040Lrmw` are gone.
  - Stateful timing: `CYCLES_68020(c)` is `syncCyclePenalty(c +
    budget.penalty)` (penalty reset at instruction start, EA penalties, +2
    per i-cache fill, +4 per narrow port; then an `accumulated`/`stalled`
    state machine, Moira.cpp:513-546). A block's charge is not a JitCost.h
    constant; generated code must replay the state machine per instruction,
    and JitCost.h's columns must be re-checked against Dirk's tables.
  - `flags`: `State::MMU` stays set while the MMU is on, `SAVE_REGS` at
    reset; the engine's idle test must mask both.
- **F3 — the 68030 is now in the 68020's position** ("the cache owns every
  fetch", POM68K_JIT.md § 2.1): Dirk's 030 I- and D-caches are emulated and
  hold data by default (Moira.cpp:120-121, Cache.cpp). The fork's 030 JIT
  was built over a timing-only overlay, so a window or DTLB reading RAM
  directly no longer matches the interpreter (stale bytes, latch, fills,
  penalties). Dirk's 68040 emulates no cache: every fetch is TT test +
  linear 64-entry `atcFind` with history touch + virtual `read16`, exactly
  what the window removed. **The 68040 is the clean first family; the 68030
  needs a decision (Q1).**
- **F4 — `docs/MOIRA_CONVERGENCE.md` § The Mac layer is out of date:** a
  CACR-change delegate exists (`didChangeCACR`, MoiraInterface.h:293,
  invoked at MoiraRegisters_cpp.h:81).
- **F5 — gate registration is all-or-nothing:** one `if(POM68K_JIT)` wraps
  Pom68kJitGates.cmake, Pom68kJitFuzzGates.cmake and Pom68kCpuGates.cmake:94;
  Pom68kGatePolicy.cmake puts all 106 rows of `pom68k_jit_gates.tsv` in the
  absent roster when OFF; the `jit-fast` lists hard-code the 040 copyback
  trio (040 cache model, out). Staged admission needs a mechanism (S0).

## 1. What main_old's JIT consumed from the fork core

Classes: **(a)** available in Dirk's core as is; **(b)** doable in the Mac
layer (MoiraCompat, wrappers) or JIT side without touching his core;
**(c)** needs a core hook (smallest shape given; may be refused).

Access in Dirk's `Moira.h`: protected `clock`, `reg`, `queue`, `budget`,
`flags`, `ipl`, `restart`, `completed`, `syncCyclePenalty`; private
`exec[]`, `advancePC0`, `checkForIrq`, `translate<>`, `peekInstr` and the
Dataflow/Exceptions/ALU members; public `mmu` (ATC arrays, `atcFind`,
`atcInsert`, `atcFlush*`, `testTT`, `pageMask`, `peek`), `iCache`/`dCache`
(`emulated`, `active`, `visitEntries`), `execute()`, `executeUntil()`.

| Fork interface | Class | Notes |
|---|---|---|
| `pomJitExecOne` | a | `Moira::execute()`, full boundary contract, per-call overhead |
| `pomJitIdle` | b | MoiraCompat accessor masking `MMU|SAVE_REGS` |
| getters (`getPC`, `getClock`, `getIRD/IRC`, …) | a | |
| `pomJitLayout` | b | offsets from a live object; new fields `prev_pc0`, `budget.*`, `nmiTaken`; fork-only fields drop |
| `pomJitSync` | b | wrappers override `sync()`; `syncCyclePenalty` via a MoiraCompat helper |
| `pomJitSimpleIpl` | a | moot: `POLL_IPL` is `reg.ipl = ipl`, no IPL delay |
| `pomJitBiasClock` | b | `clock` protected |
| `PomJitTiming` & probes | b (mostly) | IPL-poll position would be (c); used only by the late-poll prototype — drop |
| `pomJitMmuGen` / `MapMoved` / `AtcEvict` | c or b | hook: `u32 epoch` bumped in `atcInsert`, `atcFlush*`, `setTC`, TT and root writes (~10–15 lines). Alternative (b): MMU/cache instructions and RESET end blocks, engine generation bumped there and on reset/load, ATC line revalidated from the public arrays at arm/page-cross/entry |
| `pomJitProbeCode/Data` | b | from the public ATC arrays + `testTT`/`pageMask`; **never `mmu.peek()`**, which walks on a miss (the fork's J3b lesson) |
| ATC history touch on a hit | c or b | `atcTouch` is idempotent for the same line; hook: make it public (~2 lines), or replicate its 6 lines under lockstep |
| `pomMmuPipeLive` | — | obsolete (no pipe carry on Dirk's core) |
| CACR invalidation | a | `didChangeCACR` (F4); 040 CINV/CPUSH as block terminators (b) |
| Fetch window (`pomJitWindow`, `Fetch`, `Fetch020`, `Covers`, `Arm`, …) | c | single funnel `readInstr<C,F>` (MoiraDataflow_cpp.h:877-961); hook: compile-time-gated `{base, end, host}` tested in the non-cache branches after the watchpoint/IPL steps, replacing `translate`+`read16` (~20–40 lines). Host-performance seam, may be refused; carry like patches 0002/0005. First offer Dirk an O(1)/last-hit `atcFind`. On 020/030 the window cannot bypass `iCache.fetch` (F3) |
| 68000 window + bus stall | b | RAM/ROM fast path in the wrapper's `read16` forwarder; contention stays in `read16`; delete the inert `pomJitSetBusStall` stub |
| DTLB (`pomJitDtlbR/W`, flush, tags, `pomJitSuper`) | b | tables live in the JIT Context |
| Interpreter data window | c | at `readBus`/`writeBus`; on the 030 behind `dCache.active`; on the 040 a measured loss on the fork's 32-entry ATC, Dirk's has 64 — low priority, candidate to drop |
| Faulting access thunks (`pomJitReadData`, `WriteData`, `ReadProg`, `ReadOperandProg`, `Jsr040`) | c or b | hook = public noexcept access entry (likely refused). (b): whole-instruction fallback (`POM68K_JIT_ACCESS_THUNK=0`), or a Mac-layer replica of `readBus` for TT/identity I/O under lockstep. JSR helpers must be re-derived against `queue.irc = read<PROG>(ea)` |
| 040 cache model (`pomCache040*`, `pomJitCache040*`, ATC replay/COW, `atcWindow040`) | out | policy; Dirk's 040 ATC is fully associative with history bits |
| Code guard / SMC hooks | b | already in MoiraCpu.h and the memory maps; on the 030, guard-driven invalidation would diverge from Dirk's data-bearing cache |
| Save-state (`pomFlushAtcs`, `pomVisitState`) | b | already in MoiraCompat; engine flushes and disarms on restore; no JIT state serialized |

## 2. Stages

Rules throughout: the interpreter is the oracle; engine and backend are
separate decisions; `threaded` correct for every family before any native
backend; conformance, whole-tier stability and same-process ABBA
performance are three separate admissions; `guestFamilies` never inferred
from `autoFamilies`; the 040 cache model stays out.

- **S0 — seam decisions and staged-admission plumbing.** Write § 1 into
  `POM68K_VENDOR.md` and `MOIRA_CONVERGENCE.md` (fix F4). Decide what to ask
  Dirk, in order: O(1)/last-hit ATC lookup; public `atcTouch`; MMU epoch;
  compile-time fetch window (owner approves each text). Add an admitted-gate
  mechanism (stage column or `pom68k_jit_admitted.tsv`, a
  `pom68k_jit_gate(<name>)` guard, `jit-fast`/`smoke` lists derived from the
  admitted set). ~150 lines of CMake + docs.
- **S1 — engine shell, threaded, no core hook.** `POM68K_JIT=ON` with
  `POM68K_JIT_BACKENDS=threaded` builds: JitEngine.cpp's 77 fork-symbol
  sites through MoiraCompat accessors, threaded replays with `execute()`,
  harnesses ported (~50 sites). Correctness scaffold only, no speed.
  Gates: jit_backend_test (policy), jit_lockstep_test/_blocks_test,
  jit_lockstep_board_q630_threaded_test, _iici_threaded_blocks_test,
  jit_lockstep_030_test/_blocks_test, cpu_vectors0{30,40}_jit_test,
  jit_sst68040*, jit_sst68030*, the 040 and 030 fuzz sets,
  jit_program_fuzz_lc040_test, jit_mactv_boot_etalon. Exit: identical
  lockstep, `-L m040`/`-L m030` green under `POM68K_CPU_ENGINE=jit`,
  `savestate_040_test` on both engines.
- **S2 — interpreter windows.** S2a 68000: wrapper fast path; gates
  jit_lockstep_68000_test (retargeted), jit_program_fuzz_000{,_trace}_test,
  sst68000, plus/se/classic etalons; ABBA on jit_bench_plus. S2b 68040:
  `readInstr` window (queue patch or upstream), epoch or validation,
  ATC-only probe, exact touch, interpreter arms it too; gates: S1's 040 set
  with the window, jit_lockstep_board_{q630,q700,centris650}_test,
  _q630_jitter_test, jit_{q605,q630,q700,centris650}_boot_etalon and their
  `interp_*` twins; ABBA on jit_bench (Q605). S2c 68030: profile first
  (Dirk's latch and cache may already remove most walks), then only if
  justified a miss-path window in `ICache::fill` and/or a data window behind
  `dCache.active`. A window that does not pay is not merged.
- **S3 — threaded admitted.** Blocks, hot threshold and dispatch cache on
  the S2b window; `guestFamilies` all, `autoFamilies` only where ABBA shows a
  gain (040 expected, 030 not determined). Whole-tier `-L m040` (and `m030`
  if admitted) under the engine.
- **S4 — first native backend, 68040 only.** Entry: S3 closed and Phase 1 ›
  Temps closed (the cost model is rebuilt against it). Rewrite in JitIr.h,
  JitCost.h, JitEaPlan.h and the chosen emitter: PC convention, queue
  refill, `budget`/`syncCyclePenalty` replay, fault phases under the
  restart model, MoiraCompat layout, JIT-side DTLB, whole-instruction
  fallback first. Gates: jit_asset_free_lockstep_test
  (`POM68K_JIT_REQUIRE_NATIVE=1`), jit_backend_test, jit_backend_parity_test,
  x64 (jit_lockstep_x64_test, _x64_fine_test, _noaccess_test) or a64
  (jit_lockstep_a64_coarse_test, jit_store_guard_a64_test), the 040 vectors,
  SST, fuzz, boards and boot etalons with `REQUIRE_NATIVE`. S4b: exact I/O
  thunks only on ABBA evidence.
- **S5 — second native ISA for the 040.** Same gates + parity table;
  requalify `REG_CACHE`, `A64_PACING`, `WATCH_OPCODE`, `LATE_POLL`, x64
  compact CCR, `profitScore68030`/`maxAccessThunk030`.
- **S6 — native 68030,** gated on Q1. Gates: jit_lockstep_030_* (incl.
  x64/a64 alignment), jit_restart_write_030_test re-derived, the 030 boards
  (iici, iici_jitter, se30, iifx, iivx, lc3, duo230, pb170, pb180), the 030
  boot etalons, vectors, SST, fuzz (especially `_smc_`). Retire or requalify
  the 030 experimental/packed-CCR prototypes.
- **S7 — default policy restored:** `POM68K_JIT` ON, `auto` per CLAUDE.md,
  regenerate the gate TSV and absent roster, restore the CI JIT proof and
  `tools/check_jit_performance.py`, re-base `performance_budgets.tsv`;
  Windows stays on `threaded`.
- **S8 — 040 D-cache JIT mode,** only if a 040 cache model is adopted above
  the core (held-back gates: the jit_copyback_* set, the 040 dcache fuzz
  set, the 16×4-ATC `jit_atc040_cases`).
- Retire with a CHANGELOG reason: jit_program_fuzz_020_irq7_test (no 020
  engine; it compared the interpreter with itself).

## 3. Decisions for the owner

- **Q1 — 68030 vs Dirk's caches (largest risk).** (i) the JIT models latch,
  I- and D-cache state and penalties exactly (new design); (ii) the 030
  stays on the interpreter plus at most miss-path windows, like the 020;
  (iii) disable cache emulation on Macs (changes the oracle machine,
  contradicts Phase 1's timing work). Decides whether S6 exists.
- **Q2 — queue patch vs asking Dirk.** Offer the behaviour-preserving ATC
  speed-up first; carry the fetch window as a compile-time-gated
  host-interface queue patch (like 0002/0005) if he declines.
- **Q3 — 68040 first** (deviates from TODO steps 2/4); the 68000 window
  moves to the Mac layer.
- **Q4 — save states:** no format change if the JIT stays exact; under
  Q1(i) the 030 cache entries and latch are serialized, so states must not
  depend on the engine (`savestate_040_test`, `lcii_savestate_etalon` on
  both engines).
- **Q5 — native ISA order:** TODO says a64 first, but Phase 1's green tier
  is x86-64; AArch64 needs its own Phase 1 tier first; Pi figures date from
  the fork.
- **Q6 —** admit S1 as a correctness scaffold only, never a default.
- **Q7 —** data windows and exact thunks need a hook Dirk may refuse or a
  replicated core fragment; they can be dropped at a fallback cost.
- **Q8 —** may S0 (paper work, upstream asks) start before Phase 1 exits?

Other risks: Dirk's `master` moves (re-read his commits at each stage);
`exec[]` is private, so per-instruction `execute()` overhead bounds
`threaded`; recovering the fork's timing attribution from `budget` deltas
is unproven.

## 4. Sizing (lines on main_old, already in tree on main)

- Engine layer ~7.4k (JitEngine.cpp 1720, JitIr.h 2263, JitConfig.h 449,
  JitEngine.h 603, JitBackend 495, …); threaded 127; x64 ~6.2k; a64 ~5.45k.
- Fork core seam ~1.3–1.5k lines (estimate). Expected on Dirk's core:
  window ~20–40, epoch ~10–15, `atcTouch` ~2; MoiraCompat JIT section
  ~200–400 (estimate).
- Tests: jit_asset_free_lockstep 3969, jit_restart_write_030 2921,
  jit_program_fuzz 1604, jit_backend 1511, …; CMake 663 + 261; 106 gate rows.
- Per stage: S0 ~150 CMake + docs; S1 77 engine sites + ~50 test sites +
  MoiraCompat +150; S2 core +30–60 (queue), MoiraCompat +150–250, wrapper
  50–100; S4 the largest (~2.3k IR + 0.3k cost + several thousand emitter
  lines touched; not determined); S6 depends on Q1.

Not determined by reading: whether Dirk's 040 cycle tables differ from his
030's; the share of ATC walks left on a warm 030 with his caches; how many
emitter sites encode `pc0 == pc`; every performance figure on this core.

Critical files: `src/MoiraCompat.h`; `src/jit/JitEngine.cpp` (with
`JitIr.h`, `JitCost.h`); `extern/moira/Moira/MoiraDataflow_cpp.h`
(`readInstr`, `readBus`, `readDataCache`) and `MMU/MMU0{30,40}.cpp`;
`cmake/Pom68kJitGates.cmake`, `cmake/Pom68kGatePolicy.cmake`,
`cmake/pom68k_jit_gates.tsv`; `src/jit/backends/JitBackendThreaded.cpp`,
then `JitBackendX64.cpp` / `JitBackendA64.cpp`.
