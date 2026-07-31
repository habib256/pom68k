# POM68K JIT — design, invariants, journal

A **second execution engine**, living beside the Moira interpreter and never
in front of it. Off by default everywhere — GUI, headless, CTest. The user
turns it on from the **CPU** menu (or `POM68K_CPU_ENGINE=jit`) to see what it
does; the interpreter remains what every accuracy claim in this project rests
on.

Read `extern/moira/POM68K_VENDOR.md` § *JIT seam* for the ten-point
extension this subsystem needs inside the vendored core.

**On the name (2026-07-28, after an honest question).** What ships by
default under the CPU menu is NOT a JIT: it is the interpreter running
behind a fetch window, with a block replayer on top. Only the x86-64
backend actually emits machine code — a JIT in the strict sense, and a
cycle-exact one, which is precisely why its wins are bounded (§ 7). The GUI
says "Moteur accéléré" and names the backend (`main.cpp:855` and `:691`);
the subsystem keeps its internal name because `src/jit/` names the seam and
the machinery, which a future non-conformant fast mode
(`docs/HLE_OVERLAY.md`) would build on. The five relaxations a classic 68k
JIT makes and this engine refuses — coarse time, coarse interrupts, a big
soft TLB instead of exact ATC semantics, lazy flags (legal here, still to
do), long traces — are catalogued in the CHANGELOG's 2026-07-28 eighth-pass
entry and in `TODO.md`.

**State.** Two backends. `threaded` replays a recorded block through Moira's
own handlers with the fetch window armed, and is valid for every guest.
`x86-64` (§ 7) generates host code for a real subset of the ISA, with an
inline data TLB (§ 8) and control transfers compiled as block terminators,
so a loop closing on itself never returns to the engine. On the 68040
machines `auto` picks x64 and it is the faster of the two on every regime
measured (§ 3). Both are bit-exact against the interpreter — registers,
supervisor stacks, cycle clock and the low 2 KB of guest RAM, compared at
every instruction boundary.

**Where the engine is wired.** Eight CPU wrappers hold a `jit::Engine`:
`Cpu040`, `CentrisCpu`, `Q630Cpu`, `Q700Cpu` (68040) and `Cpu030`,
`RbvCpu`, `SonoraCpu`, `VaspCpu` (68030, plus the Macintosh LC's 68020
flavour of `Cpu030`). The 68000 machines (`Cpu68k`) and the Mac II /
IIx / IIcx (`Cpu020`) have none. The **x86-64 code generator** is narrower
still: 68040 guests only, by declared capability (§ 7).

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
        threaded  ─────  x86-64 (J2)  ─────  aarch64 (planned)
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
| `nativeCode` | `blockCacheEnabled()` | the block cache's default (§ 3) |

The per-opcode question is **not** a block boundary: `Backend::canEmit()` is
consulted by the opcode census (`POM68K_JIT_HISTO`) and by
`X64Backend::compile()`, which refuses a block whose native coverage is below
half. Anything an otherwise-compilable block contains that the backend cannot
emit becomes a per-instruction cold stub inside the generated code, not a
shorter block. Block termination is the classifier's job alone (§ 4).

---

## 2. Invariants

Each one names the gate that would catch it breaking.

| # | invariant | gate |
|---|---|---|
| 1 | **The interpreter is the reference.** Any divergence between engines is a JIT bug, never an interpreter bug. | `jit_lockstep_test` (five registrations, § 5) |
| 2 | **Exits happen at instruction boundaries only.** No partial guest state — registers, CCR, PC, clock — ever survives a block exit. Everything unusual (interrupt, trace, STOP, breakpoint, MMU fault, an opcode outside the classifier) is handed back to `Moira::execute()` at a clean boundary. | `jit_lockstep_x64_fine_test` (one cycle per comparison); `jit_backend_test` for the classifier rules |
| 3 | **The default is the interpreter**, in the GUI, headless and under CTest (`JitConfig.h defaultEngine()`). | every gate that is not `jit_*`; the `jit_*` gates are separate registrations of the same binaries under `POM68K_CPU_ENGINE=jit` |
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

### Measured

A boot etalon is a poor stopwatch — it stops the moment it recognises the
Finder, so the two engines get timed over *different* amounts of guest work
and the ratio flatters whichever arrived first. The table below therefore
comes from `tests/jit_bench.cpp` (a dev tool, `make jit_bench`, **not** a
CTest gate), which runs a **fixed guest-cycle budget**: the same
instructions, the same peripheral schedule, wall clock the only variable.
`POM68K_BENCH_FRAMES` sets the budget in frames of 416 667 cycles. It prints
a fingerprint of the whole architectural state at the end, and every number
here was taken with all three engines printing the **same** fingerprint.

Quadra 605, Mac OS 8.1. Idle host, adjacent A/B pairs ×3, load 1.00,
**2026-07-31**:

| | 5 G cycles (12 000 frames) | 20 G cycles (48 000 frames) |
|---|---|---|
| what the guest is doing | System + extensions loading | whole boot, then idle |
| Moira interpreter | 213.1 s | 903.9 s |
| JIT, `threaded` | 126.1 s ×1.69 | 565.7 s ×1.60 |
| JIT, `x86-64` — what `auto` picks on an 040 | **97.6 s ×2.18** | **432.3 s ×2.09** |

End to end, `q605_boot_etalon` reaching the 256-colour Finder: **61.3 s
interpreted, 22.9 s on the JIT — ×2.68.**

**What still costs, and it is not code size.** Over 12.2 G instructions the
exit counters show **794 M window-lost exits** on `threaded` — one window
death every ~15 instructions. Mac OS 8.1's VM ages pages by writing
descriptor U bits; every ATC eviction kills the derived window and TLB state
for that page (`Moira::pomJitAtcEvict`, and that is the exactness contract,
not a bug); the idle Finder lives under that regime. No conformant backend
escapes it. It is the measured ceiling at the idle Finder, and the reason
the 20 G ratio is below the 5 G one.

Superseded measurements, and what changed between them, are in § 10.

### Why the block cache is OFF for the portable backend

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
the `$4Exx` group *except* the carve-outs below (RTE, RTD, TRAP, TRAPV, RTR,
RESET, STOP, MOVE USP, MOVEC); `MOVE`/`ANDI`/`ORI`/`EORI` to or from SR/CCR;
`MOVES`, `CAS`/`CAS2`, `CMP2`/`CHK2`; `TAS` (a locked RMW — it sets
`mmu040Lrmw`, so its read translates with write semantics); `BKPT`;
`TRAPcc`; the whole A-line; and the whole F-line (FPU, PFLUSH/PTEST,
CINV/CPUSH, MOVE16). Everything else is already caught by the replay checks.

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

Do not iterate against a bare `ctest` — 129 gates, ~2h30, and `-j` is unsafe
because the boot etalons are contention-sensitive. Do not iterate against a
bare `make` either: tree-wide LTO relinks ~90 binaries after any core change.

```bash
make -j4 jitdev && ctest -L smoke     # ~2.5 min end to end
```

`jitdev` builds the three binaries `-L smoke` needs (`jit_backend_test`,
`jit_lockstep_test`, `q605_boot_etalon`) — the other five registrations
re-run those same binaries under different environments. `-L smoke` is
**eight** gates: `jit_backend_test`, five flavours of `jit_lockstep_test`,
and the q605 boot etalon **on both engines**.

`jit_lockstep_test` is the one that matters: two Quadra 605 machines from one
ROM and one read-only disk image, one interpreted and one JIT-driven,
compared at every checkpoint on every architectural register, the supervisor
stacks, the cycle clock — **and the first 2 KB of guest RAM**. That last one
is not decoration. A JIT bug in a STORE shows up in a register only much
later, when something reads the byte back; the 68k system globals live in low
RAM and are written constantly during a boot, which makes them a cheap, high
yield tripwire. Its FIVE registrations exist because each covers something
the others cannot:

| gate | what it pins |
|---|---|
| `jit_lockstep_test` | the default backend at one cycle per comparison — the sharpest possible check, but a block can never be more than one instruction long |
| `jit_lockstep_blocks_test` | `threaded` + `POM68K_JIT_BLOCKS=1` — the portable floor with its block path forced on |
| `jit_lockstep_x64_test` | the code generator at 256 cycles per comparison — long blocks, and a loop closing on itself entirely inside generated code |
| `jit_lockstep_x64_fine_test` | the code generator at one cycle per comparison |
| `jit_lockstep_noaccess_test` | x64 + the conservative data path (`POM68K_JIT_ACCESS_THUNK=0`) |

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

| command | gates | time | when |
|---|---|---|---|
| `ctest -L unit` | 59 | ~40 s | anything touching non-machine code |
| `ctest -L jit` | 16 | ~30 min | before proposing a JIT change |
| `ctest -L m040` | 30 | ~1h30 | the 68040 family — the JIT's blast radius |
| `ctest` | 129 | ~2h30 | the release gate, once |

(Gate counts re-derived from `ctest -N` on 2026-07-31; timings from a real
run. `CMakeLists.txt`'s own inline comment carries older, more optimistic
estimates. Both drift every time a gate lands — re-derive rather than trust
either.)

Labels are derived from test names in `CMakeLists.txt`, so a new gate is
classified the moment it is registered.

---

## 6. Environment surface

Everything in `JitConfig.h` unless noted.

| Variable | Default | Meaning |
|---|---|---|
| `POM68K_CPU_ENGINE` | `interp` | `jit` starts on the JIT (the GUI menu still switches live) |
| `POM68K_JIT_BACKEND` | `auto` | `auto` \| `threaded` \| `x64` \| `a64` |
| `POM68K_JIT_UNSAFE_BACKEND` | `0` | force an explicitly named backend onto a guest family it does not declare (`JitBackend.cpp`) — for developing that family's support, never for use |
| `POM68K_JIT_FETCH` | `1` | the instruction-fetch code window (J1a) |
| `POM68K_JIT_BLOCKS` | *backend* | block discovery and replay (J1b). The default is the ACTIVE BACKEND's answer, not a constant (`blockCacheEnabled(dflt)`): OFF for `threaded`, which measured slower with blocks than with the window alone, ON for a code generator, which has nothing to run without them |
| `POM68K_JIT_BLOCK_MAX` | `64` | straight-line instruction ceiling per block, itself capped by `caps().maxBlockInstrs` |
| `POM68K_JIT_HOT` | `512` | visits before a recorded block is translated |
| `POM68K_JIT_MAX_BLOCKS` | `65536` | blocks kept before the engine STOPS RECORDING (it does not flush — a flush is what a code generator cannot afford) |
| `POM68K_DATA_WINDOW` | `0` | the INTERPRETER's data window (§ 8) — opt-in since the ATC-exactness capping made it a net loss (`JitEngine.cpp:39-53`) |
| `POM68K_JIT_PARANOID` | `0` | re-validate the translation at every arm — for differential testing (`JitEngine.cpp`) |
| `POM68K_JIT_VERBOSE` | `0` | backend selection, block dumps and flush chatter on stderr |
| `POM68K_JIT_ACCESS_THUNK` | `2` | 0 = whole-instruction fallback, 1 = loads, 2 = loads and stores |
| `POM68K_JIT_HISTO` | `0` | dynamic opcode census, dumped at exit, with the backend's `canEmit()` coverage (`JitEngine.cpp dumpHisto()`) |
| `POM68K_JIT_LOCKSTEP_N` | `5000000` | instructions compared by `jit_lockstep_test` |
| `POM68K_JIT_LOCKSTEP_BUDGET` | `1` | cycles between comparisons (higher = longer blocks) |
| `POM68K_JIT_LOCKSTEP_FINE_AT` | — | step after which the comparison drops to one cycle |
| `POM68K_BENCH_FRAMES` | `3000` | `tests/jit_bench.cpp` only — frames of 416 667 cycles to run |

`POM68K_JIT_FETCH` and `POM68K_JIT_BLOCKS` are not independent, and that is
deliberate: block discovery reads opcodes out of the code window, so
`POM68K_JIT_FETCH=0` disables both and leaves the engine measuring nothing
but its own dispatch overhead — useful exactly once, as the zero point.
`POM68K_JIT_BLOCKS=0` is the interesting attribution knob on a
code-generating backend: window on, no generated code at all.

---

## 7. The x86-64 backend (J2)

> **Scope: the 68040 family only** (`caps().guestFamilies = kGuest68040`).
> Everything below is written against the 040's instruction-boundary
> contract, and the differences from the 68030 are semantic, not cosmetic:
> `(An)+` updates the register *before* the access on an 030 and *after* it
> on an 040 (`MoiraDataflow_cpp.h:326-332`), the 030 marks its last write
> restartable and stacks a format $A frame (`:355-361`), the prefetch queue
> is refilled at the end of an instruction on one and not the other (so
> `queue.irc` means different things at a block exit), and the data thunks
> `pomJitReadData`/`pomJitWriteData` reach `mmu040Read`/`mmu040Write`
> unconditionally while `pomJitProbeData` refuses everything below
> `M68EC040` outright.
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
> machines and on x64 for the 040s.
>
> Widening the scope is a project, not a flag: the 030's update order and
> prefetch semantics in the emitters, a 030 branch in `pomJitProbeData`,
> model-correct access thunks, and an `lcii`/x64 lockstep gate to prove it —
> the lockstep gates today run **two Quadra 605 machines**, so the 030 code
> generator would have no differential coverage at all until one exists.

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

Source of truth: `X64Backend::canEmit()` (`JitBackendX64.cpp:2074`) plus the
emitters it dispatches to.

* straight-line: `MOVE`/`MOVEA`/`MOVEQ`; the
  `ADD`/`SUB`/`AND`/`OR`/`EOR`/`CMP` families in both directions;
  `ADDA`/`SUBA`/`CMPA`; `ADDQ`/`SUBQ`; the
  `ADDI`/`SUBI`/`ANDI`/`ORI`/`EORI`/`CMPI` immediates; `TST`, `CLR`, `NEG`,
  `NOT`, `EXT`, `SWAP`, `LEA`, `BTST` (both forms), `LINK`/`UNLK`/`NOP`, and
  **`MOVEM`** (both directions, both sizes, one span probe per burst, the
  040 restart latch `mmu040MovemArmed` checked);
* as block terminators: `Bcc`/`BRA`, `JSR`/`BSR`/`RTS`, **`DBcc`** (loops
  close internally like `Bcc`) and **`JMP <ea>`**;
* over addressing modes `Dn`, `An`, `(An)`, `(An)+`, `-(An)`, `d16(An)`,
  `(xxx).W`, `(xxx).L`, `d16(PC)` and immediate (`eaIndex()`).

Everything else — including every 68020 indexed mode, every shift and
rotate, `Scc`, `PEA`, `MULU`/`MULS`/`DIVU`/`DIVS`, `ABCD`/`SBCD`/`EXG`,
`ADDX`/`SUBX`, `CMPM` and `MOVEP` — falls back per instruction to a cold
stub that runs that one instruction through Moira and rejoins the compiled
stream. A block whose native coverage falls below half is refused outright:
it would be the same interpreter work plus a call and a frame.

### What it is worth

The generator wins on both bench regimes and on the boot etalon — the
numbers are in § 3's table; this section is what produced them. Three landed
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

**Coverage.** The last census (`POM68K_JIT_HISTO=1`, blocks off, x64 column)
read **89.6 % native over 12.2 G instructions** — but it was taken on
2026-07-30 **before** `MOVEM`/`DBcc`/`JMP` landed, and those were the 5.2 %
sitting at the top of its uncovered list. Coverage today is therefore
around 95 %, and **has not been re-measured** — re-run the census before
quoting a number. After the five, the uncovered list is led by line-$E
shifts (0.9 %), `Scc`, `PEA`, and the 68020 indexed modes, which are what
QuickDraw's blitters are built from.

**The next lever is not code density.** The binding cost at the idle Finder
is the exactness contract itself (§ 3: one window death per ~15
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
  modified, because that write owes the descriptor an M bit;
* **no I/O and no unmapped hole** — `dataSpan` (per machine, e.g.
  `Q605Memory::dataSpan`) hands back plain RAM, ROM for a read, and the
  **framebuffer aperture**, and nothing else. The framebuffer is in
  deliberately: QuickDraw drawing a 640×480×8 desktop is an enormous number
  of plain array stores, and leaving them on the slow path made the code
  generator slower than the interpreter for the whole Finder phase. The
  video CELL registers next door (`$F98000xx`) stay out — reads there latch
  and auto-increment. So do ROM seen by a store, and RAM while a debug
  write-watch is armed;
* **no store into a page holding translated code** — that one has to go
  through the memory map, so the write guard sees it (`codePage_`);
* **4 KB and 8 KB MMU pages only**, filled as independent 4 KB slices. The
  first cut refused anything but 4 KB — "the 68040 boots with 4 KB on every
  Mac" — which stops being true the moment the System arms paging, and the
  refusal cost a call, a probe and a rejection on *every* data access:
  ~7 s slower across engines.

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
clearing both 4 KB tables, once per ~15 idle instructions (§ 3), paid even
by `threaded`, which never reads the DTLB at all. It was standing in for
invalidations that every one already have an exact owner:

| what could go stale | who kills it |
|---|---|
| the translation itself changed | `Moira::pomJitMapMoved()` flushes at the source of every `pomJitMmuGen` bump |
| an ATC entry was evicted | `pomJitAtcEvict()`, per page and per space |
| privilege changed | it rides in each entry's tag (bit 31) |
| a page gained translated code | `markPages()` flushes when it marks |
| a page lost its last block | `serviceGuard()` flushes when it unmarks |

So deleting it is conformance-neutral (two 60 M-step locksteps bit-identical,
x64 and threaded; `ctest -L jit` 16/16) and worth −23 to −33 % of wall clock
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

The link is a **table**, not a patched jump: `Engine::linkTable_`, 4096 slots,
direct mapped on the guest pc (`(pc >> 1) & 4095`) and tagged with
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

* the target is entered **past its prologue** (`linkEntry_`), so it inherits
  the callee-saved registers — `Moira*`, the frame, the clock and its target,
  the pacing baseline — and the retired-instruction count accumulated so far;
* privilege cannot change inside a chain (every SR write is `Unsafe`), so a
  block compiled for one privilege can only be reached from another with the
  same one — which is what the tag's bit 0 enforces;
* the MMU generation cannot change either, so the data TLB entries the chain
  relies on stay valid without the engine re-arming anything between blocks.

Every block's first instruction still runs the budget and flag guards, so a
chain cannot outrun the caller's cycle target or ignore a pending interrupt.

---

## 10. Journal

* **2026-07-31 — the arm-time DTLB flush deleted, and everything
  re-measured.** § 8 has the argument, § 3 the current table. This is also
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
* **2026-07-27 — J0/J1 measured and hardened.** The fetch window is worth
  −55 % on the Quadra 605 boot; the block cache is a net loss on `threaded`
  and ships off there. An adversarial review pass found and closed two real
  defects in the block path before the gates were called green:
  `flushAll()` was reachable re-entrantly from inside a replaying block (a
  guest `MOVEC` to CACR → `didChangeCACR`), freeing the `BlockIr` under the
  backend's own loop; and the block cache had no notion of `pomJitMmuGen`,
  so a `PFLUSH` or a TC/URP/SRP write could point a cached script at
  unrelated code. Flushes are now deferred while a block is in flight, and
  the cache tracks the MMU generation.
* **2026-07-27 — J0/J1.** Engine, host-neutral IR, backend interface,
  portable W^X code buffer, `threaded` backend, code window, block tracing,
  write guard, GUI **CPU** menu with live switching and a gauge window.
  Machines wired: Quadra 605 / LC 475 / LC 575 (`Cpu040`), Centris and Quadra
  610/650/800 (`CentrisCpu`), Quadra 630 / LC 580 (`Q630Cpu`), Quadra 700
  (`Q700Cpu`). The 68030 wrappers (`Cpu030`, `RbvCpu`, `SonoraCpu`,
  `VaspCpu`) followed on 2026-07-28.
