# POM68K JIT — design, invariants, journal

A **second execution engine**, living beside the Moira interpreter and never
in front of it. It is the default on the fully proved 68040 family and remains
opt-in on every other guest. The **CPU** menu switches it live;
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
"fenêtres (threaded)" and names the backend (`main.cpp:920-930`, gauge
window at `:706-718`);
the subsystem keeps its internal name because `src/jit/` names the seam and
the machinery, which a future non-conformant fast mode
(`docs/HLE_OVERLAY.md`) would build on. The five relaxations a classic 68k
JIT makes and this engine refuses — coarse time, coarse interrupts, a big
soft TLB instead of exact ATC semantics, lazy flags (legal here, still to
do), long traces — are catalogued in the CHANGELOG's 2026-07-28 eighth-pass
entry and in `TODO.md`.

**State.** Three backends. `threaded` replays a recorded block through Moira's
own handlers with the fetch window armed, and is valid for every guest.
`x86-64` (§ 7) generates host code for a real subset of the ISA, with an
inline data TLB (§ 8) and control transfers compiled as block terminators,
so a loop closing on itself never returns to the engine; on an x86-64 host
`auto` picks it for the 68040 machines and it beats `threaded` on every
regime measured (§ 3.4). `aarch64` covers the same 68040 *scope* — its
declared family is identical — with a near-identical but not identical
opcode set (§ 7): register and memory ALU, MOVE/MOVEA, effective addresses
including brief indexed, bit tests, internal branches, calls/returns,
LINK/UNLK, MOVEM, immediate shifts and register bitfields, backed by an
inline big-endian DTLB and exact per-instruction fallback. Five-million-step
fine/coarse locksteps and the complete Q605 Finder boot are green, so `auto`
selects it on AArch64. Release/native/LTO measures
1.22 s against 4.55 s for threaded on the fixed 1,000-frame Q605 workload
(3.73x, identical fingerprint); LLVM PGO measures 1.01 s against 3.41 s.
The complete Finder gate is 9.19 s native against 21.14 s threaded (2.30x),
or 7.86 s against 15.28 s under PGO (1.94x).
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
on 2026-08-06. **Both code generators** are narrower: 68040 guests only, by
declared capability (`JitBackendX64.cpp:2497`, `JitBackendA64.cpp:2156`;
§ 7).

**And what each is worth.** The engine being wired is not the same as the
engine being worth switching on. Ranked by measured gain (§ 3.4):

| Guest | Machines | Window buys | Because |
|---|---|---|---|
| 68040 | Quadra 605/610/650/700/800/900/950, Centris, Q630 | **×5.0** on a fixed budget (x64, § 3.4); ×2.68 end to end on `q605_boot_etalon` (2026-07-31, § 3.4) | an ATC walk per fetch, replaced by a bounds check |
| 68030 | LC II family, Sonora, VASP, RBV, **IIx/IIcx/SE-30**, **IIfx**, Duo | **×1.21** (LC II, fixed budget, threaded — no native 030 generator ships) | same, through `mmuFetchWord` |
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
  layer 2  jit::Backend       compile(IR) → Compiled ; run(Compiled, Context)
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
as the dispatch test in front of `emitRegInstr` (`JitBackendA64.cpp:2294`;
the x64 emitters carry their own switch and never call it). The coverage
floor is a separate thing and is not built on it: `compile()` counts what
the emitter actually produced and refuses a block below half native
(`JitBackendX64.cpp:2604`). Anything an otherwise-compilable block contains
that the backend cannot emit becomes a per-instruction cold stub inside the
generated code, not a shorter block. Block termination is the classifier's
job alone (§ 4).

---

## 2. Invariants

Each one names the gate that would catch it breaking.

| # | invariant | gate |
|---|---|---|
| 1 | **The interpreter is the reference.** Any divergence between engines is a JIT bug, never an interpreter bug. | `jit_lockstep_test` (five registrations, six on AArch64 — § 5), plus its 68000 and 68030 twins (§ 3.2) |
| 2 | **Exits happen at instruction boundaries only.** No partial guest state — registers, CCR, PC, clock — ever survives a block exit. Everything unusual (interrupt, trace, STOP, breakpoint, MMU fault, an opcode outside the classifier) is handed back to `Moira::execute()` at a clean boundary. | `jit_lockstep_x64_fine_test` (one cycle per comparison); `jit_backend_test` for the classifier rules |
| 3 | **The fastest proved conformant engine is the default, per guest family.** Today that is `jit/auto` for 68040 and the interpreter elsewhere. `POM68K_CPU_ENGINE=interp` always restores the oracle. | `jit_backend_test` pins the policy and both overrides; `interp_{q605,centris650,q630,q700}_boot_etalon` keep one interpreter reference per 68040 platform |
| 4 | **Peripheral time stays owed.** Blocks never run past the caller's cycle target (`Context::clockTarget`) and generated cycles go through the machine's virtual `sync()` (`pomJitSync`), so VIA, ASC, SWIM and the Egret/Cuda MCU keep their pacing. | `jit_mactv_boot_etalon` — registered for exactly this reason: Tinker Bell's Cuda transport deadlocks on a 2 % shift in MCU pacing long before a Finder signature would fail |
| 5 | **Nothing cached survives a change of the address map.** Overlay flips (`CodeGuard::invalidate()`), MMU/ATC changes (`blocksGen_` vs `Moira::pomJitMmuGen`) and cache-control writes (`didChangeCACR` → `flushAll()`) drop the block cache and the code window. | `jit_q605_boot_etalon` (the boot overlay flips in the first milliseconds); `jit_lockstep_test` |
| 6 | **No host knowledge above `jit::Backend`.** An architecture `#ifdef` outside `src/jit/backends/` or `JitCodeBuffer.cpp` is a design error. | `jit_backend_test` (its header states invariants 6 and 7 as its purpose) |
| 7 | **Every host POM68K builds for can run the JIT** — on `threaded` at worst. | `jit_backend_test` |

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
  which is what `jit::CodeGuard` protects.
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
Same `_blocks_test` variant; on AArch64 a third registration
(`jit_lockstep_030_a64_experimental_test`) drives generated arm64 030 code
under `POM68K_JIT_UNSAFE_BACKEND=1`, which is the development lane for a
family no generator declares (`docs/JIT_BRINGUP.md` § C).

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
therefore comes from `tests/jit_bench.cpp` / `tests/jit_bench_lcii.cpp`
(dev tools, `make jit_bench`, `EXCLUDE_FROM_ALL`, **not** CTest gates),
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

### 3.4 The current numbers (2026-08-10)

The pre-2026-07-31 figures this file used to carry are superseded — they
predate three landed emitters, two cost-table fixes and the arm-time DTLB
flush deletion (§ 8), and § 10 records what changed between them. Same
instrument, same rule (identical fingerprints across engines), one budget:

**Quadra 605, 3 000 frames (1.25 G machine cycles, 5.0 G core, idle Finder),
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
  (`:2129` vs `:421-423`). This refused **every** `CMPA`: a further 12 %.

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
  the default is the old whole-page refusal.
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

The indexed modes stay OPEN rather than dropped, and the reason is a limit
of the instrument: **this workload does not draw.** The idle Finder is
exactly where QuickDraw's blitters — the thing the indexed modes were
motivated by — are absent. Re-open with a census taken over a drawing-heavy
phase; do not close it on an idle-Finder number.

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

**`MOVE SR,Dn` is the one carve-out out of that SR group** (`JitIr.h:207`,
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
| `JMP <ea>` | 2026-07-30 | 0.66 % of the idle Finder in the census; a terminator simpler than `JSR` (no stack push). **Plain EA modes only** — `(An)`, `d16(An)`, `(xxx).W/.L`, `d16(PC)`. The 68020 indexed modes stay `Unsafe` (they are a brief-extension-word decoder in their own right) |

`LINK`, `UNLK` and `NOP` are carved out of `$4Exx` as ordinary
straight-line `AddrCalc`: they transfer no control and touch no SR/MMU/cache
state, they are 3.6 % of a real Mac OS workload, and they sit at every
function entry and exit — which is exactly where straight-line code begins.

A branch's length cannot be read off the pc delta the way every other
instruction's can (it jumps), so `branchWords()` reads it off the encoding
instead.

---

## 5. The working loop

Do not iterate against a bare `ctest` — 183 gates, hours, and `-j` is unsafe
because the boot etalons are contention-sensitive. Do not iterate against a
bare `make` either: tree-wide LTO relinks ~90 binaries after any core change.

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
| `jit_lockstep_a64_coarse_test` | the arm64 generator at 50 cycles per comparison, 5 M comparisons — **AArch64 hosts only** (`CMakeLists.txt:1459-1476`), which is also why it is the one smoke gate an x86-64 developer never sees |

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
| `ctest -L unit` | 92 | anything touching non-machine code |
| `ctest -L jit` | 29 | before proposing a JIT change |
| `ctest -L m040` | 41 | the 68040 family — the JIT's blast radius |
| `ctest -L etalon-core` | 12 | one profile per platform, ~32 min — the pre-commit tier |
| `ctest` | 183 | the release gate, once |

(Counts from `ctest -N` on 2026-08-12, on an **AArch64** host with
`POM68K_JIT_BACKENDS=auto`. Two gates are host-conditional
(`jit_lockstep_a64_coarse_test`, `jit_lockstep_030_a64_experimental_test`),
so an x86-64 host configures 181 / 90 unit / 27 jit — `m040` and `etalon`
are host-independent. `CMakeLists.txt`'s own inline comment near the label
block carries older numbers. All of them drift every time a gate lands —
re-derive rather than trust either.)

Labels are derived from test names at the end of `CMakeLists.txt`, so a new
gate is classified the moment it is registered — and the derivation
OVERWRITES any `LABELS` a registration set inline.

---

## 6. Environment surface

Everything in `JitConfig.h` unless noted.

| Variable | Default | Meaning |
|---|---|---|
| `POM68K_CPU_ENGINE` | 68040 `jit`, others `interp` | explicit `interp` or `jit` overrides the per-family default (the GUI menu still switches live) |
| `POM68K_JIT_BACKEND` | `auto` | `auto` \| `threaded` \| `x64` \| `a64` |
| `POM68K_JIT_UNSAFE_BACKEND` | `0` | force an explicitly named backend onto a guest family it does not declare (`JitBackend.cpp`) — for developing that family's support, never for use |
| `POM68K_JIT_FETCH` | `1` | the instruction-fetch code window (J1a) |
| `POM68K_JIT_BLOCKS` | *backend* | block discovery and replay (J1b). The default is the ACTIVE BACKEND's answer, not a constant (`blockCacheEnabled(dflt)`): OFF for `threaded`, which measured slower with blocks than with the window alone, ON for a code generator, which has nothing to run without them |
| `POM68K_JIT_BLOCK_MAX` | `64` | straight-line instruction ceiling per block, itself capped by `caps().maxBlockInstrs` |
| `POM68K_JIT_HOT` | native `1`, threaded `512` | visits before a recorded block is translated |
| `POM68K_JIT_LINKS` | `1` | direct block-to-block linking for native backends; `0` is the attribution/debug path |
| `POM68K_JIT_A64_PACING` | `1` | AArch64 inline peripheral deadline/batch test; `0` calls `sync(cycles)` after every emitted instruction for attribution |
| `POM68K_Q605_EVENT_SCC` | `1` | Q605 carries serialized SCC time debt to its exact event/MMIO boundary; `0` restores per-`tick` stepping for A/B attribution |
| `POM68K_Q605_EVENT_SCSI` | `1` | Q605 carries serialized 53C96 latency debt to its exact IRQ/MMIO/pseudo-DMA boundary; `0` restores per-`tick` stepping |
| `POM68K_JIT_A64_STORE_GUARD_OPCODE` | `0xB592` | AArch64 bring-up only: the opcode whose store-guard fallback is removed opcode-locally, parsed `strtoul` base 0 and refused above `0xFFFF` (`JitBackendA64.cpp:2181-2187`) |
| `POM68K_JIT_ICACHE_EMIT` | `1` | ATTRIBUTION knob for the emitted 68030 i-cache charge (`docs/JIT_BRINGUP.md` § B). Off, an 030 block charges the instruction cost alone, so a residual divergence provably belongs to something else. Only a bring-up measurement should turn it off |
| `POM68K_JIT_MAX_BLOCKS` | `65536` | blocks kept before the engine STOPS RECORDING (it does not flush — a flush is what a code generator cannot afford) |
| `POM68K_DATA_WINDOW` | `0` | the INTERPRETER's data window (§ 8) — opt-in since the ATC-exactness capping made it a net loss (`JitEngine.cpp:76-90`) |
| `POM68K_JIT_PARANOID` | `0` | re-validate the translation at every arm — for differential testing (`JitEngine.cpp`) |
| `POM68K_JIT_VERBOSE` | `0` | backend selection, block dumps and flush chatter on stderr — **plus a retired / window-covered / arms / failed line and a dtlb-refusals-by-reason line at teardown**, which is how you tell "the engine is on" from "the engine is doing something" (§ 3.1) |
| `POM68K_JIT_VERBOSE_BLOCKS` | `40` | how many compiled blocks the dump prints under `POM68K_JIT_VERBOSE`. The dump is the only place a block's MEASURED per-instruction cycles are visible, and 40 only ever reaches ROM reset code — raise it to diagnose a refusal deep in a boot |
| `POM68K_JIT_ACCESS_THUNK` | `2` | 0 = whole-instruction fallback, 1 = loads, 2 = loads and stores |
| `POM68K_JIT_HISTO` | `0` | dynamic opcode census, dumped at exit, with the backend's `canEmit()` coverage — plus the static/runtime fallback census and its per-reason split (`JitEngine.cpp dumpHisto()`) |
| `POM68K_JIT_WINDOW_KILL` | `0` | **measurement instrument, not tuning**: kill the code window every N retired instructions on purpose, so the price of ONE window-lost exit is the slope of wall time against exit count (§ 3.6). A kill is architecturally invisible, so a bench fingerprint must not move with N — that is what makes the fit a measurement rather than a story. Slows the engine down by design |
| `POM68K_BENCH_FRAMES` | q605 `3000`, lcii `6000` | `tests/jit_bench.cpp` / `tests/jit_bench_lcii.cpp` — frames of 416 667 (Q605) or 640×407 = 260 480 (LC II) **machine** cycles |
| `POM68K_BENCH_SLICES` | `0` = CPU alone | `jit_bench_lcii` only: N ≥ 1 runs the GUI's own quantum, N slices per frame with a raster catch-up at each boundary (§ 3.6) |

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
| `POM68K_JIT_LOCKSTEP_PERIPH_TRACE_AT` | — | — | every `executeUntil` edge and peripheral delivery inside comparison N, with a hash of the save-stated V8 device tree. Null outside that one comparison |
| `POM68K_JIT_LOCKSTEP_FULL_RAM_AT` | — | — | compare the complete 10 MiB RAM bus from this checkpoint on, not only low globals |
| `POM68K_JIT_LOCKSTEP_WRITE_TRACE_AT` | — | — | journal writes overlapping `$533E` inside this coarse quantum, including direct A64 DTLB stores |

`POM68K_JIT_FETCH` and `POM68K_JIT_BLOCKS` are not independent, and that is
deliberate: block discovery reads opcodes out of the code window, so
`POM68K_JIT_FETCH=0` disables both and leaves the engine measuring nothing
but its own dispatch overhead — useful exactly once, as the zero point.
`POM68K_JIT_BLOCKS=0` is the interesting attribution knob on a
code-generating backend: window on, no generated code at all.

Two more `jit` gates carry no environment at all, because what they pin is
a boundary rather than a configuration: `jit_restart_write_030_test`
(a native `MOVE.B D0,d16(A6)` block pointed into a `/BERR` hole, all 32
bytes of the 68030 format-$A frame compared with a pure-interpreter oracle)
and `jit_store_guard_a64_test` (mask-null RAM goes direct; a true overlap
with translated code is seen by the memory map and evicts the block). Both
soft-skip away from AArch64. `docs/JIT_BRINGUP.md` § C is where they come
from.

---

## 7. The x86-64 backend (J2)

> **Scope: the 68040 family only** (`caps().guestFamilies = kGuest68040` —
> `JitBackendX64.cpp:2497`, and the same declaration in
> `JitBackendA64.cpp:2156`).
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
> § *GuestFamily*), so `auto` lands on `threaded` for the 68000/020/030
> machines and on a code generator for 040s where available — x64 on
> x86-64, a64 on Apple Silicon and other AArch64 hosts.
>
> **The 68k seam below the backends is no longer 040-only, and the scope
> box is now the only thing holding the line.** `pomJitProbeData` grew an
> 030 branch (data-space fc, write-protect and owed-M-bit refusals,
> `MoiraExecMMU_cpp.h:2085-2135`) and `pomJitReadData`/`pomJitWriteData`
> reach `mmuRead`/`mmuWrite` on an 030 instead of `mmu040Read`/`Write`
> (`:2226-2257`). `jit_lockstep_030_test` gives the family the differential
> coverage it lacked. What is still missing is the emitters' side of the
> 030 contract — `docs/JIT_BRINGUP.md` § C is the live plan, and until it
> closes, generated 030 code is reachable only under
> `POM68K_JIT_UNSAFE_BACKEND=1`.

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
exactly as it always did. This is also why an instruction with two guest
accesses may not use the access thunk for either of them: a bail-out on the
second would re-run the first, and an I/O read is not repeatable.

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

Source of truth: `X64Backend::canEmit()` (`JitBackendX64.cpp:2525`) plus the
emitters it dispatches to.

* straight-line: `MOVE`/`MOVEA`/`MOVEQ`; the
  `ADD`/`SUB`/`AND`/`OR`/`EOR`/`CMP` families in both directions;
  `ADDA`/`SUBA`/`CMPA`; `ADDQ`/`SUBQ`; the
  `ADDI`/`SUBI`/`ANDI`/`ORI`/`EORI`/`CMPI` immediates; `TST`, `CLR`, `NEG`,
  `NOT`, `EXT`, `SWAP`, `LEA`, `PEA`, `Scc`, `BTST` (both forms),
  `LINK`/`UNLK`/`NOP`, and
  **`MOVEM`** (both directions, both sizes, one span probe per burst, the
  040 restart latch `mmu040MovemArmed` checked);
* as block terminators: `Bcc`/`BRA`, `JSR`/`BSR`/`RTS`, **`DBcc`** (loops
  close internally like `Bcc`) and **`JMP <ea>`**;
* over addressing modes `Dn`, `An`, `(An)`, `(An)+`, `-(An)`, `d16(An)`,
  `(xxx).W`, `(xxx).L`, `d16(PC)` and immediate (`eaIndex()`).

Everything else — including every 68020 indexed mode (`eaIndex()` returns
−1 for mode 6 and for 7.3), every shift and rotate,
`MULU`/`MULS`/`DIVU`/`DIVS`, `ABCD`/`SBCD`/`EXG`,
`ADDX`/`SUBX`, `CMPM`, `MOVEP` and `MOVE SR,Dn` — falls back per instruction
to a cold stub that runs that one instruction through Moira and rejoins the
compiled stream. A block whose native coverage falls below half is refused
outright: it would be the same interpreter work plus a call and a frame.

**The two generators are not the same set, and neither is a superset.**
x64 has `Scc` and `PEA`; a64 (`canEmitReg()`, `JitBackendA64.cpp:572`) has
the immediate line-$E shifts and rotates (no `ROX` yet), the register
bitfield forms, brief-indexed `d8(An,Xn)` and `d8(PC,Xn)`, and
`MOVE SR,Dn` — and has neither `Scc` nor `PEA`. `jit_backend_test` asserts
the divergences explicitly (`0x40C0` and `0x0130` are keyed to the active
backend's name), so closing one is a gate edit as well as an emitter.

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

**Coverage.** 98.5 % native on the 3 000-frame Q605 budget after § 3.5's
work (was 89.6 % in the 2026-07-30 census, 96.2 % before the two cost-table
fixes). The residual 0.4 % of retired instructions is enumerated in § 3.5
along with the reason two of its items are OPEN rather than dropped.
Re-measure with `POM68K_JIT_HISTO=1` before quoting any of these; a census
taken over an idle Finder cannot price the indexed modes.

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
  `BackendCaps::dtlbCodeMask` the safety declaration. A backend that has not
  opted in still gets the whole-page refusal;
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

**One block class is barred from the chain in both directions**, and it was
found by measurement rather than reasoning: on the 68030 a block containing
a restartable write cannot be crossed as a transparent native boundary, so
the a64 backend publishes no link entry for it and emits no outgoing links
from it (`JitBackendA64.cpp:2216`, `:2453`). Disabling links altogether made
an otherwise-diverging 120k lockstep exact; barring only this class kept
every other link and passed the same gate (`docs/JIT_BRINGUP.md` § C).

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
  (`JitConfig.h:45`, `JitEngine.cpp:129`): `jit/auto` on 68040, interpreter
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
