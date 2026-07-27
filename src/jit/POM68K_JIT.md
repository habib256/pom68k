# POM68K JIT — design, invariants, journal

A **second execution engine**, living beside the Moira interpreter and never
in front of it. Off by default everywhere — GUI, headless, CTest. The user
turns it on from the **CPU** menu (or `POM68K_CPU_ENGINE=jit`) to see what it
does; the interpreter remains what every accuracy claim in this project rests
on.

Read `extern/moira/POM68K_VENDOR.md` § *JIT seam* for the five-point
extension this subsystem needs inside the vendored core.

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

| engine | wall clock | vs interpreter |
|---|---|---|
| Moira interpreter (default) | 58.7 s | — |
| JIT, **fetch window only** (default) | **27.6 s** | **−53 %**, ×2.13 |
| JIT, window + block cache | 47.9 s | −18 % |
| JIT, no window (engine overhead alone) | 62.1 s | +6 % |

The last row is the honest zero point: with the window off, the engine's own
dispatch costs 6 %. Everything the JIT wins, it wins in the window.

### Why the block cache is OFF by default

Because it measured **slower** than the window alone, and the reason is
structural rather than a tuning failure. Moira re-fetches `ird` itself in
`mmu040InstrStart` and dispatches on it, so a recorded block does not change
*which* handler runs — it only lets the engine batch bookkeeping and verify.
Meanwhile real 68k code is branch-dense: recorded blocks average **1.04
instructions**, so almost every block is one instruction followed by a hash
lookup, a failed lookup for the branch, and a trace attempt that immediately
gives up. That is strictly more work than running the same two instructions
in the window loop.

It stays in the tree, switchable with `POM68K_JIT_BLOCKS=1`, because it is
exactly the structure a code generator needs — block boundaries, an IR,
`caps()`-driven fallback, invalidation — and because a path with no gate rots.
`jit_lockstep_blocks_test` runs the whole differential comparison with it on.

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
(`JitIr.h`) calls `Kind::Unsafe`. The classifier runs at build time only, so
it is explicit rather than clever. "Unsafe" means exactly two things, because
everything else is already caught by the replay checks:

* it can change the MMU translation, the ATC, the caches or the supervisor
  bit — those would silently stale the code window (`MOVEC`, `MOVES`,
  `PFLUSH`/`CINV`/`CPUSH` and the rest of the F-line, `MOVE`/`ANDI`/`ORI`/
  `EORI` to SR or CCR, the whole `$4Exx` group);
* it transfers control (`Bcc`, `DBcc`, `TRAPcc`, A-line), which would make the
  recorded straight line meaningless.

---

## 5. The working loop

Do not iterate against a bare `ctest` — 104 gates, ~2h30, and `-j` is unsafe
because the boot etalons are contention-sensitive. Do not iterate against a
bare `make` either: tree-wide LTO relinks ~90 binaries after any core change.

```bash
make -j4 jitdev && ctest -L smoke     # ~2.5 min end to end
```

`jitdev` builds only the three binaries `-L smoke` needs (56 s, 4 links).
`-L smoke` is five gates, 98 s: `jit_backend_test`, `jit_lockstep_test`,
`jit_lockstep_blocks_test`, and the q605 boot etalon **on both engines**.
`jit_lockstep_test` is the one that matters — it compares the JIT against the
interpreter register by register at every instruction boundary, so anything a
JIT change can break shows up in 1.1 s.

Widen only when the smoke tier is green:

| command | gates | time | when |
|---|---|---|---|
| `ctest -L unit` | 50 | 9 s | anything touching non-machine code |
| `ctest -L jit` | 7 | ~8 min | before proposing a JIT change |
| `ctest -L m040` | 26 | ~25 min | the 68040 family — the JIT's blast radius |
| `ctest` | 104 | ~2h30 | the release gate, once |

Labels are derived from test names in `CMakeLists.txt`, so a new gate is
classified the moment it is registered.

## 6. Environment surface

| Variable | Default | Meaning |
|---|---|---|
| `POM68K_CPU_ENGINE` | `interp` | `jit` starts on the JIT (the GUI menu still switches live) |
| `POM68K_JIT_BACKEND` | `auto` | `auto` \| `threaded` \| `x64` \| `a64` |
| `POM68K_JIT_FETCH` | `1` | the instruction-fetch code window (J1a) |
| `POM68K_JIT_BLOCKS` | `0` | block discovery and replay (J1b) — measured slower, see above |

The two are not independent, and that is deliberate: block discovery reads
opcodes out of the code window, so `POM68K_JIT_FETCH=0` disables both and
leaves the engine measuring nothing but its own dispatch overhead — useful
exactly once, as the zero point. `POM68K_JIT_BLOCKS=0` is the interesting
attribution knob: window on, block cache off.

| `POM68K_JIT_BLOCK_MAX` | `64` | straight-line instruction ceiling per block |
| `POM68K_JIT_LOCKSTEP_N` | `5000000` | instructions compared by `jit_lockstep_test` |
| `POM68K_JIT_MAX_BLOCKS` | `16384` | cache size before a full flush |
| `POM68K_JIT_PARANOID` | `0` | re-validate the translation at every arm — for differential testing |
| `POM68K_JIT_VERBOSE` | `0` | backend selection and flush chatter on stderr |

---

## 7. Journal

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
