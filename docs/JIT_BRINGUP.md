# Conformant-JIT completion — blueprint and milestone plan

What is left of the **conformant** second engine, in one place: the 68040
instructions the code generators still hand back to Moira, the 68030
instruction cache under generated code, the 68030 code generator itself, and
the decision this whole chantier exists to earn — **making the fastest
conformant LLE engine the default**.

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

## 0. What is already true (verified 2026-08-09, file + line)

Facts this plan builds on. Each was read out of the tree, not remembered.

| Fact | Where |
|---|---|
| x64 and a64 both declare `guestFamilies = kGuest68040` **only** | `JitBackendX64.cpp:2057`, `JitBackendA64.cpp:1581` |
| `threaded` declares `kGuestAny` and is what `auto` gives every 020/030 | `JitBackendThreaded.cpp:42` |
| Selection tests guest validity *before* host ranking | `JitBackend.cpp:65` |
| `pomJitProbeCode` **already has** an 030 branch (TT regs, TC.E-off identity, read-only 22-entry ATC scan, last-hit memo) | `MoiraExecMMU_cpp.h:1993-2029` |
| `pomJitProbeData` has **no** 030 branch — `cpuModel < M68EC040 → false`, so the inline DTLB never fills on an 030 | `MoiraExecMMU_cpp.h:2079` |
| `pomJitReadData`/`pomJitWriteData` call `mmu040Read`/`mmu040Write` unconditionally | `MoiraExecMMU_cpp.h:2138-2150` |
| The 030 i-cache overlay is charged **inside `mmuFetchWord`, before the JIT window hook** — so the fetch window and the `threaded` backend are already conformant on it | `MoiraExecMMU_cpp.h:421-454` |
| `PomIcache` = MC68030UM §6: 256 B, 16 lines × 4 longwords, logical, direct-mapped, tag = A[31:8] + supervisor, per-longword valid bits, gated on `CACR` bit 0, `missPenalty` cycles per miss | `Moira.h:832-842`, rationale in `Cpu030.h:124-177` |
| The cold fallback stub re-enters Moira through `pomJitExecOne()`, whose **030 branch runs `mmuExecuteStart<C68020>()`** — i.e. it fetches through `mmuFetchWord` and charges the i-cache itself | `Moira.cpp:318-348`, `JitBackendX64.cpp:70` |
| Moira runs the 68030 on `Core::C68020` cycle counts — **the same 68020 column the x64 cost tables are already transcribed from** | `JitBackendX64.cpp:27-29`, `Cpu030.h:124-128` |
| Every lockstep gate runs **two Quadra 605s**. There is no 030 differential coverage at all | `tests/jit_lockstep_test.cpp:4-8` |
| Last opcode census: **89.6 % native / 12.2 G instructions, taken 2026-07-30 — before MOVEM/DBcc/JMP landed.** Not re-measured since | `POM68K_JIT.md:635` |

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

## Phase 0 — measure before touching anything — **DONE 2026-08-10**

Nothing in phases A-D is allowed to start from a remembered number.

**Results and what they changed** (full table: `POM68K_JIT.md` § 3.1bis):

* Q605 idle Finder, fixed budget, identical fingerprints: interpreter
  ×1.03 real time, `threaded` ×1.78, `x86-64` ×5.15 — i.e. **×5.00 over the
  interpreter**, well past the ×2.68 the boot etalon reports (an etalon stops
  at the Finder; this does not).
* LC II: interpreter ×1.98, `threaded` ×2.40 (**×1.21**). No native 030
  backend exists yet — that is Phase C.
* **B.0 is proved, by measurement**: the 030 i-cache counters are identical
  to the digit between the interpreter and `threaded` (1 602 507 733 fetches
  / 1 093 456 393 hits / 509 047 173 misses). `jit_bench_lcii` now prints
  them, so any future engine that stops charging the i-cache says so.
* **The census the phase depended on was dead on x86-64.** `slowStaticHisto`
  / `slowRuntimeHisto` were written by a64 and never by x64. Wired; it then
  named two wrong cost-table cells in one run — `kMoveDst[(xxx).W]` (3 → 2,
  47.4 % of all fallbacks) and `CMPA` (`kEaRead` → `kEaRead + 2`, 12 %).
  Fixed: native share 96.2 → 97.6 %, fallbacks −55 %, wall −5.7 %,
  fingerprint unchanged.
* `POM68K_JIT_VERBOSE_BLOCKS=N` added (default 40) — the block dump is where
  a block's measured per-instruction cycles are visible, and the old
  hard-coded 40 only ever showed ROM reset code.

**What the census changed about Phase A.** The doc's expected target list
(line-$E shifts, `Scc`, `PEA`, indexed modes) is *not* what a real workload
spends its fallbacks on. After the two cost fixes, the ordered list is:

| rank | opcode | form | kind | share |
|---|---|---|---|---|
| 1 | `48E7` | `MOVEM.L regs,-(A7)` | runtime | 30.7 % |
| 2 | `2F38` | `MOVE.L (xxx).W,-(A7)` | runtime | 26.6 % |
| 3 | `0829` | `BTST #n,d16(A1)` | no emitter | 4.6 % |
| 4 | `1A2B` | `MOVE.B d16(A3),D5` | no emitter | 3.6 % |
| 5-8 | `24D1` `22D8` `12D8` `2F2E` | `MOVE` with two memory operands | runtime | 6.0 % |
| 9 | `B4B1` | `CMP.L d8(A1,Xn),D2` | no emitter | 1.2 % (indexed) |
| … | `E048` `E380` `E181` … | line-$E shifts | no emitter | ~2 % total |

**57 % of what is left is the DATA PATH, not the instruction set** — two
stack-push forms that compile natively and then bail at run time. Phase A
is re-ordered accordingly: the data path first, the emitters after.

- **0.1 Re-run the opcode census.** `POM68K_JIT_HISTO=1`, blocks off, x64
  column, on the Q605 boot. The 89.6 % is stale by three landed emitters;
  the current figure and the *ordered uncovered list* are what pick Phase A's
  order. Same census on the LC II (`jit_bench_lcii`) for the 030 target list —
  a 030 workload is not a 040 workload and nobody has ever taken that census.
- **0.2 Baseline the four engines per family**, fixed cycle budget,
  fingerprints compared: `jit_bench` (Q605: interp / threaded / x64) and
  `jit_bench_lcii` (LC II: interp / threaded). Never a boot etalon for a
  timing comparison — an etalon stops when it recognises the Finder, so two
  builds get timed over different amounts of guest work (`TODO.md` § 3).
- **0.3 Record the LC II i-cache hit rate** (`Cpu030::icacheStats()`) over
  the same fixed budget. Phase B has to reproduce it exactly; without the
  baseline there is nothing to compare against.

**Deliverable:** a table in `POM68K_JIT.md` § 3 with today's date on it.

---

## Phase A — the 68040 coverage tail — **DONE 2026-08-10** (x64)

Full account and numbers: `POM68K_JIT.md` § 3.1bis and § 3.1ter. Summary:
the tail was **not** the instruction set. Landed, in the order the census
picked them:

1. the fallback census itself, which was dead on x86-64;
2. two wrong cost-table cells (`MOVE` to `(xxx).W`, `CMPA`);
3. `PomJitDtlbEntry::codeMask` — per-256-byte-slice write refusal instead of
   per-4-KB-page, gated by `BackendCaps::dtlbCodeMask`;
4. `markPages()` flushing the data TLB on a slice's first block — a real
   self-modifying-code hole that § 8's invalidation table claimed was
   already closed;
5. the `PEA` and `Scc` emitters.

Result on the fixed Q605 budget, fingerprint unchanged throughout:
**10.30 s → 8.98 s (−12.8 %)**, native 96.2 → 98.5 %, block fallbacks
−84 %, runtime (data-path) fallbacks −94 %. `ctest -L jit` 23/23.

**Left open, deliberately:** line-$E shifts (~113 k fallbacks) and the 68020
indexed modes (~167 k). Both are now worth ~0.02 % of retired instructions
*on this workload* — and that is the catch: the idle Finder does not draw,
so it cannot price the indexed modes, which exist for QuickDraw's blitters.
Re-open with a census over a drawing-heavy phase; do not close either on an
idle-Finder number.

### The original plan for this phase, kept for the record

Widen `canEmit()` + emitters on x64 first, a64 second (a64 has tracked x64's
family level since 2026-08-04 and should not be allowed to drift).

Order is set by census 0.1, but the expected order and the traps:

- **A.1 `Scc`** — refused today (`JitBackendX64.cpp:1819`). The condition
  evaluator already exists for `Bcc`/`DBcc`; `Scc` writes a byte through
  `<ea>` and branches nothing. Cheapest item on the list.
- **A.2 `PEA`** — `LEA`'s address computation plus the push the subroutine
  emitters already own (`emitSubroutine`).
- **A.3 line-$E shifts and rotates** — the largest single named item in the
  old census (0.9 %). All of `ASL/ASR/LSL/LSR/ROL/ROR/ROXL/ROXR`, immediate
  *and* register count, byte/word/long, plus the one-bit memory forms.
  **Traps:** the X bit (`FlagSetsX` — no host has one), `ASL`'s V rule (V is
  set if the sign changed at *any* point during the shift, which no host
  flag reports), a count of 0 leaving C alone, and counts ≥ 64. Each of
  these is a correctness cliff, so each gets an explicit case rather than a
  clever host-flag shortcut — invariant 1 of the IR (`JitIr.h:12-18`).
- **A.4 `EXG`, `ADDX`/`SUBX`, `CMPM`** — small, X-bit-heavy, currently
  `aluDirection() == -1` (`JitBackendX64.cpp:171-175`). `ABCD`/`SBCD` only
  if the census says they are non-zero; BCD arithmetic is a lot of emitter
  for a rounding error's worth of instructions.
- **A.5 the 68020 indexed addressing modes — the big block.** `kEaRead`
  rows 6 and 10 are `-1` today (`JitBackendX64.cpp:36`), which is what keeps
  QuickDraw's blitters interpreted. Scope decision, deliberately narrow for
  the first cut: implement the **brief** extension-word format and the
  **full** format *without* memory indirection (no BD/OD, no `[bd,An,Xn]`
  pre/post-index). That covers `d8(An,Xn.SIZE*SCALE)`, which is what the
  blitters actually use; memory-indirect stays a per-instruction fallback
  and costs coverage, not correctness. Every added mode needs its `kEaRead`
  / `eaRmwCost` / `kMoveDst` cost-table row, and the tracer cross-check
  refuses it if the row is wrong — which is the safety net that makes this
  item safe to do at all.
- **A.6 `MOVEP`** — only if the census gives it a number. Expected: it will
  not.
- **A.7 not doing:** `MULU`/`MULS`/`DIVU`/`DIVS`. Their cycle counts are
  data-dependent, the cross-check refuses them honestly, and forcing them
  would mean a second timing model. They stay fallback, on purpose.

**Gate for every A item:** `jit_lockstep_x64_fine_test` (per-instruction
clock + register + RAM compare) and `jit_lockstep_x64_test` green, then
`jit_q605_boot_etalon`. Then the same on a64. Re-run the census after each
landed item and record the coverage delta — an emitter whose census share
does not move did not do what it claimed.

### Phase A2 (a64) — **BLOCKED on an arm64 host, 2026-08-10**

`JitBackendA64.cpp` is compiled only when `CMAKE_SYSTEM_PROCESSOR` is
aarch64 (`CMakeLists.txt`), so on an x86-64 machine it can be neither
compiled nor run. Writing its half of the code mask there would be code
nobody could validate — which is exactly what `BackendCaps` exists to
prevent. a64 therefore keeps `dtlbCodeMask = false` and with it the older,
correct, whole-page refusal: it forgoes the −9.8 % ceiling on Apple Silicon
and the Pi, and forgoes nothing in correctness.

What it needs when an arm64 host is available: `memProbe`
(`JitBackendA64.cpp:596`) loads the entry into `x14` and then immediately
overwrites `x14` with the host pointer, so the mask must be read BEFORE
that — and the `fill` tail, which takes its host straight from the thunk's
return value, holds no entry pointer at all and has to recompute one. The
two cost-table fixes and the `PEA`/`Scc` emitters are architecture-neutral
facts about the 68020 cycle column; a64 re-derives them in its own decode
pass, held to the same cross-check against `Instr::cycles`.

---

## Phase B — the 68030 instruction cache under generated code — **LANDED 2026-08-10, not yet provable**

`Emitter::chargeIcache` (`JitBackendX64.cpp`) emits the MC68030UM §6 model
for every natively emitted instruction, constant-folded exactly as B.2 below
describes: the line, the tag and the longword bit are compile-time for a
known pc, `BlockIr::super` is constant per block, only the first touch of a
(line, tag, longword) inside a block can miss, and the whole group sits
behind one `CACR` bit-0 test — which is also what makes the fold sound, since
a folded "guaranteed hit" is only guaranteed if the update that justified it
actually ran. `PomJitLayout` grew `cacr` and the six `PomIcache` offsets, plus
`icLive` — deliberately **not** `PomIcache::armed`, because a 68040 wrapper
can have the flag set while the charge, which lives in `mmuFetchWord`, is
unreachable; a backend reading `armed` would emit the model on Quadras, where
it is dead code that moves the clock.

Placement is the part that has to be right rather than merely present: the
charge is emitted after `pollIpl()` and INSIDE the emitter's rewind mark, so
an instruction that turns out not to be emittable loses its charge along with
its code and the cold stub charges it itself through
`pomJitExecOne` → `mmuExecuteStart` → `mmuFetchWord`. The compiler's shadow
of the cache advances either way, because the real cache changes either way.

**It cannot be judged yet, and that is an honest verdict rather than a
hedge.** The 030 generator is wrong from its first compiled instruction
(§ C.4's blocker), so nothing downstream of that can be evaluated. Zero
effect on the 68040 machines (`icLive` is false there): `ctest -L smoke` 8/8
and the Q605 bench fingerprint unchanged.

**One thing the attempt did establish, and it is a design correction the
next session should start from.** With the emission in, the gate reports
**+613 745 instruction fetches on the JIT side for +17 cycles of clock**.
Fetches that carry no time are not fetches — they are double counts, and
their source is exact: a compiled instruction charges the model *before* it
runs (the interpreter's order), and an instruction that then bails at RUN
time — a TLB miss the fill refuses, a page straddle — is re-run through
`pomJitExecOne` → `mmuExecuteStart` → `mmuFetchWord`, which charges it
again. The re-charge is a HIT (the forward charge left the tag and valid bit
set), so it costs a counter and no cycles, which is precisely the +613 745 /
+17 signature.

So the **clock** half of the model is very likely already right and the
**counter** half is not, and the fix is a placement question rather than an
arithmetic one:

* charging at the END of an instruction's native code instead of the start
  is exact at every instruction boundary (the interpreter includes the
  penalty from the boundary AFTER the instruction either way) and a runtime
  bail then never charges — but a `Kind::Branch` commits its own boundary
  and jumps away, so its charge cannot go there;
* `Bcc`/`BRA`/`DBcc` have compile-time targets and no memory access, so for
  them charging before is already exact; `RTS` and `JSR`/`JMP <ea>` are the
  residual, and they are the ones that can still bail.

Do not "fix" this by relaxing what the gate compares. The counters are the
only instrument that separates a missing charge from a doubled one.

### The design, for reference

**B.0 — pin what is already true.** The interpreter charges the i-cache in
`mmuFetchWord` *before* the JIT window hook, so both the fetch window and
the `threaded` backend already charge it identically. Nobody has ever
proved that with a test. The LC II lockstep of C.1 proves it in one line
(`clock` compared at every instruction boundary), and `Cpu030::icacheStats()`
compared at the end proves it in the other direction — the two engines must
report the *same* hits/misses/fetches, not merely the same clock.

**B.1 — the problem a code generator creates.** Generated code does not
fetch instructions. It never calls `mmuFetchWord`, so it charges zero
i-cache misses and diverges from the interpreter's `clock` at the first
compiled instruction. On a machine whose whole timing model rests on
`cacheBoost_` × `icacheMiss_` (`Cpu030.h:124-147`), that is not a rounding
error: it is a different guest tempo, and the LC II's known failure mode
under a wrong tempo is a livelock (the VBL task re-entering itself).

**B.2 — the model, constant-folded.** For a compiled instruction at a known
pc, every input to the model is a **compile-time constant**:

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
  miss.** Nothing but instruction fetch touches the i-cache (data accesses
  do not), and `MOVEC` — the only way `CACR` changes — is `Kind::Unsafe`
  and cannot appear inside a block. So the compiler walks the block's
  footprint once and emits a check only at first touches; every later fetch
  of the same longword is provably a hit and emits nothing.
* **`CACR` bit 0 is constant across a block** but not across two runs of it,
  so the check is emitted once at block entry, branching over the whole
  i-cache preamble when the cache is disabled.
* **`pomIcache.armed` is constant per machine**, tested at compile time. A
  Quadra emits none of this.

**B.3 — where the charge is emitted, and why it is exactly there.** Per
*natively emitted* instruction, immediately before that instruction's own
code and *after* any runtime bail-out test. Rationale: the fallback stub
re-enters Moira through `pomJitExecOne()` → `mmuExecuteStart<C68020>()` →
`mmuFetchWord`, which charges the i-cache itself (`Moira.cpp:327`). Emitting
a charge on the fallback path would double-charge; emitting it before the
bail-out decision would charge an instruction that then re-runs and charges
again. At the instruction boundary, with nothing committed, the two engines
agree by construction — which is the same argument the whole backend rests
on.

**B.4 — the `fetches` counter.** `pomIcache.fetches` increments whenever
armed, cache enabled or not. It is a diagnostic, not guest state, and the
lockstep does not compare it — but `Cpu030::icacheStats()` exposes it and
B.0 compares it. Generated code increments it by the same constant-folded
count, on the same path as the tag check.

**Gate:** the C.1 lockstep, run with `POM68K_JIT_BACKEND=x64` once C lands,
comparing `clock` per instruction *and* the three i-cache counters at the
end. A missing charge shows up within a few thousand instructions.

**B.5 — the question this phase does NOT answer.** `cacheBoost_` (default 4)
runs the 030 core at 4× machine rate and charges `icacheMiss_` per miss. That
is an explicitly *non-conformant* throughput overlay, documented as such
(`Cpu030.h:124-147`, "Long-term: a fuller Moira cache model"). Whether to
replace it with real 68030 cycle counts + a real cache model is a separate,
larger chantier with its own oracle problem, and it is **out of scope
here** — see § *Open question* at the end. Phase B makes the JIT agree with
whatever the interpreter does; it does not change what the interpreter does.

---

## Phase C — the 68030 code generator

> **The "localized to the data path" premise is REFUTED — measured
> 2026-08-10, the day `jit_lockstep_030_test` first existed.**
>
> `TODO.md` § 3 and the first draft of this file both said: with
> `POM68K_JIT_ACCESS_THUNK=0` — which hands every memory-touching
> instruction back to the interpreter — x64 boots the LC II to the Finder,
> so what breaks is the natively compiled *memory-access* path and not the
> emitters. The new gate says otherwise. Forcing x64 onto the LC II
> (`POM68K_JIT_UNSAFE_BACKEND=1`, blocks on):
>
> | `POM68K_JIT_ACCESS_THUNK` | first divergence | i-cache misses interp / jit |
> |---|---|---|
> | 2 (default) | step **5956** | 22 788 / 22 927 |
> | 0 (every access handed back) | step **5956** | 22 788 / 22 916 |
>
> **The same step, to the unit.** Disabling the compiled access path changes
> nothing, so the divergence is not in it. What *does* differ in both runs is
> the 68030 i-cache accounting — which is Phase B's prediction exactly:
> generated code fetches no instructions, charges no miss penalty, and the
> clock drifts from there. The gate reports it first because it is the
> difference that explains the others.
>
> Two further facts the same session established, both worth not
> re-deriving:
> * the divergence appears within **~50 coarse steps of the window first
>   engaging at all** — at 5 000 steps the JIT has retired 0 instructions
>   on *every* backend, so early-boot numbers say nothing;
> * **C.2 and C.3 cannot be validated on their own.** The interpreter's
>   opt-in data window (`POM68K_DATA_WINDOW`) reaches `pomJitData` only from
>   `mmu040Read`/`mmu040Write`; the 68030 interpreter uses `mmuRead`/
>   `mmuWrite` and never touches it. A run with the window on and off
>   produced identical fingerprints *and identical zero fills* — a dead
>   path, not a passing test. Their validation is C.5.
>
> **So Phase B comes first, not third.**

**C.1 — the LC II lockstep gate, FIRST.** `tests/jit_lockstep_030_test.cpp`,
modelled on `jit_lockstep_test.cpp`: two identical LC II machines
(`Cpu030` + `V8Memory`) from the same ROM, one interpreter, one JIT, stepped
one instruction at a time from power-up, comparing all 16 registers, PC, the
three stack pointers, SR, `clock`, the first 2 KB of RAM — plus, new for this
family, the three `PomIcache` counters. Registered as
`jit_lockstep_030_test` (threaded) and `jit_lockstep_030_x64_test`,
label `jit`, soft-skip without the ROM.

This is not scaffolding for the rest of the phase — it *is* the phase's
value. An 030 code generator without differential coverage is exactly the
2026-07-30 one-hour timeout again.

**C.2 — the 030 branch in `pomJitProbeData`.** Mirror the `pomJitProbeCode`
030 branch (`MoiraExecMMU_cpp.h:1998-2028`) with the data-space function
code (`fc = super ? 5 : 1`) and the two write rules the read path does not
have: refuse a write-protected page, and refuse a write to a page whose
descriptor still owes an M bit — that write-back is a guest-memory store and
a probe may not perform one. Page size comes from `mmuPageMask()` and is
anything from 256 B to 32 KB, so the DTLB's 4 KB slicing has to hold for a
256-byte page too (fill only the slice actually covered; a page smaller than
a slice must not claim the slice).

**C.3 — model-correct access thunks.** `pomJitReadData`/`pomJitWriteData`
call `mmu040Read`/`mmu040Write` unconditionally. They need an 030 branch
using the 030's own translated access path, still `noexcept`, still
reporting a fault as a return value, still committing nothing on failure.

**C.4 — the 030 semantic deltas in the emitters.**

> **THE BLOCKER, found 2026-08-10 and not in any earlier list.** The 030
> generator is wrong from its **first compiled instruction**: forcing x64
> onto the LC II diverges at step 5956 with `POM68K_JIT_BLOCK_MAX` = 1, 4
> and 64 alike, so it is not block length, not an internal branch loop, and
> not the compiled access path (§ C's refutation above). It is the
> **per-instruction contract**.
>
> `mmuExecuteStart` (`MoiraExecMMU_cpp.h:521-529`) resets, on EVERY 68030
> instruction, a block of MMU bookkeeping that has no 68040 counterpart in
> that form:
>
> ```
> mmuState[0..2] = 0;   mmuIdx = mmuIdxDone = 0;   mmuAd[] = 0;
> mmuFixupReg[0..1] = 0;   mmuCcrSave = getCCR();
> mmuLogging = true;   mmuRmw = false;   mmuOpcodeV = 0xFFFFFFFF;
> ```
>
> Generated code reproduces `mmu040InstrStart`'s contract and **none of
> this**. Two of the fields are immediately guest-visible: `mmuCcrSave` is
> what a bus-error frame stacks, and `mmuRmw` left true from an earlier
> locked access sends every subsequent translation down the RMW path.
> `mmuRmw` is now cleared in the block prologue — once per linked chain,
> since `TAS` and `CAS` are its only setters and both are `Kind::Unsafe`.
> **It was not the cause**: the numbers did not move by a bit. The hole is
> real and worth closing; the blocker is elsewhere.
>
> **And it is not a runaway.** The "613 745 extra fetches in one step"
> reading was an artefact of comparing after a whole 8192-cycle step, by
> which time the two machines had long since parted. Re-bisected at a
> 32-cycle fine budget with one-instruction blocks, the first divergence is
> tiny and exact:
>
> | | interpreter | JIT |
> |---|---|---|
> | i-cache fetches | 72 789 167 | 72 789 **169** |
> | i-cache misses | 22 731 | 22 731 |
> | clock | 195 202 671 | 195 202 **673** |
> | pc | `$40A00922` | `$40A00924` |
> | A2 | `$40A0098C` | `$40A0098E` |
>
> Identified to the opcode. The block dump around that pc reads:
>
> ```
> $40A00920  1 native   [6B0E/4c ]  branch->exit    BMI.S  +$0E
> $40A00922  0 native   [365A/10c]                  MOVEA.W (A2)+,A3
> ```
>
> so the JIT executed **one instruction more than the interpreter inside the
> same cycle budget**: it ran `MOVEA.W (A2)+,A3`, which is precisely pc +2,
> A2 +2 (the post-increment) and A3 written — every difference in the table,
> including A3's, accounted for by that one instruction. The JIT's clock is
> 2 cycles ahead going into it, and 2 cycles is what moved the budget
> boundary between them.
>
> **FOUND AND FIXED, 2026-08-10 — two defects, one of them not 68030-specific
> at all.** The 030 lockstep now runs from step 5956 to **step 25768** before
> diverging, 4.3x further, with pc, clock, every register and the RAM window
> matching at each point along the way. What is left is a +2 instruction-fetch
> count, i.e. the same double-count class as below at a site the un-charge
> does not yet cover.
>
> **1. `Emitter::chargeCycles` threw the charge away on any machine that does
> not pace the engine.** The clock lives in a callee-saved register (`kClk`)
> for the whole chain of linked blocks. The `!paced_` path called the sync
> thunk — which reads `clock` from memory and adds to it — **without spilling
> the register first or reloading it after**, so the epilogue's
> `spillClock()` wrote the stale value back over the charge. The paced path's
> own cold half has always done the spill/reload; this branch simply never
> did.
>
> It was latent because the four wrappers that pace the engine — `Cpu040`,
> `CentrisCpu`, `Q630Cpu`, `Q700Cpu` — are **precisely the four families the
> code generators declare**. No generated code had ever taken that branch.
> The first thing that did, an x86-64 block on a 68030, ran its only compiled
> instruction for FREE: the guest then ran one instruction past its cycle
> budget and parted company with the interpreter. Worth stating plainly
> because it generalises: **every one of the eight other CPU wrappers
> (`Cpu020`, `Cpu030`, `Cpu68k`, `IIfxCpu`, `MscCpu`, `RbvCpu`, `SonoraCpu`,
> `VaspCpu`) would have hit this the moment a generator reached it.**
>
> **2. The i-cache fetch model was off by one on every instruction longer
> than one word.** On the 68030 `readExt()` consumes `queue.irc` and then
> **refetches** the next word (`reg.pc += 2; queue.irc = mmuFetchWord(reg.pc)`),
> so a W-word instruction performs **W + 1** fetches, at pc, pc+2, … pc+2W —
> one past its own last word, because the queue always runs a word ahead. The
> first cut charged `max(2, W)`, right only for one-word instructions. The
> gate measured the shortfall as 2 084 missing fetches with the miss count
> and the clock already correct: the shape of a systematic off-by-one rather
> than a lost event.
>
> Two supporting changes went in with them: the **exact un-charge** on the
> runtime-bail door (below), and a temporary refusal of multi-word branches
> on an 030 — a `Bcc.W` fetches a different number of words on its two paths
> (the not-taken one consumes the displacement through `readExt`, the taken
> one reads it straight out of `queue.irc`), and a charge emitted before the
> condition is evaluated cannot express that. Coverage, not correctness,
> until the charge is split across `emitBranch`'s own paths.
>
> **How it was narrowed, 2026-08-10.** Six candidates, each switched off in
> turn against the same bisect — the method is the reusable part:
>
> | probe | first divergence | verdict |
> |---|---|---|
> | baseline | step 7511, clock +2, pc +2 | — |
> | `POM68K_JIT_ICACHE_EMIT=0` | step **7511**, identical | **not** the emitted i-cache |
> | `POM68K_JIT_LINKS=0` | step **7511**, identical | **not** block linking |
> | taken `Bcc` charged 8 instead of 6 | step **7508**, worse | the constant is right at 6 |
> | `emitBranch` refused on an 030 | step **7516**, and pc AND clock agree there | the branch was the only NATIVE instruction |
> | `POM68K_JIT_BLOCKS=0` (window only) | **40 000 steps identical** | the compiled block, not the window |
> | `FINE_BUDGET` 32 → 4 → 2 | resolved to ONE instruction | see below |
>
> The last row is what made it solvable. At a coarse budget the two machines
> had long since parted and the report was noise (613 745 "extra" fetches);
> at 2 machine cycles per comparison the picture is exact: both start at
> `$40A00920`, both spend 10 cycles, and the interpreter retires **two**
> instructions where the JIT retires **three**. An instruction that ran for
> nothing — which is a missing `chargeCycles`, not a wrong one. The loop, read
> straight out of the ROM image, was five instructions of which exactly one
> was compiled natively:
>
> ```
> $40A0091E  361A       MOVE.W  (A2)+,D3
> $40A00920  6B0E       BMI.S   -> $40A00930     <- the only native one
> $40A00922  365A       MOVEA.W (A2)+,A3
> $40A00924  0700       BTST    D3,D0
> $40A00926  67F6       BEQ.S   -> $40A0091E
> ```
>
> Note how nearly every step was a *refutation*: it is not the i-cache, not
> the linking, not the cost constant, not the access thunk. The one row that
> pointed forward — blocks off is clean — is the one that said the fault had
> to be in code the generator emits, and the resolution change is what turned
> "somewhere in a block" into "this instruction charged zero".
>
> **A structural fact the same hunt turned up, and it changes how a 68030
> block dump must be read.** `Instr::cycles` — the tracer's measurement, and
> the whole safety net behind "cycle counts are checked, not trusted" — is
> the CLOCK DELTA over the instruction, and on a 68030 `mmuFetchWord` adds
> the i-cache miss penalty to that clock *during* the instruction. So a
> traced value is the table cost **plus** `misses x missPenalty`, not the
> table cost. It is what explains the dump around the divergence:
>
> ```
> 67F6 / 8c   BEQ.S taken in this loop … 4 (not taken) + 4 (one miss)
> 361A / 10c  MOVE.W (A2)+,D3           … 6 + 4
> 365A / 10c  MOVEA.W (A2)+,A3          … 6 + 4
> 6B0E / 4c   BMI.S                     … 4 + 0   <- the one accepted
> ```
>
> Every 68030 instruction that happened to miss during its trace is refused
> for a reason that has nothing to do with its cost table. That costs
> coverage rather than correctness — the check stays conservative — but it
> means the cross-check is not *validating* anything on this family, and a
> serious 030 generator wants the tracer to record the cost NET of the
> i-cache charge.
>
> Everything needed to reproduce it is one command:
> `POM68K_JIT_BACKEND=x64 POM68K_JIT_UNSAFE_BACKEND=1 POM68K_JIT_BLOCKS=1`
> `POM68K_JIT_HOT=1 POM68K_JIT_BLOCK_MAX=1 POM68K_JIT_LOCKSTEP_BUDGET=8192`
> `POM68K_JIT_LOCKSTEP_FINE_AT=5950 POM68K_JIT_LOCKSTEP_FINE_BUDGET=32`
> `./jit_lockstep_030_test 20000`
>
> Note for whoever picks this up: the gate's `FINE_BUDGET` must stay well
> above 1. A one-cycle budget never gives the engine room to build a block —
> measured, a 60 000-step fine run compiled nothing, retired zero JIT
> instructions and reported "identical", i.e. it compared the interpreter
> with itself. Hence the 64-cycle default.
>
> The fix is the 030 twin of `Emitter::commitQueue`/`guards`: either emit the
> resets per instruction, or refuse to compile for an 030 until they are.
> `TODO.md` § 3 already carries "Compact `mmu040InstrStart` — eight
> per-instruction field resets + a `getCCR()` pack" as a 68040 performance
> item; the 68030 has its own, and the JIT never learned it.

Then four more, all named in `JitBackend.h:38-56`:

1. **`(An)+` updates the register BEFORE the access on an 030, AFTER it on
   an 040** (`MoiraDataflow_cpp.h:326-332`). Every post-increment emitter
   needs a model-conditional order. This is the one that will bite silently:
   it only differs when the access faults or when the base register is also
   the operand.
2. **The 030 marks its last write restartable and stacks a format $A frame**
   (`:355-361`). The 040 does not. A compiled store that faults must leave
   the 030's frame, not the 040's.
3. **The prefetch queue is refilled at the end of an instruction** on one
   and not the other, so `queue.irc` means something different at a block
   exit. The block epilogue has to leave the queue in the state the
   interpreter would.
4. **`mmu040MovemArmed`** (the MOVEM restart latch, `PomJitLayout::movemArmed`)
   is 040 state. The 030's MOVEM restart contract is its own; until it is
   modelled, MOVEM is refused on an 030 rather than emitted — coverage, not
   correctness.

Plus **B.2/B.3**: the i-cache charge, which is 030-only and has no 040
counterpart at all.

**C.5 — flip the declaration.** `guestFamilies |= kGuest68030` in x64 once
C.1-C.4 are green, then the same walk on a64. Not before: the declaration is
the thing that stopped the 2026-07-30 wedge from recurring, and flipping it
early converts a coverage gap into a wedged guest.

**C.6 — the full-boot gates.** `jit_lcii_boot_etalon` on x64 (this is the
gate that timed out at one hour on 2026-07-30 — it is the phase's exit
criterion), then `jit_lcii_sys7_etalon`, `jit_lcii_soak_etalon`,
`jit_lcii_persist_etalon`. Then the other 030 platforms, one per family:
Sonora (`lc3`), VASP (`iivx`), RBV (`iisi`), MSC (`duo230`), IIfx. Each is a
different `*Cpu` wrapper with its own peripheral pacing, and the IIfx and
the Duo carry IOP/PMU firmware whose handshakes are phase-fragile
(`pom68k-mactv-gate-broken` — a 2 % instruction-rate shift deadlocks the
Mac TV; the same class of failure is live here).

**C.7 — the 020 question, deferred.** The Mac II family and the LC's `as020`
profile would be next. They are named here only so that "all the JIT
instructions for 68030" is not silently read as including them. The 020 has
no MMU, so `pomJitProbeData` is trivial there — but the measured JIT worth on
a 68020 guest is ×1.0-1.2 (`CLAUDE.md`), so it is not obviously worth a
family declaration. Decide with a number after Phase D, not before.

---

## Phase D — the default engine

> **The requirement:** the fastest *conformant* LLE mode must be the
> default. If the JIT wins on a family and is proved bit-identical on that
> family, the JIT is what a user gets without setting anything.

Today `defaultEngine()` returns `Interp` unconditionally (`JitConfig.h:44`)
and the engine is off everywhere — GUI, headless and CTest. That default is
a statement about *evidence*, not about speed, and it is the right one until
the evidence exists per family. This phase produces the evidence and then
changes the default.

**D.1 — the evidence bar, fixed in advance.** A (guest family, backend) pair
becomes a default only when all four hold:

1. a per-instruction lockstep gate on that family is green (`clock`,
   registers, stacks, RAM window — and the i-cache counters on an 030);
2. that family's boot etalons are green **under the JIT**, with fingerprints
   identical to the interpreter's;
3. `jit_bench` / `jit_bench_lcii` at a fixed cycle budget show the JIT
   faster, with matching fingerprints;
4. the whole `-L etalon` tier is green with the new default in force.

**D.2 — the shape of the default.** `defaultEngine()` stops being a
constant and becomes a per-family answer, resolved where the CPU wrapper is
constructed (it is the only place that knows its own guest family and its
selected backend). `POM68K_CPU_ENGINE=interp` becomes the documented opt-out
and stays the thing every accuracy claim can fall back to.

**D.3 — the blast radius, which is the real work.** Flipping the default
changes what ~87 `etalon` gates are testing. Today `jit_*` gates set
`POM68K_CPU_ENGINE=jit` explicitly and the rest run the interpreter; after
the flip, the plain gates run the JIT and the *interpreter* is what needs an
explicit tier. The gate registration in `CMakeLists.txt` has to be
re-derived accordingly, and `docs_test` (which since 2026-08-09 fails on any
unlabelled gate) is the thing that will catch a row that got missed.
This is done **last**, in its own commit, behind a full `-L etalon` run —
not folded into a phase that also changes emitters.

**D.4 — say so in the GUI.** The Machine/CPU menu shows which engine is
running and, when it is the JIT, which backend. A user who is running a
different engine than they think is the failure mode this whole plan is
trying to avoid.

---

## Where this stands, 2026-08-10

| phase | state |
|---|---|
| 0 — measure | **done**, `POM68K_JIT.md` § 3.1bis |
| A — 68040 tail (x64) | **done**, −12.8 % wall, `ctest -L jit` 23/23 |
| A2 — a64 mirror | **blocked**, needs an arm64 host; a64 stays conservative |
| C.1 — 030 lockstep gate | **done**, `jit_lockstep_030_test` (+ blocks variant) |
| C.2 / C.3 — 030 probe + thunks | **written, unvalidated**; their gate is C.5 |
| B — emitted i-cache | **written, unjudgeable** behind C.4's blocker; a placement correction is already identified |
| C.4 — per-instruction contract | **open, and it is THE blocker** — `mmuExecuteStart`'s resets |
| C.5 / C.6 — declare + boot gates | not started |
| D — default engine | not started; nothing in it may move before C |

Everything written for the 68030 is unreachable in any shipped
configuration: `guestFamilies` still excludes `kGuest68030`, so the code
above runs only under `POM68K_JIT_UNSAFE_BACKEND=1`. That is deliberate —
it is the mechanism the tree already has for developing a family a backend
does not claim yet, and it is why work in progress here cannot wedge a user's
machine.

## Gates this plan adds

| Gate | Label | What it proves | Phase |
|---|---|---|---|
| `jit_lockstep_030_test` | `jit` | LC II, threaded ≡ interpreter per instruction, i-cache counters included | C.1 |
| `jit_lockstep_030_x64_test` | `jit` | LC II, x64 ≡ interpreter per instruction | C.1 / C.5 |
| `jit_lcii_boot_etalon` (x64) | `jit` | the 030 code generator boots a real Finder | C.6 |
| `jit_lc3_/iivx_/iisi_/duo230_/iifx_boot_etalon` | `jit` | one per 030 platform | C.6 |
| existing `jit_lockstep_x64_fine_test` | `jit` | every Phase A emitter, per instruction | A |

Everything else in phases A, B and D is proved by gates that already exist.

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
  `POM68K_CPU_ENGINE=jit` alone selects `threaded`, and that trap has
  already published one wrong number (`TODO.md` § 3).
* **Never a boot etalon for a timing comparison** — use `jit_bench` /
  `jit_bench_lcii` at a fixed cycle budget and compare fingerprints.
* **On spec/oracle conflict, the oracle wins.** For anything the 68030
  manual and WinUAE disagree about, `oracle/` decides.

---

## The i-cache reading — SETTLED 2026-08-09

`PomIcache` today is a **timing overlay** paired with `cacheBoost_ = 4`:
the core runs at 4× machine rate and pays `icacheMiss_` per miss. It is
honest about being a fudge (`Cpu030.h:124-147`) and it is *not* an
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
rather than a drift — its reopening condition is a 68030 timing oracle worth
believing, which does not exist today.

**Also decided 2026-08-09 (Phase D):** the default flips **per family, at the
end**, each family only once its own lockstep + boot etalons + fixed-budget
bench are green (D.1's four conditions), in a commit of its own behind a full
`-L etalon` run. The 68040 families may therefore flip before the 68030 ones.
No default changes before Phase A is done.
