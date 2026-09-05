# B.2 slice 5 — running progress log

Worktree: `/home/gistarcade/src/pom68k/.claude/worktrees/agent-af30bba2444f67915`
Build:    `<worktree>/build` (Release, LTO=ON, NATIVE=ON, FAST_LINK=ON, TESTS=ON, WARNINGS=ON)
Base:     `/home/gistarcade/src/pom68k/build` — unmodified HEAD 346f084
Host:     x86_64, loaded (three agents building). **No timing is quoted anywhere here.**

Assets reached from the worktree through symlinks: `roms/*` and `hdv/*` point at
the shared checkout's copies (both paths are `.gitignore`d, so the diff is clean).
`ScsiDisk::open(path, writeBack=false)` — the images are read into memory, never
written, so the runs are deterministic and safe alongside the other agents.

---

## 1. Build — DONE, clean

    cmake -S <worktree> -B <worktree>/build -DCMAKE_BUILD_TYPE=Release \
      -DPOM68K_LTO=ON -DPOM68K_NATIVE=ON -DPOM68K_FAST_LINK=ON \
      -DPOM68K_TESTS=ON -DPOM68K_WARNINGS=ON
    systemd-run --user --scope -p MemoryMax=11G --quiet \
      make -C <worktree>/build -j3 jit_bench_lcii jit_lockstep_030_test

Verdict: **PASS** — no warnings, no errors.

## 2. Knob-OFF identity vs unmodified HEAD, and knob-ON conformance — DONE

    scratchpad/2026-09-04/b2s5/run_identity.sh     (per-run logs in logs/)

18 runs: {HEAD binary, mine knob-off, mine knob-on} x {interp, jit+threaded,
jit+x64} x {2000, 6000 frames}. Full table in `identity_table.txt`.

Verdict: **PASS.**
* 2000 frames — every one of the nine runs: `fp=3de5c5ab62b4eca8`,
  `icache: 571592391 fetches, 443637308 hits, 127950916 misses`.
* 6000 frames — every one of the nine runs: `fp=cfb184b6faddabec`,
  `icache: 1602507733 fetches, 1093456393 hits, 509047173 misses`.

So knob OFF is byte-identical to HEAD *and* knob ON is byte-identical to both,
on all three engines at both budgets.

## 3. The knob is no longer a dead path on the 68030 — DONE

`Moira::pomJitData030Hits` / `pomJitData030Refusals`, printed by
`jit_bench_lcii` (and by `jit_lockstep_030_test`). Counters are **accesses**,
not instructions. Knob off is 0/0 on every arm — the branch is genuinely dead
when the knob is off.

| budget | engine | window hits | window refusals |
|---|---|---|---|
| 2000 | interp | 87 960 249 | 13 623 614 |
| 2000 | threaded | 87 962 700 | 13 621 163 |
| 2000 | x64 | 12 885 356 | 14 202 572 |
| 6000 | interp | 210 352 943 | 13 800 817 |
| 6000 | threaded | 210 356 575 | 13 797 185 |
| 6000 | x64 | 47 172 553 | 15 645 046 |

Population it comes out of (orchestrator's numbers off the fresh HEAD build,
x64 / BLOCKS=1 / HOT=1): instructions that pass through the interpreter and
therefore through `mmuRead`/`mmuWrite` are
`interp_instrs + slow_instrs + window_instrs`
= 27 637 880 + 6 082 194 + 751 327 = **34 471 401 of 661 452 534 retired
(5.21 %)** at 6000 frames, and **11 113 385 of 236 821 650 (4.69 %)** at 2000.
The x64 hit counts (47.2 M / 12.9 M accesses) are ~1.4 and ~1.2 accesses per
such instruction, which is the shape to expect and bounds the whole slice at
about 5 % of the run whatever the A/B later says.

## 4. 030 lockstep gates, knob ON, fresh seeds — see LOCKSTEP.md

## 5. `ctest -L asset-none`, knob off and knob on — see CTEST.md

---

### 4. 030 lockstep gates, knob ON, fresh seeds — DONE, **PASS** (see `LOCKSTEP.md`)

    scratchpad/2026-09-04/b2s5/run_lockstep.sh     (per-run logs in logs/)

All five registered 68030 lockstep gates (`jit_lockstep_030_test`,
`_blocks_test`, `_x64_experimental_test`, `_x64_packed_ccr_test`,
`_x64_alignment_test`), each `jit_lockstep_030_test 120000`, four rounds:

| round | knob | seed | extra | result |
|---|---|---|---|---|
| A | ON | BUDGET=6144 FINE_AT=73331 | — | 5/5 `120000 steps identical` |
| B | ON | BUDGET=12288 FINE_AT=95003 | `FULL_RAM_AT=119000` (store side, whole 10 MB) | 5/5 `120000 steps identical` |
| C | ON | BUDGET=6144 FINE_AT=73331 | `ICTRACE=1` (fetch side) | 5/5 identical, **0 `[ictrace]` lines on every gate** |
| D | OFF | BUDGET=6144 FINE_AT=73331 | control | 5/5 identical, `0 hits / 0 refusals` |

20/20 green, rc=0 everywhere. Both seeds are off the registered
8192 / 110000 cadence. `vramDiff` runs at every boundary in all rounds (the
gate does that itself since B.2 slice 1), so the framebuffer store side is
covered throughout.

Round D is the proof that the knob-off branch is genuinely dead: both CPUs
report `0 hits / 0 refusals`.

### 5. `ctest -L asset-none` twice — DONE, **PASS after the docs_test repair** (see `CTEST.md`)

Both legs (`POM68K_DATA_WINDOW=0`, then `=1`, sequentially, same build dir,
never piped) failed on **`docs_test` only** — the four self-asserted headline
numbers in `extern/moira/POM68K_VENDOR.md` that `docs_test` § 7 recomputes.
Three of them moved with this slice (identifiers 89->92, marked lines
398->406, patch groups 32->33); recomputed and written in.
`ctest -R '^docs_test$'` then rc=0. Every other gate in the tier passed in
both legs.

### 6. Post-rebuild identity re-check — DONE, **PASS**

After the final full rebuild from the finished tree: nine arms at 150 frames
(HEAD / knob-off / knob-on × interp / threaded / x64) all
`fp=0685879ca2d506fd` with the identical icache triple; window dead with the
knob off, live with it on.
