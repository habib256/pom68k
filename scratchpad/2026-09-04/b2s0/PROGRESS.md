# TODO § B.2 slice 0 — progress and evidence

Agent worktree (uncommitted, dirty by design):
`/home/gistarcade/src/pom68k/.claude/worktrees/agent-a3615ccef642adf65`

**Why this file is not in the shared checkout:** the harness refuses writes
from this agent to `/home/gistarcade/src/pom68k/scratchpad/...` ("This agent is
isolated in the worktree ... Edit the worktree copy of this file instead").
This *is* the same tracked path, in the worktree, and it merges at integration.

Base commit `346f084`. Patch snapshot (479 lines, 3 files):
`<worktree>/scratchpad/2026-09-04/b2s0/b2s0_instrument.patch`
All raw logs cited below are in `<worktree>/scratchpad/2026-09-04/b2s0/`.

Build dir: `<worktree>/build`
`cmake -S <worktree> -B <worktree>/build -DCMAKE_BUILD_TYPE=Release -DPOM68K_LTO=ON -DPOM68K_NATIVE=ON -DPOM68K_FAST_LINK=ON -DPOM68K_TESTS=ON -DPOM68K_WARNINGS=ON`
Built only with `systemd-run --user --scope -p MemoryMax=11G --quiet make -C <worktree>/build -j3 <targets>`.
Bench binary: `<worktree>/build/jit_bench_lcii`

Profile build dir: `<worktree>/build-profile` — RelWithDebInfo, **LTO=OFF**,
NATIVE=ON, `-fno-omit-frame-pointer`. Same recipe as the main checkout's
`build-profile`, which this agent must not touch. LTO off is mandatory:
`tools/profile_census.py`'s header records that identical-code folding
reassigns samples to arbitrary sibling symbols and the report lies.

Assets: `hdv/ref`, `hdv/boot.vhd` and `roms/*` symlinked into the worktree
from the main checkout (gitignored private inputs). `jit_bench_lcii` resolves
`hdv/boot.vhd`; the censuses resolve `hdv/ref/GISTPERSO-boot.vhd` through
`preferReferenceFixture`. **Nothing below soft-skipped.**

Engine env common to every run:
`POM68K_CPU_ENGINE=jit POM68K_JIT_BACKEND=x64 POM68K_JIT_BLOCKS=1 POM68K_JIT_HOT=1`

---

## What the patch does (work item A)

**A1 — `RuntimeNonPlain` reaches the per-address table.** `src/jit/JitEngine.cpp`
now prints a per-address table for `{NonPlain, CodeMask, CrossPage, Other}`;
`RuntimeFillTag` is deliberately excluded and the reason is written down (a
probe refusal is *transient*, `fillDtlb:1147`, so it fires on ordinary RAM
operands and keying it per address would grow the map without bound).
`JitBackendA64.cpp` now marks the access at `nonPlainMiss` and `fillMiss` too
(the guest address is live in w9 at both), records `RuntimeOther`
unconditionally, and `clearRuntimeAccess` zeroes all four access fields at
instruction entry instead of only `bytes`.

That last change fixes a self-check that was false by construction: on a64 the
`other-runtime-access` row printed `0 / N MISMATCH` on every run ever taken
(see `scratchpad/2026-09-03-night/q605_rogue.sample.txt.err` lines 280, 622,
961). It now reads `exact`.

**A2 — the x64 leg of the cause census, which never existed.** `JitBackendX64.cpp`
never filled `slowRuntimeReasonHisto` and never called `runtimeAddressObserver`,
so on x86-64 `[jit] runtime fallback causes` printed five zeros and
`attributed 0 / 5608573 runtime MISMATCH` for as long as it has existed —
`docs/MEASURING.md` § 3's own trap, with the reader on the backend that was not
looking (2026-09-04 `b2impl/census_verbose_base.log:214`, `:1294`).

Mirrored from a64: the Frame grew `slowRuntimeReasonHisto` / `dtlbFillReason` /
`runtimeAddressObserver` / `runtimeAddressSelf` / `runtimeReason` / four
`runtimeAccess*` slots (offsets 112–160, all `static_assert`ed); `memProbe`
gained four cold diagnostic doors (cross-page, codeMask, non-plain, fill
refusal) that name the refusal and then join the untouched `miss`; every
instruction entry resets the reason to `RuntimeOther`; the exact access thunks
reset it again on success; the runtime door indexes the flat `[reason][opcode]`
table with a 19-bit shift and then calls the address observer.

Everything folds to nothing when the census is off — the four doors literally
*are* `miss`, so not one byte of the probe changes.

Two things I could not mirror, both stated rather than papered over:

* **x64 attributes non-plain before cross-page; a64 does the reverse.** x64's
  probe loads the host pointer and tests it for null *before* the page-straddle
  test. An access that is both remembered-null and straddling is therefore
  attributed differently on the two backends. Moving either test to make them
  agree would change the hot path, so the difference is documented in the code
  instead.
* **`RuntimeFillTag` gets no per-address table on either backend** (reason
  above). Its opcode plane remains the instrument for it.

The address observer *was* mirrorable — register pressure at the x64 runtime
door is nil. Every register the backend keeps live across a block (kCpu, kFrm,
kTgt, kCnt, kPer, kClk) is callee-saved under System V, so the observer needs
no spilling at all; it does not go through `Emitter::call()` because that
helper exists to publish the deferred CCR around helpers that observe Moira,
and this one touches only an `unordered_map`. Its seventh argument (`codeMask`)
travels on the stack — two pushes, not one, because rsp is 16-byte aligned at
every call site in this backend.

---

## Checks actually run

| # | check | command | verdict |
|---|---|---|---|
| 1 | baseline bench 2000, **unmodified HEAD** build | `POM68K_BENCH_FRAMES=2000 build/jit_bench_lcii` | `fp=3de5c5ab62b4eca8`; retired 236 821 650; icache 571 592 391 / 443 637 308 / 127 950 916 → `base_bench_2000.log` |
| 2 | baseline bench 6000 | `POM68K_BENCH_FRAMES=6000 …` | `fp=cfb184b6faddabec`; icache 1 602 507 733 / 1 093 456 393 / 509 047 173 → `base_bench_6000.log`. Matches the coordinator's own HEAD numbers exactly, so this build is comparable. |
| 3 | build with the instrument | `systemd-run … make -C build -j3 jit_bench_lcii lcii_simcity_census lcii_speedometer_census jit_backend_parity_test` | rc=0, **zero warnings** |
| 4 | **conformance — census OFF, 2000** | as #1, instrumented binary | `new_bench_2000.log`: `fp=3de5c5ab62b4eca8`; native / block fallback / window-interp / tracing / icache triple / every not-native cause **identical to #1** |
| 5 | **conformance — census OFF, 6000** | as #2, instrumented binary | `new_bench_6000.log`: `fp=cfb184b6faddabec`; all counters **identical to #2** |
| 6 | **conformance — census ON, 2000** | `POM68K_JIT_HISTO=1 POM68K_BENCH_FRAMES=2000 …` | `histo_bench_2000.log`: `fp=3de5c5ab62b4eca8` **unchanged with the census on**, every self-check `exact` (below) |
| 7 | census ON, 6000, `POM68K_JIT_ACCESS_THUNK=1` | `POM68K_JIT_HISTO=1 POM68K_JIT_VERBOSE=1 POM68K_JIT_ACCESS_THUNK=1 POM68K_BENCH_FRAMES=6000 …` | `histo_6000_thunk1.log`: `fp=cfb184b6faddabec`, `attributed 5915765 / 5915765 runtime  exact` |
| 8 | census ON, 6000, `POM68K_JIT_ACCESS_THUNK=2` | …`=2`… | `histo_6000_thunk2.log`: `fp=cfb184b6faddabec`, `attributed 5183138 / 5183138 runtime  exact` |
| 9 | `lcii_simcity_census`, census ON | `POM68K_JIT_HISTO=1 POM68K_JIT_VERBOSE=1 build/lcii_simcity_census` | `simcity_census_thunk1.log`: reached `app-load` (**not** the `INVALID: nothing launched` guard), `fp=762e9d25153dd20e screen=b7904048ca2400ad` — **identical to the 2026-09-04 reference run**. All six phases `attributed N / N runtime  exact`. |
| 10 | profile build | `cmake … build-profile` + `make -j3 jit_bench_lcii lcii_simcity_census` | rc=0 |
| 11 | gperftools profile, jit arm | `run_profile.sh jit` | **first attempt VOID** — see "instrument failure" below. Re-run pending. |

### The `exact` self-check output (check #6, `histo_bench_2000.log`)

```
[jit] runtime fallback causes
  fill/tag MMU            22330    0.67%  5368:3119  16AC:2134  24D0:1914  117C:1687  4CDE:1484  22D8:1065  2968:1008  2F70:725
  non-plain/MMIO        2491619   74.59%  24D0:1642326  177C:275031  1747:185902  14D0:104660  169A:96133  117C:46712  1084:28858  1740:19225
  codeMask               785998   23.53%  08A9:646513  22D8:39293  12DC:19973  10D9:15560  2089:12578  32FC:8909  2081:6281  30BC:4449
  cross-page              40550    1.21%  4CDE:15214  48E6:10582  48E7:5646  4CDF:4819  48EA:1432  48D2:359  4EB9:353  EFD1:257
  other guard                 1    0.00%  80C1:1
  attributed            3340498 /      3340498 runtime  exact

[jit] exact non-plain/MMIO addresses — 2491619 / 2491619 exact
[jit] exact codeMask addresses — 785998 / 785998 exact
[jit] exact cross-page addresses — 40550 / 40550 exact
[jit] exact other-runtime-access addresses — 1 / 1 exact
```

All five rows `exact`, on the backend that previously printed five zeros.

---

## Results already established

### Slice 5 — the answer is a refusal, and it is unambiguous

The `non-plain/MMIO` per-address table is 100 % `$50Fxxxxx`, the LC II's I/O
space. Not one RAM operand appears in it. Top rows:

```
  op=24D0 R addr=$50F06060  1642326      SCSI pseudo-DMA window (V8Memory.cpp:502, scsiDma_())
  op=177C W addr=$50F10010   201894      53C80 SCSI register file (V8Memory.cpp:505)
  op=1747 W addr=$50F10010   115375
  op=169A W addr=$50F10000    96133
  op=14D0 R addr=$50F06060    90318
  op=117C W addr=$50F27C13    26725      VIA space
  op=1C80 W addr=$50F14000    18746      ASC
  op=17BC W addr=$50F04002     5514      SCC
```

TODO § B.2's question for slice 5 was "of the residual `mmuRead`/`mmuWrite`
traffic, how much is genuine I/O (which a data window MUST refuse) and how much
is interpreted instructions' RAM operands (which a window could serve)". On
this workload the residual the *generator* refuses is **entirely genuine I/O**.
The RAM-operand half a window could serve does not appear here at all, because
by construction it never reaches this census — it is served inline by the DTLB.
Sizing that half is exactly what work item B's caller-split profile is for, and
it is the one thing still missing.

### Slice 3's mechanism, named (checks #7/#8)

Same binary, both arms `fp=cfb184b6faddabec`. Counters only, no wall clock.

| cause | thunk=1 | thunk=2 | delta |
|---|---:|---:|---:|
| fill/tag MMU | 38 484 | 34 014 | −4 470 |
| **non-plain/MMIO** | **2 503 605** | **1 858 373** | **−645 232** |
| codeMask | 3 333 114 | 3 251 345 | −81 769 |
| cross-page | 40 561 | 39 405 | −1 156 |
| other guard | 1 | 1 | 0 |
| **attributed** | **5 915 765** | **5 183 138** | **−732 627** |
| static "unsupported" | 1 845 905 | 1 996 290 | +150 385 |

The whole gain is one cause. The opcodes that leave the non-plain plane are
`177C` (275 031), `1747` (185 902), `117C` (46 716), `1084` (28 858), `1740`
(19 225) — every one a **byte STORE into a device register**. That is exactly
what `exactWrites = thunks >= 2` buys: a refused store stops replaying the whole
instruction through `mmuExecuteStart` and takes `pom68kJitWrite` instead.

What does *not* move is equally informative: `24D0 R $50F06060` is 1 642 326 at
mode 1 and 1 642 333 at mode 2 — a `MOVE.L (A0),(A2)+` reading the SCSI
pseudo-DMA window, a two-access instruction with no exact-thunk form on either
backend. This is the same invariance 2026-08-29 (seventh) recorded as
"1 644 266 vs 1 644 272 — the thunk path is not in the story"; the census now
supplies the *reason* rather than the observation.

**The cost side, also named.** `backend declined` 751 327 → 919 539 (+168 212)
comes from 19 more rejected blocks, and the reject reasons say which kind:
`coverage` **42 → 63**, `context/IR` 16 → 14. With `exactWrites` on,
`memoryProofPlan` marks more stores `exactRequired`; those stop counting toward
a block's native coverage, the block falls under the acceptance threshold, and
the *whole block* is declined and runs through the window
(`JitBackendX64.cpp:5517`, `JitEngine.cpp:1477` `Miss::Rejected`).
`guard replay` 47 231 → 47 039: bounded, not the 2026-08-29 storm.

### dtlb refusal tables (checks #7/#8/#9)

```
jit_bench_lcii 6000, thunk=1:  probe=153480    pagelen=0  codepage=37233    notram=32328   cache040=0
jit_bench_lcii 6000, thunk=2:  probe=143028    pagelen=0  codepage=34653    notram=31388   cache040=0
lcii_simcity_census, thunk=1:  probe=18595994  pagelen=0  codepage=3796442  notram=151809  cache040=0
```

### The plan's slice-0 metric is defective (the coordinator's finding, confirmed here)

The plan says "`pomIcache.fetches` **is** the count of `mmuFetchWord` calls that
reached the overlay, so `fetches / retired` prices slice 4 directly". It is not:
the JIT lowering hands the counter's *address* to generated code
(`MoiraExecMMU_cpp.h:2299`, consumed per `JitIr.h:1296`), so native blocks bump
it without ever calling `mmuFetchWord`. Measured here:
**2.414 fetch-words per retired instruction at 2000 frames** (571 592 391 /
236 821 650) and **2.42 at 6000** (1 602 507 733 / 661 452 534) — a number that
prices nothing, since it is dominated by native blocks.

The metric that does price slice 4 is the interpreted-instruction fraction,
`interp_instrs + slow_instrs + window_instrs` from the bench's own JSON:
**11 113 385 / 236 821 650 = 4.69 %** at 2000 frames and
**34 471 401 / 661 452 534 = 5.21 %** at 6000.

---

## Instrument failure worth recording

The first `run_profile.sh jit` produced a perfectly well-formed profile **of
`/usr/bin/cat`**. The script exported `LD_PRELOAD`/`CPUPROFILE` for its whole
body, so the `cat /proc/loadavg` that stamps the host load was profiled too and,
exiting last, overwrote the output. Forty samples, valid header, entirely the
wrong process. The script now scopes the shim to the emulator with `env`.
This is another instance of `docs/MEASURING.md` § 3: a precise instrument
pointed at the wrong quantity.

---

## NOT RUN

- **Work item B, the caller-separated profile — NOT RUN.** `tools/profile_callers.py`
  is written (it extends `profile_census.py`'s parser to keep the whole stack
  and attributes each memory-bucket sample to the dispatch seam above it —
  `pom68kJitRead`/`pom68kJitWrite` = generator access thunk, `pom68kJitStep` =
  whole-instruction replay, `Engine::fillDtlb` = translation, `Engine::runWindow`
  / `Moira::execute` = interpreter). `build-profile` is built and the shim
  compiles. Only the corrected profiling run and the analysis remain.
- `lcii_speedometer_census` with the instrument — NOT RUN.
- `ctest -L asset-none` — NOT RUN.
- Narrow JIT gates (`jit_backend_test`, `jit_backend_parity_test`,
  `jit_lockstep_030_*`) — **NOT RUN**. `jit_backend_parity_test` matters most:
  it is the only thing on this x86-64 host that compiles *and emits* the
  modified a64 backend over the whole opcode space.
- No timing claim is made anywhere above. Host load ran between 0.2 and 39.8
  during these runs (three agents building); every number quoted is a counter
  or a fingerprint, never a second.
- Tonight's null experiments measured a 1.6–6.3 % floor against the 1.0 %
  recorded in `performance_budgets.tsv:67`. Unresolved — stale floor or busy
  host — and it bars any wall-clock claim on this host until settled.
