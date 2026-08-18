# Conformant-JIT completion — what is left, and what it costs

What remains of the **conformant** second engine: the 68030 code generator,
the 68030 instruction cache under generated code, and the per-family default
flip that follows each one. The 68040 half is finished and shipped.

Design and invariants of the engine itself: `src/jit/POM68K_JIT.md`. This
file is the *plan*; every measured result lands there and in `CHANGELOG.md`,
not here. Backlog cross-reference: `TODO.md` § 3.

> **Premise, stated once.** Everything below is *conformant* work: the
> interpreter stays the reference and every step is proved bit-identical to
> it by a lockstep gate before it is allowed to be fast. Nothing here trades
> accuracy for speed. The non-conformant lane (relaxed JIT profile, HLE
> overlay — `docs/HLE_OVERLAY.md`, `TODO.md` § 8) stays behind this,
> per the 2026-08-09 ordering decision.

---

## 0. What is already true (re-verified 2026-08-12, file + line)

Facts this plan builds on. Each was read out of the tree, not remembered.

| Fact | Where |
|---|---|
| x64 and a64 both declare `guestFamilies = kGuest68040 | kGuest68030` **since 2026-08-18** — correctness scope; `auto` still skips them on 030 (see C.5) | `JitBackendX64.cpp`, `JitBackendA64.cpp`, `JitBackend.cpp` (the auto skip) |
| `threaded` declares `kGuestAny` and is what `auto` gives every 000/020/030 | `JitBackendThreaded.cpp:42` |
| Selection tests guest validity *before* host ranking | `JitBackend.cpp:64-66`, `:136-139` |
| `pomJitProbeCode` has an 030 branch (TT regs, TC.E-off identity, read-only 22-entry ATC scan, last-hit memo) | `MoiraExecMMU_cpp.h:1993-2029` |
| **`pomJitProbeData` now has one too** (data-space `fc = 5/1`, write-protect and owed-M-bit refusals) — C.2 is landed | `MoiraExecMMU_cpp.h:2085-2135` |
| **`pomJitReadData`/`pomJitWriteData` now branch on the model**, reaching `mmuRead`/`mmuWrite` on an 030 and `mmu040Read`/`mmu040Write` otherwise — C.3 is landed | `MoiraExecMMU_cpp.h:2226-2257` |
| The 030 i-cache overlay is charged **inside `mmuFetchWord`, before the JIT window hook** — so the fetch window and the `threaded` backend are conformant on it by construction | `MoiraExecMMU_cpp.h:408-461` |
| `PomIcache` = MC68030UM §6: 256 B, 16 lines × 4 longwords, logical, direct-mapped, tag = A[31:8] + supervisor, per-longword valid bits, gated on `CACR` bit 0, `missPenalty` cycles per miss | `Moira.h:925-940`, rationale in `Cpu030.h:140-200` |
| The cold fallback stub re-enters Moira through `pomJitExecOne()`, whose **030 branch runs `mmuExecuteStart<C68020>()`** — i.e. it fetches through `mmuFetchWord` and charges the i-cache itself | `Moira.cpp:318-348` |
| Moira runs the 68030 on `Core::C68020` cycle counts — **the same 68020 column the x64/a64 cost tables are transcribed from** | `JitBackendX64.cpp:163-223`, `Cpu030.h:144-168` |
| `Instr` carries the traced cost **split** into total / base / i-cache / post-exception, with `total = base + cache + post` asserted before the split is exposed | `JitIr.h:81-89` |
| `jit_lockstep_030_test` exists: two LC IIs, register + clock + low-RAM + **three i-cache counters** per checkpoint | `tests/jit_lockstep_030_test.cpp` |
| The 030 emitters are reachable by EXPLICIT `POM68K_JIT_BACKEND=x64|a64` since 2026-08-18 (no unsafe override), and by nothing else: `auto` skips native generators on 030 guests, so a shipping default cannot pick them by accident | `JitBackend.cpp` (auto skip), `jit_backend_test` pins all four cases |

Two consequences worth naming up front, because they cut work out of the
plan:

* **The 030 cost tables are free.** The x64/a64 tables are the 68020 column,
  which is what Moira's 030 charges. Widening to 030 does not need a second
  set of tables — and the tracer cross-check (`Instr::cycles`) keeps that
  claim honest instruction by instruction.
* **The i-cache does not have to be modelled on the fallback path.** The
  cold stub charges it by construction (it goes through `mmuFetchWord`), so
  only *natively emitted* instructions need an emitted charge. That is what
  makes Phase B tractable.

---

## Phases 0, A and A2 — **DONE** (2026-08-10)

Measure-first, then the 68040 coverage tail on x64, then the same on a64.
All three are closed and their numbers live in `src/jit/POM68K_JIT.md`
§ 3.4-3.5, which is where they belong. What they established, in one place:

* **A boot etalon is not a stopwatch.** Every timing claim in this chantier
  comes from `jit_bench` / `jit_bench_lcii` at a fixed cycle budget with
  identical fingerprints. Q605 idle Finder: interpreter ×1.03 real time,
  `threaded` ×1.78, `x86-64` ×5.15. LC II: interpreter ×1.98,
  `threaded` ×2.40.
* **B.0 is proved by measurement**, not by argument: the 030 i-cache
  counters are identical to the digit between the interpreter and
  `threaded` over a 6 000-frame LC II budget (1 602 507 733 fetches /
  1 093 456 393 hits / 509 047 173 misses). `jit_bench_lcii` prints them, so
  any future engine that stops charging the i-cache says so.
* **The tail was the DATA PATH, not the instruction set.** The predicted
  target list (line-$E shifts, `Scc`, `PEA`, indexed modes) was wrong: after
  two cost-table fixes, 57 % of what remained were two stack-push forms that
  compiled natively and bailed at *run* time, 95.6 % of remembered DTLB
  refusals being "this 4 KB page holds translated code somewhere". The fix
  was `PomJitDtlbEntry::codeMask` (per-256-byte slice) plus the
  `markPages()` flush it needed — and that flush was a real
  self-modifying-code hole that § 8 of `POM68K_JIT.md` had claimed was
  closed for as long as the section had existed.
* **An instrument wired on one backend and not the other reports success on
  the backend that is not looking.** `Context::slowStaticHisto` /
  `slowRuntimeHisto` were written by a64 and never by x64, so the fallback
  census printed `0` on x86-64 for its whole life, next to a `JitBackend.h`
  comment describing what it was telling us.

**Left open, deliberately:** line-$E shifts (~113 k fallbacks) and the 68020
indexed modes (~167 k) on x64 — a64 has both. Both are ~0.02 % of retired
instructions *on this workload*, and that is the catch: the idle Finder does
not draw, so it cannot price the indexed modes, which exist for QuickDraw's
blitters. **Re-open with a census over a drawing-heavy phase; do not close
either on an idle-Finder number.**

Two items were ruled out rather than deferred: `MULU`/`MULS`/`DIVU`/`DIVS`
stay fallback on purpose (their cycle counts are data-dependent, the
cross-check refuses them honestly, and forcing them would mean a second
timing model), and `MOVEP` never showed a number worth an emitter.

---

## Phase B — the 68030 instruction cache under generated code — **correctness-proved on AArch64, 2026-08-10**

`Emitter::chargeIcache` emits the MC68030UM §6 model for every natively
emitted instruction, constant-folded as B.2 below describes, on both
generators (`JitBackendX64.cpp:482`, `JitBackendA64.cpp:342`, each paired
with an `unchargeIcache` on the runtime-bail door). `PomJitLayout`
carries `cacr`, the six `PomIcache` offsets and `icLive` — deliberately
**not** `PomIcache::armed`, because a 68040 wrapper can have that flag set
while the charge, which lives in `mmuFetchWord`, is unreachable; a backend
reading `armed` would emit the model on Quadras, where it is dead code that
moves the clock.

The AArch64 twin closes the proof: it charges `words + 1` fetches, aggregates
the fetch/hit counters once per instruction, folds repeated longword checks,
adds miss penalties to the live clock register, and exactly **un-charges**
fetches and hits before a runtime replay. `jit_lockstep_030_a64_experimental_test`
passes 120 000 comparisons with ~351.8 M native instructions and identical
948 544 659 fetches / 681 239 356 hits / 267 301 136 misses.
`POM68K_JIT_ICACHE_EMIT=0` turns the model off for attribution, which is the
only reason that knob exists. Zero effect on the 68040 machines (`icLive` is
false there): `-L smoke` green and the Q605 bench fingerprint unchanged.

**It is correct and it is not yet fast.** Two fixed 6 000-frame Release pairs
measured `threaded` at 19.52/19.50 s and a64 at 20.18/20.16 s, same
`cfb184b6faddabec` fingerprint and same i-cache counters. The full run
retires only 18.4 % of instructions natively and takes 64.6 M exits against
threaded's 50.7 M. **Correct but slower native code must not replace the
faster conformant floor**, which is why the declaration has not moved.
Coverage inside compiled blocks is no longer the dominant term; block-exit
economics are.

### B.2 — the model, constant-folded

For a compiled instruction at a known pc, every input is a compile-time
constant:

```
line = (addr >> 4) & 15          tag = (addr >> 8) | (super ? 1<<31 : 0)
bit  = 1 << ((addr >> 2) & 3)    super = BlockIr::super, constant per block
```

so the emitted per-word check is a load of `pomIcache.tag[line]` at a
constant offset, a compare against an immediate, a test of
`pomIcache.valid[line]` against an immediate — and a **cold** miss stub that
sets the tag, clears/sets the valid bits and adds `missPenalty` to `clock`.
Three folds make it cheaper than it looks:

* **Within a block, only the first touch of a (line, tag, longword) can
  miss.** Nothing but instruction fetch touches the i-cache, and `MOVEC` —
  the only way `CACR` changes — is `Kind::Unsafe` and cannot appear inside a
  block. So the compiler walks the block's footprint once and emits a check
  only at first touches.
* **`CACR` bit 0 is constant across a block** but not across two runs of it,
  so the test is emitted once at block entry, branching over the whole
  i-cache preamble when the cache is disabled. That test is also what makes
  the fold *sound*: a folded "guaranteed hit" is only guaranteed if the
  update that justified it actually ran.
* **`icLive` is constant per machine**, tested at compile time. A Quadra
  emits none of this.

### B.3 — where the charge goes, and the double-count it cost to learn

The charge is emitted after `pollIpl()` and INSIDE the emitter's rewind
mark, so an instruction that turns out not to be emittable loses its charge
along with its code and the cold stub charges it itself through
`pomJitExecOne` → `mmuExecuteStart` → `mmuFetchWord`. The compiler's shadow
of the cache advances either way, because the real cache changes either way.

The first attempt got the **clock** half right and the **counter** half
wrong, and the gate said so precisely: **+613 745 instruction fetches on the
JIT side for +17 cycles of clock.** Fetches that carry no time are not
fetches, they are double counts, and the source is exact — a compiled
instruction charges the model before it runs (the interpreter's order), and
an instruction that then bails at RUN time is re-run through `mmuFetchWord`,
which charges it again. The re-charge is a HIT (the forward charge left the
tag and valid bit set), so it costs a counter and no cycles: precisely the
+613 745 / +17 signature. The fix is the exact un-charge on the
runtime-bail door.

Placement alternatives, for whoever revisits it: charging at the END of an
instruction's native code is also exact at every boundary and a runtime bail
then never charges — but a `Kind::Branch` commits its own boundary and jumps
away, so its charge cannot go there. `Bcc`/`BRA`/`DBcc` have compile-time
targets and no memory access, so charging before is already exact for them;
`RTS` and `JSR`/`JMP <ea>` are the residual that can still bail.

**Do not "fix" a divergence here by relaxing what the gate compares.** The
counters are the only instrument that separates a missing charge from a
doubled one.

### B.4 — the `fetches` counter

`pomIcache.fetches` increments whenever armed, cache enabled or not. It is a
diagnostic, not guest state, and the lockstep does not compare it directly —
but `Cpu030::icacheStats()` exposes it and the gate compares that. **The
count is `W + 1`, not `W`, for a W-word instruction**: on the 68030
`readExt()` consumes `queue.irc` and then refetches the next word
(`reg.pc += 2; queue.irc = mmuFetchWord(reg.pc)`), so the fetches land at
pc, pc+2, … pc+2W — one past the instruction's own last word, because the
queue always runs a word ahead. The first cut charged `max(2, W)`, right
only for one-word instructions, and the gate reported the shortfall as
2 084 missing fetches with the miss count and the clock already correct: the
shape of a systematic off-by-one rather than a lost event.

### B.5 — the question this phase does NOT answer

`cacheBoost_` (default 4) runs the 030 core at 4× machine rate and charges
`icacheMiss_` per miss. That is an explicitly *non-conformant* throughput
overlay, documented as such (`Cpu030.h:144-168`). Whether to replace it with
real 68030 cycle counts plus a real cache model is a separate, larger
chantier with its own oracle problem — see § *The i-cache reading* at the
end. Phase B makes the JIT agree with whatever the interpreter does; it does
not change what the interpreter does.

---

## Phase C — the 68030 code generator

**C.1 — the LC II lockstep gate — DONE.** `tests/jit_lockstep_030_test.cpp`:
two identical LC IIs (`Cpu030` + `V8Memory`) from the same ROM, one
interpreter, one JIT, comparing all 16 registers, PC, the three stack
pointers, SR, `clock`, the terminal instruction queue, the first 2 KB of RAM
and the three `PomIcache` counters. Registered as `jit_lockstep_030_test`
(threaded, budget 8192, fine from 110 000), `jit_lockstep_030_blocks_test`
(block path forced on) and, on AArch64 only,
`jit_lockstep_030_a64_experimental_test` (generated arm64 code under the
unsafe override). This is not scaffolding for the rest of the phase — it *is*
the phase's value. An 030 code generator without differential coverage is
exactly the 2026-07-30 one-hour timeout again.

**C.2 / C.3 — the 030 probe and thunks — DONE**, `MoiraExecMMU_cpp.h:2085`
and `:2226`. Three things about them that cost a round to learn:

* The probe uses **data** space (`fc = super ? 5 : 1`), not the program
  space the code probe uses. The 68030 ATC tags every entry with its `fc`
  and matches it exactly, so probing the data side with the program-space
  `fc` misses every entry and silently refuses everything — an engine that
  looks merely slow.
* Page size comes from `mmuPageMask()` and is anything from 256 B to 32 KB.
  The DTLB fills in 4 KB slices, so a page *narrower* than a slice stays
  refused (one slice would span several translations); anything ≥ 4 KB fills
  as independent slices. The LC II's System picks neither 4 nor 8 KB, and
  the old exact-4-KB test cost **17 425 292** refused fills in one run.
* Neither is validated on its own. Their gate is C.5: the interpreter's
  opt-in data window (`POM68K_DATA_WINDOW`) reaches `pomJitData` only from
  `mmu040Read`/`mmu040Write`, and the 68030 interpreter uses
  `mmuRead`/`mmuWrite`, so a run with that window on and off produces
  identical fingerprints and identical *zero* fills — a dead path, not a
  passing test.

### C.4 — the 030 semantic deltas in the emitters — **partial**

This is the live work. Three of the five are named in `JitBackend.h` §
*GuestFamily*; the other two are the MOVEM latch and the per-instruction
reset block.

1. **`(An)+` updates the register BEFORE the access on an 030, AFTER it on
   an 040** (`MoiraDataflow_cpp.h:326-332`). **Closed on a64:** a refused
   translation leaves `An` untouched; after a successful probe the register
   is updated before the load/store; the exact MMIO thunk sees the updated
   register and rolls it back if it reports a fault, so Moira can replay the
   untouched instruction and build the frame. The 040 post-access order is
   unchanged.
2. **The 030 marks its last write restartable and stacks a format $A frame**
   (`:355-361`); the 040 does not. **Closed on a64 for a narrow family** —
   `restartWrite030()` (`JitBackendA64.cpp:644`): register/immediate-source
   `MOVE` to `(An)`, `(An)+`, `-(An)`, `d16(An)`, brief-indexed or absolute.
   Predecrement is performed before the access and reversed before replay;
   brief indexed needs no rollback. `jit_restart_write_030_test` is the
   oracle: it trains a native `MOVE.B D0,d16(A6)` block against plain RAM,
   then points the same EA into an external `/BERR` hole, and compares all
   **32 bytes** of the frame with a pure-interpreter run — SR, next PC,
   `$A008`, internal `LASTWRITE`, SSW `$0315`, fault address, opcode `1D40`
   and the sign-extended output buffer. Full 68020 indexed extensions stay
   rejected by `decodeEa()`.
3. **Neither mode-5 core refills the prefetch queue at instruction end**, so
   `queue.irc` remains the word held by the path that just ran: the next
   linear word, the last extension skipped by `SKIP_LAST_RD`, or a
   branch/DBcc displacement. The tracer records terminal PC/IRD/IRC
   separately (`Instr::terminalIrd/terminalIrc/terminalQueueValid`) and the
   a64 branch emitters confirm their formula against it before compiling.
4. **`mmu040MovemArmed`** (`PomJitLayout::movemArmed`) is 040 state; the
   030's MOVEM restart contract is its own and is **not modelled**. There is
   no explicit 030 guard on the MOVEM emitter — it is refused in practice
   only because its cost check compares against the raw traced cycles, which
   on an 030 include the i-cache penalty. **Make that a real guard before
   C.5 flips the declaration**; an accident of the cross-check is not a
   safety property.
5. **`mmuExecuteStart` resets a block of MMU bookkeeping on EVERY 68030
   instruction** that has no 68040 counterpart in that form
   (`MoiraExecMMU_cpp.h:521-528`):

   ```
   mmuState[0..2] = 0;   mmuIdx = mmuIdxDone = 0;   mmuAd[] = 0;
   mmuFixupReg[0..1] = 0;   mmuCcrSave = getCCR();
   mmuLogging = true;   mmuRmw = false;   mmuOpcodeV = 0xFFFFFFFF;
   ```

   Generated code reproduces `mmu040InstrStart`'s contract and **none of
   this**. Two fields are immediately guest-visible: `mmuCcrSave` is what a
   bus-error frame stacks, and `mmuRmw` left true from an earlier locked
   access sends every subsequent translation down the RMW path. `mmuRmw` is
   now cleared in the a64 block prologue (`JitBackendA64.cpp:2233`) — once
   per linked chain, since `TAS` and `CAS` are its only setters and both are
   `Kind::Unsafe`. It was measured and it was **not** the cause of any
   divergence; the hole is real and the rest of the block is still open.

Plus **B.2/B.3**: the i-cache charge, which is 030-only and has no 040
counterpart at all.

#### Two defects this phase found that generalise beyond it

**`Emitter::chargeCycles` threw the charge away on any machine that does not
pace the engine.** The clock lives in a callee-saved register for a whole
chain of linked blocks. The `!paced_` path called the sync thunk — which
reads `clock` from memory and adds to it — **without spilling the register
first or reloading it after**, so the epilogue's `spillClock()` wrote the
stale value back over the charge. It was latent because the four wrappers
that pace the engine (`Cpu040`, `CentrisCpu`, `Q630Cpu`, `Q700Cpu`) are
**precisely the four families the generators declare**; no generated code had
ever taken that branch. The first thing that did ran its only compiled
instruction for free, and the guest immediately ran one instruction past its
cycle budget. Every one of the other eight CPU wrappers would have hit it the
moment a generator reached them.

**On an 030, `Instr::cycles` is not the table cost.** `mmuFetchWord` adds the
i-cache miss penalty to the clock *during* the instruction, so the traced
value is the table cost **plus** `misses × missPenalty`. That is why a 68030
block dump reads the way it does:

```
67F6 / 8c   BEQ.S taken in this loop … 4 (not taken) + 4 (one miss)
361A / 10c  MOVE.W (A2)+,D3           … 6 + 4
365A / 10c  MOVEA.W (A2)+,A3          … 6 + 4
6B0E / 4c   BMI.S                     … 4 + 0   <- the one accepted
```

Every 030 instruction that happened to miss during its trace is refused for a
reason that has nothing to do with its cost table. `Instr` now carries the
**split** (`baseCycles` / `icacheCycles` / `postExceptionCycles`, filled
while recording only, invariant `total = base + cache + post` checked before
the split is exposed), and a64 consumes it for deliberately narrow forms:
register-only `ADDQ`/`SUBQ ...,An`, read-only `TST (An)`, and the sole-write
`restartWrite030` family. **Consuming it more widely has diverged every time
it was tried** — subtracting i-cache penalties from the cross-check globally
diverged at step 31 162; accepting a larger traced cost for `TST.B (An)` and
`ADDQ.W #n,An` diverged at 7 798 and 10 902. `4A11` (`TST.B (A1)`) traces
**70 = 70 base + 0 i-cache + 0 post**: its excess is real data-bus work, so
it must go through `pomJitReadData` and owns only its six fixed instruction
cycles in generated code.

#### The block-link contract a 68030 write breaks

Admitting all `MOVE register/immediate → memory` forms on the strength of a
successful writable DTLB probe diverged at lockstep step **10 455**. The
first diagnosis blamed the pre-store CCR/PC/LASTWRITE boundary and was
disproved by isolating the census representative `1D40`.
`POM68K_JIT_LOCKSTEP_PERIPH_TRACE_AT=N` was added to settle it: it records
every `executeUntil` edge and peripheral delivery during comparison N — PC,
core/machine clocks, target, deadline, scaling remainder, delivered cycles,
next-event distance and a hash of the complete save-stated V8 device tree —
and is null outside that one comparison.

It named the cause in one run. At the divergence the first 23 trace points
are identical; point 24 is a peripheral delivery with the same clock, machine
clock, deadline, remainder, next event and device hash on both machines, but
the two PCs differ, and the *previous* trace point was fully identical. No
hidden peripheral state precedes the split. Disabling native block links made
the whole run exact — which identifies the missing contract: **a block
containing an 030 write cannot be crossed as a transparent native chain
boundary.** a64 now suppresses both its published link entry and its outgoing
links for that block alone (`JitBackendA64.cpp:2216`, `:2453`), and with
every other link enabled the full 120k gate passes.

#### How to reproduce and bisect

```
POM68K_JIT_BACKEND=x64 POM68K_JIT_UNSAFE_BACKEND=1 POM68K_JIT_BLOCKS=1 \
POM68K_JIT_HOT=1 POM68K_JIT_BLOCK_MAX=1 POM68K_JIT_LOCKSTEP_BUDGET=8192 \
POM68K_JIT_LOCKSTEP_FINE_AT=5950 POM68K_JIT_LOCKSTEP_FINE_BUDGET=32 \
./jit_lockstep_030_test 20000
```

Three rules the bisect method earned, all of them still live:

* **`FINE_BUDGET` must stay well above 1.** A one-cycle budget never gives
  the engine room to build a block — measured, a 60 000-step fine run
  compiled nothing, retired zero JIT instructions and reported "identical",
  i.e. it compared the interpreter with itself. Hence the 64-cycle default.
* **`POM68K_JIT_BLOCKS=0` is the discriminator.** Window-only clean over
  40 000 steps while the block path diverges at 7 511 is what turns
  "somewhere in the engine" into "in code the generator emits".
* **A coarse budget reports noise.** The "+613 745 extra fetches in one step"
  reading was an artefact of comparing after a whole 8192-cycle step, by
  which time the two machines had long since parted. Resolution, not
  instrumentation, is what makes a divergence solvable.

### C.4bis — where the 68030 throughput actually goes (2026-08-18, x86-64)

C.4 reads as an emitter-coverage problem. Measured on this host it is not,
or not yet. `jit_bench_lcii`, 2000 frames, **fingerprint
`3de5c5ab62b4eca8` on every line below**:

| engine | CACR hint armed | disarmed |
|---|---|---|
| interpreter | 17.90 s (×1.86) | — |
| `threaded` | 15.14 s (×2.20) | **14.19 s (×2.34)** |
| x86-64 generator | 21.91 s (×1.52) | 17.13 s (×1.94) |

Two things follow, and neither is about which opcodes the emitters accept.

**1. The x64 generator on a 68030 is slower than the INTERPRETER** (21.91 s
against 17.90 s), and at its own ceiling it still loses to `threaded`
(17.13 against 14.19). The AArch64 half was already known to lose to
`threaded` (`TODO.md` § 3, 2026-08-12); the x64 half is worse, and neither
number is a coverage tail away from winning.

**2. Almost nothing runs in a block.** Native residency is **14.4 %** and
the window/interpreter path carries **82.2 %** of retired instructions.
A new gauge (`jit::Miss`, printed by every bench) attributes those
instructions to the reason they never became generated code:

```
cpu flags 2 963 286 (1.3%)   window refused 998 655 (0.4%)
arm backoff 31 956 960 (13.5%)   tracing 12 113 082 (5.1%)
backend declined 158 390 083 (66.9%)
```

**Two thirds of the guest runs on the window because the backend refused
the block outright**, and over the whole run that refusal is *always* the
same one: 202 848 of 277 002 compile attempts (73 %) fail the
native-coverage bar in `X64Backend::compile`, and **zero** fail `emit()`.
The bar wants half a block's instructions emitted natively, which never
bites on a 68040 (98.5 % coverage) and rejects almost everything on a
68030.

### C.4ter — and lowering that bar makes it WORSE (the residency trap)

The obvious next move is to lower the bar. It is wrong, and the sweep says
so unambiguously — `POM68K_JIT_MIN_NATIVE`, same 2000 frames, fingerprint
`3de5c5ab62b4eca8` on all seven runs:

| `MIN_NATIVE` | wall | native | block fallback | window/interp |
|---|---|---|---|---|
| 0 | 29.99 s | 31.3 % | 52.2 % | 16.5 % |
| 10 | 23.45 s | 19.1 % | 14.0 % | 67.0 % |
| 25 | 22.23 s | 16.8 % | 7.3 % | 75.9 % |
| **50 (default)** | **21.84 s** | 14.4 % | 3.4 % | 82.2 % |
| 65 / 80 / 95 | 21.86 / 21.95 / 21.86 s | ~11 % | ~0.5 % | ~88 % |

**Native residency is not the objective function.** At `MIN_NATIVE=0`
residency is 2.2× higher and the engine is **37 % slower**: an instruction
that falls back *inside* a block costs more than the same instruction on
the window path, because it pays a call, a frame and a boundary commit
where the window pays a straight interpreter dispatch. Everything from 50
upward is one flat plateau, so the inherited 68040 number is already at the
optimum and there is nothing to win by tuning it.

That retires the framing this document and `TODO.md` § 3 both carried —
"the measured lock is global native residency (18.4 %)". Residency is a
**symptom**. The lock is how many instructions per block the emitters can
take, because that is what carries a block over a bar which is correctly
placed. Widening the emitters is right after all; widening them to raise
*residency* is not what makes it right.

**What the 68030 actually wants emitted**, from its own fallback census
(`POM68K_JIT_HISTO=1` on the same run) — and it is **not** the drawing
census's answer for the 68040, which is a lesson about censusing the guest
you are optimising:

| opcode | form | share of in-block fallbacks |
|---|---|---|
| `56C9` | `DBNE D1,disp` | **37.8 %** |
| `24D0` | `MOVE.L (A0),(A2)+` (memory→memory) | 13.1 % |
| `205F` `221F` `245F` `4A1F` | `(A7)+` pops and `TST.B (A7)+` | **28.6 %** combined |
| `584F` `4E5E` | `ADDQ.W #4,A7`, `UNLK A6` | 12.2 % |

`idx(An)` — the mode the 68040 drawing census puts first — is **3 708**
here, next to 1 976 626 for `(An)+` as a MOVE source.

### C.4quater — DBcc is refused by ONE named guard, and unlocking it buys 3 %

The census reads as "no emitter for `56C9`". It is not: `emitDbcc` exists,
`canEmit(0x56C9)` says yes, and instrumenting every guard inside it logs
**zero** refusals. The refusal is upstream of the dispatch, at
`JitBackendX64.cpp:2684`, and it is deliberate:

```cpp
if (ic_ && ir_.instrs[i].kind == Kind::Branch && ir_.instrs[i].words > 1) {
    a_.jmp(staticStub(i)); ... continue;   // 68030 i-cache charge only
}
```

**With the 68030 i-cache charge armed, every multi-word branch is refused**
— and a DBcc is always two words. The reason is in the comment: the two
paths of a long branch fetch a different number of words (the not-taken
path consumes the displacement through `readExt`, the taken path reads it
out of `queue.irc`), while `chargeIcache(i)` is emitted once, before the
condition is evaluated. The hard part is not the emitter — `emitDbcc`
already has three separate paths, each ending in its own `chargeCycles` —
it is that the compiler's shadow of the i-cache is a **compile-time**
model, and after a conditional branch its state depends on the path taken
at run time.

Priced with the existing knob (`POM68K_JIT_ICACHE_EMIT=0`, which changes
the fingerprint and is therefore a measurement, not an option):

| | wall | native | declined |
|---|---|---|---|
| armed (`3de5c5ab62b4eca8`) | 21.96 s | 14.4 % | 66.9 % |
| charge off (`f51a3e54f16ba414`) | 21.26 s | **21.4 %** | 61.2 % |

Native residency rises by half and wall clock moves **3.2 %**. Again:
residency is not the objective function.

### C.4quinquies — the honest ceiling on x86-64

Both non-conformant ceilings together — no CACR flush, no i-cache charge
(`fp=42c0af0935a63304`, so architecturally this is not POM68K any more):

| engine | best measured |
|---|---|
| x86-64 generator, both ceilings | 16.49 s (×2.02) |
| **`threaded`, its own ceiling** | **14.19 s (×2.34)** |
| x86-64 generator, as shipped | 21.96 s (×1.51) |

*(Post-1b re-measure, 2026-08-18 late: with the correctness port complete —
heldIrc, restartable-write contract, PI-conservative — the as-shipped
generator prices at **+32.9 % over the interpreter** at 2000 frames, ABBA
quiet-host, fp `3de5c5ab62b4eca8`, spreads ~1 %. Slightly worse than the
morning's 21.96 s: PI-destination stores under the exact contract are now
conservatively refused, which is correctness the old number did not carry.
The auto skip stands on a quotable number. One instrument note: the bench's
build stamp is the TU's `__DATE__`, blind to a library-only relink — trust
the link mtime, or touch the bench TU before quoting.)*

**Strip the 68030 generator of both its known costs and it is still 16 %
slower than the portable threaded backend.** That is the state of Phase C
on x86-64: no measured lever reaches parity, and the three that were
plausible are respectively non-conformant (the flush), already optimal
(the coverage bar) and worth 3 % (the branch guard). C.5's declaration
staying shut on x64 is not caution, it is the measurement.

What is left unmeasured, and is where the remaining distance must be: **why
82 % of execution is on the window path at all**, when only 66.9 % is
explained by declined blocks and 13.5 % by the post-refusal arm backoff.
The a64 half is a different story — within 3 % of `threaded` — and porting
its `baseCycles` consumption and `(An)+` ordering to x86-64 remains the
obvious coverage work, but C.4quater says not to expect parity from it.

Where the flushes came from, named by the new `Stats::flushCauses` gauge
(`jit::Flush`, printed by every bench): of 28 816 whole-cache flushes,
**26 544 were the CPU wrapper's CACR hint**, 2 264 a translation move and
**8** a write into code the precise evictor could not localise. 74 154
blocks compiled for 29 272 distinct ones — 2.8 blocks per flush, a cache
that never gets to keep anything.

Two results about that hint, one negative and worth as much as the other:

* **Gating it on the instruction-cache strobes buys nothing here.** CACR
  bits 3 (CI) and 2 (CEI) are the only ones that can announce code — the
  rest are the data cache, which on a write-through 68030 cannot — and the
  four 68030 wrappers now test for them. Flushes went 26 544 → **26 529**.
  Every CACR write this guest makes really is an i-cache clear. The gate is
  still right; it is simply not where the cost is.
* **Removing the hint entirely is worth −21.8 % of wall clock**, measured
  with `POM68K_JIT_030_CACR_FLUSH=0` (an UNSAFE instrument, § 6, not a
  tuning knob). The gain is *compile* time not paid, not residency: with
  the hint gone, native share **falls** 14.4 → 11.4 % while wall drops
  21.91 → 17.13 s. `threaded` gains too, but 3.5× less (−6.3 %), because a
  flush costs it only a window and costs the block path generated code.

**Making that conformant is a real question, not a knob.** On the V8 the
guard already sees every write into RAM — SCSI is CPU-driven pseudo-DMA
(`V8Memory::scsiDma_` is a *read* of the controller; the store into RAM is
an ordinary guest `MOVE` through `write8`/`write16`, which `note()`s the
guard), the IWM is polled, and generated-code stores cross the DTLB's
`codeMask`. If that inventory holds for every 68030 board, the hint is
redundant and can go. One workload's matching fingerprint is not that
proof, and the four-proof bar in `TODO.md` § 3 applies.

**C.5 — flip the declaration.** SPLIT AND HALF-LANDED 2026-08-18. The
original coupling — declaration == default — protected against the
2026-07-30 wedge when nothing else did. Since the x64 030 lockstep went
green (the IRC fix + the base-charge rule) and got its own registered gate,
correctness has a standing guard, so the two halves were separated:

* **Declaration: LANDED.** Both generators declare `kGuest68030`; an
  explicit `POM68K_JIT_BACKEND=x64|a64` on an 030 is honoured with no
  unsafe override. Proved by `jit_backend_test` (four pinned selection
  cases), both 120k lockstep gates, and an LC II Finder boot under
  explicit x64 (the gate that timed out at an hour on 2026-07-30).
* **Default: NOT flipped.** `selectBackend()`'s auto path skips native
  generators for 030 guests, because they measure SLOWER than `threaded`
  (§ C.4quinquies) and the shipped default must be the fastest conformant
  mode. Deleting that skip is the real C.5 flip; it fires per D.1
  condition 3 — a fixed-budget bench win, on a quiet host — and never as
  a side effect. C.4bis/C.4ter still say how to get there: emitter
  coverage on the forms the **68030** census names, not residency.

**C.6 — the full-boot gates.** `jit_lcii_boot_etalon` on the native backend
(this is the gate that timed out at one hour on 2026-07-30 — it is the
phase's exit criterion; it has reached the identical Finder signature in
57.05 s under the development override), then `jit_lcii_sys7_etalon`,
`jit_lcii_soak_etalon`, `jit_lcii_persist_etalon`. Then the other 030
platforms, one per family: Sonora (`lc3`), VASP (`iivx`), RBV (`iisi`), MSC
(`duo230`), IIfx. Each is a different `*Cpu` wrapper with its own peripheral
pacing, and the IIfx and the Duo carry IOP/PMU firmware whose handshakes are
phase-fragile — a 2 % instruction-rate shift deadlocks the Mac TV's Cuda
transport, and the same class of failure is live here.

**C.7 — the 020 question, deferred.** The Mac II family and the LC's `as020`
profile would be next. They are named here only so that "all the JIT
instructions for 68030" is not silently read as including them. The 020 has
no MMU, so `pomJitProbeData` is trivial there — but the measured JIT worth on
a 68020 guest is ×1.0-1.2, so it is not obviously worth a family
declaration. Decide with a number, not before.

---

## Phase D — the default engine — **68040 landed**

> **The requirement:** the fastest *conformant* LLE mode must be the
> default. If the JIT wins on a family and is proved bit-identical on that
> family, the JIT is what a user gets without setting anything.

`defaultEngine(bool jitByDefault)` (`JitConfig.h:45`) is no longer a
constant; `Engine::Engine` passes `guestFamily == kGuest68040`
(`JitEngine.cpp:129`). So `jit/auto` is the shipped default on the 68040
machines and the interpreter everywhere else, with `POM68K_CPU_ENGINE`
overriding in either direction. `jit_backend_test` pins all four cases
without touching an asset.

**D.1 — the evidence bar, and it is the reusable part of this phase.** A
(guest family, backend) pair becomes a default only when all four hold:

1. a per-instruction lockstep gate on that family is green (`clock`,
   registers, stacks, RAM window — and the i-cache counters on an 030);
2. that family's boot etalons are green **under the JIT**, with fingerprints
   identical to the interpreter's;
3. `jit_bench` / `jit_bench_lcii` at a fixed cycle budget show the JIT
   faster, with matching fingerprints;
4. the whole `-L etalon` tier is green with the new default in force.

The 68030 fails (3) today, which is the whole of why C.5 has not fired.

**D.2 — the blast radius, which is the real work.** The 68040 flip changed
what its plain `etalon` gates test: they now run the JIT, while four
explicit `interp_{q605,centris650,q630,q700}_boot_etalon` registrations
preserve one reference per platform. The existing `jit_*` names remain as
explicit-engine regressions and for stable tooling. `docs_test` (which since
2026-08-09 fails on any unlabelled gate) catches a row that gets missed.
Each future family flip follows the same shape, in its own commit and never
folded into an emitter change.

---

## Where this stands, 2026-08-12

| phase | state |
|---|---|
| 0 — measure | **done**, `POM68K_JIT.md` § 3.4 |
| A — 68040 tail (x64) | **done**, −12.8 % wall, native share 98.5 % |
| A2 — a64 mirror | **done**, including the 256-byte DTLB code mask |
| B — emitted 030 i-cache | **correctness-proved on AArch64** by the 120k lockstep + a Finder boot; still slower than `threaded` |
| C.1 — 030 lockstep gate | **done** (threaded, blocks, a64-experimental) |
| C.2 / C.3 — 030 probe + thunks | **written**; validated only indirectly, their gate is C.5 |
| C.4 — per-instruction contract | **partial** — resets, split timing, `(An)+` order and the narrow restartable-write family done; the MOVEM guard and the throughput problem remain |
| C.5 / C.6 — declare + boot gates | **declaration landed 2026-08-18** (auto still `threaded` on 030 pending D.1-3); LC II Finder boots under explicit x64; the plain-name boot gates remain to register |
| D — default engine | **68040 landed**: `jit/auto` by default, explicit interpreter oracle per platform; 030 behind C.5/C.6 |

## Gates this plan still adds

| Gate | What it proves | Phase |
|---|---|---|
| `jit_lcii_boot_etalon` under a native backend | the 030 code generator boots a real Finder | C.6 |
| `jit_lc3_/iivx_/iisi_/duo230_boot_etalon` under a native backend | one per remaining 030 platform | C.6 |

Everything else in phases A, B, C.1 and D is proved by gates that already
exist (`ctest -N` lists them; `jit_lockstep_030*`, `jit_restart_write_030_test`,
`jit_store_guard_a64_test` are the ones this chantier added).

---

## Working rules (not negotiable — each cost a debugging round already)

* **Never iterate on a full `ctest` or a full `make`.** The loop is
  `make -j4 jitdev && ctest -L smoke` (~2.5 min). Labels are derived from
  test names at the end of `CMakeLists.txt`.
* **No concurrent `ctest` and `make`, and no edits during a background
  build.** Etalons are contention-sensitive and a mixed tree produces
  phantom failures.
* **A green `ctest` is only worth the freshness of its binaries.** Build
  first, check the exit code (the locale says `Erreur`, not `error`), and
  check for 0-byte binaries from a link killed mid-flight.
* **Any x64 measurement must set `POM68K_JIT_BACKEND=x64`.**
  `POM68K_CPU_ENGINE=jit` alone selects whatever `auto` picks, which on an
  x86-64 host without a code generator is `threaded` — and that trap has
  already published one wrong number.
* **Never a boot etalon for a timing comparison** — use `jit_bench` /
  `jit_bench_lcii` at a fixed cycle budget and compare fingerprints.
* **On spec/oracle conflict, the oracle wins.** For anything the 68030
  manual and WinUAE disagree about, `oracle/` decides.

---

## The i-cache reading — SETTLED 2026-08-09

`PomIcache` today is a **timing overlay** paired with `cacheBoost_ = 4`:
the core runs at 4× machine rate and pays `icacheMiss_` per miss. It is
honest about being a fudge (`Cpu030.h:144-168`) and it is *not* an
architectural cache model.

Two readings of "the i-cache for the 68030" follow, and they are different
projects:

1. **Make the JIT honour the overlay** (this plan, Phase B): generated 030
   code charges exactly what the interpreter charges, the two engines stay
   bit-identical, and the LC II keeps the tempo it has today. Bounded,
   gated, and a prerequisite for Phase C either way.
2. **Replace the overlay with a real model**: real 68030 cycle counts
   (Moira charges 68020 counts today), a real i-cache, and `cacheBoost_`
   retired. That is a conformance *improvement* to the interpreter, it has
   no oracle (WinUAE's 030 timing is the closest thing), and it would move
   every 030 etalon fingerprint in the tree.

**Decided 2026-08-09: reading 1.** The JIT honours the overlay; the
interpreter's timing model is not touched by this chantier. Reading 2 is
written down so it is not lost, and so that choosing it later is a decision
rather than a drift — its **reopening condition is a 68030 timing oracle
worth believing**, which does not exist today.

**Also decided 2026-08-09 (Phase D):** the default flips **per family**,
each family only once its own lockstep + boot etalons + fixed-budget bench
are green (D.1's four conditions), in a commit of its own behind a full
`-L etalon` run. The 68040 families flipped first; the 68030 ones have not.
