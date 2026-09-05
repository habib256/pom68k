# TODO § B.2 slice 3 / § B.3 clause 1 — the x64 `maxAccessThunk030` clamp, priced

*2026-09-04/05, x86-64 host `gistarcadePC` (16 threads, 39 GB), binary
`build/jit_bench_lcii` rebuilt at `346f084`. Protocol `docs/MEASURING.md`
§ 1 R1/R2/R3 via `abba_knob.sh` — one binary, two values of an explicit-wins
env knob, so nothing but the knob differs between the arms.*

## What was asked

`JitBackendX64.cpp` clamps `caps().maxAccessThunk030 = 1`; `JitEngine.cpp:99`
lets `POM68K_JIT_ACCESS_THUNK` override it explicitly. The plan's slice 3 and
TODO § B.3's first clause both ask the same question: does mode 2 — where
`memStore`'s DTLB miss takes `pom68kJitWrite` (one access) instead of
`runtimeStub` (a full instruction replay through `mmuExecuteStart` + handler)
— earn the default?

## Answer: no, and the counters say why before the stopwatch does

Mode 2 is **conformant** on this workload. Fingerprints are identical in both
arms at both budgets (`cfb184b6faddabec` at 6000 frames, `3de5c5ab62b4eca8` at
2000), and so are the three `PomIcache` counters — the 030 half of the
fingerprint — to the digit.

Mode 2 also does exactly what it promises. Deterministic counters, mode 1 →
mode 2:

| counter | 6000 frames | 2000 frames |
|---|---|---|
| retired | 661 452 534 → *unchanged* | 236 821 650 → *unchanged* |
| native | 626 845 746 → 627 444 008 | 225 574 854 → 225 966 681 |
| block fallback | 6 082 194 → 5 558 149 (−8.6 %) | 3 511 664 → 3 013 639 (−14.2 %) |
| window/interp | 28 389 207 → 28 315 097 | 7 601 721 → 7 707 949 |
| backend declined | 751 327 → 919 539 (+168 212) | 728 374 → 896 656 (+168 282) |
| arm backoff | 1 180 704 → 951 488 | 673 600 → 620 512 |
| window refused | 36 897 → 29 734 | 21 050 → 19 391 |
| guard replay | 47 231 → 47 039 | 31 189 → 31 002 |
| blocks compiled / run | 23 794 / 14 252 458 → 23 705 / 14 190 839 | 23 515 / 3 894 057 → 23 455 / 3 843 096 |

Two things to read out of that table.

**The prize is 0.09 of a percentage point.** Mode 2 converts about half a
million block fallbacks into native instructions — a real 8.6 % of that bucket
at 6000 frames, 14.2 % at 2000. But the bucket it empties is **0.9 % of
retired instructions**, so the whole conversion moves the native share from
94.8 % to 94.9 %. This is the same shape as B.2 slices 1 and 2, which retired
26.9 % of a SimCity session's interpreted replays for no measurable wall
change: a large fraction of a small bucket.

**`Miss::GuardReplay` is bounded, and the 2026-08-29 storm really is closed.**
47 231 → 47 039 at 6000 frames and 31 189 → 31 002 at 2000: mode 2's guard
replays do not grow with the budget in the way the >600 s wedge did. The
comment at `JitBackendX64.cpp` (the `maxAccessThunk030 = 1` site) still says
"the mechanism of the mode-2 storm is NOT yet run to ground". That was true
when it was written and is now stale — 2026-08-29 (late night) ran it to
ground and repaired it with one interpreted step counted as
`Miss::GuardReplay`. The comment should be corrected whether or not the
default moves.

**The gain and the cost have the same cause.** The x64 runtime-reason census
(wired for the first time by B.2 slice 0 the same night — the x64 backend had
never filled `slowRuntimeReasonHisto`, so this question was unanswerable on
this host until then) names both halves. Raw:
`../b2s0/thunk_1v2_causes.md`, `../b2s0/histo_6000_thunk{1,2}.log`.

The whole −524 045 is **one cause**: `non-plain/MMIO`, 2 503 605 → 1 858 373,
**−645 232 (−25.8 %)**, and the per-address table names the opcodes that leave
the plane — `177C`, `1747`, `117C`, `1084`, `1740`, 275 031 / 185 902 /
46 716 / 28 858 / 19 225 in mode 1 and **gone** in mode 2. Every one is a byte
**store into a device register**: `$50F10000+` is the 53C80 SCSI register
file, `$50F26`/`$50F27` the VIA space. That is exactly what `exactWrites`
buys — a store the DTLB refuses stops replaying the whole instruction through
`mmuExecuteStart` and takes `pom68kJitWrite` instead.

And the +168 212 `backend declined` — budget-independent, therefore a
compile-time effect — is the **same lever's cost**: `coverage` rejects rise
42 → 63, because `exactWrites` demotes stores out of native coverage until the
block falls under threshold. Nineteen blocks stop being compiled.

So mode 2 converts device-register byte stores from whole-instruction replays
into single accesses, and pays for it by dropping nineteen blocks out of native
code. Both effects are real, both are small, and they are in opposite
directions — which is the mechanical reason the stopwatch sees nothing.

What does *not* move is equally informative: `24D0 R $50F06060` reads
1 642 326 in mode 1 and 1 642 333 in mode 2. It is a `MOVE.L (A0),(A2)+`
whose source is the SCSI pseudo-DMA window — a two-access instruction with no
exact-thunk form on either backend. The 2026-08-29 (seventh) entry recorded
that invariance as an observation ("the thunk path is not in the story"); the
census now supplies its reason.

## Wall clock (PROVISIONAL — see the caveat)

ABBA, warm-up pair discarded, 5 pairs per budget, verdict against the widest
of (spread A, spread B, recorded floor):

| budget | A (`=1`) | B (`=2`) | delta | widest noise | verdict |
|---|---|---|---|---|---|
| 6000 | 15.95 s | 16.01 s | +3.8 ‰ (+0.38 %) | 62.7 ‰ | NOT A CLAIM |
| 2000 | 5.73 s | 5.78 s | +8.7 ‰ (+0.87 %) | 33.2 ‰ | NOT A CLAIM |

Paired differences within each ABBA pair (drift cancels): +6.7 ‰ at 6000,
+6.5 ‰ at 2000, both with the sign unresolved at 95 %.

The 2026-08-29 (late night) entry's **+3 %** for mode 2 — *one unpaired run*,
and that entry itself refused to move the default on it — does not reproduce.
If anything the sign is the other way, which the `backend declined` row makes
plausible, but the honest statement is that this host cannot resolve it.

> **Caveat, `docs/MEASURING.md` § 4.1bis.** These runs overlapped three
> reading agents (1-minute load 1.33 at the start, 3.05 at the end), so they
> are **provisional** whatever their spread says. Worse, the null experiments
> measured this host's floor at **1.6 % (6000 frames) and 6.3 % (2000
> frames)** against the **10 permille** recorded in
> `performance_budgets.tsv:67`, and the bench printed `HOST NOISIER THAN
> POLICY` on both. Nothing under about 2 % is resolvable here under those
> conditions. A clean re-run on an idle host is recorded separately.

## Ruling

Do not flip `X64Backend::caps().maxAccessThunk030` to 2. The mechanism is
sound, conformant and measured; what it buys is 0.09 points of native share
and no resolvable wall change at either budget. Close the item as *measured,
not worth the default* — which is what the plan's own ceiling paragraph asked
for if the residual landed under the floor — and keep the explicit knob for
diagnosis.

That also answers TODO § B.3's first clause: modes 1 and 2 have now been
compared in the same binary. The remaining B.3 work (native locksteps,
`ctest -L m030`, the full etalon tier on x86-64) is unchanged, and is
gated on a different question than this one.

## Files

* `abba_knob.sh` — the driver. Same protocol as `../b2impl/abba.sh`, plus a
  paired-difference verdict beside the range verdict: the ABBA already runs A
  and B adjacent in time, so the per-pair difference cancels the slow drift
  that dominates a range taken over separate process invocations (§ R2's own
  warning about "process start-up, page cache and the afternoon"). The range
  verdict stays the headline; the paired one distinguishes "no effect" from
  "not resolvable here".
* `provisional_thunk_1v2.times` / `.raw` — the provisional run above, with
  every arm's full stderr.
