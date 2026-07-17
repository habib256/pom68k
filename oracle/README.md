# 68030 oracle infrastructure (Phase 2 — Mac LC II)

This directory is the differential-testing backbone behind Moira's
68030 + MMU + 68882 execution core (TODO.md § Phase 2). The core itself is
written from the **Motorola MC68030 / MC68881-882 User's Manuals**; every
behaviour is then verified against the **WinUAE oracle**. On spec/oracle
conflict, **the oracle wins** (undocumented flags, MMUSR quirks,
exception-frame contents…) — unless a real-hardware trace proves it wrong.

> The second oracle (MAME/Musashi) was **retired 2026-07-15**: 0
> arbitrations won across D1-D22, architecturally divergent fault model
> (D8/D9), ~13 % FPU agreement (D18-D20) — see
> `fuzz/disputes/NOTES.md § Musashi retired` and CHANGELOG.md. The
> `fuzz030.py --b` slot stays open should a second oracle return.

```
oracle/
  oracle_api.h        common C ABI: set_state / step / get_state over a
                      host-owned flat big-endian buffer (MMU tables live there)
  uae/                O1 — WinUAE 68030 + cpummu030 + 68882 → liboracle_uae.so
                      (extracted via Hatari)
  fuzz/               O3 — Python: random-state + MMU-tree generator,
                      oracle driver (ctypes), SST030 JSON emitter, loop.sh
```

## The improve loop

1. `fuzz/gen030.py` produces random initial states — including real MMU
   translation trees in RAM (valid / invalid / cascaded / indirect
   descriptors) so PTEST/PLOAD/translated accesses exercise real tables.
   Families: `core` (integer + MOVES), `mmu` (PMOVE/PTEST/PFLUSH/PLOAD),
   `random` (raw opcodes), `fault` (memory ops aimed at invalid/WP/
   remapped pages), `fpu` (68881/68882 ops).
2. The oracle steps each state and its result **is** the vector — solo
   mode: the oracle's word is law. (With `--b`, only vectors both oracles
   agree on are emitted; disagreements land in `fuzz/disputes/`.)
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
   `tests/data/sst68030/` (CTest gate `sst68030`).

`fuzz/loop.sh [N-per-cell] [seed]` runs one full turn: build the oracle,
fuzz the `{core,mmu,random,fpu} × {off,identity,tt}` grid plus
`fault × {identity,tt}`, replay everything.

## Licensing / provenance

The WinUAE core is GPL-compatible (GPLv2+). `uae/VENDOR.md` carries the
upstream URL, commit, extracted file list and every local patch — same
discipline as `extern/moira/POM68K_VENDOR.md`. Oracle sources are
**test-time only**: nothing here links into the POM68K binary.
