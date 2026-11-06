# Fresh-seed sweep on the final core — 2026-11-07

Run after the last core change of the night (`c30436b`, ruling D24 applied),
with `build-plain` harnesses and the oracle of the same tree. 40 cells of
1 500 vectors, seeds 1001 to 1040, generated three at a time under `nice`
while the validation tier was running; the 68030 cells against the strict
oracle (`--strict-fpu`). Plus the raw `$F400-$F7FF` window, seeds 1101-1103.

| corpus | score | what the rest is |
|---|---|---|
| 68030, 14 cells (`core`, `mmu`, `random`, `fpu` × 3, `fault` × 2) | 20 946 / 20 969 | 14 D23b, 4 D28, 4 + 1 new (below) |
| 68030 `fpuraw`, strict | 4 339 / 4 500 | D28 95, D29 46, D26 12, D23b 6, and 2 of the open "$B frame tail" defect |
| 68040, 11 cells (68LC040) | 16 497 / 16 500 | 3 bit-field faults (SSW size, the known "always a long" item) |
| 68040 `fpu` + `fpuraw` (full 68040) | 8 960 / 9 000 | D25 21, D26 18, D27 1 |
| 68040 `fline` (68LC040) | **4 500 / 4 500** | — |
| 68040 `fpu2`, two instructions | 4 497 / 4 500 | 3 state-frame internals after a posted exception |
| 68040 `sysraw`, `$F400-$F7FF` | **6 000 / 6 000** | — (first replay; the family is new) |

65 969 vectors, 230 divergences, 225 of them inside a ruling or a defect
that already has a name and a `TODO.md` line. No class the day's work
touched reappears: no F-line frame shape, no posted-exception delivery, no
FBcc predicate, no last-write on an FPU store.

## The five that are new

**Format $A where the fork stacks $B (four vectors).** A read-modify-write
instruction in user mode whose WRITE faults; the oracle stacks the short
bus-fault frame with the next instruction's PC (the write was the
instruction's last), the fork the long one.

- `random/identity seed=1008 #358` — `810A`, SBCD -(A2),-(A0)
- `random/identity seed=1008 #1071` — `9D93`, SUB.L D6,(A3)
- `random/identity seed=1008 #1191` — `01EE D6F1`, BSET D0,(d16,A6)
- `random/tt seed=1009 #419` — `9125`, SUB.B D0,-(A5)

Reproduce: `fuzz030.py --family random --mmu identity --strict-fpu --seed
1008 --n 1500` (and `--mmu tt --seed 1009`). The `fault` family never emits
these forms — its opcode list is its own — which is why 9 000 pinned and
fresh `fault` vectors pass. A `fault` family built from the `core` opcode
set would cover the class.

**PTEST leaves MMUSR.I clear (one vector).** `mmu/tt seed=1006 #1188`,
`F028 8615 0C59`: MMUSR $0001 here, $0401 in the oracle.

## What the sweep is evidence of

That the classes corrected today are closed on seeds they never saw, and
that the two raw windows written today (`fline`, `sysraw`) hold at 100 %.
It is not coverage: the 68030 generator still builds one MMU geometry, and
no vector runs with a 68040 cache on.
