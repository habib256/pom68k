# B.2 slice 5 — the 68030 interpreter data window

*Conformance evidence only. **No timing was measured and none is claimed**;
the three-arm A/B belongs to the orchestrator on a quiet host.*

Worktree `/home/gistarcade/src/pom68k/.claude/worktrees/agent-af30bba2444f67915`,
uncommitted, on top of `346f084`. Build `<worktree>/build` — Release,
`POM68K_LTO=ON POM68K_NATIVE=ON POM68K_FAST_LINK=ON POM68K_TESTS=ON
POM68K_WARNINGS=ON`, i.e. the same configuration as the reference build
`/home/gistarcade/src/pom68k/build`, which is the binary this one must be
compared against.

---

## 1. What changed

`Moira::mmuRead` and `Moira::mmuWrite` (`extern/moira/Moira/MoiraExecMMU_cpp.h`)
now consult `Moira::pomJitData<N,W>` before the ATC/translate chain, for
**naturally aligned Byte / Word / Long only**, behind `POM68K_DATA_WINDOW=1`.
That is the 68030 twin of what `mmu040Read`/`mmu040Write` have done since J3.
The fast path sits after the watchpoint hook, after the `mmuLogging` entry
bookkeeping and after the `mmuRteSubst` check, and it finishes the call's
existing logging tail (`mmuAd[mmuIdxDone]`, `pomMmuBumpIdxDone`) unchanged.
Unaligned and page-straddling forms — the only ones that touch `mmuState[1]`
and `mmuDataBuffer` — keep the long path verbatim.

`Moira.h` gains `pomJitData030Ok()` (the refusal set) and two counters,
`pomJitData030Hits` / `pomJitData030Refusals`.

No production wiring was needed for the knob: `jit::Engine`'s constructor
already binds `pomJitDtlbFillFn` under `config_.dataWindow` regardless of
guest family and regardless of `enabled_`, so **`src/jit/JitEngine.cpp` is
untouched by this slice** and the window is live on an interpreter-only
session too (`jit::Engine` is a *member* of every CPU wrapper, constructed
even when `POM68K_CPU_ENGINE=interp`).

### The `POLL_IPL` argument, in one paragraph

`mmuRead`/`mmuWrite` place `if (F & POLL) POLL_IPL;` at three *different*
points depending on size: **before** the access for Byte, **after** it for
Word, and **between the two halves** of an aligned Long. Interrupt
recognition is what that placement decides, so the fast path reproduces each
size's own position rather than picking one — byte polls then loads, word
loads then polls, long loads the high half, polls, then loads the low half.
Nothing else in the skipped tail moves: `SYNC(x)` expands to nothing on
`Core::C68020` (`MoiraMacros.h`, and 020/030/040 all *are* that core), so no
cycle accounting is skipped; and the in-flight access context that
`mmuTranslateAccess` stamps is deliberately not written on a hit, for the
same reason `mmu040Read` does not — a hit is plain guest memory behind a
resident, permitted translation, no fault is possible there, and the next
slow access stamps its own before `extBusError()` could read it. Exactness
is inherited whole from J3b: `pomJitAtcEvict` already kills the derived slice
per page and per space on both 030 eviction sites, so a hit implies the
interpreter's own ATC would have hit and no descriptor U-bit write is skipped.

### Where the plan was wrong: the 030 refusal set is WIDER

The plan named two 030-specific refusals on top of `pomJitData`'s own —
`fcSource != 0` and `mmuRmw`. Both are necessary, and both are in. But there
is a **third, and it is the load-bearing one**:

> `mmuRead`/`mmuWrite` serve **program-space** accesses as well as data ones.

`Moira::read<C, AddrSpace::PROG, S, F>` funnels into `mmuRead` on an M68030
exactly as the DATA form does (`MoiraDataflow_cpp.h` § `read`), and it is not
a rare path: `prefetch`, the queue refills and the JSR/JMP target read all go
through it. `mmu040Read` never had this problem because it takes `data` as an
explicit argument. Meanwhile the DTLB is filled from `pomJitProbeData`'s
**data-space** probe (`fc = 1/5`) and the 68030 ATC matches an entry's `fc`
*exactly* (`MmuAtcEntry::fc`) — the same trap `docs/JIT_BRINGUP.md` § C.2
already records for the probe itself. A program-space access served from that
table would have been translated in the wrong space.

So the guard is `fcSource == 0 && fcl == FC::USER_DATA && !mmuRmw`, plus the
knob (`pomJitDtlbFillFn != nullptr`) tested **first** and by hand rather than
left to `pomJitDataSlow`'s own null test — with the knob off an 030 access
must pay one predictable branch, not an out-of-line call it can only be
refused by, or the orchestrator's `POM68K_DATA_WINDOW=0` arm would no longer
be comparable to HEAD.

`Size` also admits `Quad`/`Extended`; those never reach this funnel (the FPU
assembles them from Long reads), and an `if constexpr` size guard states that
rather than relying on it.

### Test-file changes — instrumentation only

Called out explicitly because it matters at integration:

* `tests/jit_bench_lcii.cpp` — **one added `printf`** of
  `cpu.pomJitData030Hits` / `pomJitData030Refusals` next to the existing
  `icache:` line. Nothing else.
* `tests/jit_lockstep_030_test.cpp` — **one added `printf`** of the same two
  counters for *both* CPUs, inside the existing end-of-run summary block.
  Nothing else.

**What the lockstep gate compares is untouched.** No comparison was added,
removed, relaxed or reordered; the `ramDiff`/`vramDiff`/register/icache
comparisons are byte-for-byte what HEAD has.

## 2. Documentation defects from the brief

* **`src/V8Memory.h` :131-134** — the stale claim the plan quotes ("the 030
  has no data window today … `pomJitProbeData` is 040-only") was **already
  gone at HEAD**: B.2 slice 1 rewrote that block when it published the VRAM
  aperture. What was left wrong is a *citation*: it credited the 030 branch
  of `pomJitProbeData` to "patch 31", which is `pomIdentityProbeBound`, not
  the 030 data probe. Corrected, and the comment now names both consumers of
  the door — generated code since 2026-08-10, and the interpreter since this
  slice.
* **`src/jit/POM68K_JIT.md` § 8 versus `docs/JIT_BRINGUP.md` § C.2** — they
  now point at each other. § 8 gains the paragraph saying the interpreter
  window was 68040-only until 2026-09-04 and citing § C.2 for the dead-path
  complaint; § C.2 gains the "closed 2026-09-04" note citing the wider 030
  refusal set and the counters. `POM68K_VENDOR.md` gains **patch row 33** and
  a paragraph under § J3 point 11.

## 3. Proofs run

See `PROGRESS.md` for the running log, and `identity_table.txt`,
`LOCKSTEP.md`, `CTEST.md`, `logs/` for the raw output.

## 4. The number worth arguing about

The window counters count **accesses** — one increment per `mmuRead`/`mmuWrite`
call that got as far as trying the window. They therefore exclude instruction
fetches entirely (those use the separate fetch window through
`mmuFetchWord`/`pomJitFetch`) and exclude everything `pomJitData030Ok()`
rejects up front, program space included.

Two denominators, and it is easy to cross them:

| arm | budget | window hits | refusals | instructions through `mmuRead`/`mmuWrite` |
|---|---|---|---|---|
| interp | 2000 | 87 960 249 | 13 623 614 | **236 821 650** (all of them) |
| interp | 6000 | 210 352 943 | 13 800 817 | **661 452 534** (all of them) |
| x64 | 2000 | 12 885 356 | 14 202 572 | **11 113 385** (4.69 % of retired) |
| x64 | 6000 | 47 172 553 | 15 645 046 | **34 471 401** (5.21 % of retired) |

On the **interpreter** arm every instruction is interpreted, so the ratio is
0.37 window-served data accesses per instruction at 2000 frames and 0.32 at
6000 — the ordinary shape of 68k code (a large minority of instructions are
register-only). On the **x64** arm only the interpreted residue reaches the
funnel, and there the ratio is 1.16 and 1.37 accesses per interpreted
instruction. Both are sane. The "≈ 8 accesses per instruction" that falls out
of dividing the interpreter arm's 88.0 M hits by the *x64* arm's 11.1 M
interpreted instructions is an artefact of mixing two arms' populations, not
a property of the code.

The honest bound therefore stands where the plan put it: on the arm that
decides the product default (x64, the shipping 030 configuration) the window
can only ever touch the **4.7 – 5.2 %** of retired instructions that run
interpreted, and it serves about 47 M of the 63 M aligned data-space accesses
those make (75 %).

One datum the plan did not predict, and it cuts in the window's favour:
**the refusals are almost entirely a boot-time transient.** On the
interpreter arm the refusal count moves from 13 623 614 at 2000 frames to
13 800 817 at 6000 — 177 k new refusals against 122 M new hits over those
4000 frames. The remembered-refusal design (a tagged null entry in the
256-entry table, promoted into level 0) is doing exactly what it was built
for: an I/O poll loop pays a level-0 tag compare, not a fill. Whether that is
enough to beat the 68040's standing net loss (`POM68K_VENDOR.md` § J3 point
11: 73 s versus 42 s) is a measurement, not an argument, and it is not mine
to make. The knob stays **off by default**.

## 5. Verdict

**Conformance: settled.** Twenty 120 000-step 68030 lockstep runs (all five
registered gates × four rounds) are `120000 steps identical`, at two fresh
cadences neither of which is the registered 8192 / 110000, with
`POM68K_JIT_LOCKSTEP_FULL_RAM_AT` widening the store comparison to the whole
10 MB of RAM for the last 1000 steps of round B, and with
`POM68K_JIT_LOCKSTEP_ICTRACE=1` printing **zero** lines on every gate in round
C — the fetch side does not move. Eighteen bench runs put the same fingerprint
and the same `icache:` triple on HEAD, on knob-off and on knob-on, on all
three engines at both budgets. The knob-off control round shows `0 hits /
0 refusals`, so the branch really is dead when the knob is off.

**Default: open, and not mine to decide.** The window is opt-in and stays
opt-in. The standing precedent is *against* it: `POM68K_VENDOR.md` § J3
point 11 records that this exact feature measured a net loss on the 68040
(73 s versus 42 s) once J3b capped it at ATC residency, and that is why it
was made opt-in there. Nothing in this slice measures the 68030 arm, and
nothing here should be read as an argument that it will win — the two facts
that plausibly differ (the 030's larger per-entry coverage, and `mmuRead`
being the exact-thunk target from generated code rather than a hot MRU
probe) are reasons to *measure*, not to predict. The three-arm A/B on a quiet
host decides it.

## 6. Two non-obvious safeties, checked rather than assumed

**The write guard is not bypassed.** A window write goes straight to host
bytes, so the memory map's `jitGuard_->note()` never sees it — which would be
a self-modifying-code hole if a write entry could cover a page holding
translated code. It cannot: `fillDtlb` computes `PomJitDtlbEntry::codeMask`
from `codePage_`/`pageMap_` and `pomJitDataSlow` refuses any write entry with
a non-zero mask outright (level 0 carries tag + host and has no room for a
per-slice mask, so the window keeps the *older, coarser* whole-page rule that
generated code has since replaced). The remaining question — a page that
becomes code-bearing *after* its entry was filled — is answered by
`Engine::compile`'s `cpu_.pomJitDtlbFlush()` immediately before compilation,
which clears the 256-entry tables **and** `pomJitDataR1`/`W1`. So no stale
"no code here" entry can survive a compilation. Pre-existing mechanism,
inherited whole.

**`observeWrite` loses interpreter coverage while the knob is on.** V8's
`write8`/`write16` call `observeWrite`, which is what
`POM68K_JIT_LOCKSTEP_WRITE_TRACE_AT` reads. A window-served interpreter store
skips it, exactly as an x64 inline DTLB store already does. That trace is a
diagnostic, not a gate assertion, and the gates' real store proof —
`ramDiff` over the whole 10 MB under `FULL_RAM_AT`, plus `vramDiff` at every
boundary — is unaffected and was run. Worth knowing before anyone debugs with
the write trace and `POM68K_DATA_WINDOW=1` at the same time.
