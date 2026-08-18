# Measuring POM68K — how a number becomes quotable

Every performance claim in this tree is a comparison, and a comparison is only
worth the variables it holds still. This file is the protocol: what a
measurement must carry before it may be written into `CHANGELOG.md`,
`TODO.md` or a design doc, and which of those rules is enforced by a tool
rather than by discipline.

It exists because on **2026-08-18** one number — "`threaded` is worth +18 % on
a 68030" — was published, retracted to "1-3 %", and re-measured at −9.6 %,
all within one session and all from the same tree. None of the three came
from a bug. All three came from a protocol that was not written down.

**This file is about wall clock only.** Architectural conformance has its own
and much older instrument: an identical state fingerprint. Nothing here
replaces it, and a timing claim whose fingerprint moved is not a timing claim
at all — it is a different emulator running a different program.

---

## 1. The five rules

### R1 — One variable, counterbalanced, at least three times

Two invocations minutes apart are not an A/B. The machine is not the same
machine: a build, another session, or a thermal state moves wall clock by
more than most of the effects being chased.

> **What it cost.** The `+18 %` came from comparing an interpreter run with
> the CACR flush **armed** against a `threaded` run with it **disarmed**. The
> engine was credited with a saving that belonged to the flush. Interleaved
> on one binary, the same comparison is −7.6 %.

So both arms run **in one process**, from a fresh machine each repeat, and
the harness is the only thing allowed to print a delta (`bench::compare`,
`POM68K_BENCH_COMPARE=N`). Three things it does that a hand-run alternation
does not:

* **ABBA, not ABAB.** Alternating `A B A B` still gives every B slot a later
  position than the A it is paired with, so a host that drifts one way — a
  thermal ramp, a job winding down, a cache filling — lands its entire drift
  on one arm and it reads as an effect. `A B / B A / A B` pairs each arm once
  early and once late and cancels first-order drift. It is free.
* **A discarded warm-up pair.** The first run of anything pays a cold page
  cache for the ROM, the disk image and the code. Counting it charges that
  once, to whichever arm went first. `POM68K_BENCH_WARMUP=0` to skip, at that
  price.
* **The result carries its own conditions.** Not "name every knob in the
  write-up" — a rule addressed to a writer is a rule the write-up forgets.
  Every `POM68K_*` variable of the process, the binary's compile timestamp,
  the host load average before and after, and the architectural fingerprint
  are printed *with the number*, so a pasted result cannot hide the knob that
  differed.

### R2 — Below the noise floor there is no claim, in either direction

The floor is a property of the host, so it is **measured, not assumed** — by
a null experiment: the reference arm against itself, `POM68K_BENCH_NULL=1`.
Nothing differs between the arms, so whatever the harness reports is the
harness. It is recorded per host in `performance_budgets.tsv`
(`host_wallclock/any/<host>/noise_floor_permille`) and reaches the bench as a
compile-time constant, like every other reviewed budget in this tree.

> **The first floor in this file was wrong by 6×, and wrong in the expensive
> direction.** It read: three interpreter runs spread 1.4 %, so nothing under
> ~3 % is a claim. Those were three *separate invocations* — they priced
> process start-up, page cache and the afternoon. Measured properly, on the
> same x86-64 host, three null experiments give worst arm spreads of **0.3 %,
> 1.0 % and 0.5 %** with deltas of −0.0 / +0.0 / +0.1 %. A 3 % rule on a host
> that resolves 1 % does not protect the tree from noise; it protects noise
> from being contradicted, and it silently buries every real 1-2 % effect. A
> floor is a measurement, not a safety margin — the harness now says
> `POLICY TOO LOOSE` when the recorded one is more than twice the measured.

The bar the harness applies is the **widest evidence of noise available**:
each arm's own spread *and* the recorded host floor, never the narrowest of
them. Taking only the reference arm's spread is how a `POM68K_JIT_ARM_BACKOFF`
"2.9 % win" survived its first re-measurement — the arm that moved was B.

And the verdict is **two-sided**. A regression inside the floor is exactly as
unreal as a win inside it, and it is the more expensive mistake: a phantom
win wastes a paragraph, a phantom regression reverts a good change.

### R3 — Two budgets, and if the gain moves, the trend is the answer

A fixed-cycle budget is the right stopwatch (`POM68K_JIT.md` § 3.3: a boot
etalon stops when it recognises the Finder, so two engines get timed over
different work). But the **dual is just as true and was not written down**: a
budget that ends inside the boot is a poor stopwatch for anything that
caches, because boot is single-pass code where a window, a block cache or a
branch predictor amortises nothing.

The same change, same binary, measured under § 1's protocol (ABBA, one
warm-up pair discarded, 3 repeats per arm, fingerprint identical within each
budget), 2026-08-18, on a QUIET host — the `HOST BUSY` refusal silent on
every run:

| `POM68K_BENCH_FRAMES` | interpreter | `threaded` | delta | arm spreads |
|---|---|---|---|---|
| 1200 | 10.35 s | 9.95 s | −3.9 % | 0.1 / 0.2 % |
| 2000 | 17.19 s | 15.79 s | −8.1 % | 2.9 / 2.7 % |
| 3000 | 24.78 s | 22.13 s | **−10.7 %** | 0.5 / 0.5 % |

Three defensible numbers for one change. Two earlier same-day tables of the
same comparison priced the protocol instead: ABAB with no warm-up read
−1.3 / −7.6 / −9.6 % (up to 2.6 points off), and a first ABBA pass taken
while a concurrent compile could not be ruled out read −3.7 / −8.3 / −10.5 —
within 0.2 points of the quiet numbers, which is what the load stamps
suggested and only the re-run could prove.

Quote the trend, say which budget each point came from, and remember that a
user's session is minutes long — so the largest budget measured is a
**floor**, not a result.

### R4 — Separate the measurement from the recommendation

The measurement goes into the document. The recommendation waits for the
next measurement. On 2026-08-18 a fresh number and an immediate
recommendation were paired three times and were wrong three times —
including the recommendation that *retracted* the previous one. Confidence
tracks recency of the number, not its quality, and nothing about a fresh
measurement makes the inference from it any safer.

### R5 — A guard nobody has watched say both yes and no is not an instrument

Written the day `tools/check_binaries_fresh.py` was born, because it spent
its first hours answering **STALE to everything**.

Its question was `make -q <target>`, asked of the build directory's top-level
Makefile. Every CMake convenience rule there reads
`adbline_test: cmake_check_build_system`, and that prerequisite is *phony* —
never up to date, by construction. So `make -q` answered "would rebuild" for
every target in the tree, freshly linked ones included: three gates checked,
three reported stale, all three current. The real dependency graph lives one
level down, in `CMakeFiles/<t>.dir/build.make` (target
`CMakeFiles/<t>.dir/build`), depfiles and all. Asked *there*, the same tool's
first honest run found **58 gate executables in this tree older than
`libpom68k_core.a`** — the exact phantom-pass condition `CLAUDE.md` says was
once quoted as 143/143.

A guard that cannot say "yes" is worse than no guard: the one it cries wolf
at is its reader, who stops running it inside a day. So every guard here
ships with a way to watch it fail on the real tree, and
`check_binaries_fresh.py --self-test` is the pattern — `make -W` (what-if)
makes one prerequisite artificially new and the verdict must flip, without a
single file being touched.

---

## 2. What the tools must do, so the rules are not left to discipline

| Guard | Enforces | State |
|---|---|---|
| architectural fingerprint printed beside every bench result | conformance | **in place** (`tests/BenchHarness.h`) |
| `POM68K_BENCH_COMPARE=N` — both arms in ONE process, **ABBA**, N repeats (floor 3), a discarded warm-up pair, median + spread, and a refusal to print a delta if the fingerprint moved | R1 | **in place** (`bench::compare`, wired in both `jit_bench` and `jit_bench_lcii`) |
| result stamped with its own conditions: every `POM68K_*` of the process, build timestamp, host load before/after | R1 | **in place** (`bench::envStamp`, `loadStamp`, `buildStamp`) |
| `POM68K_BENCH_NULL=1` — the null experiment that measures the host floor instead of assuming it, and says so when policy is too tight *or* too loose | R2 | **in place** (`bench::compare`) |
| noise floor per host, reviewed and compiled in | R2 | **in place** (`performance_budgets.tsv`, `POM68K_BENCH_NOISE_FLOOR_PERMILLE`) |
| two-sided `NOT A CLAIM` against the widest of (arm A spread, arm B spread, host floor) | R2 | **in place** (`bench::compare`) |
| busy-host refusal: 1-min load over ¼ of the hardware threads at either end → `HOST BUSY`, exit 1, nothing quotable — a load *stamp* someone must notice is not a guard, and it was the reader, not the tool, who caught a concurrent compile on 2026-08-18 | R1, R2 | **in place** (`bench::compare`) |
| pre-tier freshness assertion: every gate binary exists **and** `make` considers it up to date, asked of the rule that carries the dependencies | staleness | **in place** (`tools/check_binaries_fresh.py`) |
| that guard proving both verdicts on the real tree, mutating nothing | R5 | **in place** (`--self-test`) |
| second budget measured automatically alongside the first | R3 | *proposed* — today it is two invocations and a promise |
| a budget-manifest edit forces reconfigure + recompile of its consumers | staleness | **in place** (`CMAKE_CONFIGURE_DEPENDS` on both TSVs — `file(STRINGS)` alone registers nothing, found when a bench printed the OLD floor beside a build stamp minutes older than the TSV) |

The comparison prints `NOT A CLAIM` itself when the delta lands inside the
floor, so R2 is enforced by the tool rather than remembered by the reader.

**It works, and the first thing it did was kill a change of mine.** A
single-run sweep of `POM68K_JIT_ARM_BACKOFF` read 32 → 16.04 s, 8 → 15.65 s,
2 → 15.57 s, 0 → 15.64 s: a tidy curve with a minimum at 2, and a 2.9 %
"win" that was written into the default. Interleaved, three repeats per arm:
32 medians **15.61 s** (15.36-15.75), 2 medians **15.95 s** (15.47-16.01) —
both spreads wider than the gap, and the sign reversed. A sweep of single
runs always finds a minimum somewhere; that is what a noise floor looks like
when you plot it. The default went back to 32.

**Why the binary must identify itself.** `CLAUDE.md` states the rule already —
*a green `ctest` is only worth the freshness of its binaries* — and gives the
phantom **pass**: a run over binaries older than the library they link, which
proved nothing and got quoted. Its mirror image arrived on 2026-08-18: a full
build was killed at 90 % to free the machine, `ctest` then reported four
gates "failed", and all four were **missing executables**. A phantom failure
costs a diagnosis; a phantom pass costs a release. One assertion kills both,
and it is cheaper than either.

Separately, a `cd` that failed inside an `&&` chain left a bench binary
unrelinked, and its report — `engine=interp` where `engine=jit` was expected —
was nearly read as "the change does not work". A measurement taken from the
wrong binary must be **visible in its own output**, never inferred from the
absence of a build line in a log.

---

## 3. The trap that is not about time at all

An instrument can be perfectly precise about the wrong quantity. Four from
one session, kept because the shape recurs:

* **A view is not a total.** The 68020 indexed modes were carried in
  `POM68K_JIT.md` § 3.5 as "~167 k of block fallbacks" for two days. The
  figure was read off a **top-60 opcode list**, and an addressing mode
  fragments across hundreds of opcodes, each far below any top-N cut. The
  all-opcode rollup for the same workload says **11.2 M**, 67× more. Nothing
  was wrong with the measurement; it answered "which opcodes are hottest",
  not "how much does this mode cost". *Whenever an aggregate comes from a
  truncated view, say what it truncates.*

* **A proxy that usually correlates can anti-correlate.** A harness declared
  a census run **INVALID** — the game visibly played, monster killed, XP 002 —
  because it scored cumulative screen change per round, and the move list was
  a closed walk: the player ended each round where it started. Per round,
  13/36. Per key, 441/468. `tests/FolderProbe.h` already states the general
  form — *the signal is not the biggest count, it is the count that changes* —
  and it has now arrived from a third direction.

* **An instrument wired on one backend reports success on the backend that is
  not looking** (2026-08-10, the fallback census that printed `0` on x86-64
  for as long as it existed). Corollary adopted 2026-08-18: when the question
  is backend-independent, wire the instrument in the **engine**, not in each
  code generator — `Engine::recordIndexForms` is tallied once and both
  backends report it.

* **A calibration that reads its own policy can only ever confirm it.** The
  first null experiment folded the *recorded* floor into the floor it
  reported, so a host that had just demonstrated 0.3 % was told to record
  3.0 % — the number already in the file. A measurement that takes policy as
  an input is not a measurement, and this one would have frozen a 6× error in
  place for as long as anyone kept running it. Calibration reads the run and
  nothing else; comparing it to policy is a **separate** sentence.

---

## 4. Working protocol

1. **Measure the instrument before the change.** A null experiment costs one
   coffee and prices everything you will do next. `POM68K_BENCH_NULL=1`.
1bis. **The machine must be yours alone, and the tool now checks.** A
   measurement taken while anything else compiles is provisional whatever its
   spread says — the 2026-08-18 A/B campaign was requoted as provisional for
   exactly this. The bench refuses its verdict when the 1-minute load
   exceeds ¼ of the hardware threads at either end of the run.
2. **One lever, one proof, one commit.** Three changes were chained ahead of
   the first one's proof on 2026-08-18; the resulting confusion was not about
   the changes but about which binary carried which.
3. **Never edit sources with `make` or `ctest` in flight.** Run performance
   experiments in a separate git worktree so the build directory is not a
   moving target.
4. **Absolute paths, no `cd` in a compound command.** See § 2.
5. **Wait on a signal, not on a poll.** Repeatedly interrogating a background
   run changes nothing about when it finishes and distorts your own sense of
   how long it has been going — a `ctest` believed stuck had been running
   fifteen minutes.
6. **Name the tier.** A CPU-family label (`m040`, `m030`) exists so that
   proving an engine-policy change is one command instead of a hand-written
   alternation of forty gate names. The cost of the proof is a real reason
   changes do not land: a 68030 default measured on 2026-08-10 was still
   unflipped a week later, and the doubt was never the expensive part.
