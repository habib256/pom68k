# B.2 slice 4 — fused 68030 `ird`/`irc` fetch: progress log

Worktree : `/home/gistarcade/src/pom68k/.claude/worktrees/agent-a41945e984124a4b6`
Build    : `<worktree>/build` — `Release, POM68K_LTO=ON, POM68K_NATIVE=ON, POM68K_FAST_LINK=ON, POM68K_TESTS=ON, POM68K_WARNINGS=ON`
Bench    : `<worktree>/build/jit_bench_lcii`
Base     : `/home/gistarcade/src/pom68k/build/jit_bench_lcii` (unmodified HEAD 346f084) — this is the binary mine must be compared against
Host     : i7-10700F, 16 threads, x86_64. All logs in this directory.

Assets are symlinked into the worktree (`link_assets.sh`) so the asset-required
gates EXECUTE instead of soft-skipping: `roms/{128KB,256KB,512KB,1MB} ROMs`
and `hdv` → the main tree. Both are `.gitignore`d; `hdv` shows as `?? hdv`
because the ignore rule is `hdv/` and a symlink is not a directory. **Delete
both symlinks before integrating.**

## The change (written, built, proved)

`extern/moira/Moira/Moira.h` + `extern/moira/Moira/MoiraExecMMU_cpp.h`, 82 diff
lines. `mmuExecuteStart` serves `ird` and `irc` from ONE `pomJitFetch(pc, 4)`
with one pipe test and one in-flight stamp at `pc+2`; the i-cache overlay moved
into `Moira::pomIcacheFetch<Words>`, the single body `mmuFetchWord` now calls
with `Words = 1` and the fused path with `Words = 2`. Refused when the
pre-switch pipe is live (`!pomMmuPipeLive()`, vendor row 32), when
`State::CHECK_WP` is set, or when the window does not cover all four bytes.

## Checks actually run

| # | Check | Command | Verdict |
|---|---|---|---|
| 1 | Gate ratio at HEAD, x64, 2000 + 6000 frames | `POM68K_BENCH_FRAMES={2000,6000} POM68K_CPU_ENGINE=jit POM68K_JIT_BACKEND=x64 POM68K_JIT_BLOCKS=1 POM68K_JIT_HOT=1 build/jit_bench_lcii` → `gate-ratio-head.txt` | `icache fetches/retired` = 2.414 / 2.423 — **the plan's gate metric is wrong under the JIT** (see FINDINGS). Interpreted share 4.69 % / 5.21 % |
| 2 | Same at `POM68K_JIT_ACCESS_THUNK=2` (slice-3 preview) | `thunk2-6000.txt` vs `thunk1-6000-recheck.txt` | interpreted share 4.95 % vs 5.03 % — slice 3 removes 1.6 % of the population, does **not** collapse the slice-4 ratio |
| 3 | Build, capped | `systemd-run --user --scope -p MemoryMax=11G --quiet make -C <build> -j3 jit_lockstep_030_test jit_bench_lcii` | clean, 0 warnings on the touched files |
| 4 | Full capped build, all targets | same, no target list → `build-all.log` | exit 0; 0 errors; the only 2 warnings are pre-existing `-Wstringop-overflow` in `src/SaveState.h:192`, untouched by this slice |
| 5 | Codegen check | `objdump -d` on `mmuExecuteStart` | fused path emitted inline: pipe test → `CHECK_WP` → window `armed`/`len-4`/`gen`/`super` — the `lea -0x4(%rcx)` confirms the 4-byte bound |
| 6 | **Fingerprint + `icache:` triple, HEAD vs SLICE, interp / threaded / x64, 2000 + 6000 frames** | `run_fp_table.sh` → `fingerprint-icache-table.txt` | **12/12 identical.** 2000: `fp=3de5c5ab62b4eca8`, icache `571592391 / 443637308 / 127950916`, retired `236821650`. 6000: `fp=cfb184b6faddabec`, icache `1602507733 / 1093456393 / 509047173`, retired `661452534`. Blocks compiled/run, SCSI count and final pc identical too |
| 7 | **ICTRACE over a full 120 000-comparison run** | `run_ictrace.sh` (x64, BLOCKS=1 HOT=1, BUDGET=8192, FINE_AT=110000, `POM68K_JIT_LOCKSTEP_ICTRACE=1`) → `ictrace-x64-120k.log` | **0 `[ictrace]` lines**, `OK — 120000 steps identical`, `icache 948544659 / 681239356 / 267301136 (identical on both)`, and `arm refusals: … pipe 1` proves the row-32 path was exercised |
| 8 | The 5 registered 030 lockstep gates | `ctest --test-dir build -R jit_lockstep_030 --output-on-failure` → `ctest-lockstep030.log` | 5/5 **passed and executed** (44.0 / 63.1 / 44.8 / 47.0 / 45.1 s — not soft-skips) |
| 9 | Fresh seeds ×5 | `run_fresh_seeds.sh` → `seed-*.log` | 5/5 identical, 0 `[ictrace]` lines each: `x64 BUDGET=4093 FINE_AT=57000` (120k), `x64 BUDGET=1021` no FINE_AT (120k), `x64 BUDGET=260480` real-frame cadence (6000), alignment knobs `RESTART_BASE=1 BSRW=1` at `BUDGET=260480` (6000), `threaded BUDGET=3079 FINE_AT=91000` (120k) |
| 10 | `POM68K_VENDOR.md` row 33 | Edit | written, plus the four `docs_test` § 7 headline numbers recomputed: identifiers 89→**90**, marked lines 398→**402**, patch groups 32→**33**, marked files unchanged at 13 of 25 (`vendor_numbers.py` reproduces docs_test's own semantics) |

Nothing has crashed. No run has been re-run silently.

## NOT RUN yet

- [ ] `ctest --test-dir <build> -L asset-none --output-on-failure` — **launched
      detached, in flight** (`run_assetnone.sh`, pid 566064) → `ctest-asset-none.log`.
      Treat as NOT RUN until that log shows a pass line and `ctest exit=0`.
- [ ] `docs_test` specifically, to confirm the four recomputed vendor numbers.
      It is inside `-L asset-none`, so the item above covers it.
- [ ] `git diff --check`.
- [ ] `FINDINGS.md`.
- [ ] Any A/B or wall-clock measurement — **deliberately not run**, per brief.

## Gate arithmetic (the verdict the plan asked for)

Instructions passing through `mmuExecuteStart` = `interp_instrs + slow_instrs +
window_instrs`, from the bench's own JSON on unmodified HEAD:

* 6000 frames: 27 637 880 + 6 082 194 + 751 327 = **34 471 401** of 661 452 534
  retired (**5.21 %**), wall 15.92 s → 24.1 ns per retired instruction.
* 2000 frames: 6 873 347 + 3 511 664 + 728 374 = **11 113 385** of 236 821 650
  (**4.69 %**), wall 5.98 s.

Each of those pays two `mmuFetchWord` calls; the fusion collapses them to one
window probe. Counting the removed x86 instructions in the HEAD binary's
disassembly of `mmuFetchWord` (≈78 on the window-hit path) the fold removes,
per interpreted instruction: the prologue/epilogue and call (~28), the pipe
test (3), the `fc` recompute (5), the four-store stamp (5) and the
`pomJitFetch` body (~15) ≈ **56 x86 instructions**, keeping both overlay
bodies. At 15–25 cycles on this 4.6 GHz core that is **3.3–5.4 ns** per
interpreted instruction.

| arm | instructions through `mmuExecuteStart` | ceiling at 3.3 ns | at 5.4 ns |
|---|---|---|---|
| x64 native, 6000 f | 34 471 401 / 15.92 s | 0.72 % | 1.17 % |
| x64 native, 2000 f | 11 113 385 / 5.98 s | 0.61 % | 1.00 % |
| **threaded, 6000 f** | **661 452 534** / 52.12 s | **4.19 %** | **6.85 %** |
| interpreter, 6000 f | 661 452 534 / 49.63 s | 4.40 % | 7.19 % |

The two bottom rows are not a footnote: `X64Backend::caps()` sets
`autoFamilies = kGuest68040` only (`JitBackendX64.cpp:5128`), so **on this
x86-64 host a 68030 guest's automatic backend is `threaded`**, and
`JitBackendThreaded.cpp:83` runs *every* instruction through
`pomJitExecOne()` → `mmuExecuteStart`. The brief's gate arm (`BACKEND=x64`) is
a diagnostic override, and it is the one arm where the slice is worth least.

Floors to weigh it against: recorded policy 10 permille (x86_64,
`performance_budgets.tsv:67`); the coordinator's null experiments tonight
measured 1.6 %–6.3 % on this host. So: **under the floor on x64-native, above
it on `threaded` and on the interpreter** — which is where the LC II actually
runs on x86-64 and where the whole `-L m030` etalon tier runs. Conformance is
settled by items 6–9; the default question is left open for the orchestrator's
quiet-host A/B.
