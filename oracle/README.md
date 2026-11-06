# 68030 / 68040 oracle infrastructure

This directory is the differential-testing backbone behind Moira's
68030 + MMU + 68882 core (Phase 2, Mac LC II) and its 68040 + MMU core
(Phase Q, the Quadras). The cores themselves are written from the
**Motorola MC68030 / MC68040 / MC68881-882 User's Manuals**; every
behaviour is then verified against the **WinUAE oracle**.

**Who wins a conflict (2026-11-06).** The oracle is another emulator: grade D
in the evidence grades of `CLAUDE.md`, below a manual (B) and far below a
real-hardware trace (A). Where the manual is **silent** — undefined flags,
MMUSR quirks, the internal words of a frame — the oracle is the best reading
there is and its word stands. Where the manual **speaks** and the oracle
disagrees, the manual wins. The earlier rule read "the oracle wins" without
that distinction; the rulings taken under it are listed in § *Rulings taken
against a manual* below and are kept, because each is pinned by vectors a
reversal would turn red.

> **One oracle, by ruling.** The second one (MAME/Musashi) was **retired
> 2026-07-15** and its tree deleted: 0 arbitrations won across D1-D22,
> architecturally divergent fault model (D8/D9), ~13 % FPU agreement
> (D18-D20) — `fuzz/disputes/NOTES.md`. Every cell is WinUAE-solo; the
> `--b` flag of `fuzz030.py`/`fuzz040.py` keeps the differential slot open
> should a second oracle ever return.

```
oracle/
  oracle_api.h        common C ABI: set_state / step / get_state over a
                      host-owned flat big-endian buffer (MMU tables live there);
                      oracle_set_model() picks 68030 or 68040
  uae/                O1/Q1 — WinUAE 68030 + cpummu030 + 68882 and 68040 +
                      cpummu → liboracle_uae.so (extracted via Hatari);
                      smoke.c / smoke040.c are its own sanity checks
  fuzz/               O3 — Python: random-state + MMU-tree generators
                      (gen030/gen040), oracle driver (ctypes), SST030/SST040
                      JSON emitters, loop.sh
```

## Rulings taken against a manual

Kept as known exceptions: the project applies the evidence grades to an
existing ruling only when doing so turns no gate red (maintainer's decision,
2026-11-06). Each line is the oracle (D) followed against a Motorola manual
(B), with the pinned vectors that hold it and what would settle it. Detail:
`fuzz/disputes/NOTES.md`.

| Ruling | Oracle (kept) | Manual | Held by | Settled by |
|---|---|---|---|---|
| **D12** | Zero divide, CHK/CHK2, TRAPcc/TRAPV: the format $2 "instruction address" is the **next** instruction's | MC68030UM reads "the instruction that caused the exception" | `sst68030` | a frame stacked by a real 68020/68030 |
| **D23a** | 68030 FPU post-instruction exception stacks **format $3** with the operand EA | MC68030UM: format $2, instruction address | `sst68030` fpu cells (10 vectors went red on the 2026-08-17 attempt), `fpu_sanity` check 14 | a frame stacked by a real 68030 + 6888x. Note the fork's 68020/030 RTE does not accept a $3 frame (`TODO.md § Moteur › Justesse 68030`) |
| **D23b** | FRESTORE accepts a version-$41 (68040) frame on a 68882 — WinUAE's own "horrible hack" | MC68882UM § 6 knows no such frame version: format error | `sst68030` fpu cells (4 vectors) | a real 68882 given a $41 frame |

Two 68000 rules of the same kind predate the 68030 oracle and are flagged
in `extern/moira/POM68K_VENDOR.md` § *SingleStepTests/680x0 convergence
patches*: the early exit of every DIVS overflow at 16/18 cycles, and ASR
past the operand width clearing C and X. Both follow SingleStepTests (D)
and are held by `sst68000`; NeoST's copy disagrees on the first
(`TODO.md § Moteur › Convergence`).

Rulings where the manual is silent — D13-D15 (undefined CCR), D20 (sign of a
created NaN) and their like — are not exceptions: nothing of a higher grade
contradicts them.

## The improve loop

1. `fuzz/gen030.py` produces random initial states — including real MMU
   translation trees in RAM (valid / invalid / cascaded / indirect
   descriptors) so PTEST/PLOAD/translated accesses exercise real tables.
   Families: `core` (integer + MOVES), `mmu` (PMOVE/PTEST/PFLUSH/PLOAD),
   `random` (raw opcodes), `fault` (memory ops aimed at invalid/WP/
   remapped pages), `fpu` (68881/68882 ops).
2. The oracle steps each state and its result **is** the vector — solo
   mode: the oracle's word is law. (With `--b`, only vectors both oracles
   agree on are emitted; disagreements land in `fuzz/disputes/`. Unused
   since the retirement.)
3. Vectors are written as **SST030 JSON** (the 68000 format of
   `tests/sst68000.cpp` extended with isp/msp, vbr, sfc/dfc, cacr/caar,
   crp/srp/tc/tt0/tt1/mmusr, fp0-fp7 + fpcr/fpsr/fpiar; `length` in
   cycles is advisory — the LC II targets functional accuracy).
4. `tests/sst68030` replays the vectors against Moira (Model::M68030);
   failures drive the next round of Moira exec-layer work. When Moira and
   the oracle disagree, arbitration is **manual**: probe both via
   `fuzz/oracle_driver.py`, read the manual, apply the oracle-wins policy,
   and log the ruling in `fuzz/disputes/NOTES.md` (real-hardware traces
   welcome and outrank everything).
5. Repeat per family until the corpus converges, then pin it under
   `tests/data/sst68030/` (CTest gate `sst68030`). `tests/data/` is
   **gitignored** (`.gitignore:28`), so a fresh clone has no corpus and
   both SST gates soft-skip until you run the loop.

`fuzz/loop.sh [N-per-cell] [seed]` (defaults 200 / 1) runs one full turn:
build the oracle, fuzz the `{core,mmu,random,fpu} × {off,identity,tt}`
grid plus `fault × {identity,tt}` into `tests/data/sst68030/`, then replay
with `tests/sst68030`. A failing cell does not stop the sweep (`|| true`):
the replay at the end is the verdict.

## The 68040 side

Same machinery, `oracle_set_model(68040)`: `gen040.py` (families `core`
— MOVE16 included —, `mmu` in the 040 dialect PTESTR/PTESTW/PFLUSH,
`random`, `fault`; no `fpu` family, the 040 FPU is not fuzzed here),
`fuzz040.py` with the same flags, `sst040.py` = SST030 plus the eight 040
MMU registers (`urp040 srp040 tc040 itt0 itt1 dtt0 dtt1 mmusr040`). A
SST040 reader loads an SST030 corpus unchanged (missing keys read as 0).
There is no `loop.sh` equivalent: the 040 corpus under
`tests/data/sst68040/` is pinned and replayed by the `sst68040` gate.

## Licensing / provenance

The WinUAE core is GPL-compatible (GPLv2+). `uae/VENDOR.md` carries the
upstream URL, commit, extracted file list and every local patch — same
discipline as `extern/moira/POM68K_VENDOR.md`. Oracle sources are
**test-time only**: nothing here links into the POM68K binary.
