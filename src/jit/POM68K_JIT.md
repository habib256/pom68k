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
cycle-exact one, which is precisely why its wins are modest (§ 7). The GUI
now says "Moteur accéléré" and names the backend; the subsystem keeps its
internal name because `src/jit/` names the seam and the machinery, which a
future non-conformant fast mode (docs/HLE_OVERLAY.md) would build on. The
five relaxations a classic 68k JIT makes and this engine refuses — coarse
time, coarse interrupts, a big soft TLB instead of exact ATC semantics,
lazy flags (legal here, still to do), long traces — are catalogued in the
CHANGELOG's eighth-pass entry and in TODO.

**State (2026-07-28).** Two backends. `threaded` replays a recorded block
through Moira's own handlers with the fetch window armed and is the measured
default. `x86-64` (§ 7) generates host code for a real subset of the ISA,
with an inline data TLB (§ 8) and branches compiled as block terminators, so
a loop closing on itself never returns to the engine. Both are bit-exact
against the interpreter — registers, supervisor stacks, cycle clock and the
low 2 KB of guest RAM, compared at every instruction boundary.

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
`POM68K_JIT_BACKEND=auto` walks the candidate list most-capable-first and
takes the first one that reports `usable()`; the floor is never missing.

`jit::BackendCaps` is the growth path: the block builder stops a block before
anything the active backend cannot handle, so a new backend can start with
twenty opcodes and widen without the engine changing at all.

---

## 2. Invariants

These are not aspirations; each one is checked by a gate.

1. **The interpreter is the reference.** Any divergence between engines is a
   JIT bug, never an interpreter bug. Gate: `jit_lockstep_test`.
2. **Exits happen at instruction boundaries only.** No partial guest state —
   registers, CCR, PC, clock — ever survives a block exit. Everything unusual
   (interrupt, trace, STOP, breakpoint, MMU fault, an opcode outside the
   classifier) is handed back to `Moira::execute()` at a clean boundary.
3. **The default is the interpreter**, in the GUI, headless and under CTest.
   Gate: the full default-engine `ctest` run must match the pre-JIT baseline.
4. **Peripheral time stays owed.** Blocks never run past the caller's cycle
   target, so `sync()`/`catchUp()` batching is unchanged and VIA, ASC, SWIM
   and the Egret/Cuda MCU keep their pacing.
5. **Nothing cached survives a change of the address map.** Overlay flips,
   MMU/ATC changes and cache-control writes drop the block cache and the code
   window.
6. **No host knowledge above `jit::Backend`.** An architecture `#ifdef`
   outside `src/jit/backends/` or `JitCodeBuffer.cpp` is a design error.
7. **Every host POM68K builds for can run the JIT** — on `threaded` at worst.

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
measured win comes from.

Two consequences worth stating explicitly:

* The window points **into the guest memory buffer itself**, so a guest write
  is visible to it immediately. Self-modifying code needs no invalidation
  here — what goes stale is the *recorded block* (and, later, generated
  code), which is what `jit::CodeGuard` protects.
* On the 68040 path Moira's `SYNC(x)` macro expands to nothing
  (`MoiraMacros.h`: `if constexpr (C != Core::C68020)`), so serving a fetch
  from the window changes **no** cycle accounting whatsoever. The window is a
  pure host-side saving.

### Measured (q605_boot_etalon, boot to the 256-colour Finder)

A boot etalon is a poor stopwatch — it stops the moment it recognises the
Finder, so the two engines get timed over *different* amounts of guest work
and the ratio flatters whichever arrived first. Every number below therefore
comes from `tests/jit_bench.cpp`, which runs a **fixed guest-cycle budget**:
the same instructions, the same peripheral schedule, wall clock the only
variable. It prints a fingerprint of the whole architectural state at the
end, and every measurement here was taken with all three engines printing
the **same** fingerprint.

| Quadra 605, Mac OS 8.1 | 0.83 G cycles | 5 G cycles | 20 G cycles |
|---|---|---|---|
| what the guest is doing | ROM POST, RAM sizing | System + extensions loading | whole boot, then idle |
| Moira interpreter | 6.41 s | 42.59 s | 215.77 s |
| JIT, `threaded` — **the default** | **3.48 s ×1.84** | 17.33 s ×2.46 | **131.77 s ×1.64** |
| JIT, `x86-64` | 3.48 s ×1.84 | **16.75 s ×2.54** | 180.6 s ×1.19 |

All three print the same state fingerprint at every row. And the end-to-end
number a user actually feels, `q605_boot_etalon` reaching the 256-colour
Finder: **60.05 s interpreted, 29.02 s on the JIT — ×2.07.**

The x86-64 column crosses over: it is the fastest engine while the guest is
executing System code, and loses once the Finder is up. § 7 says why, and
the second half of that section is now a measurement rather than a guess.

> **STALE TABLE — re-baselined 2026-07-30.** The rows above predate the
> ATC-eviction bit-exactness capping (`pomJitAtcEvict`, 2026-07-28) and are
> kept only for the shape of the argument. Current idle-host numbers:
>
> * `q605_boot_etalon`: interp 61.4 s → threaded 32.3 s (×1.90) → **x64
>   25.6 s (×2.40)**; `auto` = x64, confirmed.
> * `jit_bench` (2026-07-31 night matrix — idle host, adjacent A/B pairs
>   ×3, load 1.00): interp 213.1 / 903.9 s; threaded 126.1 / 565.7 s;
>   **x64 97.6 / 432.3 s** on the 5 G / 20 G regimes — x64 is ×2.18 /
>   ×2.09 over the interpreter, and the boot etalon reads 61.3 s interp
>   → **22.9 s JIT (×2.68)**. Includes the five census opcodes AND the
>   arm-time DTLB-flush removal (below). The §7 crossover has FLIPPED:
>   x64 beats threaded on both regimes.
> * **The churn finding (2026-07-31):** every window re-arm performed an
>   unconditional `pomJitDtlbFlush()` — a 2 KB memset, once per ~15 idle
>   instructions, ≈1.3 TB of traffic per long run, paid even by threaded
>   which never uses the DTLB. Every invalidation it stood in for has an
>   exact owner (gen bumps flush at the source, ATC evictions kill per
>   entry and per space, privilege rides in the entry tag, code-page
>   mark/unmark flush themselves), so deleting it is conformance-neutral
>   (2×60 M-step locksteps bit-identical) and worth −23 to −33 % wall
>   clock depending on backend and regime. DTLB fills: 942 M → 7.8 M
>   over one 60 M-step lockstep.
> * Both engines are ~10× slower on this bench than the stale rows, and
>   the exit counters name the cause: 794 M window-lost exits on threaded
>   over 12.2 G instructions — one window death every ~15 instructions.
>   Mac OS 8.1's VM page-aging writes descriptor U bits, every ATC
>   eviction kills the derived window/TLB state (the exactness contract),
>   and the idle Finder lives under that regime. This — not code density —
>   is the measured conformant ceiling at the idle Finder.
> * Dynamic census (`POM68K_JIT_HISTO=1`, blocks off, x64 coverage
>   column): **89.6 % native** over 12.2 G instructions (not the 66-70 %
>   quoted below). The uncovered top of the idle phase is five opcodes:
>   MOVEM push/pop/load ($48E7/$4CDF/$4CEE, 3.3 %), **DBRA ($51C8,
>   1.26 % — classified Branch but REFUSED by canEmit, so every DBRA loop
>   iteration exits the block**), and JMP abs.L ($4EF9, 0.66 %).

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

A block is recorded **by executing it**. The engine runs instructions one at
a time through `Moira::pomJitExecOne()` and writes down where each one
started and how far the pc moved. Consequences:

* no second 68k decoder to keep in sync with Moira — instruction lengths fall
  out of the pc deltas;
* a recorded block can never describe something that did not happen;
* variable-length instructions, extension words and `caps()`-driven
  termination all come for free.

Replay re-verifies, every instruction: the clock budget, `flags == 0`, that
`getPC()` equals the recorded pc, and that the pc is still inside the window.
The pc check is the catch-all — an unpredicted trap, a fault redirect or code
rewritten under us all land there and exit the block cleanly.

A block ends **before** the first instruction that `jit::classify()`
(`JitIr.h`) calls `Kind::Unsafe`, and **after** the first one it calls
`Kind::Branch`. The classifier runs at build time only, so it is explicit
rather than clever.

"Unsafe" means it can change the MMU translation, the ATC, the caches or the
supervisor bit — all of which would silently stale the code window (`MOVEC`,
`MOVES`, `PFLUSH`/`CINV`/`CPUSH` and the rest of the F-line,
`MOVE`/`ANDI`/`ORI`/`EORI` to SR or CCR, the whole `$4Exx` group, and `BSR`,
which stacks a return address). Everything else is already caught by the
replay checks.

"Branch" is `Bcc`/`BRA`/`DBcc`: the block's **terminator**, and part of it.
Their target is a compile-time constant, so a backward branch into the block
it belongs to becomes an internal jump and the loop never returns to the
engine at all — which is most of what a code generator is for here. A
branch's length cannot be read off the pc delta the way every other
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

(Counts and timings verified against `ctest -N` and a real run on
2026-07-31. They drift every time a gate lands — re-derive rather than
trust them.)

Labels are derived from test names in `CMakeLists.txt`, so a new gate is
classified the moment it is registered.

## 6. Environment surface

| Variable | Default | Meaning |
|---|---|---|
| `POM68K_CPU_ENGINE` | `interp` | `jit` starts on the JIT (the GUI menu still switches live) |
| `POM68K_JIT_BACKEND` | `auto` | `auto` \| `threaded` \| `x64` \| `a64` |
| `POM68K_JIT_FETCH` | `1` | the instruction-fetch code window (J1a) |
| `POM68K_JIT_BLOCKS` | *backend* | block discovery and replay (J1b). The default is the ACTIVE BACKEND's answer, not a constant (`JitConfig.h blockCacheEnabled(dflt)`): OFF for `threaded`, which measured slower with blocks than with the window alone, ON for a code generator, which has nothing to run without them |
| `POM68K_JIT_BLOCK_MAX` | `64` | straight-line instruction ceiling per block |
| `POM68K_JIT_HOT` | `512` | visits before a recorded block is translated |
| `POM68K_JIT_MAX_BLOCKS` | `65536` | blocks kept before the engine STOPS RECORDING (it does not flush — a flush is what a code generator cannot afford) |
| `POM68K_DATA_WINDOW` | `0` | the INTERPRETER's data window (§8) — opt-in since the ATC-exactness capping made it a net loss |
| `POM68K_JIT_PARANOID` | `0` | re-validate the translation at every arm — for differential testing |
| `POM68K_JIT_VERBOSE` | `0` | backend selection, block dumps and flush chatter on stderr |
| `POM68K_JIT_ACCESS_THUNK` | `2` | 0 = whole-instruction fallback, 1 = loads, 2 = loads and stores |
| `POM68K_JIT_HISTO` | `0` | dynamic opcode census, dumped at exit, with the backend's coverage |
| `POM68K_JIT_LOCKSTEP_N` | `5000000` | instructions compared by `jit_lockstep_test` |
| `POM68K_JIT_LOCKSTEP_BUDGET` | `1` | cycles between comparisons (higher = longer blocks) |
| `POM68K_JIT_LOCKSTEP_FINE_AT` | — | step after which the comparison drops to one cycle |

The first two are not independent, and that is deliberate: block discovery
reads opcodes out of the code window, so `POM68K_JIT_FETCH=0` disables both
and leaves the engine measuring nothing but its own dispatch overhead —
useful exactly once, as the zero point. `POM68K_JIT_BLOCKS=0` is the
interesting attribution knob on a code-generating backend: window on, no
generated code at all.

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
> hour**: generated code wedged the guest in the ROM's Egret handshake poll
> loop (blocks around `$40A148xx-$40A149xx` and `$40A0A8E6`, `BTST`/`TST` +
> branch), eight blocks compiled and then nothing at all, while the same
> machine boots in **2 min 21 s** on `threaded`. Selection now tests guest
> validity before host usability ranking (`JitBackend.h § GuestFamily`), so
> `auto` lands on `threaded` for the 68000/020/030 machines and on x64 for
> the 040s — which is exactly where its 26.1 s vs 32.6 s was measured.
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
value in a host register across an exit.

**Cycle counts are checked, not trusted.** The cost tables are transcribed
from Moira's own `CYCLES_*` tables (the 68020 column, which is what the
68040 core uses), and an instruction is compiled only if the table agrees
with what the tracer measured when it actually ran it. A wrong or missing
entry costs coverage, never correctness — and it is what automatically
excludes every access whose cost depends on a device wait state.

What it emits natively: `MOVE`/`MOVEA`, the `ADD`/`SUB`/`AND`/`OR`/`EOR`/
`CMP` families in both directions, `ADDA`/`SUBA`/`CMPA`, `ADDQ`/`SUBQ`,
`MOVEQ`, the `ADDI`/`SUBI`/`ANDI`/`ORI`/`EORI`/`CMPI` immediates, `TST`,
`CLR`, `NEG`, `NOT`, `EXT`, `SWAP`, `LEA`, `BTST`, `LINK`/`UNLK`/`NOP`,
**`MOVEM`** (both directions, both sizes, one span probe per burst, the
040 restart latch checked), and as block terminators `Bcc`/`BRA`,
`JSR`/`BSR`/`RTS`, **`DBcc`** (loops close internally like Bcc) and
**`JMP <ea>`** — over addressing modes `Dn`, `An`, `(An)`, `(An)+`,
`-(An)`, `d16(An)`, `(xxx).W`, `(xxx).L`, `d16(PC)` and immediate. The
2026-07-30 census pass added the last four names (five opcodes carried
~5 % of the idle Finder; measured −3.0 % / −1.7 % on the 5 G / 20 G
regimes, boot etalon 25.6 → 24.7 s). Everything else, including every
68020 indexed mode, falls back per instruction.

### What it is worth (re-measured 2026-07-31, idle host)

**The generator now wins on both regimes**, which retires the old
"crossover" story in this section. Against the fetch window on
`jit_bench`: 5 G cycles 97.6 s vs 126.1 s, 20 G cycles 432.3 s vs 565.7 s
— and against the interpreter, ×2.18 and ×2.09. The user-facing
`q605_boot_etalon` reads 61.3 s interpreted → **22.9 s (×2.68)**, through
what this file used to call a ~×2.5 conformant ceiling.

Three landed changes produced that, in order of size:

1. **The arm-time DTLB flush is gone** (2026-07-31). Every window re-arm
   used to `pomJitDtlbFlush()` unconditionally — a 2 KB memset once per
   ~15 idle instructions. Deleting it is conformance-neutral (every
   invalidation it stood in for has an exact owner: MMU-generation bumps,
   `pomJitAtcEvict`, the privilege bit in each tag, code-page mark/unmark)
   and worth −23 to −33 % depending on backend and regime. DTLB fills over
   one 60 M-step lockstep: 942 M → 7.8 M.
2. **Block linking** (§ 9) plus `LINK`/`UNLK`/`NOP`: block entries on the
   loading phase fell 53 % (2.41 M → 1.14 M), 268 → 566 instructions per
   entry.
3. **The census five** (2026-07-30): `MOVEM`, `DBcc`, `JMP <ea>` — ~5 % of
   idle-Finder instructions, −3.0 % / −1.7 % on the two regimes.

**Coverage is 89.6 % native** over 12.2 G instructions (`POM68K_JIT_HISTO`,
2026-07-30) — the "70 % / 66 %" figures this section used to quote
predate `JSR`/`RTS`/`LINK`/`MOVEM`/`DBcc`/`JMP`. The uncovered remainder
is led by line-$E shifts (0.9 %), `Scc`, `PEA`, and the 68020 indexed
modes, which are a brief-extension-word decoder in their own right and
are what QuickDraw's blitters are built from.

**What still costs at the idle Finder is the exactness contract itself**,
not code size: 794 M window-lost exits over 12.2 G instructions is one
window death per ~15 instructions, because Mac OS 8.1's VM ages pages by
writing descriptor U bits and every ATC eviction kills the derived
window. No conformant backend escapes that. The old FOOTPRINT theory
(47 000 blocks × ~1.5 KB) is not disproved but is no longer the leading
term, and the DENSITY item it motivated (~150 B of host code per guest
instruction, mostly the per-instruction contract) is now third-order:
worth doing (local `rel8`, shared cold stubs) but not the lever.

What is already right, and should not be traded away: the block cache no
longer flushes on a write into translated code (§ 8), the data path never
takes a C++ exception through a JIT frame, and every cycle count is
cross-checked against the interpreter's own measurement before an
instruction is compiled at all.

---

## 8. The data path: an inline TLB, and what it refuses

Generated code cannot call `mmu040Read` (it throws). So a data address is
translated inline, against `Moira::pomJitDtlbR/W` — 64 direct-mapped
entries, 16 bytes each so one `lea` reaches them, tagged by logical page.

`jit::Engine::fillDtlb` is the ONLY door into that cache, and the refusals
are the safety argument for the whole path:

* **no page-table walk, no U/M write-back** — `pomJitProbeData` only reads
  resident ATC entries, and refuses a write to a page not already marked
  modified, because that write owes the descriptor an M bit;
* **no I/O, no VRAM aperture, no unmapped hole** — `dataSpan` hands back
  plain RAM/ROM bytes and nothing else;
* **no store into a page holding translated code** — that one has to go
  through the memory map, so the write guard sees it.

A refusal is *cached* as an entry with a null host pointer, because a
hardware poll loop would otherwise pay a call per iteration to be told the
same thing. A refused address then costs a tag compare and a null test
before taking the access thunk.

### Invalidation: evict, do not flush

A write into memory a block was translated from used to drop the whole
cache. With generated code in it that is ruinous: one boot phase took
**5 313** such flushes, and the engine spent its time re-translating code it
had already translated. Two changes fixed it — the guard's granularity went
from 4 KB to 256 bytes (68k code and its data share pages constantly), and
`serviceGuard()` now evicts only the blocks that overlap the written slices.
Same phase, after: **27** flushes.

---

## 9. Block linking

A block that ends on a branch used to return to the engine, which then did a
hash lookup and built a fresh frame to enter the next one. On a real Mac OS
workload a THIRD of all instructions are control transfers (23 % branches,
10 % call/return), so that round trip was being paid every few instructions.

The link is a **table**, not a patched jump: `Engine::linkTable_`, direct
mapped on the guest pc and tagged with `pc | super` — pc is always even, so
the privilege bit rides in bit 0 and a user-mode and a supervisor block at
one address cannot be confused. An exit whose target is a compile-time
constant emits four instructions: load the table, compare the tag, jump
through the entry. The slot index is folded at compile time because the
target pc is a constant.

Why not patch the jumps directly, as most JITs do? Because a boot evicts
blocks **237 000 times**, and un-patching every jump INTO an evicted block
means maintaining incoming/outgoing link lists that can dangle when a block
is freed. Invalidating one table slot is O(1), cannot dangle, and is exact:
a block's slot is a function of its pc, so if the slot still carries this
block's tag it IS this block.

Three things make jumping straight into another block safe:

* the target is entered **past its prologue**, so it inherits the
  callee-saved registers — `Moira*`, the frame, the clock target, the
  peripheral baseline — and the retired-instruction count accumulated so far;
* privilege cannot change inside a chain (every SR write is `Unsafe`), so a
  block compiled for one privilege can only be reached from another with the
  same one — which is what the tag's bit 0 enforces;
* the MMU generation cannot change either, so the data TLB entries the chain
  relies on stay valid without the engine re-arming anything between blocks.

Every block's first instruction still runs the budget and flag guards, so a
chain cannot outrun the caller's cycle target or ignore a pending interrupt.

## 10. Journal

* **2026-07-27 — J0/J1 measured and hardened.** The fetch window is worth
  −55 % on the Quadra 605 boot; the block cache is a net loss and ships off.
  An adversarial review pass found and closed two real defects in the block
  path before the gates were called green: `flushAll()` was reachable
  re-entrantly from inside a replaying block (a guest `MOVEC` to CACR →
  `didChangeCACR`), freeing the `BlockIr` under the backend's own loop; and
  the block cache had no notion of `pomJitMmuGen`, so a `PFLUSH` or a TC/URP/
  SRP write could point a cached script at unrelated code. Flushes are now
  deferred while a block is in flight, and the cache tracks the MMU
  generation.
* **2026-07-27 — J0/J1.** Engine, host-neutral IR, backend interface,
  portable W^X code buffer, `threaded` backend, code window, block tracing,
  write guard, GUI **CPU** menu with live switching and a gauge window.
  Machines wired: Quadra 605 / LC 475 / LC 575 (`Cpu040`), Centris and Quadra
  610/650/800 (`CentrisCpu`), Quadra 630 / LC 580 (`Q630Cpu`), Quadra 700
  (`Q700Cpu`).

---

