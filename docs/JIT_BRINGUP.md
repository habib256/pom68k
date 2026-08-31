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
| x64 and a64 both declare `guestFamilies = kGuest68040 | kGuest68030` **since 2026-08-18** — correctness scope. Speed scope is the separate `caps().autoFamilies` mask, and the two **no longer agree**: a64 carries 68040+68030 since its independent 2026-08-20 promotion, x64 carries 68040 alone since its 030 promotion was **withdrawn on 2026-08-29** (§ C.5 box); `threaded` carries `kGuestAny` as the floor | `JitBackendX64.cpp`, `JitBackendA64.cpp`, `JitBackendThreaded.cpp`, `JitBackend.cpp` (selection) |
| `threaded` declares `kGuestAny`; `auto` uses it for 000/020, while an 030 reaches the native generator on both ISAs | `JitBackendThreaded.cpp`, `JitBackendA64.cpp` |
| Selection tests guest validity *before* host ranking | `JitBackend.cpp:63-66`, `:137-140` |
| `pomJitProbeCode` has an 030 branch (TT regs, TC.E-off identity, read-only 22-entry ATC scan, last-hit memo) | `MoiraExecMMU_cpp.h:1997-2033` |
| **`pomJitProbeData` now has one too** (data-space `fc = 5/1`, write-protect and owed-M-bit refusals) — C.2 is landed | `MoiraExecMMU_cpp.h:2088-2139` |
| **`pomJitReadData`/`pomJitWriteData` now branch on the model**, reaching `mmuRead`/`mmuWrite` on an 030 and `mmu040Read`/`mmu040Write` otherwise — C.3 is landed | `MoiraExecMMU_cpp.h:2287` and `:2312` |
| The 030 i-cache overlay is charged **inside `mmuFetchWord`, before the JIT window hook** — so the fetch window and the `threaded` backend are conformant on it by construction | `MoiraExecMMU_cpp.h:408-461` |
| `PomIcache` = MC68030UM §6: 256 B, 16 lines × 4 longwords, logical, direct-mapped, tag = A[31:8] + supervisor, per-longword valid bits, gated on `CACR` bit 0, `missPenalty` cycles per miss | `Moira.h:1050-1082`, rationale in `Cpu030.h:164-189` + `:203-208` |
| The cold fallback stub re-enters Moira through `pomJitExecOne()`, whose **030 branch runs `mmuExecuteStart<C68020>()`** — i.e. it fetches through `mmuFetchWord` and charges the i-cache itself | `Moira.cpp:318-348` |
| Moira runs the 68030 on `Core::C68020` cycle counts — **the same 68020 column the x64/a64 cost tables are transcribed from** | `JitBackendX64.cpp:190-250`, `Cpu030.h:164-189` |
| `Instr` carries the traced cost **split** into total / base / i-cache / post-exception, with `total = base + cache + post` asserted before the split is exposed | `JitIr.h:949-957`, `JitEngine.cpp:891` |
| `jit_lockstep_030_test` exists: two LC IIs, register + clock + low-RAM + **three i-cache counters** per checkpoint | `tests/jit_lockstep_030_test.cpp` |
| The 030 emitters are reachable by explicit `POM68K_JIT_BACKEND=x64|a64` since 2026-08-18 (no unsafe override). `auto` reaches a64 on an AArch64 030 since 2026-08-20; it reached x64 on an x86-64 one from 2026-08-21 until the withdrawal of 2026-08-29, and now resolves such a guest to `threaded`. A shipping default reaches only a generator that earned the (family, backend) pair on D.1 evidence | `JitBackend.cpp` (selection), `jit_backend_test` pins the per-host cases |

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

## Phase B — the 68030 instruction cache under generated code — **correctness-proved on AArch64, 2026-08-19**

`Emitter::chargeIcache` emits the MC68030UM §6 model for every natively
emitted instruction, constant-folded as B.2 below describes, on both
generators. Since 2026-08-19 the charge sits on the SUCCESS path, past every
bail, and `unchargeIcache` is deleted on both x64 and a64. Exact data thunks
add a runtime hit proof: a possible miss replays untouched in Moira, where
the miss penalty precedes peripheral access; a proved hit stays native and
has no cycle component to reorder. `PomJitLayout`
carries `cacr`, the six `PomIcache` offsets and `icLive` — deliberately
**not** `PomIcache::armed`, because a 68040 wrapper can have that flag set
while the charge, which lives in `mmuFetchWord`, is unreachable; a backend
reading `armed` would emit the model on Quadras, where it is dead code that
moves the clock.

The AArch64 twin closes the proof: it charges the tracer's exact
`Instr::fetchWords`, aggregates the fetch/hit counters once per instruction,
folds repeated longword checks, and adds miss penalties to the live clock
register only after the native body can no longer bail.
`jit_lockstep_030_a64_experimental_test` now runs production `HOT=1` for
6 000 full LC II frame budgets (260 480 machine cycles each), with ~608.0 M
generated instructions and identical 1 602 507 733 fetches /
1 093 456 393 hits / 509 047 173 misses.
`POM68K_JIT_ICACHE_EMIT=0` turns the model off for attribution, which is the
only reason that knob exists. Zero effect on the 68040 machines (`icLive` is
false there): `-L smoke` green and the Q605 bench fingerprint unchanged.

**The first correct port was not yet proved faster.** Its 6,000-frame Release
ABBA matched `cfb184b6faddabec`, final PC/SCSI and all three i-cache counters,
but `threaded` 20.18 s versus a64 20.11 s was only −0.3 %, inside this host's
3.0 % noise floor. It therefore remained explicit at that point. This result
was superseded on 2026-08-20: keeping native i-cache/retirement state across
linked blocks and applying the measured cold-code score of 64 produced the
fixed-budget win, while the long lockstep and native LLE platform gates stayed
green. The final three-repeat ABBA is `threaded` 19.93 s versus a64 18.88 s
(−5.3 %, 0.4/0.3 % arm spreads, fingerprint `cfb184b6faddabec`), beyond the
3.0 % host floor. A64 `autoFamilies` now carries 040+030; x64 made the same
promotion separately on 2026-08-21, on its own host's evidence (§ C.5).

The 2026-08-21 exact-source MOVE extension exposed the boundary of this
proof: granting its two-access exact token to the 030 moved two fetches at
lockstep comparison 20,770 even though CPU state still matched immediately
before it. The token is now 040-only. With CMPA, ADDQ/SUBQ.W An, EXG and
distinct-register CMPM native on both families, the full 120,000-comparison
LC II run again matches all **1,028,955,568 fetches**, 731,919,464 hits and
297,031,937 misses.

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
overlay, documented as such (`Cpu030.h:164-189`). Whether to replace it with
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
(block path forced on), and one generated-code twin per host ISA —
`jit_lockstep_030_x64_experimental_test` on x86-64,
`jit_lockstep_030_a64_experimental_test` on AArch64 — each under an
explicit `POM68K_JIT_BACKEND`, no unsafe override since the 2026-08-18
declaration. This is not scaffolding for the rest of the phase — it *is*
the phase's value. An 030 code generator without differential coverage is
exactly the 2026-07-30 one-hour timeout again.

**C.2 / C.3 — the 030 probe and thunks — DONE**, `MoiraExecMMU_cpp.h:2088`
and `:2287`. Three things about them that cost a round to learn:

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
   **Dependent destination closed on both generators, 2026-08-31:**
   Speedometer's `3F5F`/`2F5F` calculate `d16(A7)` from A7 *after* the
   source `(A7)+`. A64 and x64 now prove the old source mapping and the
   speculative `old A7 + step + d16` destination mapping before access zero,
   then load, store and publish A7 once. MMIO, code guard, crossing and
   `/BERR` refusals therefore replay from a pristine boundary. The 030 oracle
   pins RAM lockstep, the A7 visible inside the exact MMIO callback and the
   complete 32-byte destination-fault frame for both word and long forms;
   native A64 and x64/Rosetta execute the RAM cases with zero slow
   instructions.
2. **The 030 marks its last write restartable and stacks a format $A frame**
   (`:355-361`); the 040 does not. **Closed on a64 for a narrow family** —
   `restartWrite030()` (a64: the local `restartWrite` at
   `JitBackendA64.cpp:1265`; x64 member `JitBackendX64.cpp:291`):
   register/immediate-source
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
   030's MOVEM contract is the format-$B RESUME (`mmuState`: count/EA in
   flight, RTE continues mid-instruction). The explicit 030 guard on the
   MOVEM emitter was REAL from 2026-08-19 — `if (L_.is030) return false;`
   on both backends — because it had been refused in practice only by its
   cost check comparing raw traced cycles, and `traced030` making the base
   rule global would have un-refused it silently. **Lifted on a64
   2026-08-23 (CHANGELOG (third)), and on x64 2026-08-25**: native MOVEM
   proves every byte of the span before the
   first access (the OrderedSpan preflight), so no fault can occur in
   flight and the resume state is never observable; a span the DTLB
   cannot prove bails to the untouched instruction. The A64 120k and
   6000-frame locksteps are identical with it; the shared asset-free native
   oracle now pins a predecrement push/postincrement pop pair on both A64 and
   x64. The long x64 LC II tier remains a true-x86-64 release-host gate.
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
   now cleared in the a64 block prologue (`JitBackendA64.cpp:2542`) — once
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
links for that block alone (`JitBackendA64.cpp:2525`, `:2764`), and with
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

> **Superseded 2026-08-19 — § C.4sexies item 4.** The x64 guard described
> below is gone: `Emitter::traced030` made the base-cost rule global and
> DBcc / conditional Bcc.W compile. The AArch64 charge-on-success port now
> uses the same base-cost rule and path-specific fetch model. The measurement
> stands as the record of what the old guard cost.

The census reads as "no emitter for `56C9`". It is not: `emitDbcc` exists,
`canEmit(0x56C9)` says yes, and instrumenting every guard inside it logs
**zero** refusals. The refusal was upstream of the dispatch, in the x64
`compile` loop, and it was deliberate:

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

> **Retracted 2026-08-19 — see § C.4sexies.** The ceiling was a real
> measurement of three bugs and a policy, not of a limit: with the i-cache
> uncharge hole fixed, the CACR hint retired on the proven V8 inventory,
> the base-cost cross-check made global and the MMU-generation flush made
> a lazy revalidation, the generator beats `threaded` by **12 %** at this
> bench's default budget.

What is left unmeasured, and is where the remaining distance must be: **why
82 % of execution is on the window path at all**, when only 66.9 % is
explained by declined blocks and 13.5 % by the post-refusal arm backoff.
At that checkpoint the a64 half was a different story — within 3 % of
`threaded`; its later 2026-08-20 native-state/score-64 promotion is recorded
in § B. Porting
its `baseCycles` consumption and `(An)+` ordering to x86-64 was the
obvious coverage work (both landed with § C.4sexies: `Emitter::traced030`
at `JitBackendX64.cpp:471`, the restartable family at `:1789`), though
C.4quater said not to expect parity from it alone — and it was right: the
parity came from the uncharge fix.

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

### C.4sexies — the ceiling was three bugs and a policy (2026-08-19)

The 2026-08-19 session took the x86-64 generator from **+45 %** behind
`threaded` to **−12 %** ahead at the bench's default budget, conformantly —
every number below is `jit_bench_lcii`, ABBA in one process, 3 repeats per
arm, quiet host, fingerprint identical within each budget
(`3de5c5ab62b4eca8` at 2000 frames, `cfb184b6faddabec` at 6000):

| `POM68K_BENCH_FRAMES` | `threaded` | x86-64 generator | delta |
|---|---|---|---|
| 1200 | 8.80 s | 10.42 s | +18.4 % |
| 2000 | 14.30 s | 14.77 s | +3.3 % |
| 3000 | 21.21 s | 20.23 s | **−4.6 %** |
| 6000 (default) | 42.29 s | 37.22 s | **−12.0 %** |

The trend is the boot-warmup story: below ~2500 frames the run is
single-pass System-loading code and the compile investment cannot amortize;
a real session sits far beyond the 6000-frame floor. What moved, in causal
order — the full forensic narrative is `CHANGELOG.md` 2026-08-19:

1. **CACR hint retired on the V8** (per-board constant
   `V8Memory::kJitStoreInventoryComplete`; the knob is three-valued now).
   26 544 flushes/run → 1.
2. Retention exposed a **pre-existing uncharge hole**: a runtime-bailing
   instruction whose emitted i-cache charge had missed left a charge (miss
   count, tag, +penalty) for an instruction that never ran whenever
   `pom68kJitStep` declined the re-run the uncharge had pre-subtracted.
   **The charge now sits on the success path only** (after the body;
   inside each control-flow emitter past its last bail) and the uncharge
   is gone. Diagnosed with three instruments this phase keeps: the
   lockstep's i-cache CONTENT compare, per-delivery i-cache counters in
   the peripheral trace, and the engine dispatch ring
   (`POM68K_JIT_DISPATCH_RING=1`).
3. That fix re-attributes the old **block-link chain-boundary contract**
   (same divergence signature): restart-write blocks take links again on
   x64 and a64, and their exact fault-frame gates still hold.
4. **`Emitter::traced030` makes the base-cost cross-check the global 030
   rule** (post-exception traces never match), unlocking the whole
   2026-08-18 census — plus DBcc (a64's 2-fetch rule), conditional Bcc.W
   (2 common words + the fall-through's own pc+4), and `JSR d16(PC)`.
   The traced **fetch count** (`Instr::fetchWords`) replaces the words+1
   guess — `MOVEA.L (xxx).W,An` under SKIP_LAST_RD was the counterexample.
5. **MMU-generation flushes are a lazy revalidation**: blocks carry the
   (logical page, physical page, length) triple their recording window
   proved; a generation bump drops the DTLB and the links (the link wipe
   now visits only published slots — it was a 1 MiB memset ×4527), and a
   block runs again only after the fresh window re-proves its triple.

**The a64 port landed on 2026-08-19.** A production-cadence lockstep first
found the exact-thunk ordering case above, then a second latent bug that the
old 8192-cycle checkpoint cadence hid: `MOVE.B (A7)+,(A1)` kept its
destination host pointer in x15 across an EA commit, while large Moira-layout
accesses also use x15 as scratch. The store could therefore land in the CPU
object's A7 field. Source and destination pointers now live in separate frame
slots and are reloaded only at the access. The dedicated native MOVE
regression plus 6 000 full frame checkpoints pass, as do the 040 copyback-pair
gates touched by the same fix.

**Parked with reproducers** (isolate with `POM68K_JIT_DENY_FROM/_TO` + the
dispatch ring): base-cost admission of the **restartable-write family**
still diverges at a coarse budget cut — those keep the total-cost check,
priced at ~44 % of remaining in-block fallbacks, the biggest known lever
left. **BSR.W** and the wider single-path branch exemptions swap one miss
for one hit at an identical pc (step 16 097) and stay refused.
*(A "target-side charge" fix shape proposed earlier on 2026-08-21 was
REFUTED the same day by measurement: the traced `fetchWords` is 2 —
mode-5 `execBsr` consumes the displacement from `queue.irc` with no
readExt, so the linear charge is already right, and the target pair is
charged by the target block like `$4EBA`'s. The real blocker is the
forensic box below.)*

> **Forensic closure of the restart-write reproducer (2026-08-21).** The
> divergence is now in-tree as `POM68K_JIT_RESTART_BASE=1` and was run to
> ground with the DENY bisection: TWO independent culprit pockets,
> `$40A07500-7600` (the historical step 19 658) and `$40A08A00-8B00`
> (step 19 150 — it fires first), each disassembling to the SAME motif —
> an interrupt handler's delay loop: `MOVE.L D0,-(A7)` (the newly
> admitted restartable write), a status read, then a one-instruction
> `DBRA` self-loop, `MOVE.L (A7)+,D0; RTE`. The per-delivery peripheral
> trace names the first observable slip, and it is NOT a cost or i-cache
> charge: at step 19 140, delivery point 1, both arms hold IDENTICAL
> clock, machine time, deadline, device hash and all three i-cache
> counters — only the pc differs by one instruction
> (interp `$40A143E4`, jit `$40A143E0`), and the jit records one fewer
> delivery point. This is the **peripheral-phase class** — the same
> failure that reverted the global `restartableWriteRequired` on
> 2026-08-18: the fallback path services deadlines at the exact
> instruction, native code at its block pacing points, and making the
> handler's write native moves one delivery boundary by one instruction.
> The later +1 miss/+4-cycle signature at the failing checkpoint is a
> CONSEQUENCE of that alignment slip, not the cause. The admission check
> is innocent; the unlock for the ~44 % is **delivery-boundary alignment
> of native blocks with the fallback path**, which is engine work, not
> emitter work.
>
> **The same day, BSR.W converged on the SAME class.** With
> `POM68K_JIT_BSRW=1` (the second reproducer knob) the 120k gate
> diverges at the historical step 16 097 — and the per-delivery trace at
> step 16 090 shows the identical signature: clock, machine time, device
> hash and all three i-cache counters EQUAL, the pc one instruction
> apart at a delivery point (interp `$40A132E2`, jit `$40A132DE`), the
> point counts off by three. The final hit↔miss swap at an identical pc
> with equal fetch counts is the interrupt returning to a
> one-instruction-different pc and re-walking the direct-mapped lines in
> a different order. So BOTH parked levers — the restart-write base
> admission (~44 % of fallbacks) and BSR.W plus the wider single-path
> exemptions — wait on ONE fix: deliver at the same instruction
> boundaries as the fallback path. Surgical hypothesis for that
> chantier: the interpreter services deadlines AFTER the instruction
> retires, while the native pacing test runs at block entry — a
> post-versus-pre placement (or a `>=`-versus-`>` comparison) to
> reconcile, starting from the two one-command reproducers.
>
> **CLOSED on x64 2026-08-21 — and the hypothesis above was WRONG in the
> instructive way: § C.4nonies.** The take plumbing was already aligned
> (pin→take latency identical on both arms); what slipped was the
> DELIVERY — the forced peripheral flush at a native I/O access runs at a
> clock missing the i-cache fetch penalty the interpreter has already
> charged at that point. The access thunks now bias the clock for the
> access alone; both reproducers pass the full 120k, and
> `jit_lockstep_030_x64_alignment_test` keeps them closed.

### C.4septies — the IIsi dies under the generator, and the `jit_*` 030 gates never tested it (2026-08-19, parked with reproducer; CLOSED 2026-08-21)

> **CLOSED 2026-08-21.** The crash did not survive the 2026-08-19→21
> hardening window: on a fresh full relink of the current tree, the same
> host that produced the four deterministic SIGSEGVs boots the IIsi to the
> Finder under the explicit native generator
> (`POM68K_JIT_BACKEND=x64` + `POM68K_JIT_REQUIRE_NATIVE=1`, exit 0), and
> all six IIsi gates are green under the flip — the four that crashed
> (boot 162 s, `jit_` 161 s, input 130 s, persist 355 s) plus soak and the
> `interp_` oracle. Attribution was deliberately NOT bisected: seventeen
> commits touched the engine and both backends in the window (compile
> telemetry + `CompileResult`, native-state/null-callback hardening, the
> lazy MMU-generation revalidation, the exact-read seam), and a bisect
> prices at that many crash re-runs, which the host discipline refuses
> without cause. The reproducer and the triage order below STAY, so a
> return of the crash starts from a written procedure, not from memory.

The first `-L m030` run with the flip in force: **all four IIsi gates
SIGSEGV at ~4-5 s wall** (`iisi_boot_etalon` 5.22 s, `jit_iisi_boot_etalon`
5.21 s, `iisi_input_etalon` 4.26 s, `iisi_persist_etalon` 5.17 s — a
deterministic early-boot point), while every 030 platform visible around
them boots the generator green: **the IIci passes on the same RBV board**
(183 s), VASP (`iivx` 263 s, `iivi` 154 s), Sonora (`lc3plus` 283 s, soak
716 s, persist 467 s), LC II (persist 202 s), IIfx. (The run's full tally
was lost to a `| tail -40` — the 2026-08-19 (third) CHANGELOG entry owns
that lesson; the four SegFaults and the listed greens are verbatim.)

**This is latent, not a regression of the parity night** — and the way it
hid is itself the finding: the `jit_*_boot_etalon` gates on 030 set
`POM68K_CPU_ENGINE=jit` and nothing else, so their backend is `auto` —
which resolved to `threaded` on every 030 until the flip. `jit_iisi` green
on 2026-08-18 was `threaded` green. **The x64 generator had never executed
the IIsi at all.**

What a read-only pass eliminated (all audited sound): `codeSpan`/`dataSpan`
bounds against `totalRam_`; `CodeGuard::note()` slice bounds; the link
table's `kNoLink` init (virgin tag-0 aliasing was already handled at
construction); precise eviction (`retractLink` before `release`; since
2026-08-22 the slice mark is a 32-byte sub-slice mask of the blocks' own
bytes, a hit carries the written range, and only the blocks it touches
go — the mask recomputed from what remains); `serviceGuard()` reachable only from the
dispatch loop with `running_` false; `flushAll` deferring under `running_`
via `pendingFlush_` (a MOVEC-to-CACR executed by the in-block fallback is
safe); the RBV video decoder (fixed `ram_.data()` base, screen ≪ RAM).
What distinguishes the IIsi from the passing IIci: the **Egret 344S0100
LLE** and its early-boot transport — the HOST-paced VIA1 PB4 bit-bang that
already earned this machine its own pacing entry (CHANGELOG 2026-07-25) —
plus 20 vs 25 MHz.

Reproducer: any build with 030 in x64's `autoFamilies`, then
`./build/iisi_boot_etalon` — SIGSEGV ≈ 5 s. Triage order for the next
session: (1) `POM68K_JIT_BACKEND=threaded` must stay green; (2) get the
crash PC (gdb or coredumpctl) — the first question is whether it lies
INSIDE the code buffer (wild block transfer: stale link, freed block) or
in a helper (bad host pointer); (3) `POM68K_JIT_DISPATCH_RING=1` for the
last 8192 dispatch decisions; (4) bisect by pc with
`POM68K_JIT_DENY_FROM/_TO`. The i-cache CONTENT compare in the lockstep
gate does not cover this machine — the 030 lockstep is LC II only.

**Gate hardening follow-up (2026-08-19).** Native builds now rewrite every
existing `jit_*` 68030 boot registration to request the compiled generator
by name and set `POM68K_JIT_REQUIRE_NATIVE=1`; Engine construction aborts if
selection or W^X falls back to `threaded`. CMake also accepts explicit
`POM68K_JIT_BACKENDS=x64|a64`, so the x64 tier can be cross-built on Apple
Silicon. That x86-64 binary reached the IIsi Finder for 1,000 frames under
Rosetta (47.83 s), and the native AArch64 twin passed in 9.36 s. This closed
the false-green gate, not the Linux-native crash: the historical SIGSEGV did
not reproduce on macOS/Rosetta, so the x64 `autoFamilies` flip stayed
blocked until the original host passed the same now-native gate — which it
did on 2026-08-21 (the closure box above).

### C.4octies — compile refusals are attributed; cold blocks earn code by work (2026-08-19)

`Backend::compile` now returns a structured result. Whole-block refusals are
counted and timed as context/IR, emit/fixup, coverage or code-memory/W^X;
this is separate from the existing per-instruction static/runtime fallback
census inside an accepted block. On the 1,000-frame AArch64 LC II workload,
score 64 leaves only 365 rejected attempts (7 context, 358 coverage) costing
0.63 ms: refusal itself is not the throughput problem.

The experimental `POM68K_JIT_PROFIT_SCORE` adds a second admission condition,
`visits × potentially-native instructions >= score`, without changing the
default (`0`) or the existing HOT floor. `jit_bench_lcii` accepts
`a64@score=0,a64@score=64`, so the policy is compared directly inside one
ABBA process. Score 64 wins 7.4 % at 1,000 frames and 3.3 % at 3,000 with
matching fingerprints. Its 6,000-frame result is a provisional 2.4 % win,
inside the 3 % floor and rejected by the busy-host guard. It therefore stays
an instrument, not production policy; the representative budget must clear
both bars before `ResolvedConfig` gains a nonzero default. (A64 later
earned its backend default of 64 with the native-state hardening. **x64
measured the same candidate on 2026-08-21 and REFUSED it**: −0.8 % at the
6000-frame budget, inside the host's 1.0 % floor — the score is per
backend, and x64 ships at 0.)

**C.5 — flip the declaration.** SPLIT AND HALF-LANDED 2026-08-18. The
original coupling — declaration == default — protected against the
2026-07-30 wedge when nothing else did. Since the x64 030 lockstep went
green (the IRC fix + the base-charge rule) and got its own registered gate,
correctness has a standing guard, so the two halves were separated:

* **Declaration: LANDED.** Both generators declare `kGuest68030`; an
  explicit `POM68K_JIT_BACKEND=x64|a64` on an 030 is honoured with no
  unsafe override. Proved by `jit_backend_test` (four pinned selection
  cases), the x64 120k gate, the a64 6,000-frame production-cadence gate,
  and an LC II Finder boot under
  explicit x64 (the gate that timed out at an hour on 2026-07-30).
> **WITHDRAWN on x86-64, 2026-08-29 — and condition 4 below is exactly how
> long the blind spot lasted.** That tier (118/118, 2 h 02) is the LAST time
> anything ran a 68030 etalon on an x86-64 host. Eight days later
> `ctest -L m030` hung EVERY 68030 gate that reaches the generator — 23
> `Timeout`, 20 more still running after 11 h — while the six `interp_*`
> references and the two 68020 Mac LC gates passed. **Measured, not
> inferred:** two arms built from `d4a18b6` *without* that day's Moira patch
> 31 wedge identically (x64 pinned 900.08 s, HEAD's own `auto` default
> 900.06 s), so this is a REGRESSION of the eight days since the flip, in
> ordinary post-MMU generated code, and the product consequence is that no
> 68030 machine boots under the shipping default on x86-64.
> `caps().autoFamilies` on x64 drops back to `kGuest68040`; `guestFamilies`
> is untouched, so the pinned gates below keep pointing at the defect, and
> the bisect between the flip commit and `d4a18b6` is the open work.
> **The a64 flip has no fresher evidence than this one had** — the AArch64
> host must re-run its own 030 tier before that promotion is trusted.
> `CHANGELOG.md` 2026-08-29.

* **Default: decided independently per backend — and BOTH have now fired.**
  The mechanism: `BackendCaps::autoFamilies` is the SPEED mask `auto`
  consults, separate from the `guestFamilies` correctness mask an explicit
  `POM68K_JIT_BACKEND=` consults, and a family enters it per
  (family, backend) pair on D.1 evidence only. A64 cleared all four
  conditions on 2026-08-20 after native-state hardening and the measured
  score-64 policy. **x64 followed on 2026-08-21**, on its own host's
  evidence, never by symmetry: the flip was written 2026-08-19 and blocked
  on the IIsi segfault its first `-L m030` run found (§ C.4septies); with
  that crash cleared, condition 3 was re-measured on the flip build —
  `threaded` 41.19 s median vs generator 36.01 s at the default
  6000-frame budget, **−12.6 %**, fingerprint `cfb184b6faddabec`,
  arm spreads 2.1/1.7 % — and condition 4 ran the full 118-gate `-L etalon`
  tier green under the flip: 106 gates at `-j16` in 81 min, then the 12
  serialized ones (the Q700Memory family + the two UDP-port gates) in
  40 min — 118/118, 2 h 02 total. `threaded` declares
  `kGuestAny`: the floor the selection loop terminates on. Below ~2500
  frames `threaded` still wins the boot phase, which the flip accepts: the
  default is set for the session, not the boot.

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

### C.4nonies — the peripheral-phase class, run to its mechanism and CLOSED on x64 (2026-08-21); the a64 thunks carry the bias too (2026-08-22)

Both parked levers — the restartable-write base admission (§ C.4sexies,
step 19 658/19 150) and BSR.W plus the wider single-path exemptions
(step 16 097) — died of ONE mechanism, and it was not the one the parking
note guessed.

**The refuted hypothesis first.** The parked surgery ("the interpreter
services deadlines after the instruction retires, native code tests at
block pacing points — reconcile the placement, or the `>=`") assumed the
IRQ *take* plumbing was the slipping stage. A new interrupt trace
(`Cpu030::setIrqTrace` — one point per pin CHANGE from `updateIpl`, one
per `execInterrupt` via the `willInterrupt` delegate) measured, on the
BSR.W reproducer at its step-16 097 divergence:

```
interp  pin  lvl=2 clk=527611367        jit  pin  lvl=2 clk=527611430
interp  TAKE lvl=2 clk=527611382        jit  TAKE lvl=2 clk=527611445
```

The pin→take latency is IDENTICAL (15 cycles) in both arms: `guards()`'s
per-instruction `flags != 0` exit and the engine's `pomJitIdle` fences do
their job. **The PIN ITSELF rises 63 cycles late** — the *delivery* is
misaligned, not the take.

**The mechanism, from the per-delivery trace with its new `src` door and
old-deadline fields.** The V8 forces a peripheral flush before every I/O
register access ("registers see current time", `V8Memory.cpp:389/467/673`),
at the CURRENT clock. The interpreter reaches that access with the 030
i-cache fetch penalty already charged — `mmuFetchWord` charges at fetch
time, before exec — while generated code charges on the success path AFTER
the body (§ C.4sexies item 2, the uncharge lesson). So a native I/O access
flushes `missPenalty × misses` cycles earlier than the interpreter's same
access: at the divergence window the two arms' covering flushes sat at
527 611 367 vs 527 611 359 — 8 cycles = exactly the 2 misses the trace
shows — and the VBL's machine time fell between them, so the jit delivered
the event one whole flush later (527 611 430). An interrupt landing at a
different pc re-walks the direct-mapped lines in a different order on the
way back through RTE: the terminal `hits +1 / misses −1 / clock −4` was
always downstream fallout. The knobs never created the class — they moved
IRQ-handler delay loops (I/O status polls) from interpreter execution into
native blocks, which is where the skew becomes an IRQ landing. **The class
was latent in the shipping defaults** wherever a native thunk access polls
a device; the reproducers were just the first collision with a pinned gate.

**The fix is an access-clock alignment, not a charge move.** The exact
access thunks (`pom68kJitRead`/`pom68kJitWrite`) now take the
instruction's traced fetch stream (`fetchWords << 32 | pc`, 0 when the
block emits no i-cache charge) and bias the clock by a READ-ONLY peek of
the overlay (`Moira::pomJitIcachePeekPenalty` — same walk as the emitted
charge, one-line local override, no mutation) for the duration of the
access alone. Success or fault, the bias is gone before anything else
runs: the success-path charge is untouched, the fault re-run recharges
exactly as before, and only the flush the access forces sees the
interpreter-aligned clock. Nothing changes on the 68040 (`pomIcache`
unarmed → bias 0) or on any fallback-run instruction.

**A direct consumer added 2026-08-31.** Speedometer 4's `0829`, `1029` and
`1429` reads all land in the LC II `$50F0xxxx` device aperture and trace at
59–129 base cycles. Their IR accesses are now `exactRequired`: the thunk owns
that live peripheral delay while A64/x64 charge the fixed BTST/MOVE tail.
This is intentionally opcode-scoped policy, not a rule that any slow 030
trace is a device access. `jit_restart_write_030_test` injects 23 cycles per
MMIO read and compares every architectural boundary with zero native slow
instructions; the real 120k LC II lockstep remains identical.

**Validated 2026-08-21:** both reproducers heal at the full 120k —
`POM68K_JIT_RESTART_BASE=1`, `POM68K_JIT_BSRW=1`, both together, and the
default config, all four `OK — 120000 steps identical` with i-cache
identical. `jit_lockstep_030_x64_alignment_test` (both knobs ON) now pins
the class closed.

**Still open, deliberately:**
- ~~**The a64 port.**~~ **LANDED 2026-08-22 (afternoon) — and the
  premise was wrong.** The class was never latent on AArch64: that
  backend had closed it by `guardIcacheHits`, a runtime replay of every
  thunk-capable instruction whose fetch the block shadow could not prove
  an i-cache hit — which is why the a64 120k lockstep with both
  admissions explicitly ON was already `identical` BEFORE the port (the
  first thing measured on the ARM host). The port replaces the replay
  with x64's bias: `pom68kA64Read/Write` take the packed operand in x4,
  the compile loop packs it per instruction, the guard and its shadow
  proof are deleted. Four 120k runs identical after (default, both knobs
  on, both off, and the 6000-frame production-cadence gate), i-cache
  counters identical, replays 17.0 M → 12.8 M with every other counter
  equal. `jit_lockstep_030_a64_alignment_test` is the x64 gate's twin.
  The backend declares `accessClockBias`. The afternoon read the
  identical on/off counts as "the knobs are inert on a64, its admissions
  being unconditional" — HALF RIGHT, and the wrong half mattered: the
  emitter never consulted the knobs because it had the OLD rule
  hard-wired (total-cost admission for the restartable-write family,
  BSR.W refused), so every push traced on an i-cache miss fell back — 120 M
  of the idle Finder's 238 M in-block fallbacks. Wired to the same two
  knobs as x64 the same evening (CHANGELOG 2026-08-22 (sixth)): 120k
  locksteps identical with the knobs on and off (now with DIFFERENT
  counts), native share 49 → 71 % at 30 000 frames. The declaration is
  what the per-backend default rides on, and `jit_backend_test` pins it.
- **Dest-extension forms.** The linear bias counts ALL traced words; the
  interpreter fetches a memory-destination's extension words AFTER the
  source read. A form with an I/O source and dest extensions would be
  biased early by its dest-word misses. No such form is in the admitted
  native set today, and the 120k gate is the tripwire.
- **The admissions are ON BY DEFAULT under x64 since 2026-08-22 —
  per-backend, through the `caps().accessClockBias` declaration.** The
  backend that carries the access-clock bias declares it, and
  `applyBackendDefaults` turns the two admissions on only under that
  declaration (explicit env wins either way; `jit_backend_test` pins the
  coupling on both hosts — a64 declared FALSE until its thunks carried
  the bias, which they do since the afternoon of the same day, so both
  native backends now declare it and an admission default can never
  outrun the alignment on the ISA that runs it). The
  speed evidence, measured the same day: `bench::compare` on the LC II,
  ABBA, 3 repeats/arm, quiet host, fingerprint identical within each
  budget (`cfb184b6faddabec` at 6000 frames — the same fp as the C.5
  flip evidence), host floor 1.0 %:

  | arms (`POM68K_BENCH_ARMS`) | 3000 frames | 6000 frames |
  |---|---|---|
  | `x64,x64@restart=1` | −3.0 % | **−4.3 %** |
  | `x64,x64@bsrw=1` | −0.9 % (NOT A CLAIM) | **−2.3 %** |
  | `x64,x64@restart=1@bsrw=1` | **−7.7 %** | **−8.0 %** |

  The pair is SUPER-additive (−8.0 % against a −6.6 % sum): BSR.W
  admission extends blocks across calls, and the restart-base admission
  then natively serves more of what those longer blocks contain. The
  trend rises with budget, so per R3 the −8.0 % is a floor for a real
  session. The default-path 120k lockstep (backend=x64, NO knobs) is
  bit-identical to the both-knobs run — same retired-instruction and
  block counts — so the per-backend resolution is proved live, and the
  flip's tier evidence is recorded in the flip commit.

## Phase D — the default engine — **68040 landed**

> **The requirement:** the fastest *conformant* LLE mode must be the
> default. If the JIT wins on a family and is proved bit-identical on that
> family, the JIT is what a user gets without setting anything.

`defaultEngine(bool jitByDefault)` (`JitConfig.h:203`) is no longer a
constant; `Engine::Engine` passes
`(guestFamily & (kGuest68040 | kGuest68030)) != 0` (`JitEngine.cpp:163`).
So `jit/auto` is the shipped default on the 68040 machines **and the
68030 ones** — where `auto` resolves to `threaded` — and the interpreter
everywhere else, with `POM68K_CPU_ENGINE` overriding in either direction.
`jit_backend_test` pins the cases without touching an asset.

**D.1 — the evidence bar, and it is the reusable part of this phase.** A
(guest family, backend) pair becomes a default only when all four hold:

1. a per-instruction lockstep gate on that family is green (`clock`,
   registers, stacks, RAM window — and the i-cache counters on an 030);
2. that family's boot etalons are green **under the JIT**, with fingerprints
   identical to the interpreter's;
3. `jit_bench` / `jit_bench_lcii` at a fixed cycle budget show the JIT
   faster, with matching fingerprints;
4. the whole `-L etalon` tier is green with the new default in force.

(68030, a64) cleared all four on 2026-08-20 and is the automatic AArch64
path. (68030, x86-64) held (1) and (3) since 2026-08-19 with (2) and (4)
red on one machine — the IIsi segfault under the generator (§ C.4septies) —
and cleared them on 2026-08-21 once that crash proved gone: fresh 120k
lockstep, all six IIsi gates green under the flip, the bench re-measured at
−12.6 % on the flip build, and the full 118-gate `-L etalon` tier green
(106 at `-j16` in 81 min + the 12 serialized in 40 min).

**D.2 — the blast radius, which is the real work.** The 68040 flip changed
what its plain `etalon` gates test: they now run the JIT, while ten
explicit `interp_*_boot_etalon` registrations preserve one reference per
platform — four 68040 (q605, centris650, q630, q700) and, since the
2026-08-18 030 engine default, six 68030 (lcii, lc3, iivx, iisi, iifx,
duo230). The existing `jit_*` names remain as
explicit-engine regressions and for stable tooling. `docs_test` (which since
2026-08-09 fails on any unlabelled gate) catches a row that gets missed.
Each future family flip follows the same shape, in its own commit and never
folded into an emitter change.

---

## Where this stands, updated through 2026-08-25

| phase | state |
|---|---|
| 0 — measure | **done**, `POM68K_JIT.md` § 3.4 |
| A — 68040 tail (x64) | **done**, −12.8 % wall, native share 98.5 % |
| A2 — a64 mirror | **done**, including the 256-byte DTLB code mask |
| B — emitted 030 i-cache | **correctness-proved on AArch64** by the 6,000-frame production-cadence lockstep + matching benchmark fingerprint; native-state hardening and score 64 later supplied the measured win |
| C.1 — 030 lockstep gate | **done** (threaded, blocks, a64-experimental) |
| C.2 / C.3 — 030 probe + thunks | **written**; validated only indirectly, their gate is C.5 |
| C.4 — per-instruction contract | **partial** — resets, split timing, `(An)+` order, the restartable-write family, charge-on-success and native MOVEM on both backends are done; the peripheral-phase class that parked the restartable-write base admission and BSR.W is **closed on both native backends** (§ C.4nonies, `jit_lockstep_030_x64_alignment_test` + `jit_lockstep_030_a64_alignment_test`), the admissions ON by default per-backend since 2026-08-22 on measured x64 evidence, and consulted by both emitters since the a64 wiring of the same evening (CHANGELOG (sixth)); native 030 JSR reads its target's first word at run time on a64 since 2026-08-23 and x64 since 2026-08-25, with an auto-modifying asset-free oracle on both |
| C.5 / C.6 — declare + boot gates | **declaration landed 2026-08-18; AArch64 default landed 2026-08-20; x64 default landed 2026-08-21** — the IIsi segfault did not survive the hardening window (§ C.4septies CLOSED), and the original Linux-native host now passes the hardened native gates. Native builds pin both the ENGINE and compiled backend in the 030 boot gates |
| D — default engine | **68040 landed; 68030 landed on BOTH native ISAs** (a64 2026-08-20, x64 2026-08-21), with explicit interpreter oracles per platform |

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
  test names in `cmake/Pom68kGatePolicy.cmake`.
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
honest about being a fudge (`Cpu030.h:164-189`) and it is *not* an
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
