# POM68K JIT — design, invariants, journal

A **second execution engine**, living beside the Moira interpreter and never
in front of it. It is the default on the fully proved 68040 family **and,
since 2026-08-18, on the 68030 one** (`auto` selects the native AArch64 or
x86-64 generator on both families), and remains opt-in on the 68000/68020 guests. The
**CPU** menu switches it live;
`POM68K_CPU_ENGINE=interp|jit` overrides the family policy. The interpreter
remains what every accuracy claim in this project rests on and has its own
explicit etalon registrations.

Read `extern/moira/POM68K_VENDOR.md` § *JIT seam* (row 22 of the patch
table, twelve numbered points plus 3b) for the extension this subsystem
needs inside the vendored core.

**On the name (updated 2026-08-10).** The portable `threaded` backend is NOT
a JIT: it is the interpreter running behind a fetch window, with a block
replayer on top. The x86-64 and AArch64 backends emit machine code and are
JITs in the strict sense; `auto` selects one of them for the default 68040
path when the host supports it. Their exactness is why the wins are bounded
(§ 7). The GUI
says "Moteur accéléré", distinguishes "JIT `<backend>`" from
"fenêtres (threaded)" and names the backend (`GuiShell.cpp:322-357`, gauge
window `drawJitWindow()` at `GuiShell.cpp:204-268`);
the subsystem keeps its internal name because `src/jit/` names the seam and
the machinery, which a future non-conformant fast mode
(`docs/HLE_OVERLAY.md`) would build on. The five relaxations a classic 68k
JIT makes and this engine refuses — coarse time, coarse interrupts, a big
soft TLB instead of exact ATC semantics, lazy flags (the exact compact form
is now a measured opt-in prototype, § 3.8), long traces — are catalogued in the CHANGELOG's 2026-07-28 eighth-pass
entry and in `TODO.md`.

**State.** Three backends. `threaded` replays a recorded block through Moira's
own handlers with the fetch window armed, and is valid for every guest.
`x86-64` (§ 7) generates host code for a real subset of the ISA, with an
inline data TLB (§ 8) and control transfers compiled as block terminators,
so a loop closing on itself never returns to the engine; on an x86-64 host
`auto` picks it for the 68040 and 68030 machines and it beats `threaded` on every
regime measured (§ 3.4). `aarch64` covers the same 68040 *family* — with a
broader current opcode subset than x64 (§ 7): register and memory ALU,
MOVE/MOVEA, effective addresses including brief indexing and direct or
memory-indirect full-index sources, bit tests/bitfields, internal branches,
calls/returns, LINK/UNLK, MOVEM and immediate or guarded register-count
shifts, backed by an inline big-endian DTLB and exact per-instruction
fallback. Five-million-step
fine/coarse locksteps and the complete Q605 Finder boot are green, so `auto`
selects it on AArch64. Release/native/LTO measures
1.22 s against 4.55 s for threaded on the fixed 1,000-frame Q605 workload
(3.73x, identical fingerprint); LLVM PGO measures 1.01 s against 3.41 s.
The complete Finder gate is 9.19 s native against 21.14 s threaded (2.30x),
or 7.86 s against 15.28 s under PGO (1.94x).
The current non-LTO Apple-M4 reference (2026-08-23) runs the fixed
3,000-frame Q605 budget in **3.24 s median**, or **15.43x real time**, after
the Rogue-driven opcode pass described in § 3.5ter. Three-pair ABBA runs
measure 29.24 s for its paired interpreter and 15.13 s for threaded (whose
paired interpreter is 29.49 s); every arm finishes at fingerprint
`778dd7ad558108fd`.
All are bit-exact against the interpreter — registers,
supervisor stacks, cycle clock and the low 2 KB of guest RAM, compared at
every instruction boundary.

**Where the engine is wired.** **Every** CPU wrapper in the tree — twelve
of them: `Cpu040`, `CentrisCpu`, `Q630Cpu`, `Q700Cpu` (68040), `Cpu030`,
`RbvCpu`, `SonoraCpu`, `VaspCpu`, `MscCpu`, `IIfxCpu` (68030, plus the
Macintosh LC's 68020 flavour of `Cpu030`), `Cpu020` (the Mac II family,
68020 or 68030 depending on `is030`) and `Cpu68k` (the compacts). Each
passes its `GuestFamily` bit to the `jit::Engine` constructor — grep
`jit::kGuest` in `src/*.cpp` for the roster. The last four wrappers landed
on 2026-08-06. Both code generators declare 040+030 correctness through
`guestFamilies`. Their narrower `autoFamilies` speed policy is per backend
and they no longer agree: AArch64 carries 040+030 since 2026-08-20 with the
measured 68030 profitability score of 64; x86-64 carried 030 from
2026-08-21 at score 0 until the promotion was **withdrawn on 2026-08-29**
by the first whole-tier 030 run on that host (its
−12.6 % was measured without an admission score, and a score is adopted
per backend on measurement only — measured on x64 the same day:
`x64@score=0,x64@score=64` ABBA at 6000 frames gave −0.8 %, inside this
host's 1.0 % floor, so x64 REFUSES the score its sibling earned). Thus
`auto` gives a 68030 native code on both host ISAs (§ 7).

**And what each is worth.** The engine being wired is not the same as the
engine being worth switching on. Ranked by measured gain (§ 3.4):

| Guest | Machines | Window buys | Because |
|---|---|---|---|
| 68040 | Quadra 605/610/650/700/800/900/950, Centris, Q630 | **×5.0** on a fixed budget (x64, § 3.4); ×2.68 end to end on `q605_boot_etalon` (2026-07-31, § 3.4) | an ATC walk per fetch, replaced by a bounds check |
| 68030 | LC II family, Sonora, VASP, RBV, **IIx/IIcx/SE-30**, **IIfx**, Duo | **×1.21** portable baseline (LC II, fixed budget, threaded); `auto` now uses the separately gated native generator on both ISAs (a64 −5.3 %, x64 −12.6 % over `threaded` at the default budget) | same fetch win, plus native replay |
| 68020 | Macintosh LC, **Mac II** | ×1.0-1.2 | no MMU to skip — only the map decode |
| 68000 | **Plus, SE, SE FDHD, Classic** | ×1.03-1.08 | no MMU *and* the cycle accounting must be kept (§ 3.1) |

The last two rows are the honest reason the 020/000 seams took until
2026-08-06 to exist: they were never going to pay much, and saying so is
worth more than a number that flatters the subsystem.

**The 68030 row has now been three different numbers, and the third is the
first one taken on a fixed budget.** ×1.6 (before the ATC bit-exactness
capping of 2026-07-28), then "neutral, the window is worth nothing on the
LC II" (2026-07-30, retracted the ×1.6), and now ×1.21 — measured with
`jit_bench_lcii` over 4.17 G machine cycles with identical fingerprints,
where both earlier figures came from `lcii_boot_etalon`, which stops at the
Finder and therefore times the two engines over different amounts of guest
work. What changed the sign back is block linking and the 2026-08-06 seam
work, not the window itself. § 3.6 prices what is left.

---

## 1. Why multi-target from day one

POM68K is multiplatform. A JIT that only exists on x86-64 would make the
emulator behave differently depending on the host, and would make every later
architecture a rewrite rather than an addition. So the engine is split in two
layers with a hard boundary:

```
  layer 1  jit::Engine        block discovery, code window, invalidation,
                              cycle budget, fallback policy, gauges
                                    │  jit::BlockIr  (host-neutral)
  layer 2  jit::Backend       compile(IR) → CompileResult ; run(Compiled, Context)
                                    │
        threaded  ─────  x86-64 (J2)  ─────  aarch64
        portable        native code          native code
```

`threaded` needs no code generation and is therefore **always** compiled in
and **always** usable — Emscripten, a hardened kernel that refuses executable
pages, an architecture nobody has written a code generator for yet. That is
what makes "the JIT" a portable feature rather than an x86 feature.

`POM68K_JIT_BACKEND=auto` walks `kEntries` (`JitBackend.cpp`, ordered native
generators first, portable replay last) and takes the first entry that is
`usable()` on this host **and** valid for the guest CPU family. Validity
comes before ranking, and that ordering is the whole point of
`caps().guestFamilies` — see § 7's scope box for what it cost to learn.
`threaded` is usable and valid everywhere, so the loop always terminates.

`jit::BackendCaps` is the growth path, and it is worth being precise about
what it does and does not drive today:

| field | who reads it | effect |
|---|---|---|
| `guestFamilies` | `selectBackend()` | a backend is only a candidate for the families it declares; an explicit `POM68K_JIT_BACKEND=` naming an invalid pair is refused, not honoured (`POM68K_JIT_UNSAFE_BACKEND=1` overrides) |
| `maxBlockInstrs` | `Engine::Engine` | caps `POM68K_JIT_BLOCK_MAX` |
| `nativeCode` | `blockCacheEnabled()`, `hotThreshold()` | the block cache's default (§ 3.7) and the hot threshold (1 vs 512) |
| `dtlbCodeMask` | `Engine::fillDtlb` | may the engine hand out a WRITE entry for a 4 KB page holding translated code in some 256-byte slice? Only a backend that tests `PomJitDtlbEntry::codeMask` before storing (§ 8). Defaults false = the old whole-page refusal |

The per-opcode question is **not** a block boundary: `Backend::canEmit()` is
an encoding-only answer — consulted by the opcode census
(`POM68K_JIT_HISTO`), by `jit_backend_test`, and by `A64Backend::compile()`
as the dispatch test in front of `emitRegInstr` (`JitBackendA64.cpp:2604`;
the x64 emitters carry their own switch and never call it). The coverage
floor is a separate thing and is not built on it: `compile()` counts what
the emitter actually produced and refuses a block below
`POM68K_JIT_MIN_NATIVE` percent native — 50 by default
(`JitBackendX64.cpp:3384`). Anything an otherwise-compilable block contains
that the backend cannot emit becomes a per-instruction cold stub inside the
generated code, not a shorter block. Block termination is the classifier's
job alone (§ 4).

---

## 2. Invariants

Each one names the gate that would catch it breaking.

| # | invariant | gate |
|---|---|---|
| 1 | **The interpreter is the reference.** Any divergence between engines is a JIT bug, never an interpreter bug. | `jit_asset_free_lockstep_test` is the daily native floor; `jit_lockstep_test` (five registrations, six on AArch64 — § 5), plus its 68000 and 68030 twins (§ 3.2), widens it to real machines |
| 2 | **Exits happen at instruction boundaries only.** No partial guest state — registers, CCR, PC, clock — ever survives a block exit. Everything unusual (interrupt, trace, STOP, breakpoint, MMU fault, an opcode outside the classifier) is handed back to `Moira::execute()` at a clean boundary. A replay of `FlagMayTrap` also verifies that PC remained on the recorded straight line; an internal DIV/CHK exception ends the block with Moira's vector PC and queue intact. | `jit_asset_free_lockstep_test` compares 768 generated boundaries plus restart/last-write and divide-zero frames; `jit_lockstep_x64_fine_test` widens that to a machine one cycle at a time |
| 3 | **The fastest proved conformant engine is the default, per guest family.** Today that is native `jit/auto` for 68040 and 68030 on AArch64/x86-64, and the interpreter on 68000/68020. `POM68K_CPU_ENGINE=interp` always restores the oracle. | `jit_backend_test` pins the policy and both overrides; ten `interp_*_boot_etalon` registrations keep one interpreter reference per accelerated platform — q605/centris650/q630/q700 and lcii/lc3/iivx/iisi/iifx/duo230 |
| 4 | **Peripheral time stays owed.** Blocks never run past the caller's cycle target (`Context::clockTarget`) and generated cycles go through the machine's virtual `sync()` (`pomJitSync`), so VIA, ASC, SWIM and the Egret/Cuda MCU keep their pacing. | `jit_mactv_boot_etalon` — registered for exactly this reason: Tinker Bell's Cuda transport deadlocks on a 2 % shift in MCU pacing long before a Finder signature would fail |
| 5 | **Nothing cached survives a change of the address map.** Overlay flips (`CodeGuard::invalidate()`), MMU/ATC changes (`blocksGen_` vs `Moira::pomJitMmuGen`) and cache-control writes (`didChangeCACR` → `flushAll()`) drop the block cache and the code window. | `jit_q605_boot_etalon` (the boot overlay flips in the first milliseconds); `jit_lockstep_test` |
| 6 | **No host knowledge above `jit::Backend`.** An architecture `#ifdef` outside `src/jit/backends/` or `JitCodeBuffer.cpp` is a design error. | `jit_backend_test` (its header states invariants 6 and 7 as its purpose) |
| 7 | **Every host POM68K builds for can run the JIT** — on `threaded` at worst. | `jit_backend_test` |
| 8 | **A memory access has one host-neutral contract.** Count, order, EA commit, restart/LASTWRITE phase and 040 cache eligibility live in `Instr::memory`; A64 and x64 derive the same `MemoryProofPlan`. An undescribed form is an interpreter fallback, never an opcode-local guess. | `jit_backend_test` checks the decoder/planner; `jit_asset_free_lockstep_test` checks RMW/postincrement/stack/fault boundaries; `jit_copyback_{write,bsr,pair}_040_test` executes the cache protocols |
| 9 | **An emitted opcode has one host-neutral meaning.** Family, ALU operation, width, direction, operands, condition and sub-operation live in `Instr::semantics`. A64/x64 select host instructions and admissible EAs; they do not own ISA dispatch decoders. | `jit_backend_test` checks representative overlaps and native coverage; `docs_test` rejects a backend-local line dispatch or ALU decoder |
| 10 | **The 68k cost and EA-admission model is written once** (2026-08-28). Cycle columns, the CMPA surcharge, full-format prices and the `EaPlan` admission wrapper live in `JitCost.h` / `JitEaPlan.h`; a backend reads them and keeps only emission. A cost cell or admission predicate transcribed into `backends/` recreates the drift that made the two generators two projects. | `jit_backend_parity_test` sweeps both `canEmit` over the whole opcode space against a dated exception table; `docs_test` § 9 rejects a backend-local cost table, EA decode or D1F0 predicate |

---

## 3. Where the time actually goes (and what the window fixes)

On the 68040 machines, Moira has no prefetch queue: `mmu040InstrStart`
(`MoiraExecMMU_cpp.h`) fetches the opcode at `pc` and the lookahead at
`pc + 2` **through the MMU, on every single instruction**, and `readExt`
fetches each extension word the same way. Every one of those is:

> ATC probe (`mmu040Translate`) → virtual `read16()` → the machine's address
> decode (`Q605Memory::read16` and friends) → a byte-swapped load.

The **code window** (`Moira::PomJitWindow`) replaces that chain, for
instruction fetches only, with a bounds check and a two-byte load out of a
host pointer into the guest RAM/ROM buffer. That is J1a, and it is where the
first and largest win came from.

Two consequences worth stating explicitly:

* The window points **into the guest memory buffer itself**, so a guest write
  is visible to it immediately. Self-modifying code needs no invalidation
  here — what goes stale is the *recorded block* and the generated code,
  which is what `jit::CodeGuard` protects. Since 2026-08-22 the guard's
  byte per 256-byte slice is a mask of eight 32-byte sub-slices set from
  each block's OWN bytes (`Engine::blockSpan`: footprint + the two copied
  prefetch words, not the window span), `note()` trips only when a write
  covers a marked sub-slice, and `serviceGuard()` evicts only the blocks
  the written range intersects. A write into the neighbourhood of a block
  changes nothing it was translated from; on the idle System 7 Finder
  those near-misses were 21 M trips per 30 000 frames, each re-recording
  the whole slice — 609 s → 119 s on the a64 30 000-frame bench, fp
  identical, and the Quadra 630 persist gate halved with it.
* On the 68040 path Moira's `SYNC(x)` macro expands to nothing
  (`MoiraMacros.h`: `if constexpr (C != Core::C68020)`), so serving a fetch
  from the window changes **no** cycle accounting whatsoever. The window is a
  pure host-side saving.

### 3.1 …except on the compacts, where SYNC is real (2026-08-06)

The bullet above is a statement about `Core::C68020`, and that core covers
the 68020, the 68030 and the 68040 — every machine the engine reached until
2026-08-06. It does **not** cover `Core::C68000`. There `SYNC(x)` really
calls `sync(x)`, `MOIRA_PRECISE_TIMING` is on, and the Mac Plus is the one
family in POM68K whose timing claim is *cycle-exact* (`sst68000`, 1 000 058
vectors **with cycles**). A window that skipped the accounting there would
not be an optimisation, it would be a second, faster machine.

So `Moira::pomJitFetch000` replaces the **bus read and nothing else**. It
reproduces `read<C,PROG,Word,F>` step for step, in that function's own
order: the leading `SYNC(2)`, the address-error bail-out, the FC pins,
`POLL_IPL`, the machine's own bus model, the trailing `SYNC(2)`. Only the
`read16()` virtual and the machine's address decode are skipped.

That last item is why the seam is worth anything at all here:
`MacMemory::read16` is **two `read8()` switch dispatches** per opcode word.
It is also why it is not worth much — there is no ATC walk to skip, which
is where the 030/040 gains come from.

One thing had to be routed back in by hand. The Mac Plus charges
video/RAM contention wait states from *inside* `read16()`
(`Cpu68k::applyContention`), so a windowed fetch would silently stop paying
them. `Moira::pomJitSetBusStall` hands the wrapper's charge back to the
fetch path as a plain function pointer — not a virtual, because this sits
in the fetch path and a per-fetch virtual is the ~11 % the i-cache overlay
was folded inline to avoid.

**Measured** (`system_boot_etalon`, Mac Plus, System booting to the Finder;
adjacent A/B pairs ×2):

| | interpreter | JIT (`threaded`) |
|---|---|---|
| Mac Plus, System boot | 4222 / 4231 ms | 3903 / 3911 ms — **×1.08** |
| Macintosh Classic (ADB compact) | 8558 ms | 8326 ms — **×1.03** |

Small, and reproducible. The equivalence evidence is better than the speed
evidence: both engines print the **same** IWM health line — `polls 2332067,
hits 627914, overwritten 756519, nibbles 1380885` — and those counters are
cycle-sensitive. Same cycles, same guest, less wall clock.

**A footgun this seam found.** The first measurement of the compacts showed
*no* gain at all, and the reason was not the seam: the engine retired
**exactly 0 instructions**. Every other family drives its CPU through
`runCycles()`, but `MacFrameClock` subdivides a frame into 16 absolute
targets and calls `runUntil()` — which was still the plain interpreter path.
`POM68K_JIT_VERBOSE=1` now prints a retired/window/arms line at teardown so
"the engine is on" and "the engine is doing something" stop being the same
claim.

### 3.2 The cycle-exact lockstep, and the second time the same trap sprung

`jit_lockstep_68000_test` is the 68000 twin of § 5's harness, and it asks a
different question: not "same registers" but **same `clock`**, compared at
every checkpoint, alongside the VIA/IWM/drive/SCC state a cycle drift moves
first. 2.5 M checkpoints over ~646 M guest cycles (≈ 5 000 frames), coarse
at 256 cycles through the boot and dropping to one cycle for the last
100 000 — so the sharpest comparison lands in live Finder code rather than
in a boot loop. A `_blocks_test` variant forces the block path on.

`jit_lockstep_030_test` is the third machine class: two Mac LC IIs, 120 000
comparisons at 8192 cycles with a fine tail, and it adds the three
`PomIcache` counters to the comparison — the half a Quadra gate cannot have.
Same `_blocks_test` variant; plus one generated-code registration per host
ISA — `jit_lockstep_030_x64_experimental_test` on x86-64,
`jit_lockstep_030_a64_experimental_test` on AArch64 — each under an
explicit `POM68K_JIT_BACKEND` (no unsafe override since the 2026-08-18
declaration), the proof lane for the 030 emitters
(`docs/JIT_BRINGUP.md` § C).

Recorded blocks are safe here for a reason worth stating: the `threaded`
backend replays through `pomJitExecOne()` — Moira's own handlers, charging
Moira's own cycles — and never replays the recorded `Instr::cycles`. A code
generator doing that arithmetic itself would be wrong on this core, and no
architectural comparison would catch it.

**It is a gate only because the negative controls bite.** Both were run:

| what was broken | result |
|---|---|
| the contention charge deleted from `pomJitFetch000` | `DIVERGED after 982 402 steps — CLOCK DIVERGENCE, delta −8` |
| the trailing `SYNC(2)` deleted | `DIVERGED after 0 steps` (pc and D-registers immediately) |

**And the first version of this gate passed both of them**, over 2.5 M
checkpoints and 666 M guest cycles, because every one of its 83 M windowed
fetches came from **ROM** — RAM fetches, instrumented and counted, were
exactly zero. The cause is peculiar to this family: the 60.15 Hz VBL is not
raised by `MacMemory::tick()` the way every other platform's is, but by
`MacFrameClock::runFrame()` between two `runUntil()` calls. A harness that
only calls `runCycles()` never delivers one, so the machine spins in early
boot forever — overlay still up, low memory therefore ROM,
`Cpu68k::applyContention` (RAM only) never firing, and a missing charge
invisible. The harness now reproduces `runFrame()`'s shape and the gate
asserts its own reach: the overlay must have dropped and the IWM must have
been polled, or it fails. **Twice in one day, on the same subsystem, a green
result meant "nothing ran" rather than "nothing broke".**

### 3.3 The instrument, and the exit rate it exposed

**A boot etalon is a poor stopwatch** — it stops the moment it recognises
the Finder, so the two engines get timed over *different* amounts of guest
work and the ratio flatters whichever arrived first. Every number below
therefore comes from `tests/jit_bench.cpp` / `tests/jit_bench_lcii.cpp` plus
their 68000/68020 twins `jit_bench_plus` / `jit_bench_macii` (dev tools,
`EXCLUDE_FROM_ALL`, **not** CTest gates),
which run a **fixed guest-cycle budget**: the same instructions, the same
peripheral schedule, wall clock the only variable. `POM68K_BENCH_FRAMES`
sets the budget in frames of 416 667 cycles (Q605) or 640×407 = 260 480
(LC II). Each prints a fingerprint of the whole architectural state at the
end, and no number here was taken unless every engine printed the **same**
fingerprint.

**What still costs, and it is not code size.** Over 12.2 G instructions the
exit counters showed **794 M window-lost exits** on `threaded` — one window
death every ~15 instructions. Mac OS 8.1's VM ages pages by writing
descriptor U bits; every ATC eviction kills the derived window and TLB state
for that page (`Moira::pomJitAtcEvict`, and that is the exactness contract,
not a bug); the idle Finder lives under that regime. No conformant backend
escapes it. § 3.6 prices one such exit; it is worth about 3 %, which is much
less than the rate suggests.

### 3.4 The current numbers (2026-08-23)

The pre-2026-07-31 figures this file used to carry are superseded — they
predate three landed emitters, two cost-table fixes and the arm-time DTLB
flush deletion (§ 8), and § 10 records what changed between them. Same
instrument, same rule (identical fingerprints across engines), one budget:

**Current Apple-M4/AArch64 result, Quadra 605, 3 000 frames (1.25 G
machine cycles, 5.0 G core), three ABBA pairs per selected JIT after one
discarded warm-up pair, `fp=778dd7ad558108fd` in every arm:**

| engine | median wall | × real time | vs paired interpreter |
|---|---:|---:|---:|
| Moira interpreter | 29.24 s¹ | ×1.71 | — |
| Moira JIT Threaded | 15.13 s | ×3.30 | ×1.95 |
| Moira J.I.T Codegen A64 | **3.24 s** | **×15.43** | **×9.03** |

Codegen is ×4.67 over Threaded and retires 649,372,093 instructions, 99.7 %
native. ¹The interpreter median is 29.24 s in the Codegen ABBA and 29.49 s in
the Threaded ABBA; the corresponding arm spreads are 5.1/0.4 % and 4.6/0.0 %,
far below the measured 88.9 % and 48.7 % reductions. The fingerprint agrees
throughout; the direct final-tree runs also agree on SCSI count (1,324) and
terminal PC (`$0002528A`). The older x86-64 table below remains the
architecture-specific historical baseline rather than being silently
overwritten.

**2026-08-10 x86-64 baseline — Quadra 605, 3 000 frames (1.25 G machine
cycles, 5.0 G core, idle Finder),
`fp=5af1d47a9322bebf` on all three:**

| engine | wall | × real time | vs interpreter |
|---|---|---|---|
| Moira interpreter | 48.51 s | ×1.03 | — |
| JIT, `threaded` | 28.10 s | ×1.78 | ×1.73 |
| JIT, `x86-64` | **9.71 s** | **×5.15** | **×5.00** |

**Mac LC II, 6 000 frames (1.56 G machine cycles, 6.25 G core),
`fp=cfb184b6faddabec` on both:**

| engine | wall | × real time | vs interpreter |
|---|---|---|---|
| Moira interpreter | 50.27 s | ×1.98 | — |
| JIT, `threaded` | 41.63 s | ×2.40 | ×1.21 |

**2026-08-19 — the x86-64 generator overtakes `threaded` on the LC II**
(`POM68K_BENCH_ARMS=threaded,x64`, ABBA, 3 repeats/arm, quiet host, one
fingerprint per budget; `docs/JIT_BRINGUP.md` § C.4sexies has the causal
story):

| budget (frames) | `threaded` | `x86-64` | delta |
|---|---|---|---|
| 1200 | 8.80 s | 10.42 s | +18.4 % |
| 2000 | 14.30 s | 14.77 s | +3.3 % |
| 3000 | 21.21 s | 20.23 s | **−4.6 %** |
| 6000 (`fp=cfb184b6faddabec`) | 42.29 s | **37.22 s** | **−12.0 %** |

Below ~2500 frames the run is single-pass boot code and the compile
investment cannot amortize; a session sits far past the 6000-frame floor.
The C.5 flip is written (2026-08-19) as the per-backend
`caps().autoFamilies` speed declaration, separate from the `guestFamilies`
correctness one. It was **blocked for two days**: its first `-L m030` run
found the four IIsi gates in SIGSEGV under the generator (JIT_BRINGUP
§ C.4septies, parked with reproducer). The crash did not survive the
2026-08-19→21 hardening window, and the flip **fired on 2026-08-21** on
fresh evidence — 120k lockstep, all six IIsi gates green under `auto`,
`threaded` 41.19 s median vs generator 36.01 s at the default budget
(**−12.6 %**, fp `cfb184b6faddabec`, spreads 2.1/1.7 %) — so x86-64 `auto`
now serves an 030 generated code too. The first post-port AArch64
ABBA was only a statistical tie (`threaded` 20.18 s, a64 20.11 s, −0.3 %
inside a 3.0 % noise floor), so it was correctly not promoted then. On
2026-08-20 the native-state hardening retained i-cache/retirement counters
across linked blocks and applied the measured cold-code score of 64; the
6,000-frame fixed-budget comparison, long lockstep and native LLE platform
gates then cleared the independent AArch64 admission. Its `autoFamilies`
therefore carries 040+030, and the x64 mask joined it on 2026-08-21 on the
numbers above. Re-measured after
the current generated-loop compaction: three same-process ABBA repetitions
give `threaded` **19.93 s** median (19.85–19.94) and a64 **18.88 s**
(18.82–18.89), **−5.3 %** with 0.4/0.3 % arm spreads and fingerprint
`cfb184b6faddabec` unchanged — clear of the 3.0 % host floor.

The human fixed-cycle report also prints backend `compile()` attempts,
refusals, total host time and mean time per attempt. This separates a hotness
change that merely compiles less code from one that actually makes generated
execution cheaper; the architectural fingerprint remains the admission rule
for either comparison.

**2026-08-19 profitability-score experiment (AArch64/68030).** The opt-in
`POM68K_JIT_PROFIT_SCORE` makes a block earn compilation through
`visits × potentially-native instructions`, while `HOT` remains a minimum
visit count. Score 64 is the measured candidate. Direct same-process ABBA,
three repeats per arm and identical fingerprints:

| LC II budget | score 0 | score 64 | delta |
|---|---:|---:|---:|
| 1 000 frames | 4.31 s | 3.99 s | **−7.4 %** |
| 3 000 frames | 10.60 s | 10.26 s | **−3.3 %** |
| 6 000 frames | 20.31 s | 19.82 s | −2.4 %, provisional: below the 3 % floor and host-busy refusal |

At 1 000 frames it compiles 5 134 blocks instead of 81 573. Its 365 refused
attempts split into 7 context/IR and 358 coverage refusals; the refused work
costs only 0.63 ms against about 24.0 ms for accepted compilation. The gain
comes from not compiling cold code, not from making rejection cheaper. Score 64
also moves AArch64 from +19.5 % to +10.7 % behind `threaded` at 1 000 frames
and reaches a statistical tie (+0.2 %) at 3 000. It **does not become the
default**: the representative 6 000-frame evidence has not cleared the noise
and quiet-host gates.

The reports use the same JSON schema as CI, so the reviewed x86-64 floors
above can be replayed without scraping prose:

```bash
POM68K_PERF_HOST_PROFILE=reference_x86_64 \
  POM68K_JIT_METRICS_FILE=q605.json ./build/jit_bench
POM68K_CPU_ENGINE=jit POM68K_JIT_BACKEND=threaded \
  POM68K_PERF_HOST_PROFILE=reference_x86_64 \
  POM68K_JIT_METRICS_FILE=lcii.json ./build/jit_bench_lcii
python3 tools/check_jit_performance.py --require q605_jit \
  --require lcii_threaded q605.json lcii.json
```

Both fingerprints must still match their interpreter legs; the performance
checker judges the fixed workload and host row, not functional equivalence.

The one end-to-end figure the ranking table at the top of this file also
quotes, kept because it is the only one taken at the scale a user sees:
`q605_boot_etalon` to the 256-colour Finder, **61.3 s interpreted against
22.9 s on x64 — ×2.68** (2026-07-31). It is a boot gate, so it is not a
stopwatch (§ 3.3) — never quote it against a bench number.

**The 68030 i-cache is identical across engines, to the digit** —
1 602 507 733 fetches / 1 093 456 393 hits / 509 047 173 misses (68.23 %) on
both. That is not a coincidence to be grateful for, it is the ordering in
`mmuFetchWord` (`MoiraExecMMU_cpp.h:408-461`): the overlay is charged BEFORE
the window hook, and the threaded backend replays through Moira's own
handlers. It had never been measured; it is the premise a 68030 code
generator has to preserve by hand (`docs/JIT_BRINGUP.md` § B).

**Two cost-table cells were wrong, and the instrument that would have said so
was dead.** `Context::slowStaticHisto`/`slowRuntimeHisto` — the census that
separates "no emitter for this opcode" from "compiled, but its runtime access
bailed" — is written by the a64 backend and was **never written by the x64
one**, so `[jit] block fallback census` printed `0` on x86-64 for as long as
it existed, next to a `JitBackend.h` comment describing what it was telling
us. Wired 2026-08-10; it named both cells within one run:

* `kMoveDst[(xxx).W]` was 3 and is **2** — `execMove7`'s 68020 column is
  byte-for-byte `execMove2`/`execMove3`'s, because an absolute-short
  destination's extension word is already in the prefetch queue. This refused
  **every** `MOVE <ea>,(xxx).W`: 47.4 % of all block fallbacks.
* `CMPA` charges `kEaRead + 2`, not `kEaRead` — `execCmpa` holds a `SYNC(2)`
  that the `ADDA`/`SUBA` path takes only for a word or register source
  (`MoiraExec_cpp.h:2197` vs `:420-422`). This refused **every** `CMPA`: a
  further 12 %.

Fixing the two took the native share 96.2 → 97.6 % and block fallbacks from
16 475 202 to 7 387 537 (−55 %) on the 3 000-frame budget, fingerprint
unchanged; § 3.5's table carries the end state.

Both were coverage bugs and never correctness bugs — which is exactly what
cross-checking the table against the tracer's own measurement buys, and why
`jit_lockstep_x64_fine_test` stayed green through both. The lesson is the
other one: **an instrument that is wired on one backend and not the other
reports success on the backend that is not looking.**

### 3.5 The data path was the coverage tail, not the instruction set (2026-08-10)

With the census finally reporting, the ordered fallback list on a real
workload turned out to have almost nothing to do with the one § 7 predicted
(line-$E shifts, `Scc`, `PEA`, indexed modes). After the two cost fixes,
**57 % of all remaining fallbacks were two stack-push forms that compile
natively and then bail at RUN time** — `MOVEM.L regs,-(A7)` and
`MOVE.L (xxx).W,-(A7)`.

Splitting the data-TLB refusals by reason (`POM68K_JIT_VERBOSE`, new line)
named it in one run: **63 998 of 66 922 remembered refusals — 95.6 % — were
"this 4 KB page holds translated code somewhere"**. An entry maps 4 KB;
`CodeGuard` works at 256 bytes; 68k code shares its page with its own stack
constantly. Deleting the refusal *unsafely* measured the ceiling at
**-9.8 % of wall clock**, and moved the bench fingerprint — which is the
proof that the refusal was load-bearing and not merely conservative.

Two changes, both conformant:

* **`PomJitDtlbEntry::codeMask`** — 16 bits, one per 256-byte slice.
  Generated code tests it on the write path: two instructions and a branch
  that is not taken on any page with no code in it, and a cold precise
  slice test otherwise. A store that could be self-modifying still goes
  through the memory map; a store 3 KB away from the nearest block no longer
  pays for sharing a page with it. Backends declare
  `BackendCaps::dtlbCodeMask` before they are handed such an entry — a
  backend that stores without testing the mask would bypass the guard, so
  the default is the old whole-page refusal. Since 2026-08-20 the AArch64
  backend uses register-preserving `CBZ/CBNZ` fixups for **every** emitted
  store: a zero mask stays direct, while a real slice collision still takes
  the exact memory map and precise invalidation path. The former opcode-local
  `B592` exception and its bring-up knob no longer exist.
* **`markPages()` now flushes the data TLB**, which § 8's invalidation table
  has claimed it did since that table was written. It did not: only the
  1 → 0 direction in `serviceGuard()` ever flushed. A write entry filled for
  a page *before* it held code let stores past the guard undetected — a real
  self-modifying-code hole, not a coverage one. The flush is on a slice's
  0 → 1 transition only, so it stays far away from the unconditional
  arm-time flush § 8 deleted for costing 23-33 %.

Then two emitters from the original list, `PEA` and `Scc` (a flat 4 cycles
in the register form on the 020 column, and no destination READ in the
memory forms, so it is a single guest access).

**Phase A, end to end, on the 3 000-frame Q605 budget — fingerprint
`5af1d47a9322bebf` unchanged at every step:**

| | before | after |
|---|---|---|
| wall | 10.30 s | **8.98 s** (−12.8 %) |
| native share | 96.2 % | **98.5 %** |
| block fallbacks | 16 475 202 | **2 570 058** (−84 %) |
| …runtime (data path) | 5 088 852 | **308 748** (−94 %) |
| remembered dtlb refusals | 66 922 | **2 935** |

**What is left, and why it is not obviously worth doing.** The residual
2.6 M fallbacks are 0.4 % of retired instructions, led by `BTST` on a device
register (the cycle cross-check refusing an access whose cost is a wait
state — correct, and not fixable), a two-memory-operand `MOVE` (no access
thunk is allowed when a bail-out would re-run the first access), line-$E
shifts (~113 k) and the 68020 indexed modes (~167 k).

The indexed modes stayed OPEN at this checkpoint rather than being dropped,
and the reason was a limit of the instrument: **this workload does not
draw.** The idle Finder is
exactly where QuickDraw's blitters — the thing the indexed modes were
motivated by — are absent. Re-open with a census taken over a drawing-heavy
phase; do not close it on an idle-Finder number. That census led to the
brief-first x64 lowering on 2026-08-23; only the full format and the
instruction-specific indexed exclusions remain open.

### 3.5bis The drawing census, and what the idle Finder got wrong (2026-08-18)

That census exists now: `tests/q605_rogue_census.cpp` (dev harness,
`EXCLUDE_FROM_ALL`, **not** a CTest) boots the Q605 on Mac OS 8.1, mounts
`dev/mac-rogue`'s `Rogue.dsk` off the SWIM2, launches the game with the
keyboard and plays it for ~2 emulated minutes, calling
`Engine::censusPhase()` between stages so boot, idle Finder, launch, title
and gameplay are dumped as **separate** censuses instead of one cumulative
blur. It fails rather than reports when the game demonstrably never ran: an
idle census filed as a drawing census would be worse than no number.

**The `~167 k` figure above was an artefact of the reporting view, not of
the workload.** It came from the top-60 opcode list (2026-08-10); the
addressing rollup that sums *every* opcode landed two days later
(2026-08-12), and indexed modes fragment across hundreds of opcodes, so each
one is small and the total never appeared. On the same idle Finder the
rollup says **11.2 M**, 31 % of all block fallbacks — the largest single
category, and 67× the number this section quoted.

What the drawing phase actually changes is **magnitude and form**, not
share (x86-64 backend, one run, figures reproduced bit-identically across
three):

| phase | instrs | native | fallbacks | per instr | indexed | full-format |
|---|---|---|---|---|---|---|
| idle Finder (60 s) | 78.2 M | 92.6 % | 36.4 M | 0.47 | 30.7 % | 36.1 % |
| gameplay (~2 min) | 96.0 M | 91.4 % | 272.8 M | **2.84** | 28.8 % | **4.1 %** |

* **An idle Finder understates fallback pressure 6×.** Per retired
  instruction it is 0.47 against 2.84 — and boot (2.10) and app launch
  (3.63) sit with the drawing phase, not with the idle one. Every ratio § 3.5
  quotes is an idle-Finder ratio.
* **The share attributable to indexed modes barely moves** (31 % → 29 %), so
  the category was already the biggest one; drawing did not create it.
* **The form mix inverts.** Of the indexed fallbacks, the full 68020
  extension is 36.1 % at idle and **4.1 %** while drawing. The brief share
  is bracketed, not a point: **36 %** is the mass on opcodes whose every
  compiled site was brief, **96 %** is everything that is not full-only, and
  apportioning the 60 % seen both ways by each opcode's own slot ratio
  estimates **71.5 %**. All three bounds said the same thing — **the brief
  lowering § 7 now shares across A64 and x64 is aimed at the workload that
  needs it.**

The brief/full split is tallied in the *engine* (`Engine::recordIndexForms`,
per compiled slot) rather than in a code generator's cold stub. That costs
exactness — the census counts an opcode, and an opcode compiled both ways
can only be reported as `mixed`, which is why the 71.5 % is labelled an
ESTIMATE in the dump — and buys the thing § 3.4's lesson says matters more:
**both backends report it**, instead of whichever one had the counter wired.
Exactness would mean keying the census per compiled SITE inside both
generators' cold stubs; the 36-96 % bracket is what that change has to be
worth, and today it is not, because every point in the bracket decides the
same way.

#### 3.5ter Rogue re-census after the indexed/MOVEM pass (2026-08-23)

The old **29 %** is no longer a description of the current JIT. On the first
re-run, before the additions below, Rogue gameplay produced **102,852,551**
block fallbacks. Dynamic register bitfields `E9C5` and `EFC6` alone were
87,510,489 of them (85.08 %); every indexed form combined was 6,549,343,
only **6.37 %**. The earlier brief-index and MOVEM work had already changed
the workload enough that the 2026-08-18 ratio had to be retired.

The recensus then drove each lowering, in descending measured order:

* all eight dynamic register bitfields; read-only memory bitfields with an
  optional fifth byte, then `BFEXTU d16(An)`;
* direct and memory-indirect full 68020 EAs for `LEA`, `JMP`, `JSR`,
  register-destination `MOVE` and read-only ALU sources. Pointer and operand
  reads are separate IR accesses and the pointer mapping must prove plain
  before generated code reads it;
* full-direct MOVE sources, the dependent `MOVE.L (A7)+,(A7)` case, and a
  guarded dynamic-shift specialization. A shift site checks `Dn & 63` before
  any effect; a changed count replays the untouched instruction through
  Moira. The original Rogue ceiling is eight. Speedometer 4 later justified
  sixteen for logical shifts only; AS/RO retain eight.

The final identical 445/468-key drawing run reports **278,660 unsupported +
255,754 exact runtime guards = 534,414 fallbacks**, down **99.48 %** from the
first re-run. Indexed fallbacks are **107,414**, down **98.36 %** in absolute
terms; their now-small residual is 79.5 % brief, 13.5 % full and 7.1 % mixed.
The 2.90 M checkpoint immediately before the last full-indirect/shift pass
took 57.33 s; the final retained set takes 56.97 s.

Two tempting changes were measured and rejected. A free-running dynamic
cycle charge passed the synthetic lockstep but prevented the real Q605 from
reaching Finder, so only the guarded fixed-count form remains. Admitting
brief indexed MOVE destinations removed another 83 k fallbacks but changed
Rogue CPU time from 56.19 to 56.80 s on average (**+1.1 %**), so it is not a
production default. No native VRAM copy/fill/mask specialization was added:
the census did not identify one as a remaining dominant cost. This is also
why QuickDraw HLE is not the next step; improving its instructions and plain
framebuffer accesses accelerates QuickDraw and direct-framebuffer games
without replacing either guest algorithm.

#### 3.5quater Speedometer 4 separates the CPU mix (2026-08-30)

`lcii_speedometer_census` drives the existing GISTPERSO volume through
`Logiciels/Speedo402/Speedometer 4.02`, cancels the printer and registration
dialogs, then runs only `Performance Rating / CPU`. Completion is detected by
the result window's two black seven-segment panels around its light modal,
not by an arbitrary sleep. Both engines reach it after exactly 270 frames,
with CPU fingerprint `ce2e6699cc81f501`, screen `be29f2c8d37f6bb3`, SCSI
traffic and the visible CPU score `1.053` identical. The uninstrumented phase
takes 0.303736 s on A64 against 1.288846 s in the interpreter: **4.243x** for
the generated engine on this exact application benchmark.

The first CPU census caught three logical register shifts at one site group:
`E8A8`, `E4AC`, `E2AD`, each traced at base cost 22. Their dynamic base is 6,
so all carry count 16 and were refused solely by Rogue's count-eight code-size
ceiling. A shared cost-policy constant now lets logical shifts specialize to
16 on both generators while AS/RO remain at eight. The pre/post 600-frame
census moves unsupported fallbacks 330,241 → 317,894; the three opcodes'
11,979 executions disappear, and both guest fingerprints stay exact. One
instrumented wall pair moves 1.117377 → 1.095146 s (−1.99 %), which is recorded
as a signal only, not a speed claim without a repeated same-binary control.

On the exact 270-frame post-change phase, 2,284,402 / 2,298,679 instructions
are native (**99.4 %**). The next honest coverage target was multiplication:
`C7FC` 7,965 + `C9C0` 3,988 + `C2FC` 3,161 = **15,114**, 13.3 % of all
113,652 fallbacks and 21.0 % of the unsupported subset.

The subsequent audit corrected an important timing premise. Moira's
operand-dependent `cyclesMul()` applies only to the 68000/68010; the
68020/68030 use a fixed per-EA word-multiply table. Shared IR and cost policy
now expose `MULU.W`/`MULS.W` to both generators. They produce the 32-bit
product, N/Z, cleared V/C and preserved X; memory forms use the exact sole-read
contract, including a one-read MMIO witness. The directed 68030 lockstep runs
1,536 native multiplications with zero slow instructions and identical state.

The recensus reports 55,174 unsupported + 41,782 runtime fallbacks = 96,956,
down 16,696 (**14.69 %**) from 113,652; all three named multiply opcodes are
gone and the CPU/screen fingerprints remain exact. An eight-run same-binary
ABBA measures medians 0.307857 s OFF and 0.306770 s ON (**−0.35 % signal**),
below the control arm's 2.18 % span. This is a coverage result, not a promoted
speed claim. The temporary attribution switch was removed before landing.

The next census separated two superficially similar fallbacks. `24D0`
(`MOVE.L (A0),(A2)+`) always reads `$50F06060`, the V8 SCSI pseudo-DMA FIFO.
Its terminating BERR can follow a partial longword transfer, so an exact
thunk followed by replay would risk consuming bytes twice; it deliberately
stays on Moira. `4EB9`, by contrast, is single-path `JSR abs.l`: three encoded
words, three linear 030 i-cache fetches, fixed base cost 4. A64 and x64 now
admit exactly that shape, keep the live target-word read, and reject stack/
target alias before reversing the proof/read/push sequence. The 68030 gate
patches the target after compiling both d16(An) and abs.l callers; both remain
native and exact. `4EB9` disappears from the real CPU fallback table, moving
unsupported 55,174 → 49,476 in that sample with fingerprints unchanged.

The following slice closes that separate restart-state proof for full-
indirect `4EB0`. Pointer-read and stack-write mappings are both preflighted
before the plain-RAM pointer is consumed; the final target is checked for
oddness and stack overlap, its first program word is read live, and only then
is the proved push committed. Any unprovable mapping or fault replays the
untouched instruction, leaving Moira to build the exact 030 restart frame.
The shared i-cache predicate admits indexed JSR only when its traced fetches
are the encoded words plus computeEA's one final refill. The real CPU census
moves 49,476 → 37,290 unsupported and 89,908 → 79,380 total fallbacks;
`4EB0` has no unsupported row and only 87 observed fill/tag runtime replays.

The next three rows looked like ordinary missing MOVE/BTST emission but were
the opposite. `0829`, `1029` and `1429` traced at 59–129 base cycles although
their fixed costs are small; a temporary entry-boundary address probe placed
their reads at the LC II's `$50F0xxxx` device aperture (`$1000`, `$1200`,
`$1400`, `$1600`, `$1A00`). The variable part is live peripheral delay.
Their 68030 memory contracts are therefore `exactRequired`: A64 and x64 call
the exact read thunk even if a host mapping exists, while their emitted tails
charge only the fixed opcode cost. A directed oracle adds 23 cycles inside
every synthetic MMIO read and proves exact PC/queue/SR/register/clock state
with zero instruction fallback. The real 270-frame census moves 37,290 →
27,751 unsupported and 79,380 → 69,874 total fallbacks; all three rows vanish
with the CPU and screen fingerprints unchanged. The instrumented 0.310871 s
wall time is not a speed claim.

The following `D981`/`9381` cluster is register `ADDX.L`/`SUBX.L`, not an EA
or variable-timing problem: both are one word and cost two cycles. Shared
`AddSubExtend` semantics now cover register B/W/L on both generators, with X
as carry/borrow, C=X and cumulative `Z'=Z&&result==0`. X64 lowers through
width-exact ADC/SBB; A64 uses a 64-bit intermediate so byte/word carry and the
long `source+X=2^32` edge remain exact. Predecrement memory forms deliberately
remain separate because their two-access fault contract exposes An updates.
The directed oracle retires 4,915 such instructions natively with no slow
path. The real census moves 27,751 → 20,065 unsupported and 69,874 → 62,188
total fallbacks; all encountered register forms disappear and the four
fingerprints remain unchanged.

The next two large rows, `6000` (`BRA.W`) and `4EF9` (`JMP abs.l`), were
already understood by both generators; only the broad 68030 multiword-
control guard kept them out. The mode-5 Moira path proves the missing
fetch-address contract. `BRA.W` fetches exactly `pc,pc+2`, `BRA.L` fetches
`pc,pc+2,pc+4`, and both change PC only after consuming that linear stream.
Simple `JMP` d16/abs.W/abs.L/PC-d16 does the same; abs.L uses
`SKIP_LAST_RD`, leaves its low address half in IRC and performs no target
fetch because the 68030 `fullPrefetch` is a no-op. Their costs are fixed
(10 for wide BRA, 4 for abs.L JMP in the observed forms). A shared
`provedLinearControlFetch030` predicate now admits exactly those shapes on
A64 and x64; conditional Bcc keeps its path-specific proof, and indexed JMP
remains outside.

The directed 68030 gate now arms CACR and the real i-cache overlay, then
executes `BRA.W`, `BRA.L` and `JMP abs.l` from a cold cache. Each transfer
enters compiled code without increasing `slowInstrs` and matches Moira's
PC/IRD/IRC, fetch, hit, miss and clock state. All four real 030 locksteps
remain identical. In the new 270-frame CPU census neither `6000` nor `4EF9`
has an unsupported row; the secondary simple-JMP row `4EFA` is gone too.
The sampled mix moves 20,065 → 16,145 unsupported (**−19.5 %**) and 62,188
→ 59,247 total fallbacks (**−4.7 %**), while all four fingerprints, 270
frames, 2,577 SCSI commands and halted=0 remain exact. The next dominant
static row is now `E9D4`, followed by `C029`.

`E9D4` is not a shift but Speedometer's dynamic `BFEXTU (A4){…},D0`.
Its base cost is fixed at 19; its observed 19/23/27 totals differ only by
030 i-cache misses. The shared IR already described the hard part: a
longword plus an optional fifth-byte read under `PreflightAll`. A64 consumed
that contract behind `POM68K_JIT_030_MEMBF`; x64 stopped at the tailless
subset. X64 now mirrors the two-probe transaction: it proves both possible
mappings before the first load, skips the second probe when runtime
offset+width ends at bit 32, and combines the optional byte only on the tail
path. Its memory-bitfield body also cross-checks the fixed action/EA timing
table instead of accepting an arbitrary traced cost. Five-byte writes remain
undescribed because a first committed store could not be replayed after a
late tail failure.

The 030 admission is now on by default on both generators; explicit
`POM68K_JIT_030_MEMBF=0` remains the attribution/veto arm. The directed gate
executes Speedometer extension `0963`, a signed negative-offset tail and a
runtime no-tail arm with zero slow instructions on native A64 and native x64
(the latter executed under Rosetta, not merely compiled). Four real 030
locksteps remain identical. In the 270-frame CPU census every read-only
`E9D0…E9D6` row disappears: unsupported falls 16,145 → 10,483
(**−35.07 %**) and total fallbacks 59,247 → 53,586 (**−9.55 %**), with all
four fingerprints unchanged. `C029` is now the largest static CPU row.

`C029` also demonstrates why a fallback count is not an admission proof.
The two Speedometer sites are `AND.B d16(A1),D0` at displacements `1A00` and
`1C00`, with 75–103 observed base cycles around a fixed cost of 7. Three
increasingly narrow exact-thunk experiments passed a synthetic delayed-MMIO
lockstep yet changed the real benchmark from its exact 270-frame path to 450
frames and changed CPU/screen state. They were all removed: the present
thunk does not prove the access phase this device observes, so `C029` stays
with Moira.

The next row, `41F6`, has no such ambiguity. It is the full-format
postindexed `LEA ([bd.W,A6],D6.L),A0` (`6925`): a sole direct-RAM pointer
read, fixed base 16, with only i-cache misses above it. `43F0` is the same
family at fixed base 18. On the cacheless 030 both generators now preflight
the pointer mapping, read the longword, finish the postindex and commit An
last; a miss/MMIO address replays the pristine instruction. A directed
`41F6` oracle stays wholly native on A64 and x64, and all four real 030
locksteps remain identical. The 270-frame recensus removes both LEA rows:
unsupported falls 10,483 → 9,408 (**−10.25 %**) and total fallbacks 53,586 →
52,303 (**−2.39 %**), with all fingerprints unchanged. `C029` remains the
largest static row; `2470`/`2070` are the next structural candidates.

Those two MOVEA rows expose a stronger but still transactional form of the
same idea. Full-indirect `MOVE.L`/`MOVEA.L` to a register performs a pointer
longword read and a final operand read under the IR's sequential
`PreflightAll` contract, then commits Dn/An last. On the cacheless 030, A64
and x64 now require both tokens to be preflightable and non-exact, disable
thunk/cache doors for the pair, and replay the pristine instruction if
either direct mapping refuses. The directed `2470`/`2070` oracle changes the
RAM pointer result to delayed MMIO after compilation and remains exact on
native A64 and x64. The production-profile 270-frame census removes those
rows and their sibling longword-to-register forms: unsupported 9,408 → 5,286
(**−43.82 %**), total fallbacks 52,303 → 48,982 (**−6.35 %**), 2,226,797 /
2,235,488 native (**99.61 %**), with all four fingerprints, frames and SCSI
count unchanged.

The next pair, `2191`/`31A9`, turned out to be BRIEF indexed destinations,
not full-format ones. Their fixed base costs leave a five-cycle destination
formation after the ordinary source read. Both generators already prove the
source and destination mappings before that read in the two-memory
`PreflightAll` body; the 030 cost gate now accepts the indexed cell only for
that memory-source transaction. A destination refusal therefore replays
before source, flags or write effects escape. The directed oracle moves the
destination to MMIO after compilation and matches every callback boundary on
native A64 and x64. The 270-frame census removes the pair and its independent
EA siblings: unsupported 5,286 → 3,910 (**−26.03 %**), total fallbacks 48,982
→ 47,768 (**−2.48 %**), 2,224,199 / 2,232,870 native (**99.61 %**), with the
four fingerprints and SCSI count unchanged.

The next static row, `4C00`, is Speedometer's exact `MULU.L D0,D4` selector
`4004`: unsigned 32-bit result, fixed base cost 43 and no memory access. The
low result alone is written, but this is not the word multiply's ordinary
logic-flag tail: V reports whether the full unsigned 64-bit product has a
non-zero high half. A64 and x64 therefore compute that full product, write
its low half to D4, derive N/Z from the low half, set V from the high half,
clear C and preserve X. Admission remains exact-extension-only; signed,
64-bit-result and other-register MULL forms still replay through Moira.

The directed 030 oracle starts below overflow, crosses into V=1 and compares
256 queue/cycle checkpoints with 254 generated block runs and zero slow
instructions on native A64 and native x64 under Rosetta. All four real LC II
030 locksteps remain identical. The production-profile recensus removes the
351 static refusals: unsupported 3,910 → 3,559 (**−8.98 %**) and total
fallbacks 47,768 → 47,414 (**−0.74 %**); 2,224,131 / 2,232,758 instructions
are native (**99.61 %**). The four fingerprints, 270 frames, 2,577 SCSI
commands and halted=0 remain unchanged.

The next shift audit separates one useful fixed form from three misleading
dynamic rows. `E410` is exactly `ROXR.B #2,D0`, fixed cost 12. A64 and x64
now lower only that ROX opcode: X enters the byte as a ninth rotate bit, the
last outgoing bit becomes both C and X, N/Z describe the final byte, V clears
and D0's upper 24 bits survive. Its directed 030 loop matches 256 boundaries
with zero slow instructions on native A64 and x64; all four real 030
locksteps remain identical and the 116 static census refusals disappear.

The nearby `E0A9`, `E2AB` and `E4A4` rows are dynamic LSR/ASR at observed
counts 24, 27, 28 and 31. An opcode-scoped extension of the unroll ceiling
passed both native oracles, but the real recensus turned 382 static refusals
into 350 count-guard replays and removed only 32 total fallbacks. The block
cache keeps one specialization per PC, so widening the body cannot cover a
site whose count changes. That experiment was removed; a multi-version cache
would be the relevant mechanism. With only E410 retained, one sampled phase
reports 3,439 unsupported + 43,930 runtime = 47,369, but its instruction mix
also differs slightly from the prior sample, so only E410's absent 116-row is
attributed. The four fingerprints, 270 frames and SCSI count remain exact.

### 3.6 What one window exit actually costs (2026-08-09)

§ 3.3's exit count was a **rate with no price**: 794 M exits over 12.2 G
instructions says how often the window dies, not what a death costs. That gap
is what let `TODO.md` § 0·A price a relaxed ATC at "+10 to 30 %" — an estimate
resting on an unexamined intuition. `POM68K_JIT_WINDOW_KILL=N` (§ 6) removes
the intuition: it kills the window every N retired instructions on purpose, so
the price is the **slope** of wall time against the exit count the run
reports. The fingerprint must not move with N, and does not.

**LC II, `threaded`, 4.17 G machine cycles — the clean case**, because the
threaded backend has no block cache: the only thing N changes is how often
the window is re-armed.

| forced kill | window-lost exits | wall |
|---|---|---|
| none | 91 192 756 | 110.86 s |
| every 64 | 115 523 243 | 112.02 s |
| every 16 | 188 513 571 | 115.93 s |
| every 8 | 285 834 265 | 120.67 s |
| every 4 | 480 477 013 | 127.44 s |

**42.9 ns per exit** (R² = 0.990, pairwise 42.6-52.1 ns). The 91.2 M natural
exits therefore cost **3.9 s of 110.9 s — 3.5 %**, and a soft TLB that never
lost a window at all would return ×1.037. Not ×1.1 to ×1.3.

**Quadra 605, `x86-64`, 5 G machine cycles — the corroboration, with a
caveat.** Here the instrument perturbs more than the re-arm: forcing kills
collapses the block cache (258 398 blocks compiled with no kill, ~89 150 with
any), so the no-kill point sits in a different regime from the four kill
points and must not be fitted with them. Over the four kill points, where the
regime is stable, the slope is **120.4 ns per exit** (R² = 0.961) — three
times the threaded price, which is what a backend that must also re-enter
generated code should cost. Applied to this run's **30.4 M natural exits**
(one per 98.5 instructions, six times rarer than on the LC II): **3.66 s of
115.57 s — 3.2 %.**

Two backends, two guest families, per-exit prices a factor of three apart,
natural rates a factor of six apart — and the same answer to the question that
matters: **eliminating window loss entirely is worth about 3 %.** The
"+10 to 30 %" in `TODO.md` § 0·A was an estimate built by multiplying a real
rate by an intuition, and it is retracted there.

The same pass measured what the *host* side of a frame costs, since a
CPU-only bench cannot be compared with a ratio seen on screen. Running the
GUI's own quantum — `POM68K_BENCH_SLICES`, N slices per frame with a raster
catch-up at each boundary, which is what `runQuantumWithWire` does (1 slice
with the network off, 64 with AppleTalk on) — costs **2.2 %** on the LC II,
and 64 slices cost no more than one:

| LC II, 16 000 frames | CPU alone | 1 slice + raster | 64 slices + raster |
|---|---|---|---|
| interpreter | 134.33 s ×1.98 | 137.47 s ×1.94 | 136.25 s ×1.95 |
| `threaded` | 110.86 s ×2.40 | 113.79 s ×2.34 | 113.74 s ×2.34 |

(At 64 slices the fingerprint legitimately differs — slicing moves the
interrupt boundaries, so the guest runs a marginally different path for the
same machine-cycle budget. Within a slice count the engines still agree.)

Superseded measurements, and what changed between them, are in § 10.

### 3.7 Why the block cache is OFF for the portable backend

Because it measured **slower** than the window alone there, and the reason is
structural rather than a tuning failure. Moira re-fetches `ird` itself in
`mmu040InstrStart` and dispatches on it, so a recorded block does not change
*which* handler runs — it only lets the engine batch bookkeeping and verify.
That is strictly more work than running the same instructions in the window
loop, so `blockCacheEnabled()` takes its default from the ACTIVE backend: off
for `threaded`, on for anything that generates code, which has nothing at all
to run without blocks.

### 3.8 Four classic JIT levers, implemented before being believed (2026-08-23)

The four obvious WinUAE-style levers were tested against the conformant
boundary rather than promoted from intuition.  Three have complete opt-in
implementations and one already existed as the window/native admission tier:

| Lever | Exact implementation | Q605 3,000-frame ABBA result |
|---|---|---|
| deferred/liveness CCR | A64 x26 / x64 R15 carry `XNZVC` in bits 0..4 and the retired count above bit 8; every helper, fallback and exit materialises the architectural bytes | **+5.8 % wall**; A64 coverage 99.6 → 99.4 % because the first proof deliberately cold-stubs shift-register/bitfield flag cases |
| local Dn/An cache | A64 x27/x28 cache at most two read-only registers selected per block; every linked entry reloads them from canonical memory, and the Engine option fixes one chain-wide stack ABI | **+0.1 % wall** (neutral inside noise) |
| exact edge cells | each constant edge embeds a stable, collision-free target cell; publish/retract updates that cell and invalidation can never leave a dangling entry | block runs 1,080,044 → 877,751 and block-end exits 959,158 → 755,974, but **+0.5 % wall** |
| two-tier admission | the existing fetch window is tier 0 and native code is tier 1; `HOT` and profitability score decide promotion | `HOT=2` is **+8.3 % wall** versus `HOT=1`; scores 4/8/16 are all slower |

All ABBA arms retired 649,372,093 instructions and produced fingerprint
`778dd7ad558108fd`.  The first three implementations remain available as
attribution/proof knobs but default OFF; the measured production answer is
still eager architectural CCR, canonical guest registers, the direct link
table and immediate native promotion.  Keeping a correct but slower
experiment behind an immutable Engine option is useful; silently making the
default slower is not an optimization.

---

## 4. Block discovery: tracing, not decoding

A block is recorded **by executing it**. `Engine::record()` runs instructions
one at a time through `Moira::pomJitExecOne()` and writes down where each one
started and how far the pc moved. Consequences:

* no second 68k decoder to keep in sync with Moira — instruction lengths fall
  out of the pc deltas;
* a recorded block can never describe something that did not happen;
* variable-length instructions and extension words come for free;
* each instruction's cycle cost is the interpreter's own answer, recorded
  rather than modelled (`Instr::cycles`) — which is what a code generator
  must agree with before it may emit anything (§ 7).

Replay re-verifies, every instruction — the four guards at the top of
`ThreadedBackend::run()`: the clock budget, `pomJitIdle()` (no pending
interrupt, trace, STOP, breakpoint), that `getPC()` equals the recorded pc,
and that the pc is still inside the window. The pc check is the catch-all —
an unpredicted trap, a fault redirect or code rewritten under us all land
there and exit the block cleanly.

A block ends **before** the first instruction that `jit::classify()`
(`JitIr.h`) calls `Kind::Unsafe`, and **after** the first one it calls
`Kind::Branch`. The classifier runs at block-build time only, so it is
explicit rather than clever, and conservative by construction: anything not
proven safe is `Unsafe`.

**`Unsafe`** means it can change the MMU translation, the ATC, the caches or
the supervisor bit — all of which would silently stale the code window — or
it is a transfer of control the block builder cannot model. Today that is:
the `$4Exx` group *except* the carve-outs below (so RTE, RTD, TRAP, TRAPV,
RTR, RESET, STOP, MOVE USP, MOVEC are all out); `MOVE`/`ANDI`/`ORI`/`EORI`
to SR/CCR and `MOVE` **from** SR to memory; `MOVES`, `CAS`/`CAS2`,
`CMP2`/`CHK2`; `TAS` (a locked RMW — it sets `mmu040Lrmw`, so its read
translates with write semantics); `BKPT`; `TRAPcc`; the whole A-line; and
the whole F-line (FPU, PFLUSH/PTEST, CINV/CPUSH, MOVE16). Everything else is
already caught by the replay checks.

**`MOVE SR,Dn` is the one carve-out out of that SR group** (`JitIr.h:1378-1381`,
2026-08-12): on a 68010+ it is privileged, so a successful trace is
necessarily supervisor mode, and it changes no mapping and no execution
state. It is `Kind::Alu`, a read-only block member. A memory destination
(`MOVE SR,<ea>`) stays `Unsafe` — conservative, and `jit_backend_test` pins
both halves. Only the AArch64 backend emits it natively; x86-64 takes the
cold stub.

**`Branch`** is the block's **terminator**, and part of it. The set grew
twice, each time for a measured reason recorded in `JitIr.h`:

| terminator | since | why |
|---|---|---|
| `Bcc`/`BRA`/`BSR` | J1 | the target is a compile-time constant, so a backward branch into its own block becomes an internal jump and the loop never returns to the engine — most of what a code generator is for here |
| `DBcc` | J1 | same |
| `JSR <ea>`, `RTS` | 2026-07-28 | 7 % of a real Mac OS workload, and every one of them was both an interpreter round trip AND a block boundary the linker could not cross |
| `JMP <ea>` | 2026-07-30 | 0.66 % of the idle Finder in the census; a terminator simpler than `JSR` (no stack push). **Plain EA modes only** — `(An)`, `d16(An)`, `(xxx).W/.L`, `d16(PC)`. Indexed data EAs are now lowered, but indexed control flow stays `Unsafe` until its dynamic target/queue contract has a dedicated proof |

`LINK`, `UNLK` and `NOP` are carved out of `$4Exx` as ordinary
straight-line `AddrCalc`: they transfer no control and touch no SR/MMU/cache
state, they are 3.6 % of a real Mac OS workload, and they sit at every
function entry and exit — which is exactly where straight-line code begins.

A branch's length cannot be read off the pc delta the way every other
instruction's can (it jumps), so `branchWords()` reads it off the encoding
instead.

---

## 5. The working loop

Do not iterate against a bare `ctest` — 218 gates on the A64 development host,
hours, and `-j` is unsafe
because the boot etalons are contention-sensitive. Do not iterate against a
bare `make` either: tree-wide LTO relinks ~90 binaries after any core change.

The first answer after a JIT edit is the native, asset-free tier. It builds
five small binaries, runs 768 deterministic interpreter/native checkpoints
plus restart/last-write fault frames, executes generated cache protocols,
checks 384 precise one/two-slice guard evictions, checks the IR/profile
contracts and runs the documentation/configuration gates.
CI sets `POM68K_JIT_REQUIRE_NATIVE=1`, so A64/x64 selection or W^X failure is
red rather than a soft skip. Each gate has a 45-second ceiling; bounded
fallback/native-share checks and a fixed-cycle native/interpreter ratio are
the deterministic budgets. Their reviewed numbers live in
`performance_budgets.tsv`; CMake refuses a missing or malformed row and
injects the values selected by workload, guest family and host profile
into the gates. The same asset-free binary emits
`pom68k.jit.metrics.v1`; both CI hosts validate it with
`tools/check_jit_performance.py` and archive the JSON artifact. This daily
floor is still synthetic. Representative policy is separate: the fixed-cycle
`q605_jit`/68040 and `lcii_threaded`/68030 x86-64 baselines are versioned from
the measurements in § 3.4. The Apple M4 (`Mac16,10`) rows come from repeated
immutable-clone runs on 2026-08-17: Q605 ×9.50–9.67 and LC II `threaded`
×4.96–5.02. The 68000/68020 workloads intentionally have no invented
threshold yet.

`POM68K_JIT_BACKENDS=auto` compiles the generator matching the target
architecture. `threaded` is the portable control build; `x64` and `a64`
select one generator explicitly. The explicit forms are also how an Apple
Silicon developer produces a real x86-64/Rosetta proof instead of letting
the host processor reported by CMake choose AArch64:

```bash
cmake -S . -B build-x64-jit -DCMAKE_OSX_ARCHITECTURES=x86_64 \
  -DPOM68K_NATIVE=OFF -DPOM68K_JIT_BACKENDS=x64
```

```bash
cmake --build build -j4 --target jitfast
POM68K_JIT_REQUIRE_NATIVE=1 ctest --test-dir build -L jit-fast --output-on-failure
```

Then run the machine-level lockstep:

```bash
make -j4 jitdev && ctest -L smoke     # ~2.5 min end to end
```

`jitdev` builds the three binaries `-L smoke` needs (`jit_backend_test`,
`jit_lockstep_test`, `q605_boot_etalon`) — the other registrations re-run
those same binaries under different environments. `-L smoke` is **eight**
gates: `jit_backend_test`, five flavours of `jit_lockstep_test`, and the
q605 boot etalon **on both engines**; on an AArch64 host with
`POM68K_JIT_BACKENDS=auto` a sixth lockstep flavour
(`jit_lockstep_a64_coarse_test`) joins the tier, making it nine.

`jit_lockstep_test` is the one that matters: two Quadra 605 machines from one
ROM and one read-only disk image, one interpreted and one JIT-driven,
compared at every checkpoint on every architectural register, the supervisor
stacks, the cycle clock — **and the first 2 KB of guest RAM**. That last one
is not decoration. A JIT bug in a STORE shows up in a register only much
later, when something reads the byte back; the 68k system globals live in low
RAM and are written constantly during a boot, which makes them a cheap, high
yield tripwire. Its five registrations — six on AArch64 — exist because each
covers something the others cannot:

| gate | what it pins |
|---|---|
| `jit_lockstep_test` | the default backend at one cycle per comparison — the sharpest possible check, but a block can never be more than one instruction long |
| `jit_lockstep_blocks_test` | `threaded` + `POM68K_JIT_BLOCKS=1` — the portable floor with its block path forced on |
| `jit_lockstep_x64_test` | the code generator at 256 cycles per comparison — long blocks, and a loop closing on itself entirely inside generated code |
| `jit_lockstep_x64_fine_test` | the code generator at one cycle per comparison |
| `jit_lockstep_noaccess_test` | x64 + the conservative data path (`POM68K_JIT_ACCESS_THUNK=0`) |
| `jit_lockstep_a64_coarse_test` | the arm64 generator at 50 cycles per comparison, 5 M comparisons — **AArch64 hosts only** (`cmake/Pom68kJitGates.cmake:255-265`), which is also why it is the one smoke gate an x86-64 developer never sees |

Two things this gate learned the hard way, both worth keeping in mind when
extending it:

* **It has to release the Cuda reset hold.** Until 2026-07-28 it did not, so
  it spent its whole budget in the power-on self test, where every access is
  an I/O register. It reported the data path green over 768 million cycles
  having never performed a single data-TLB fill.
* **A coarse budget only says which 256 cycles diverged.**
  `POM68K_JIT_LOCKSTEP_FINE_AT=<step>` drops to one cycle per comparison for
  the last stretch, which names the instruction; the report also prints the
  last eight instruction boundaries, because a divergence is almost never at
  the pc it is noticed at.

Widen only when the smoke tier is green:

| command | gates | when |
|---|---|---|
| `ctest -L jit-fast` | 7 | native A64/x64 lockstep/IR/protocol + docs/config, asset-free |
| `ctest -L unit` | 108 | legacy non-etalon tier on AArch64 (not synonymous with asset-free) |
| `ctest -L jit` | 40 | before proposing a JIT change (`jit-fast` matches the regex too) |
| `ctest -L m040` | 51 | the 68040 family — the JIT's blast radius |
| `ctest -L etalon-core` | 12 | one profile per platform, ~32 min — the pre-commit tier |
| `ctest` | 228 | this AArch64 host; 230 in the cross-host union |

(Counts from `ctest -N` on 2026-08-26, on an **AArch64** host with
`POM68K_JIT_BACKENDS=auto`. Five gates are host-conditional: the AArch64
coarse/030-experimental/030-alignment trio and the x86-64 030 experimental +
alignment pair. Re-derive with `ctest -N` rather than arithmetic; `m040` and
`etalon` are host-independent.)

Labels are derived from test names in `cmake/Pom68kGatePolicy.cmake`, so a
new gate is classified the moment it is registered. The derivation merges and
de-duplicates any explicit `LABELS` a registration already set.

---

## 6. Environment surface

Everything in `JitConfig.h` unless noted. Product startup captures the whole
surface once in `RuntimeConfig::jit()` and injects that immutable snapshot
through every CPU wrapper into its `Engine`. Standalone gates capture the same
keys at their test boundary (`tests/JitTestConfig.h`) and inject the snapshot;
all twelve wrappers and `Engine` require it by const reference. Unit fixtures
which deliberately want deterministic policy must name
`defaultResolvedConfig()`; there is no nullable constructor parameter or
implicit fallback. The selected backend then resolves its dependent defaults
(blocks/hot).
`Context::config` publishes the same policy during compilation, so neither the
engine nor A64/x64 reads a live process environment. Later environment changes
do not affect an injected session.

| Variable | Default | Meaning |
|---|---|---|
| `POM68K_CPU_ENGINE` | 68040+68030 `jit`, others `interp` | explicit `interp` or `jit` overrides the per-family default (the GUI menu still switches live) |
| `POM68K_JIT_BACKEND` | `auto` | `auto` \| `threaded` \| `x64` \| `a64` |
| `POM68K_JIT_PROFILE` | `production` | coherent bundle: `production` = current fast conformant defaults; `conservative` = whole-instruction replay, no links/native 040-line shortcuts, paranoid revalidation; `instrumented` = production access paths plus paranoid validation, histograms and 040 native-hit counters, with links off. A leaf variable below always overrides the profile |
| `POM68K_PERF_HOST_PROFILE` | host architecture | stable performance-policy identity written to metrics; use a named measured host such as `reference_x86_64` or `apple_m4` for wall-clock baselines, never one architecture-wide threshold |
| `POM68K_JIT_METRICS_FILE` | unset | write the flat `pom68k.jit.metrics.v1` JSON artifact used by CI and fixed-cycle benches |
| `POM68K_JIT_UNSAFE_BACKEND` | `0` | force an explicitly named backend onto a guest family it does not declare (`JitBackend.cpp`) — for developing that family's support, never for use |
| `POM68K_JIT_FETCH` | `1` | the instruction-fetch code window (J1a) |
| `POM68K_JIT_BLOCKS` | *backend* | block discovery and replay (J1b). The default is the ACTIVE BACKEND's answer, not a constant (`blockCacheEnabled(dflt)`): OFF for `threaded`, which measured slower with blocks than with the window alone, ON for a code generator, which has nothing to run without them |
| `POM68K_JIT_BLOCK_MAX` | `64` | straight-line instruction ceiling per block, itself capped by `caps().maxBlockInstrs` |
| `POM68K_JIT_HOT` | native `1`, threaded `512` | visits before a recorded block is translated |
| `POM68K_JIT_ARM_BACKOFF` | `32` | single-stepped instructions after an `armWindow()` refusal before the next arm attempt (0-4096). Measured 2026-08-23 at 30 000 LC II frames, single runs, same binary: 32 → 94.2 s, 8 → 90.5 s, 4 → 92.9 s, 1 → 93.1 s, native share rising monotonically (85.6 → 88.2 %) while wall does not. A streak-growing backoff (1, 2, 4 … 32) gave 93.0 s / 88.2 % on the 030 with identical locksteps — and **diverged the two 68040 locksteps** (`D1` differs at the first coarse boundary): when the window is armed is NOT architecturally invisible on the 040, so the constant stays (CHANGELOG 2026-08-23 (fourth)) |
| `POM68K_JIT_LINKS` | profile | direct block-to-block linking for native backends; on only in `production` unless explicitly overridden |
| `POM68K_JIT_PACKED_CCR` | `0` | conformant deferred-CCR prototype (§ 3.8): keep `XNZVC` beside the generated retired count and materialise it at helper/exit boundaries. Emitted by A64 and x64; A64 lockstep-proved and measured **5.8 % slower**, so not a production default |
| `POM68K_JIT_REG_CACHE` | `0` | A64 per-block cache of up to two read-only Dn/An values in x27/x28. Linked targets reload from canonical guest state and all blocks share the option-selected frame ABI. Lockstep-proved, performance-neutral, hence opt-in |
| `POM68K_JIT_EDGE_CELLS` | `0` | constant branches use exact stable dependency cells rather than the colliding direct table; target publication/invalidation updates the cell in O(1). It removes 202,293 outer block runs on the fixed Q605 workload but measures 0.5 % slower; dynamic targets retain the table |
| `POM68K_JIT_DYNAMIC_BITFIELD` | `1` | production admission on both native generators for register bitfields and TAILLESS memory forms whose offset and/or width come from Dn; `0` is the exact attribution control. Rogue's measured mask loop is dominated by this form; `jit_backend_test` locks the resolved default and the asset-free oracle locks all eight dynamic register actions to zero fallback on A64/x64 |
| `POM68K_JIT_A64_PACING` | `1` | AArch64 inline peripheral deadline/batch test; `0` calls `sync(cycles)` after every emitted instruction for attribution |
| `POM68K_Q605_EVENT_SCC` | `1` | Q605 carries serialized SCC time debt to its exact event/MMIO boundary; `0` restores per-`tick` stepping for A/B attribution |
| `POM68K_Q605_EVENT_SCSI` | `1` | Q605 carries serialized 53C96 latency debt to its exact IRQ/MMIO/pseudo-DMA boundary; `0` restores per-`tick` stepping |
| `POM68K_JIT_ICACHE_EMIT` | `1` | ATTRIBUTION knob for the emitted 68030 i-cache charge (`docs/JIT_BRINGUP.md` § B). Off, an 030 block charges the instruction cost alone, so a residual divergence provably belongs to something else. Only a bring-up measurement should turn it off |
| `POM68K_JIT_MAX_BLOCKS` | `65536` | blocks kept before the engine STOPS RECORDING (it does not flush — a flush is what a code generator cannot afford) |
| `POM68K_DATA_WINDOW` | `0` | the INTERPRETER's data window (§ 8) — opt-in since the ATC-exactness capping made it a net loss (`JitConfig.h:150`, the door at `JitEngine.cpp:90-104`) |
| `POM68K_JIT_PARANOID` | profile | re-validate the translation at every arm; off in `production`, on in `conservative`/`instrumented` |
| `POM68K_JIT_VERBOSE` | `0` | backend selection, block dumps and flush chatter on stderr — **plus a retired / window-covered / arms / failed line and a dtlb-refusals-by-reason line at teardown**, which is how you tell "the engine is on" from "the engine is doing something" (§ 3.1) |
| `POM68K_JIT_VERBOSE_BLOCKS` | `40` | how many compiled blocks the dump prints under `POM68K_JIT_VERBOSE`. The dump is the only place a block's MEASURED per-instruction cycles are visible, and 40 only ever reaches ROM reset code — raise it to diagnose a refusal deep in a boot |
| `POM68K_JIT_ACCESS_THUNK` | profile | 0 = whole-instruction fallback, 1 = loads, 2 = loads and stores; `production`/`instrumented` use 2, `conservative` uses 0 |
| `POM68K_JIT_040_LINE_READ` / `_WRITE` / `_PAIR` | profile | native 040 D-cache line proofs; on in `production`/`instrumented`, off in `conservative`. `_PAIR` admits only the two IR contracts with a dedicated atomic-pair gate |
| `POM68K_JIT_040_LINE_STATS` | profile | native read/write proof counters; on in `instrumented`, off otherwise |
| `POM68K_JIT_HISTO` | profile | dynamic opcode/fallback census; on in `instrumented`, off otherwise (`JitEngine.cpp dumpHisto()`) |
| `POM68K_JIT_REQUIRE_NATIVE` | unset | gate-only: abort Engine construction if explicit selection/W^X resolves to `threaded`. Native builds set it on every `jit_*` 68030 boot gate, so an engine-only green run cannot masquerade as code-generator coverage; CI also uses it for the asset-free protocol tier |
| `POM68K_JIT_WINDOW_KILL` | `0` | **measurement instrument, not tuning**: kill the code window every N retired instructions on purpose, so the price of ONE window-lost exit is the slope of wall time against exit count (§ 3.6). A kill is architecturally invisible, so a bench fingerprint must not move with N — that is what makes the fit a measurement rather than a story. Slows the engine down by design |
| `POM68K_JIT_MIN_NATIVE` | `50` | percent of a block's instructions a generator must emit natively before the block is kept; below it the block is refused and the fetch window runs instead. **Sweeping it is a measurement, and the measurement says leave it alone**: 0 % more than doubles native residency and costs **37 %** of wall clock on the 68030, everything from 50 up is one flat plateau (`docs/JIT_BRINGUP.md` § C.4ter). A fallback inside a block pays a call, a frame and a boundary commit; on the window it pays a dispatch |
| `POM68K_JIT_PROFIT_SCORE` | `0` | experimental native compile gate; 0 preserves the current policy. Otherwise a recorded block must satisfy `visits × potentially native instructions >= score` in addition to `POM68K_JIT_HOT`. This lets long/native loops compile before short single-pass blocks; it is a measurement knob until repeated 1 000/3 000/6 000-frame ABBA evidence earns a default |
| `POM68K_JIT_RESTART_BASE` | per-backend | admission (030): the restartable-write family on the split BASE cost instead of the traced total. Its historical coarse-budget divergence was the peripheral-phase class, CLOSED 2026-08-21 by the access-thunk clock bias (JIT_BRINGUP § C.4nonies). **Default follows the backend's `caps().accessClockBias` declaration — ON under x64 since 2026-08-22 (−4.3 % alone, −8.0 % with BSR.W at 6000 frames, fp identical) and under a64 since the same afternoon (its thunks carry the bias, replacing the `guardIcacheHits` replay); `threaded` declares none and needs none**; an explicit 0/1 wins either way. Both emitters consult it since the evening of 2026-08-22 (a64 had the total-cost rule hard-wired, refusing every push traced on an i-cache miss — native share 49 → 71 % at 30 000 frames once wired). `jit_lockstep_030_x64_alignment_test` / `jit_lockstep_030_a64_alignment_test` pin both admissions at 120k; `jit_backend_test` pins the declaration coupling |
| `POM68K_JIT_BSRW` | per-backend | admission for BSR.W (`$6100`) into the armed-charge exemption. Charge proved correct (`fetchWords=2`); its step-16 097 divergence was the same peripheral-phase class, closed by the same fix. Same per-backend default and same gates as `POM68K_JIT_RESTART_BASE` (−2.3 % alone at 6000 frames) |
| `POM68K_JIT_030_MEMBF` | `1` | admission (030): memory bitfields through `(An)`/`d16(An)` on both native generators; explicit `0` is the attribution/veto arm. Sole reads use the exact-thunk timing contract; possible fifth-byte reads preflight both mappings before either load and branch around the tail at run time. TAILLESS BFCHG/BFCLR/BFSET/BFINS consume the shared read4/write4 RMW proof and a writable translation before the read; five-byte writes remain undescribed everywhere. A mispriced form refuses. Promoted 2026-08-31 after native A64+x64 tail/no-tail oracles, four real 030 locksteps and the exact Speedometer census; the explicit alignment gates remain. Earlier stakes: SimCity `E9D0`/`EFD1`; promotion witness: Speedometer `E9D4` |
| `POM68K_JIT_030_CACR_FLUSH` | per board | Three-valued since 2026-08-19 (68030 wrappers). **Unset = the board's own answer**: retired on the V8, whose store inventory is proved complete (`V8Memory::kJitStoreInventoryComplete` — every store into RAM passes `CodeGuard::note()`, pseudo-DMA included), honoured on VASP/RBV/MSC, whose inventories are not. `1` forces the hint back ON (prices it on a proven board: −21.8 % of generator wall clock, `docs/JIT_BRINGUP.md` § C.4bis); `0` forces it OFF — read by the V8 wrapper only (`Cpu030.cpp:85`): VASP/RBV/MSC always flush on the CI/CEI strobes and ignore the knob, so their unproven inventories cannot be un-flushed from the environment. Compare fingerprints on both sides or the number means nothing |
| `POM68K_JIT_DISPATCH_RING` | `0` | diagnosis: record the engine's last 8192 dispatch decisions (path, pc, clock, target, exit, instructions) in a ring the 030 lockstep dumps on divergence. The 2026-08-19 uncharge hole was invisible in every end state and named by this ring in one run |
| `POM68K_JIT_WATCH_OPCODE` | unset | diagnosis (a64): `<hex>[,<hex>…]`, up to four opcodes — when the compile loop hands one to the fallback stub, print its admission inputs (trace/base/i-cache cycles, fetch count, terminal queue, semantics, memory proof plan) once per pc, tagged with the stage or the emitter check that refused it (`jsr:queue`, `movem:cost`, …). Turns a fallback-census row into WHICH check, without guessing from the source (2026-08-23) |
| `POM68K_JIT_DENY_FROM` / `_TO` | unset | bisection instrument (hex pc range): refuse to COMPILE any block whose entry pc falls in [from, to). Halving the pc space is how a divergence that heals under every pacing perturbation gets pinned to one block |
| `POM68K_BENCH_ARMS` | unset | `jit_bench_lcii`: `<a>,<b>` runs TWO ENGINE arms head-to-head in the same ABBA process (each side `interp` or a backend key, applied via `POM68K_JIT_BACKEND` before that arm's machine is built). Modifiers bind Engine-resolved knobs per arm: `@score=N` (profitability, e.g. `a64@score=0,a64@score=64`), `@restart=0\|1` and `@bsrw=0\|1` (the § C.4nonies admissions, e.g. `x64,x64@restart=1@bsrw=1`). Unset = the historical interp-vs-jit comparison. Exists because separate processes reintroduce the variance `bench::compare` removes |
| `POM68K_BENCH_FRAMES` | q605 `3000`; others `6000` | the four `jit_bench*` harnesses — fixed **machine-cycle** frames: 130 240 (Plus), 261 120 (Mac II), 260 480 (LC II), 416 667 (Q605) |
| `POM68K_BENCH_SLICES` | `0` = CPU alone | `jit_bench_lcii` only: N ≥ 1 runs the GUI's own quantum, N slices per frame with a raster catch-up at each boundary (§ 3.6) |
| `POM68K_DUMP` | unset | `tests/q605_rogue_census.cpp` (`make q605_rogue_census`, `EXCLUDE_FROM_ALL`): write `q605_rogue_*.ppm` at every stage. The harness sets `POM68K_JIT_HISTO` itself and dumps one census PER PHASE (§ 3.5bis) — a drawing workload, which `jit_bench` is not |

The lockstep gates read their own knobs, none of which the emulator ever
sees. Defaults differ per gate because the machine classes do:

| Variable | `jit_lockstep_test` (Q605) | `_68000_test` | `_030_test` (LC II) |
|---|---|---|---|
| `POM68K_JIT_LOCKSTEP_N` (`argv[1]` wins) | 5 000 000 | 2 000 000 (CMake passes 2 500 000) | 120 000 (CMake passes it too) |
| `POM68K_JIT_LOCKSTEP_BUDGET` | 1 | 1 (CMake sets 256) | 8192 |
| `POM68K_JIT_LOCKSTEP_FINE_AT` | off | off (CMake sets 2 400 000) | off (CMake sets 110 000) |
| `POM68K_JIT_LOCKSTEP_FINE_BUDGET` | — | — | 64 — **and it must stay well above 1**: at one cycle the engine never gets room to build a block, so a "fine" run compares the interpreter with itself and reports identical |
| `POM68K_JIT_LOCKSTEP_HIDDEN` / `_TRACE_AT` | dump hidden CPU/peripheral state; trace from a step | — | — |
| `POM68K_JIT_LOCKSTEP_TRACE_FROM` | — | — | per-instruction trace from a step |
| `POM68K_JIT_LOCKSTEP_PERIPH_TRACE_AT` | — | — | every `executeUntil` edge and peripheral delivery inside comparison N, with a hash of the save-stated V8 device tree, the flush's door (`src` — the interpreter's per-instruction sync, the jit's `sync/0` due callout, a stall, a chunk edge) and the pre-flush deadline. Null outside that one comparison |
| `POM68K_JIT_LOCKSTEP_PERIPH_DUMP` | — | — | with the trace-at knob above: write BOTH arms' full point streams to `<prefix>.interp` / `<prefix>.jit` — the around-the-mismatch excerpt cannot show where one arm inserted points; a diff of the streams can |
| `POM68K_JIT_LOCKSTEP_IRQ_TRACE_FROM` | — | — | from step N, record every IPL pin CHANGE (`updateIpl`) and every interrupt TAKE (`willInterrupt`) on both arms — printed on divergence. The instrument that separated "the take slips" from "the pin is late" and closed the peripheral-phase class (JIT_BRINGUP § C.4nonies) |
| `POM68K_JIT_LOCKSTEP_FULL_RAM_AT` | — | — | compare the complete 10 MiB RAM bus from this checkpoint on, not only low globals |
| `POM68K_JIT_LOCKSTEP_WRITE_TRACE_AT` | — | — | journal writes overlapping `$533E` inside this coarse quantum, including direct A64 DTLB stores |

`POM68K_JIT_FETCH` and `POM68K_JIT_BLOCKS` are not independent, and that is
deliberate: block discovery reads opcodes out of the code window, so
`POM68K_JIT_FETCH=0` disables both and leaves the engine measuring nothing
but its own dispatch overhead — useful exactly once, as the zero point.
`POM68K_JIT_BLOCKS=0` is the interesting attribution knob on a
code-generating backend: window on, no generated code at all.

The asset-free lockstep also reconstructs `sliceIndex_` from the live block
cache after every alternating eviction: keys must be unique and exhaustive,
the 32-byte guard masks must be exact, and a block spanning two 256-byte
slices must disappear from both when a write hits either. That is the daily,
ROM-free tripwire for the 2026-08-22 stale-key leak; the LC II soak is no
longer its only witness.

Two more `jit` gates carry no environment at all, because what they pin is
a boundary rather than a configuration: `jit_restart_write_030_test`
(a native `MOVE.B D0,d16(A6)` block pointed into a `/BERR` hole, all 32
bytes of the 68030 format-$A frame compared with a pure-interpreter oracle,
plus Speedometer's three exact device polls under an injected live delay)
and `jit_store_guard_a64_test` (mask-null RAM goes direct; a true overlap
with translated code is seen by the memory map and evicts the block). The
restart gate judges whichever native backend the host carries; the store
guard soft-skips away from AArch64. `docs/JIT_BRINGUP.md` § C is where they
come from.

### 6.1 The IR memory protocol

`Instr::memory` is filled by `describeMemory()` when a traced instruction is
committed to a block. Ordinary instructions carry zero, one or two explicit
accesses; MOVEM carries a variable ordered span. Each access records its
direction and operand role, width, encoded EA, the model-correct EA commit
point (`(An)+` is before the access on 030 and after on 040), fault phase
(`RestartInstruction`, `LastWrite`, `RestartableLastWrite`) and cache-line
eligibility.

`memoryProofPlan()` is a pure lowering shared by the backends. A sole read or
write may use its exact thunk and one published 040 line. A memory-to-memory
instruction preflights both mappings before access zero; only the separately
proved `$2F38`/`$21DF` contracts lower to `AtomicCachePair`. RMW and MOVEM
retain whole-instruction/span proofs. Since 2026-08-30 the write bitfields
(`BFCHG`/`BFCLR`/`BFSET`/`BFINS` on memory) publish their TAILLESS form as
the two-slot RMW contract — read4 then write4 at one address,
`lastWrite = 1`. Both native generators consume it with one writable
preflight before the read, on 040 and 030 by default. Read-only bitfields
that may reach a fifth byte publish read4+read1 under `PreflightAll`; both
generators prove the two mappings before access zero and branch around the
tail probe when the runtime field fits one longword. The five-byte WRITE form
would need four slots and has no analogue of that probes-before-load protocol
— a committed first store could not replay — so it stays undescribed and
falls back whole. `POM68K_JIT_030_MEMBF=0` vetoes the 030 admission for
attribution. This makes a widening reviewable in one
place: change the contract or planner, then make the pure IR assertions and
the generated A64/x64 gate agree.

`MemoryAccess::exactRequired` is stronger than optional exact-thunk
availability: it forbids a direct host load and assigns the access's variable
bus delay to the live model callback. The 68030 LC II polls `4A11`, `0829`,
`1029` and `1429` use it. Both generators may admit a traced base cost above
the fixed table only for such a sole read, and then charge that fixed cost;
an arbitrary slow-looking 030 read still refuses rather than inferring device
semantics from one trace.

Emission consumes that lowering through `InstructionMemoryPlan`. Each
mechanically decoded EA must mint a `MemoryAccessPlan` matching direction,
operand role, width and encoded mode/register; a slot can be consumed once,
and `complete()` rejects an omitted or invented access. The ordinary A64/x64
load/store primitives accept this token rather than a `soleAccess` boolean.
Preflight, exact-thunk, cache and EA-commit policy therefore come from the IR;
the backends retain address formation and instruction selection, but no
second semantic memory decoder. RMW uses one writable preflight before its
read, and `CLR <memory>` is deliberately a single write rather than a
backend-invented read/write pair.

### 6.2 The IR instruction-semantics protocol

`Instr::semantics` is filled beside `Instr::memory` by the pure
`describeInstruction()` decoder. It names the operation family, ALU operation,
byte/word/long width, low EA, MOVE destination EA, register field, condition,
bit/shift/bitfield action and immediate-vs-register form. Encoding overlaps
such as MOVEP/dynamic-bit, CMP/EOR direction and MUL/DIV/address-ALU are
resolved there once; an unknown or unsafe form stays with Moira.

Both native backends dispatch an `Instr` by `SemanticOp` and consume the
shared `AluOperation`. Their `canEmit(uint16_t)` census entry point calls the
same pure decoder before applying only host-specific EA admissibility.

`Instr` also owns up to ten extension words and two concrete
`DecodedEffectiveAddress` plans. `describeEffectiveAddresses()` resolves
immediate, displacement, absolute, PC-relative, brief-index and complete
68020 full-index formats after tracing. Full plans name base/index
suppression, base and outer displacement sizes/values, and direct,
preindexed or postindexed memory indirection. `ControlFlowPlan` separately
owns branch/call/jump target, fallthrough and pushed return address. Both
backends consume these plans; neither calls `branchDisplacement()` nor parses
an extension. Cycle tables, register choice and host instruction selection
remain backend work. Full-index lowering is not thereby declared native:
both generators accept only the proved direct `LEA` subset. Memory-indirect
plans and every other full-format instruction replay untouched.

---

## 7. The x86-64 backend (J2)

> **Scope: correctness and automatic speed selection are declared for
> 040+030 on both native hosts.** AArch64 promoted the 030 on 2026-08-20;
> x64 followed on 2026-08-21 after its independent alignment proof. Both
> expose and automatically select 040+030 through their capability masks.
> Everything below is written against the 040's instruction-boundary
> contract, and the differences from the 68030 are semantic, not cosmetic:
> `(An)+` updates the register *before* the access on an 030 and *after* it
> on an 040 (`MoiraDataflow_cpp.h:326-332`), the 030 marks its last write
> restartable and stacks a format $A frame (`:355-361`). Both mode-5 cores
> suppress the tail refill; consequently `queue.irc` is the exact held word
> (lookahead, extension or displacement), never simply a word derived from
> the exit PC.
>
> This was learned the expensive way. On 2026-07-29 `auto` stopped filtering
> on `dflt` — correctly, since that filter meant `auto` could never reach
> x64 at all — and the generator was thereby handed every JIT machine,
> including the 68030 ones. `jit_lcii_boot_etalon` then **timed out at one
> hour** (2026-07-30): generated code wedged the guest in the ROM's Egret
> handshake poll loop (blocks around `$40A148xx-$40A149xx` and `$40A0A8E6`,
> `BTST`/`TST` + branch), eight blocks compiled and then nothing at all,
> while the same machine boots in **2 min 21 s** on `threaded`. Selection
> now tests guest validity before host usability ranking (`JitBackend.h`
> § *GuestFamily*), so `auto` lands on a code generator only where it has
> EARNED that family. The `caps().autoFamilies` speed declaration carries
> 68040+68030 on both native ISAs (AArch64 promoted 2026-08-20, x86-64
> 2026-08-21 once the IIsi segfault of JIT_BRINGUP § C.4septies proved
> gone); other JIT families reach `threaded`.
>
> **The 68k seam below the backends is no longer 040-only, and the scope
> box is now the only thing holding the line.** `pomJitProbeData` grew an
> 030 branch (data-space fc, write-protect and owed-M-bit refusals,
> `MoiraExecMMU_cpp.h:2085-2135`) and `pomJitReadData`/`pomJitWriteData`
> reach `mmuRead`/`mmuWrite` on an 030 instead of `mmu040Read`/`Write`
> (`:2287`, `:2312`). `jit_lockstep_030_test` gives the family the
> differential coverage it lacked. The emitters' side of the 030 contract
> is lockstep-proved since 2026-08-18 — generated 030 code is reachable by
> an explicit `POM68K_JIT_BACKEND=x64|a64`, no unsafe override — and since
> 2026-08-21 `auto` resolves an 030 to the native generator on both ISAs
> (`docs/JIT_BRINGUP.md` § C.5; the IIsi episode is § C.4septies).

`src/jit/backends/JitBackendX64.cpp` is the first backend that emits host
machine code; `X64Asm.h` beneath it turns method calls into bytes and knows
nothing about the 68k. Three decisions shape all of it, and each is a
consequence of the invariants rather than a preference.

**No C++ exception may cross generated code.** There is no unwind
information for bytes we emitted ourselves, so a Moira fault thrown through
a JIT frame reaches `std::terminate`, not a handler. Every call out of
generated code therefore goes to a `noexcept` thunk that reports failure as
a return value, and every failure is taken at an instruction boundary with
NOTHING committed — the interpreter then re-runs that instruction and faults
exactly as it always did. A multi-access instruction generally cannot use an
access thunk: a bail-out on the second access would re-run the first, and an
I/O read is not repeatable. The one proved exception is A64/040 two-EA MOVE:
it preflights the destination before consuming an exact source, so no later
refusal remains (§ 10, 2026-08-21).

**Guest registers stay in memory.** The 68k leaves the upper bits of a
destination alone on byte and word operations; x86's 8- and 16-bit forms
have exactly that semantics on a memory operand, so operating in place on
`reg.d[n]` gets the rule for free — no masking, no register allocator. It
also makes every bail-out trivially correct: there is never a live guest
value in a host register across an exit. (The few host registers that *are*
live across a linked chain are engine state, not guest state: `Moira*`, the
frame, the clock and its target, the retired count, the pacing baseline.)

**Cycle counts are checked, not trusted.** The cost tables are transcribed
from Moira's own `CYCLES_*` tables (the 68020 column, which is what the
68040 core uses), and an instruction is compiled only if the table agrees
with what the tracer measured when it actually ran it (`Instr::cycles`, § 4).
A wrong or missing entry costs coverage, never correctness — and it is what
automatically excludes every access whose cost depends on a device wait
state.

### What it emits natively

Source of truth: `X64Backend::canEmit()` (`JitBackendX64.cpp:3297`) plus the
emitters it dispatches to.

* straight-line: `MOVE`/`MOVEA`/`MOVEQ`; the
  `ADD`/`SUB`/`AND`/`OR`/`EOR`/`CMP` families in both directions;
  `ADDA`/`SUBA`/`CMPA`; `ADDQ`/`SUBQ`; register **`ADDX`/`SUBX`** B/W/L
  (X input, C=X, cumulative Z); the
  `ADDI`/`SUBI`/`ANDI`/`ORI`/`EORI`/`CMPI` immediates; `TST`, `CLR`, `NEG`,
  `NOT`, `EXT.W`/`EXT.L`/`EXTB.L`, `SWAP`, `LEA`, `PEA`, `Scc`, `BTST`
  (both forms),
  `LINK`/`UNLK`/`NOP`, **`EXG`** (all three forms) and **`CMPM`** with
  distinct address registers (both since 2026-08-21 — the x64 port of the
  a64 pair, PreflightAll on CMPM's two reads), and
  **`DIVU.W`/`DIVS.W`** and all four **`DIVL`** extension actions over `Dn`,
  immediate and ordinary non-`An` memory sources (32-/64-bit dividend,
  signed/unsigned; zero and quotient overflow replay the untouched
  instruction), **`MULU.W`/`MULS.W`** over register, immediate and ordinary
  memory sources, and
  **`MOVEM`** (both directions, both sizes, one span probe per burst, the
  040 restart latch `mmu040MovemArmed` checked);
* as block terminators: `Bcc`/`BRA`, `JSR`/`BSR.S`/`BSR.W`/`BSR.L`/`RTS`,
  **`DBcc`** (loops
  close internally like `Bcc`) and **`JMP <ea>`**;
* over addressing modes `Dn`, `An`, `(An)`, `(An)+`, `-(An)`, `d16(An)`,
  brief `d8(An,Xn)` / `d8(PC,Xn)` (word/long Dn or An index, ×1/2/4/8),
  `(xxx).W`, `(xxx).L`, `d16(PC)` and immediate (`eaIndex()`). A64 also
  accepts proved direct or memory-indirect full-index plans for `LEA`,
  `JMP`/`JSR`, register-destination `MOVE` and read-only ALU sources,
  including base/index suppression and base/outer displacement.

Everything else — including full-indexed `MOVEM`, memory-indirect writes and
three-access MOVE forms, unsupported shifts/rotates,
`ABCD`/`SBCD`, predecrement-memory `ADDX`/`SUBX`, same-register `CMPM`,
`MOVEP` and `MOVE SR,Dn` — falls back per instruction
to a cold stub that runs that one instruction through Moira and rejoins the
compiled stream. A block whose native coverage falls below half is refused
outright: it would be the same interpreter work plus a call and a frame.

Memory division uses a narrower transaction than an ordinary read. Plain RAM
is translated and read without publishing `(An)+`/`-(An)`; if a semantic
zero/overflow guard fails, Moira may safely read those bytes again. With the
040 D-cache active, only an already-published resident line qualifies, and its
hit counters are committed after the guards. Cache misses, MMIO and other
non-plain mappings leave before any data read, so the exact fallback observes
the divisor exactly once. This is the shared `replayableSpeculativeRead()`
contract, consumed independently by both generators.

**Backend admission remains explicit even where the sets have converged.**
A64 adds immediate and guarded register-count line-$E shifts/rotates (no
`ROX` yet), five-byte memory reads and `MOVE SR,Dn`; brief-indexed reads/RMW,
`Scc`, `PEA` and `LEA`, plus `EXG`, distinct-register `CMPM`, TAILLESS memory
bitfields (all eight read/write actions) and register bitfields (all eight
actions, static or dynamic offset/width) are now on both. The x64 static body
folds the rotate, extraction shift and destination mask into immediates; its
dynamic body uses CL-counted shifts, with both `BFFFO` paths branching around
x86's undefined `BSR`-of-zero. The two
backends admit the bitfield family with the same `canEmit` rule, so the
parity gate carries no Bitfield exception row any more; what still
refuses on x64 refuses at emission (five-byte read tails), which the census
sees and the parity sweep cannot. The shared synthetic
040 oracle demands brief An/PC reads and `LEA`, indexed `Scc`/`PEA`, complete
CPU/RAM lockstep and zero slow instructions. Its direct-full `LEA` twin must
also stay native through base/index suppression and 9/11/15-cycle forms,
while memory-indirect LEA/JSR/MOVE/CMP twins must remain exact and native on
A64. The 030 oracle separately pins native signed/scaled reads plus
brief/direct-full `LEA`, the direct-RAM full-indirect `LEA` transaction on
both hosts, and the restartable indexed-MOVE format-$A fault frame;
indexed `Scc` there remains conservatively replayed by the trace-cost guard.
Each backend's
`canEmit()` remains the source of truth, and `jit_backend_test` pins the
remaining keyed differences (`MOVE SR,Dn`, shifts and five-byte bitfields) plus the
indexed parity/control-flow refusal, so a coverage change is also a gate
change.

### What it is worth

The generator wins on both bench regimes and on the boot etalon — the
numbers are in § 3.4's table; this section is what produced them. Three landed
changes, in order of size:

1. **The arm-time DTLB flush is gone** (2026-07-31) — worth −23 to −33 %
   depending on backend and regime. It is an invalidation-ownership
   argument, so it lives in § 8.
2. **Block linking** (§ 9) plus the `LINK`/`UNLK`/`NOP` carve-out: block
   entries on the loading phase fell 53 % (2.41 M → 1.14 M), 268 → 566
   instructions per entry.
3. **The census five** (2026-07-30): `MOVEM` ×3 forms, `DBcc`, `JMP` —
   three names, ~5 % of idle-Finder instructions, −3.0 % / −1.7 % on the
   two regimes, boot etalon 25.6 → 24.7 s.

**Coverage.** **99.7 % native** on the current 3 000-frame Q605 budget
(2026-08-23), after exact sole reads, preflight-before-exact two-EA MOVE and
the CMPA/ADDQ/EXG/CMPM coverage pass; production block fallback is 813,478
instructions, or 0.1 % (3,648,316 / 0.6 % after the preceding global store-
guard correction, and 30,788,039 / 4.7 % immediately before it). The earlier
§ 3.5 phase raised the share from 96.2 % to 98.5 %; the 2026-07-30 census
began at 89.6 %. The remaining static/runtime
classes are still enumerated in § 3.5 and § 3.5bis. Re-measure with
`POM68K_JIT_HISTO=1` before quoting any of these; use the drawing census in
§ 3.5bis rather than an idle Finder to price indexed modes.

**The next lever is not code density.** The binding cost at the idle Finder
is the exactness contract itself (§ 3.3: one window death per ~15
instructions). The old FOOTPRINT theory (47 000 blocks × ~1.5 KB) is not
disproved but is no longer the leading term, and the DENSITY item it
motivated (~150 B of host code per guest instruction, mostly the
per-instruction contract) is now third-order: worth doing (local `rel8`,
shared cold stubs) but not the lever.

What is already right, and should not be traded away: the block cache no
longer flushes on a write into translated code (§ 8), the data path never
takes a C++ exception through a JIT frame, and every cycle count is
cross-checked against the interpreter's own measurement before an
instruction is compiled at all.

---

## 8. The data path: an inline TLB, and what it refuses

Generated code cannot call `mmu040Read` (it throws). So a data address is
translated inline, against `Moira::pomJitDtlbR/W` — two direct-mapped tables
(read and write are separate, because a page can be readable and
write-protected, and because a write to an unmodified page must re-walk so
the table search sets M). **256 entries each, 16 bytes per entry**
(`Moira.h`, `PomJitDtlb::kEntries`), so 4 KB per table; the index is
`(addr >> 12) & 255`, shifted rather than SIB-scaled because x86 scales stop
at 8. Generated code indexes those tables directly. The
INTERPRETER's opt-in data window (`POM68K_DATA_WINDOW`) goes through two
single-entry level-0 caches first (`pomJitDataR1`/`W1`, then
`pomJitDataSlow`): consulting the big tables directly from the interpreter
measured ~10 % SLOWER, because streaming guest data evicted their own cache
lines.

`jit::Engine::fillDtlb` is the ONLY door into that cache, and the refusals
are the safety argument for the whole path:

* **no page-table walk, no U/M write-back** — `pomJitProbeData` only reads
  resident ATC entries, and refuses a write to a page not already marked
  modified, because that write owes the descriptor an M bit. The 68030
  branch (`MoiraExecMMU_cpp.h:2085-2135`) probes DATA space, `fc = 5/1`,
  not the program space the code probe uses: the 030 ATC matches `fc`
  exactly, so probing the data side with the program-space `fc` would miss
  every entry and refuse everything — an engine that looks merely slow;
* **no I/O and no unmapped hole** — `dataSpan` (per machine, e.g.
  `Q605Memory::dataSpan`) hands back plain RAM, ROM for a read, and the
  **framebuffer aperture**, and nothing else. The framebuffer is in
  deliberately: QuickDraw drawing a 640×480×8 desktop is an enormous number
  of plain array stores, and leaving them on the slow path made the code
  generator slower than the interpreter for the whole Finder phase. The
  video CELL registers next door (`$F98000xx`) stay out — reads there latch
  and auto-increment. So do ROM seen by a store, and RAM while a debug
  write-watch is armed;
* **no store into a 256-byte slice holding translated code.** That one has
  to go through the memory map, so the write guard sees it. It was a
  refusal of the whole 4 KB page (`codePage_`) until 2026-08-10, when
  `PomJitDtlbEntry::codeMask` made it per-slice — § 3.5 has the numbers and
  `BackendCaps::dtlbCodeMask` the safety declaration. A64 tests the actual
  `codeMask` and slice-intersection registers with exact one-instruction
  branches on every store; `jit_store_guard_a64_test` proves both zero-mask
  direct RAM and true-mask self-modification. A backend that has not opted in
  still gets the whole-page refusal;
* **MMU pages smaller than 4 KB.** An entry maps one 4 KB slice, so a page
  wider than that fills as independent slices (translation preserves the
  in-page offset and pages are size-aligned), but a page *narrower* would
  put several different translations inside one entry. The first cut
  demanded exactly 4 KB — "the 68040 boots with 4 KB on every Mac" — which
  stops being true the moment the System arms paging (8 KB on the 040), and
  the refusal, being transient by design, cost a call, a probe and a
  rejection on *every* data access: ~7 s slower across engines. The 68030
  made it worse still: its TC picks anything from 256 B to 32 KB, and the
  LC II's System picks neither 4 nor 8 KB — **17 425 292** refused fills in
  one bring-up run.

A refusal is *cached* as an entry with a null host pointer, because a
hardware poll loop would otherwise pay a call per iteration to be told the
same thing. A refused address then costs a tag compare and a null test
before taking the access thunk. Privilege rides in tag bit 31, so a
supervisor-only page filled in supervisor mode can never be hit by user code
and nothing needs flushing on an `RTE`.

### The 68040 D-cache path is physical lines, not host RAM (J4)

With `POM68K_040_DCACHE=1`, every ordinary DTLB fill is a tagged-null
refusal: a copyback line can contain bytes newer than backing RAM, so even a
perfect logical-to-physical translation does not authorize a host-RAM load.
Since 2026-08-16 a successful exact access can instead publish the resident
`Cache040::Line*` in `pomJitCache040R` (256 direct-mapped, 32-byte entries).
A generated **sole-access read** uses it only after checking all five parts
of the proof: logical line plus privilege, current DATA-ATC generation,
`line.valid`, saved versus current physical tag, and no crossing of the
16-byte line. The bytes in `Line::data` are already big-endian.

Invalidation is split along the state that changed. DATA-ATC eviction and
map/control changes bump `pomJitCache040Gen` in O(1), because clearing the
256 logical lines of every evicted 4 KB page would make ATC churn the new hot
path. Physical replacement changes the live tag; CINV/CPUSH and invalidating
snoops clear `valid`; write-sink snoops and CPU writes update the same stable
line object. A non-zero configured hit charge suppresses publication because
the generated path contains no `sync()` call.

Sole-access copyback stores use a distinct `pomJitCache040W` table: only an
exact write that proved write permission, descriptor M state and CM=copyback
may publish it, so a read entry or a write-through alias can never authorize a
native store. A generated hit writes the big-endian bytes first and then ORs
the first/last covered dirty-longword bits. A miss still reaches the exact
instruction/access path; `jit_copyback_write_040_test` compares all 60 bytes
of the resulting format-$7 frame for both the last-write (next PC, final CCR)
and restart (instruction PC, restored CCR/EA) cases. The optimization is
independently disabled with `POM68K_JIT_040_LINE_WRITE=0`; the separately
registered control runs that setting and proves it reaches the exact cache
path rather than backing RAM.

The first host-time pair put ordinary MOVE write hits below noise despite
4,402,477 removed fallbacks (49.87 -> 49.81 s average). The cache-on census
then identified a better bounded consumer: BSR's return-address push, already
using the generic store seam on x64 and now using it on A64 too. It removes
another 3,933,940 fallbacks. `jit_copyback_bsr_040_test` pins the resident
stack-line bytes/dirty bit/stale backing RAM and redirects an already compiled
user-stack push to /BERR, where its complete format-$7 frame must match the
interpreter after the supervisor-stack switch. Two order-reversed 5 G-cycle
pairs measure the combined write path at **49.93 -> 49.49 s average**
(-0.88 %), with the same fingerprint, PC and SCSI count in every run.

The next bounded consumer is a true two-access instruction, not another
sole store. The hot longword pair `MOVE.L abs.W,-(A7)` (`$2F38`) and
`MOVE.L (A7)+,abs.W` (`$21DF`) first probes the source R entry and destination
W entry without touching architectural state or counters. Only two hits
publish both counters and perform the transfer; either miss replays the whole
untouched instruction. `POM68K_JIT_040_LINE_PAIR=0` is its independent
attribution control. `jit_copyback_pair_040_test` pins bytes, dirty bit,
flags/EA and a second-proof /BERR frame; its separately registered OFF twin
must produce the same result with no native pair hits. On the fixed Q605 run,
the pair converts **7,523,969** more fallbacks and averages **46.92 ->
46.63 s** over two order-reversed pairs (-0.62 %), with identical fingerprint,
PC and SCSI progress.

The permanent native gates run this mode, not just the cacheless DTLB:
`jit_lockstep_a64_coarse_test` and `jit_lockstep_x64_test` arm the cache and
`POM68K_JIT_040_LINE_STATS=1`, which makes the tests fail if an enabled native
path has no exercised hit. The A64 read run observed 18,576,390 hits over 131,823,105 JIT
instructions and stayed CPU/RAM/device-identical to the interpreter.
`POM68K_JIT_040_LINE_READ=0` retains the exact-thunk attribution control.
With native writes, BSR and the dual-line MOVE path enabled, the same
five-million-checkpoint gate observed **21,303,835 reads plus 5,896,026
copyback writes** over 131,823,105 JIT instructions, again with
CPU/RAM/device state identical.

### Invalidation: evict, do not flush

A write into memory a block was translated from used to drop the whole
cache. With generated code in it that is ruinous: one boot phase took
**5 313** such flushes, and the engine spent its time re-translating code it
had already translated. Two changes fixed it — the guard's granularity went
from 4 KB to 256 bytes (`CodeGuard::kShift`; 68k code and its data share
pages constantly), and `serviceGuard()` now evicts only the blocks that
overlap the written slices, through a slice → blocks index. Same phase,
after: **27** flushes.

### The arm-time flush that owned nothing (2026-07-31)

Every code-window re-arm used to call `pomJitDtlbFlush()` unconditionally —
clearing both 4 KB tables, once per ~15 idle instructions (§ 3.3), paid even
by `threaded`, which never reads the DTLB at all. It was standing in for
invalidations that each already have an exact owner:

| what could go stale | who kills it |
|---|---|
| the translation itself changed | `Moira::pomJitMapMoved()` flushes at the source of every `pomJitMmuGen` bump |
| an ATC entry was evicted | `pomJitAtcEvict()`, per page and per space |
| privilege changed | it rides in each entry's tag (bit 31) |
| a slice gained its first block | `markPages()` flushes on the 0 → 1 transition — **which it did not do until 2026-08-10**, and that was a real self-modifying-code hole, not a coverage one (§ 3.5) |
| a page lost its last block | `serviceGuard()` flushes when it unmarks |

So deleting it is conformance-neutral (two 60 M-step locksteps bit-identical,
x64 and threaded; the whole `jit` tier green) and worth −23 to −33 % of wall clock
depending on backend and regime. DTLB fills over one 60 M-step lockstep:
942 M → 7.8 M. What survives between two arms is exactly the set of entries
whose backing ATC rows are still resident — which is the exactness contract
itself.

---

## 9. Block linking

A block that ends on a branch used to return to the engine, which then did a
hash lookup and built a fresh frame to enter the next one. On a real Mac OS
workload a THIRD of all instructions are control transfers (23 % branches,
10 % call/return), so that round trip was being paid every few instructions.
What it bought back is measured in § 7.

The link is a **table**, not a patched jump: `Engine::linkTable_`, 65,536 slots,
direct mapped on the guest pc (`(pc >> 1) & 65535`) and tagged with
`pc | super` — pc is always even, so the privilege bit rides in bit 0 and a
user-mode and a supervisor block at one address cannot be confused. An exit
whose target is a compile-time constant emits four instructions
(`Emitter::leaveTo`): load the table, compare the tag, jump through the
entry, with the slot offset folded at compile time. A run-time target (`RTS`,
`JSR (An)`) uses `leaveToDynamic`, which computes the index instead — four
more instructions, still nothing against returning to the engine.

Why not patch the jumps directly, as most JITs do? Because a boot evicts
blocks **237 000 times**, and un-patching every jump INTO an evicted block
means maintaining incoming/outgoing link lists that can dangle when a block
is freed. Invalidating one table slot (`retractLink`) is O(1), cannot dangle,
and is exact: a block's slot is a function of its pc, so if the slot still
carries this block's tag it IS this block.

`POM68K_JIT_EDGE_CELLS=1` is the measured exact-dependency alternative for
constant edges. Generated code embeds the address of an Engine-lifetime cell,
loads its current entry and jumps only when non-null. The cell address stays
valid across unordered-map growth and code generations; `retractLink()` and
`clearLinks()` null it before translated code can be recycled. This removes
direct-table collisions without incoming jump lists or code patching. It is
off by default because the extra dependent load/indirect branch cost 0.5 % on
the fixed Q605 ABBA despite eliminating 21.1 % of block-end exits (§ 3.8).

Three things make jumping straight into another block safe:

* the target is entered **past its prologue** (`Backend::linkEntry()`; a
  backend returning null there is simply never published as a jump target),
  so it inherits
  the callee-saved registers — `Moira*`, the frame, the clock and its target,
  the pacing baseline — and the retired-instruction count accumulated so far;
* privilege cannot change inside a chain (every SR write is `Unsafe`), so a
  block compiled for one privilege can only be reached from another with the
  same one — which is what the tag's bit 0 enforces;
* the MMU generation cannot change either, so the data TLB entries the chain
  relies on stay valid without the engine re-arming anything between blocks.

Every block's first instruction still runs the budget and flag guards, so a
chain cannot outrun the caller's cycle target or ignore a pending interrupt.

**Restartable-write blocks may chain again on both native backends.** Their
former AArch64 link suppression was evidence for the old pre-charge bug, not
an architectural boundary: a runtime replay could be declined after i-cache
state had already moved. With charge-on-success past every bail, both the
incoming linked entry and outgoing links are safe. The 68030 restart-frame
test and the 6,000-frame real-cadence AArch64 lockstep pin that contract
(`docs/JIT_BRINGUP.md` § C.4sexies).

The table originally had 4,096 slots. A 6,000-frame Q605 workload compiles
more than 150k blocks over its lifetime and keeps up to 65,536 resident, so
the small direct map made unrelated hot PCs displace one another. Raising it
to 65,536 slots costs 1 MiB per CPU and, on AArch64, reduced `block end` exits
from 149,265,073 to 72,507,478. Two fixed-budget runs improved from
22.62/22.39 s to 21.27/21.41 s (about 5.2%), with fingerprint
`f8e91527781ede67` and 1,410,142,343 retired instructions unchanged. The four
68040 JIT etalons and their four explicit interpreter oracles remain green.

---

## 10. Journal

* **2026-08-21 — exact reads and the last inexpensive A64 fallbacks.** A sole
  040 read whose traced cost includes a live bus/device delay now calls the
  exact read seam and charges only the fixed opcode component. A two-EA MOVE
  proves its RAM destination before performing an exact, possibly FIFO-like
  source read, so no destination miss can duplicate that source side effect.
  The token is deliberately 040-only: its first 030 widening shifted two
  retained-cache fetches; after confinement, two complete 120,000-boundary
  LC II locksteps agree on 1,028,955,568 fetches and all CPU/device state.
  Corrected CMPA timing (+2 cycles), word-encoded ADDQ/SUBQ address-register
  decoding, native EXG and preflighted distinct-register CMPM remove the
  remaining cheap static fallbacks. The already-proved peripheral deadline
  comparison now calls a
  wrapper's due handler directly instead of re-entering virtual `sync(0)` and
  repeating the test. Five Q605 runs give **3.47 s / 14.40x** median, **99.5 %
  native** and **813,478** production fallbacks, with fingerprint
  `778dd7ad558108fd`, 649,372,093 retired instructions, SCSI=1324 and PC
  `$0002528A` unchanged. The instrumented census contains 569,763 static and
  259,489 guarded fallbacks; full-format 68020 indexing and real code-mask
  collisions are now the leading intentionally precise residues. A
  five-million-boundary Q605 hidden-state lockstep retires 666,374,221 JIT
  instructions identically. The Apple-M4 regression floor moves from 11.90x
  to 12.50x, 12.8 % below the slowest measured run.
* **2026-08-20 — the A64 store guard becomes exact globally.** The fallback
  census showed that a reserved-register `CBZ/CBNZ` fixup was testing w9
  instead of the requested w12/w10, accounting for 27.4 M false runtime
  fallbacks on the fixed Q605 workload. General fixups now preserve the
  requested register for every store and for the 040 cache-valid test. The
  generated loop also keeps `clockTarget` and the exact peripheral deadline
  in callee-saved registers (refreshing after every helper that can advance
  devices). Execution flags stay cached between helpers; the redundant IPL
  re-sample is proved unnecessary when those flags are clear; IRD/IRC use one
  packed store; N/Z and subtract-borrow are branchless; and the backend keeps
  its stable call-frame pointers across native-chain exits. `UBFX` now fuses
  the masks and shifts in DTLB/cache probes, sized results, bit operations and
  dynamic links. Five repeated 3,000-frame runs moved the median from **5.32
  to 3.61 s** (**−32.1 %, 1.47x faster**), native share **94.6 → 98.8 %**,
  block fallback **30,788,039 → 3,648,316**, and real-time throughput
  **9.40x → 13.84x**.
  Fingerprint `778dd7ad558108fd`, 649,372,093 retired instructions, SCSI=1324
  and PC `$0002528A` are unchanged. The historical opcode selector is removed.
  A long-run profile then exposed a separate lifetime failure: the 128 MiB
  bump allocator filled, but `CompileReject::CodeMemory` permanently rejected
  every later block although its own comment promised an engine flush. A
  distinct retryable `CodeCapacity` result now retracts all links and recycles
  a 64 MiB generation; real 500-second guest sessions stay at **92.0 % native**
  instead of falling to 51.7 %, and improve **161.47 → 90.82 s** wall
  (**3.10x → 5.51x real time**), with fingerprint `a4e51291f19de25d` and
  7,713,895,325 retired instructions unchanged.
* **2026-08-12 — `MOVE SR,Dn` stops ending a block.** Privileged on a
  68010+, so a successful trace is supervisor mode by construction, and it
  changes no mapping (§ 4). 2.33 % of a 10k census stream on the LC II;
  emitted natively by a64 only, cold-stubbed by x64.
* **2026-08-10/11 — Phase A, and the arm64 default.** The fallback census
  was wired on x86-64 (it had only ever been written by a64), which named
  two wrong cost-table cells; `PomJitDtlbEntry::codeMask` replaced the
  whole-page store refusal; `markPages()` started flushing the data TLB —
  a real self-modifying-code hole, not a coverage one; `PEA` and `Scc`
  landed. −12.8 % wall on the fixed Q605 budget, native share 96.2 → 98.5 %
  (§ 3.5). Separately: incremental i-cache invalidation and a
  slice → block-list index made a64 the automatic AArch64 choice, and
  `jit_lockstep_030_test` gave the 68030 family its first differential
  coverage.
* **2026-08-09/10 — the default flipped on the 68040.** `defaultEngine()`
  stopped being a constant and became a per-family answer
  (`JitConfig.h:203`, `JitEngine.cpp:163`): `jit/auto` on 68040, interpreter
  everywhere else, `POM68K_CPU_ENGINE` overriding either way. The plain
  `q605/centris650/q630/q700_boot_etalon` gates therefore now run the
  engine, and four `interp_*` registrations preserve one explicit
  interpreter oracle per 68040 platform — a default is only conformant
  while the old reference stays independently runnable on the same binary
  and assets. The evidence bar each future family flip has to clear is
  `docs/JIT_BRINGUP.md` § D.1.
* **2026-08-06 — the last two families, and one of them needed a new
  seam.** `Cpu020` (Mac II / IIx / IIcx / SE-30) needed no Moira work at
  all: the plain-020 fetch window and the identity probe have been there
  since the 030 extension, and what was missing was `MacIIMemory::codeSpan`
  and the member. `Cpu68k` (the compacts) is the first guest where the
  window is NOT free of cycle accounting, and § 3.1 is the whole argument.
  Measured: SE/30 ×1.37, Mac II ×1.21, Mac Plus ×1.08, Classic ×1.03 —
  which is why the ranking table at the top of this file now exists. The
  IIfx followed the same day (`iifx_boot_etalon`), and with it every CPU
  wrapper in the tree carries an engine: a 68030, so the ATC probe and
  `mmuFetchWord` were already there, and the easiest map the window has
  met — 32-bit clean, no HMMU, no GLUE remap, nothing to reconcile. Its one
  peculiarity is that the boot overlay drops on a ROM-region READ
  (`rom_switch_r`), which is the V8 case: while it is up, `codeSpan` must
  serve nothing at all, or the map would stay latched in its boot state.
  Three things fell out of the work rather than being the point of it: a
  `PMOVE` to the 68030's `TC`/`SRP`/`CRP` did not bump `pomJitMmuGen`
  (every 030 machine's window and block cache could survive a change of
  translation root — masked in practice by the ROM's following `PFLUSHA`);
  the compacts' main loop is `runUntil()`, not `runCycles()`, so the engine
  sat switched on and idle until it was routed there; and
  `POM68K_JIT_VERBOSE=1` printed nothing that could have told you either.
  It now prints a retired/window/arms line at teardown.
* **2026-07-31 — the arm-time DTLB flush deleted, and everything
  re-measured.** § 8 has the argument, § 3.4 the current table. This is also
  where the crossover story died: until this pass the x86-64 backend was
  the fastest engine while the guest executed System code and LOST once the
  Finder was up, which the file explained at length. It now wins both
  regimes, so the explanation is gone rather than corrected.
* **2026-07-30 — the census five, and guest-family selection.** `MOVEM`,
  `DBcc` and `JMP <ea>` compiled (§ 4, § 7). Separately: `auto` had started
  handing the 040-only code generator to the 68030 machines, which wedged
  `jit_lcii_boot_etalon` for an hour — `caps().guestFamilies` (§ 1, § 7) is
  the fix.
* **2026-07-28 — J2: the x86-64 code generator**, block linking, the inline
  data TLB, the interpreter data window, PGO, and the 030/020 seams. The
  measurements this file carried from that day, superseded by the
  2026-07-31 matrix and kept only for the shape of the argument
  (`jit_bench`, Quadra 605 / Mac OS 8.1): interpreter 6.41 / 42.59 /
  215.77 s and `threaded` 3.48 / 17.33 / 131.77 s on 0.83 G / 5 G / 20 G
  cycles, with `q605_boot_etalon` at 60.05 s interpreted against 29.02 s.
  They predate the ATC-eviction bit-exactness capping (`pomJitAtcEvict`),
  which is why every current number is roughly an order of magnitude larger
  on the bench: exactness bought at the cost of derived-state churn.
* **2026-07-27 — J0/J1.** Engine, host-neutral IR, backend interface,
  portable W^X code buffer, `threaded` backend, code window, block tracing,
  write guard, GUI **CPU** menu with live switching and a gauge window.
  Machines wired: Quadra 605 / LC 475 / LC 575 (`Cpu040`), Centris and Quadra
  610/650/800 (`CentrisCpu`), Quadra 630 / LC 580 (`Q630Cpu`), Quadra 700
  (`Q700Cpu`); the 68030 wrappers (`Cpu030`, `RbvCpu`, `SonoraCpu`,
  `VaspCpu`) followed on 2026-07-28. Measured the same day: the fetch window
  is worth −55 % on the Quadra 605 boot, the block cache is a net loss on
  `threaded` and ships off there. An adversarial review pass then closed two
  real defects in the block path before the gates were called green —
  `flushAll()` was reachable re-entrantly from inside a replaying block (a
  guest `MOVEC` to CACR → `didChangeCACR`), freeing the `BlockIr` under the
  backend's own loop; and the block cache had no notion of `pomJitMmuGen`,
  so a `PFLUSH` or a TC/URP/SRP write could point a cached script at
  unrelated code. Flushes are now deferred while a block is in flight, and
  the cache tracks the MMU generation.
