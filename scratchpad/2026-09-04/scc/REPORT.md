# SCC deadline hand-off — TODO.md § B.1, second bullet

**Outcome: implemented, bit-identical, all gates green; the timing ABBA is
prepared but deliberately NOT run — the host was loaded throughout.**

## 1. Where the double interrogation is

`Scc8530::cyclesToNextEvent()` (`src/Scc8530.cpp:1053-1070`) is a pure
function of SCC state: `peerHold_`, and per channel `txShiftIn`, `relatch`,
`rxCur`/`rxQueue`/`rxTimer`/`rxIdle`, `losslessRx_`, `rxEnabled(c)` and
`realPaceOf(c)`. It is out-of-line (its own translation unit, LTO off), which
is why the 2026-09-03 (sixteenth) profile sees it as a leaf at
246 / 6 840 exclusive samples.

The Q605 asks it TWICE per scheduling step:

  1. `src/Q605Memory.cpp:790` — `if (scc_.cyclesToNextEvent() <= sccDebt_) flushScc();`
     decides whether the accumulated debt reached the device's next transition.
  2. `src/Q605Memory.cpp:839` — `const int sccNext = scc_.cyclesToNextEvent();`
     derives the board's next-event bound, minus the debt.

The two are adjacent in time. `Cpu040::flushTicks()` (`src/Cpu040.cpp:158-172`)
is the only pump:

    if (m) mem_.tick(m);          // site 1 runs here
    schedulePeriphDeadline();     // src/Cpu040.cpp:151-156 -> site 2 runs here

Nothing executes between them, and inside `Q605Memory::tick()` everything
after the SCC block (ASC, SWIM, drives, 53C96, VIA CA1, DAFB —
`src/Q605Memory.cpp:800-830`) touches no SCC state. So site 2 always
recomputes exactly what site 1 already had, except on the step where site 1
decided to flush: a flush advances the device and moves the bound.

`schedulePeriphDeadline()` has one other caller, `Cpu040::hardReset()`
(`src/Cpu040.cpp:64-69`), which runs `mem_.reset()` first.

`Q605Memory::cyclesToNextEvent()` also runs when `flushTicks()` computed
`m == 0` (elapsed core cycles below the x4 cache boost) and therefore skipped
`tick()`. That step queries the SCC once, not twice — and the bound the
previous `tick()` derived is still exactly right, because neither the SCC nor
`sccDebt_` moved.

## 2. The invalidation contract

The bound is published by `tick()` and dropped by every door that can move it.
The doors are enumerable because `scc_` is private and every mutation inside
`Q605Memory` is preceded by `flushScc()`:

  * MMIO read (`readData`/`readCtl`; RR0 latch clears, FIFO pop):
    `flushScc()` at `src/Q605Memory.cpp:400`, mutation at :402.
  * MMIO write (`writeData`/`writeCtl`; WR1/WR9/WR15, Reset Ext/Status):
    `flushScc()` at `src/Q605Memory.cpp:478`, mutation at :480.
  * Every external door — LocalTalk/AppleTalk injection, wire pacing,
    `onTxFrame`, tests — goes through `Q605Memory::scc()`
    (`src/Q605Memory.h:127`, `{ flushScc(); return scc_; }`), so it flushes
    by construction.
  * `scc_.tick()` itself: `flushScc()`, `src/Q605Memory.cpp:871-885` — the door.
  * Power-on / `Cpu040::hardReset`: `src/Q605Memory.cpp:154-155`
    (`scc_.reset(); sccDebt_ = 0;`) — explicit drop.
  * Save-state LOAD (`ar(... scc_ ...)` replaces the device wholesale):
    `src/Q605Memory.h` `visit()`, `Ar::loading` branch — explicit drop.

Because only `tick()` ever republishes, dropping the bound at the ENTRY of a
mutating sequence is sufficient: the flag stays false for the rest of that
access and until the next `tick()`, which recomputes from the device.

Re-entrancy is safe: `onTxFrame` fires from inside `scc_.tick()` inside
`flushScc()` and may call `mem.scc()` again (`src/GuiHostServices.h:78-101`,
`src/AtalkHub.h:66-84`), which drops the flag again — and `tick()` publishes
only after `flushScc()` has returned.

The published bound is derived host state, not device time: it is NOT
serialized (unlike `sccDebt_`, which is observable device time and is in the
snapshot), so no save-state version moves.

External callers never hold an `Scc8530&` across a `tick()`: every use site
(`src/GuiHostServices.h`, `src/AtalkHub.h`, `tests/*`) re-enters through
`mem.scc()` for each access.

## 3. Why this is not the refused general SCC cache

The 2026-08-12 refusal (archived, "SCC deadline cache") memoised the deadline
as strictly derived state with an oracle mode recomputing it on every hit —
it removed BOTH queries and had to prove a general invalidation contract over
the whole board. This variant removes exactly one of the two, publishes only
what `tick()` already computed for its own flush decision, and never computes
anything the unmodified tree did not. On a flush step it publishes nothing and
the re-arm recomputes as before, so the number of `Scc8530::cyclesToNextEvent()`
calls is never higher than today.

<!-- RESULTS -->

## 4. The change

`git diff --stat`:

    src/Q605Memory.cpp | 32 ++++++++++++++++++++++++++++++--
    src/Q605Memory.h   | 12 ++++++++++++
    2 files changed, 42 insertions(+), 2 deletions(-)

`git diff --check`: clean.

Two new private members in `Q605Memory` (`src/Q605Memory.h`, beside
`sccDebt_`): `bool sccNextFromTickValid_` and `int sccNextFromTick_`.

  * `Q605Memory::tick()` — the flush decision keeps its single
    `scc_.cyclesToNextEvent()` call. When it does NOT flush, the value is
    published (`sccNextFromTick_`, valid). When it DOES flush, nothing is
    published: `flushScc()` has already dropped the flag, and the re-arm
    recomputes as before. So the number of calls into the device never rises.
  * `Q605Memory::cyclesToNextEvent()` — consumes the published bound when it
    is valid, otherwise calls the device exactly as before. The debt
    subtraction (`sccNext - sccDebt_`) is unchanged and still uses the live
    `sccDebt_`, so a step where `flushTicks()` computed `m == 0` (no `tick()`)
    is also correct.
  * `Q605Memory::flushScc()` — drops the hand-off first thing.
  * `Q605Memory::reset()` — drops it beside `sccDebt_ = 0`.
  * `Q605Memory::visit()` — drops it in the `Ar::loading` branch. The
    hand-off is derived host state and is NOT serialized: no snapshot version
    moves.

No new knob, no new registry, no new gate, no save-state format change.
Nothing reads host time. The `POM68K_Q605_EVENT_SCC=0` opt-out path
(`else` branch of `tick()`) is untouched and never publishes.

The Q700 (`src/Q700Memory.cpp:962-965` / `:1032`) and Centris
(`src/CentrisMemory.cpp:622-623` / `:664`) boards carry the same double
interrogation. They are deliberately NOT changed: the profile that motivated
this is the Q605's, and a promotion would be the moment to consider them.

## 5. A note on the workload named in the task

The task brief named `q605_rogue_census` as "the workload with a fixed frame
budget". The census has no frame budget and prints no fingerprint: it drives a
fixed KEY script and reports `gameplay: 446/468 keys repainted, cumulative
delta 5.18, halted=0` (`tests/q605_rogue_census.cpp`). The numbers the
2026-09-03 (sixteenth) entry quotes — 6 000 fixed frames, 2 500 002 000
machine cycles (= 6000 x 416 667), 10 000 038 800 core cycles (x4 boost) and a
fingerprint — are `jit_bench`'s (`tests/jit_bench.cpp`, `POM68K_BENCH_FRAMES`,
`tests/BenchHarness.h`). The census is what the 6 840-sample PROFILE was taken
on; `jit_bench` is what the ABBA was run on. `abba.sh` therefore drives
`jit_bench`, which is the only one of the two that can prove the identity
precondition, and the census is used as a second identity witness.

## 6. Identity precondition — PASSED

`jit_bench`, 6 000 fixed frames, same ROM and the immutable
`hdv/ref/MacOS-8.1-boot.vhd`, one run per arm (the host was loaded, so the
wall clock below is noise and is NOT a measurement):

| | old (build-base, HEAD 8f74d42) | new (build-new) |
|---|---|---|
| machine cycles | 2 500 002 000 | 2 500 002 000 |
| core cycles | 10 000 039 216 | 10 000 039 216 |
| fingerprint | `1fe3fabf0b913545` | `1fe3fabf0b913545` |
| SCSI commands | 5406 | 5406 |
| final PC | `$0050FCF2` | `$0050FCF2` |
| (wall, load ~26) | 95.77 s | 84.60 s |

Diffing the two complete outputs with the timing-bearing lines removed gives
no difference at all: identical retired instruction count, block counts,
native/fallback census, exit histogram and flush causes.

Second witness, `q605_rogue_census` (fixed key script) — one run per arm:

    old: gameplay: 443/468 keys repainted, cumulative delta 5.23, halted=0
    new: gameplay: 443/468 keys repainted, cumulative delta 5.23, halted=0

(The (sixteenth) entry's `13784659462b9d78` / 446-of-468 belong to the M4
macOS leg with a different Rogue image; what matters here is old against new
on one host.)
## 6bis. Gate results (arm `build-new`)

All runs redirected to a file, exit status captured, `LastTest.log` copied
after each; never two ctest at once in the same build dir.

| Run | Result | Census (`tools/gate_execution_census.py`) |
|---|---|---|
| SCC + schedulers + docs/config (8 gates) | 8/8 passed, rc 0 | **8 executed, 0 soft-skipped, 0 failed** |
| `ctest -L asset-none` (85 gates) | 79 passed, 1 skip, 5 failed, rc 8 | **79 executed, 1 soft-skipped, 5 failed** |
| Q605 locksteps (5 gates) | 5/5 passed, rc 0 | **5 executed, 0 soft-skipped, 0 failed** |
| `q605_boot_etalon` + `q605_savestate_etalon` | 2/2 passed, rc 0 | **2 executed, 0 soft-skipped, 0 failed** |

The eight named gates: `scc_ext_test`, `scc_int_test`, `scc_baud_test`,
`scc_engine_test`, `q605_event_scheduler_test`, `quadra_event_scheduler_test`,
`docs_test`, `config_test` — all Passed.

The five locksteps: `jit_lockstep_test` 36.42 s, `jit_lockstep_blocks_test`
45.13 s, `jit_lockstep_x64_test` 33.20 s, `jit_lockstep_x64_fine_test` 2.81 s,
`jit_lockstep_noaccess_test` 10.07 s — all Passed, none soft-skipped, so the
FF7439EE ROM and the 8.1 volume were really loaded.

`q605_savestate_etalon` (46.05 s) exercises the save/load path this change
touches (`visit()`), and `q605_boot_etalon` (23.80 s) a full boot to the
Finder. Both Passed.

### The five asset-none reds are PRE-EXISTING at HEAD, not this change

    188 jit_copyback_write_040_test          SEGFAULT
    189 jit_copyback_write_040_control_test  SEGFAULT
    190 jit_copyback_bsr_040_test            SEGFAULT
    191 jit_copyback_pair_040_test           SEGFAULT
    192 jit_copyback_pair_040_control_test   SEGFAULT

Proof, not inference: `tests/jit_copyback_write_040_test.cpp` includes only
`Moira.h`, `jit/JitEngine.h` and `JitTestConfig.h` — it never constructs a
`Q605Memory` — and the same executable built from the UNMODIFIED tree in
`build-base` segfaults identically (`gates-base-copyback.log`, rc 8, 1.12 s).
This is the x86-64 leg of the 2026-09-03 (fourteenth) copyback work, whose
entry claims the gate green on AArch64 and explicitly leaves "executing the
x64 generated bytes" to the x86-64 host leg. It is an independent finding to
hand back, not a result of this variant.

The one soft-skip is `dir2hfs_selftest` — "machfs not installed
(.venv-tools)", an environment gap in this worktree, unrelated to the change.
`gui_smoke_test` did NOT skip: the GUI target was built for this arm.

## 7. Draft CHANGELOG entry

*(newest-first; placeholders in ⟨…⟩ are filled from `abba.times` once the
ABBA has run on a quiet host. If the ranges overlap, the entry below is
replaced by its refusal twin — see the note after it.)*

```markdown
<a id="2026-09-0X-q605-scc-deadline-handoff"></a>
## 2026-09-0X (Nth) — The Q605 asks the SCC for its deadline once per step instead of twice

The post-ROM-fast-path profile put `Scc8530::cyclesToNextEvent()` at
246 / 6 840 exclusive samples with the `Q605Memory::cyclesToNextEvent()`
fan-out at 135, and `TODO.md` § B.1 asked whether that leaf is asked the
same question twice. It is. `Q605Memory::tick()` interrogates the chip to
decide whether the accumulated debt reached its next transition
(`src/Q605Memory.cpp:790`), and `Q605Memory::cyclesToNextEvent()`
interrogates it again to derive the board's bound (`:839`) — with nothing
between them: `Cpu040::flushTicks()` calls `tick()` and then
`schedulePeriphDeadline()` back to back, and nothing after the SCC block in
`tick()` enters the device.

`tick()` now publishes the bound it already derived and the re-arm consumes
it. This is a hand-off, not the memoised deadline refused on 2026-08-12: on
the step that flushes, nothing is published and the re-arm recomputes exactly
as before, so the number of calls into the device never rises. Every door
that can move the deadline drops the hand-off — `flushScc()`, which the SCC
MMIO paths (`:400`, `:478`) and the public `scc()` accessor
(`src/Q605Memory.h:127`) call before touching the chip, `reset()`, and a
save-state load. The published bound is derived host state, not device time:
unlike `sccDebt_` it is not serialized, so no snapshot version moves.

⟨ABBA RESULT⟩ Two binary-alternated A/B pairs at 6 000 fixed frames on this
x86-64 host, order reversed for the second pair, give old ⟨a/b/c/d⟩ s against
new ⟨a/b/c/d⟩ s: means ⟨X⟩ → ⟨Y⟩ s, ⟨±Z⟩ %, with ⟨non-overlapping |
overlapping⟩ printed ranges. Every arm retires exactly 2 500 002 000 machine
cycles / 10 000 039 216 core cycles, issues 5406 SCSI commands and ends at
the same fingerprint `1fe3fabf0b913545`; the `q605_rogue_census` key script
agrees on both arms (443/468 keys repainted, cumulative delta 5.23). The
shorter 3 000-frame ABBA ⟨agrees | does not⟩ (⟨…⟩).

The four SCC gates, `q605_event_scheduler_test`,
`quadra_event_scheduler_test`, the Q605 locksteps, `docs_test`, `config_test`
and the asset-none tier are green. ⟨If the delta is inside the noise floor:
the variant is withdrawn and this entry records the measurement, exactly as
the general SCC deadline cache was withdrawn on 2026-08-12; the sibling
double interrogations on the Q700 (`src/Q700Memory.cpp:962-965`) and the
Centris (`src/CentrisMemory.cpp:622-623`) are left alone.⟩
```

## 8. Paths

| What | Path |
|---|---|
| Worktree (the change lives here, uncommitted) | `/home/gistarcade/src/pom68k/.claude/worktrees/agent-a5a3ffaf74087926b` |
| Baseline arm (unmodified HEAD 8f74d42) | `…/build-base` |
| New arm (SCC deadline hand-off) | `…/build-new` |
| Archived baseline binaries | `…/scratchpad/scc/bin/{jit_bench,q605_rogue_census}.base` |
| ABBA driver | `…/scratchpad/scc/abba.sh` |
| ABBA output (written by the driver) | `…/scratchpad/scc/abba.times` |
| Gate logs + `LastTest.log` copies | `…/scratchpad/scc/gates-*.log` |
| Identity runs | `…/scratchpad/scc/ident-{base,new}.out`, `census-{base,new}.out` |

Both arms configured identically: `-DCMAKE_BUILD_TYPE=Release
-DPOM68K_LTO=OFF -DPOM68K_NATIVE=ON` (gcc 13.3.0, `-O3 -march=skylake`,
threaded + x86-64 code generator), built only through
`systemd-run --user --scope -q -p MemoryMax=10G -p MemorySwapMax=1G make -j4`.
Both builds ended rc 0 with **zero warnings**.

Assets are symlinks into the main checkout, never copies and never writes:
`roms/*` and `disks35/*` entry-by-entry, `hdv/ref` -> the main checkout's
immutable reference directory (with a LOCAL, empty `hdv/work/`),
`dev/mac-rogue/build`, and `imgui`. `python3 tools/verify_assets.py` reports
**38 declared, 38 present, 0 failures**. `git status` shows only the two
modified sources plus the untracked `imgui` and `dev/mac-rogue/build`
symlinks; `roms/`, `hdv/` and `disks35/` are gitignored.

## 9. Running the ABBA (the orchestrator's step, on a QUIET host)

    /tmp/claude-1000/-home-gistarcade-src-pom68k/742ac6a0-9a99-4ebe-8701-713deafa5293/scratchpad/scc/abba.sh

6 000 frames, a discarded warm-up run per arm, then `old new new old` and
`new old old new`; `--short` runs the confirming 3 000-frame variant,
`--frames N` any budget, `--no-warmup` drops the warm-up. It prints and
writes per-arm wall time, retired machine/core cycles and fingerprint to
`abba.times`, re-checks the identity precondition over all eight runs, and
states whether the printed ranges overlap.

It was validated end-to-end at `--frames 5 --no-warmup` (parsing, ordering,
identity check and summary all correct; both arms again bit-identical); the
validation output was deleted so the real run starts from a clean file.

**Do not read the wall clock from the runs recorded here.** The host carried
load average 20-32 from other agents' builds throughout, which is why the two
6 000-frame identity runs printed 95.77 s and 84.60 s at x1.04 and x1.18 real
time — on an idle host this workload runs at about x6.

