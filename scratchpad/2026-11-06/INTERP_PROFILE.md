# Interpreter time profile, 68030 and 68040 — 2026-11-06

Where the pure interpreter (`POM68K_CPU_ENGINE=interp`) spends its time, on the
two fixed-budget benches. Taken to order the work of
`TODO.md § Moteur › Interprète`, and as the baseline any interpreter
optimisation is measured against.

**Conditions.** Tree at `5871125`. `build-profile` (RelWithDebInfo, no LTO,
`-march=native`: under LTO identical-code folding misattributes samples, see
`tools/profile_census.py`). gperftools CPU profiler at 1 kHz through the
`LD_PRELOAD` shim of `scratchpad/2026-09-04/b2s0/shim.cpp`; decoded with
`tools/profile_census.py`. 1 500 frames each. The host was running a
parallel gate tier at the same time, so **the shares are quotable, the wall
clock is not**.

| bench | guest work | fingerprint |
|---|---|---|
| `jit_bench` (Quadra 605, 68040) | 625 000 500 machine cycles | `a2036281a5c2ac55` |
| `jit_bench_lcii` (LC II, 68030) | 390 720 000 machine cycles | `663d67e4bc2d9714` |

Full decodes: `q605_interp.census.txt`, `lcii_interp.census.txt`.

## 68040 — two thirds of the interpreter is address translation

| share | where |
|---|---|
| **63.2 %** | MMU and cache helpers |
| 11.9 % | instruction handlers and the run loop |
| 9.5 % | the board's memory map (`Q605Memory::read16` 6.3 %) |
| 8.7 % | engine and wrapper glue (`pomJitExecOne` 4.2 %, `sync` 2.3 %) |
| 6.0 % | peripherals |

Inside the 63.2 %:

| share | function | what it is |
|---|---|---|
| 16.6 % | `mmu040Translate` | the per-access bookkeeping around the lookup |
| 12.6 % | `mmu040Read<Word>` | the two instruction-fetch reads of `mmu040InstrStart` and the extension words |
| 9.9 % | `mmu040MatchTTR` | the transparent-translation test, run before the ATC on every access |
| 9.1 % | `mmu040AtcLookup` | the 32-entry scan |
| 6.4 % | `pomCache040ReadValue` | the physical read door (cache model off: it is the bus call) |
| 3.5 % | `mmu040InstrStart` | the loop head itself |

The instruction handlers — the part that IS the 68040 — are one eighth of the
time. Every candidate below is therefore about not re-deriving a translation
the previous access already had:

1. **A one-entry instruction micro-TLB.** Consecutive fetches stay on one
   page; `(page, privilege, pomJitMmuGen)` → physical base would skip
   `MatchTTR`, `AtcLookup` and most of `Translate` for them. It must leave the
   ATC's replacement state exactly as a lookup would (the 68030's last-hit
   probe, row 16, is the precedent and its gate the model).
2. **`mmu040MatchTTR` on a precomputed mask.** The two TTRs of each side
   change on MOVEC only; their enable/base/mask/S test can be folded once
   there instead of re-decoded per access.
3. **A fused `ird`/`irc` fetch**, as the 68030 has behind the window (row 33),
   but owned by the interpreter rather than armed by `jit::Engine`.

An O(1) ATC hint alone was measured flat on 2026-07-31; this profile agrees
that the scan (9.1 %) is not the largest term. The gain is in removing the
three calls together for the common case, not in speeding up one.

## 68030 — one third is the instruction fetch

| share | where |
|---|---|
| 39.1 % | MMU helpers |
| 17.6 % | instruction handlers and the run loop |
| 15.8 % | the board's memory map (`V8Memory::read16` 11.5 %) |
| 14.0 % | engine and wrapper glue (`pomJitExecOne` 7.0 %) |
| 6.5 % | peripherals |
| 4.9 % | `DiskTimeline::blockHash` (the bench's own fingerprinting) |

Inside the 39.1 %: `mmuFetchWord` **22.1 %**, `mmuExecuteStart` 5.8 %,
`mmuRead<Long>` 4.6 %, `mmuWrite<Long>` 1.7 %. With the bus read it lands on
(`V8Memory::read16`, 11.5 %) the instruction fetch is about a third of the
interpreter — the figure the 2026-09 callgrind run put at 44 % of
instructions executed. The data side is already cheap here: the interpreter
data window answered 72.6 M accesses and refused 13.7 M.

The fetch window and the fused fetch that remove this in the accelerated
engine are armed by `jit::Engine` only (`POM68K_SURFACE.md` § 9.2). Giving
the interpreter the same pair on its own is the single largest step for a
host that runs bare `Moira::execute()`.

## What this does not say

- Nothing about the default product path: on both families `auto` runs the
  native generator, whose profile is `src/jit/POM68K_JIT.md` § 3.
- Nothing about wall clock. Re-run on an idle host before quoting a ratio.
- The 1 500-frame budget is the boot. A workload phase (a game, a
  benchmark loop) shifts the mix towards handlers and data accesses.
