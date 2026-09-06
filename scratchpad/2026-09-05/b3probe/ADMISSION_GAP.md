# The 68030 admission gap, priced temporally on the arm that now ships

*2026-09-06, after the x64/68030 promotion. Answers TODO § B.3's "fermer
l'écart de timing/admission 68030 entre A64 et x64" with the instrument that
item demands rather than with a fallback histogram.*

## Opcode parity is zero, and it is gated

`jit_backend_parity_test` reports **0 divergence groups, 0 failures**. Whatever
the x64 emitter refuses on a 68030, the a64 emitter refuses too. So the
remaining admission gap is **not** an a64-versus-x64 asymmetry — it is a
shared coverage gap, and closing it is one piece of work in the IR serving
both emitters, which is what § B.3's own rule asks for ("toute règle 68k
commune doit vivre dans l'IR/coût partagé, pas dans un emitter").

## What is actually refused, on the shipping configuration

`POM68K_JIT_HISTO=1`, 2000 frames, `auto` (now x86-64), data window at its new
default. Block fallback census: **562 194 unsupported + 3 342 967 runtime
guard/access = 3 905 161**.

| opcode | form | unsupported | share of census |
|---|---|---:|---:|
| `2F70` | `MOVE.L idx(A0) → d16(A7)` | 391 197 | **10.04 %** |
| `4A12` | `TST.B (A2)` | 95 711 | 2.45 % |
| `E3B8` | shift | 19 384 | 0.50 % |
| `082A` | bitop | 18 357 | 0.47 % |
| `C029` | alu | 11 150 | 0.29 % |

One form is 70 % of everything the generator cannot compile.

## And why none of it is worth opening

**The tree has already bought this lesson once.** 2026-08-28 (seventh): "the
D1F0 ABBA — eliminating the biggest fallback family will buy wall time" read
**+0.02 % on a 0.6 % floor**, and its conclusion is that *the fallback
histogram is not a time profile*. `2F70` at 10 % of the census is exactly the
shape of that trap.

So it is priced in time, from the caller profile taken on this same tree
(`../b2profile/callers_x64.txt`), where whole-instruction replay —
`pom68kJitStep`, the path every unsupported instruction takes — is **8.64 %
of the run**:

| candidate | share of replay census | temporal ceiling |
|---|---:|---:|
| `2F70` alone | 10.04 % | **0.87 %** |
| every unsupported form together | 14.40 % | **1.24 %** |

This host's recorded floor is 10 permille, upheld on 2026-09-05 against
eighteen null experiments that showed excursions to 1.8 % with nothing
changed. So:

* **`2F70` is below the floor** — 0.87 % against 1.0 %, and that is its
  ceiling assuming the lowering were free, which it is not: a compiled form
  still costs its own emitted code, and the block it unblocks still pays every
  other guard in it.
* **Closing the entire admission gap at once** — every unsupported form, all
  five families — tops out at 1.24 %, barely above the floor, for an
  open-ended amount of emitter work on the highest-blast-radius code in the
  tree.

**Ruling: do not open it.** The admission gap on the 68030 is measured, named
per opcode, and worth less than the noise on this host. `C029` is in any case
already spoken for: § B.3 keeps it in Moira until a phase contract is
demonstrated.

## What the promotion already proved about the timing half

§ B.3's item also names "les pénalités i-cache, les positions d'accès". Those
are no longer unevidenced on x64: the 2026-09-06 promotion ran `-L m030`
56/56 and `-L etalon` 124/124 **with `auto` resolving a 68030 to this
generator**, plus six 030 lockstep gates at 120 000 steps identical — and the
lockstep compares the three `PomIcache` counters and the cache content, which
is precisely the i-cache-penalty and access-position contract. The i-cache
divergence at step 5 956 that the 2026-08-29 withdrawal recorded no longer
reproduces.

What remains of that item is therefore not a defect but a standing
convention — new 68k rules belong in the IR and the shared cost model — and
the a64 half, which only an AArch64 host can run.
