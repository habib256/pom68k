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

## Public vectors and CI

`tests/cpu_vectors/` ships 752 oracle-generated vectors with recipes and
uncompressed SHA-256 identities. The root `cpu-vectors` tier always replays
them, including from clean CI and ASan/UBSan checkouts. It rejects missing
data and successful soft-skips. They supplement the large optional corpora;
they do not replace a hardware reference or claim 030/040 cycle accuracy.

The Linux CI job builds this separate oracle project, runs both
`oracle_uae_smoke` and `oracle_uae_smoke040`, then regenerates the small corpus
without changing its pins. See `tests/cpu_vectors/README.md` for the same
local recipe. Runtime replay uses the SST harnesses and needs no oracle
library, NumPy, ROM or disk image.

## Rulings taken against a manual

Kept as known exceptions: the project applies the evidence grades to an
existing ruling only when doing so turns no gate red (maintainer's decision,
2026-11-06). Each line is the oracle (D) followed against a Motorola manual
(B), with the pinned vectors that hold it and what would settle it. Detail:
`fuzz/disputes/NOTES.md`.

| Ruling | Oracle (kept) | Manual | Held by | Settled by |
|---|---|---|---|---|
| **D30** | 68040, unsupported data type on an opclass 000/010 instruction: taken at once, format $3 | M68040UM § 9.7 (unsupported data types): "instructions cause a pre-instruction exception" — format $0, taken by the next FPU instruction, as the arithmetic exceptions of D24 | `sst68040_fpu` (hundreds of format-$3, vector-55 frames), `fpu040_test` | the oracle itself calls it a simplification (`fpp.c`: "simplification: always mid/post-instruction exception"); a real frame, or the D24 machinery extended to vector 55 with two-instruction vectors to hold it. Noted 2026-11-07 while reading § 9.7 for D24 |
| **D44** | 68040 CAS2 with Dc1 = Dc2 and a failing comparison leaves memory operand 2 in the register (Dc1 written, then Dc2) | M68000PRM-1992, CAS2 (PDF p. 172): "memory operand 1 is stored in the data register", stated for the MC68020, MC68030 and MC68040 alike. The 68020/68030 follow it since 2026-11-20 | `moira_rules_test` (cas2SameCompareRegister); WinUAE `gencpu.c i_CAS2` carries "68040: register update order swapped", the same author's note as `m68k_mull`'s "68040 is different" | CAS2 Dn:Dn with a failing comparison on a real 68040 |

### Rulings where the fork does not follow the oracle

The reverse case, opened 2026-11-07 by the first differential run of the
integrated 68040 FPU. Each line is a class of vectors on which the fork keeps
its own answer. D25-D29 have named vectors in `tests/sst68040_fpu.known`
and `tests/sst68030_fpuraw.known`; their gates reject a listed case that
starts passing or disappears (`tests/SstKnown.h`). D31 is instead a bus
transfer that a RAM-only vector cannot observe; D32 distinguishes external
data/fill errors from ATC faults; D34 restricts MOVEM saved-EA reuse to
indexed/PC-relative modes; D33 holds deferred U history against
eager writes. `cache040_test` supplies their host-bus witnesses.

| Ruling | Fork (kept) | Oracle | Why it is kept | Settled by |
|---|---|---|---|---|
| **D12 (applied 2026-11-17)** | Group-2 format $2: next resume PC, trapping instruction address | Both addresses contain the next PC | MC68030UM-1989 Table 8-7 and MC68040UM-1993 §8.4.3; 118 model/instruction/FPU trap/RTE cases | 286 scoped `@D12` IA rules across ten pinned corpora retain every other final-state assertion; raw references and hashes unchanged |
| **D23a (applied 2026-11-16)** | External 6888x post-instruction exception: format $2, F-line instruction address; handler RTE resumes normally | Format $3 with operand EA on 020/030, rejected by its own RTE | MC68030UM-1989 §10.4.20, Figure 10-45; `fpu_sanity` holds register/memory destinations, both FPUs and all four 020/030 models. Tagged `@D23a` rules correct only those expected frame fields, with every other byte/register still compared | primary-manual ruling; 448 scoped rules across five pinned corpora, and `sst_known_contract_test` rejects unrelated changes or stale rules |
| **D25** | FMOVE.P FPn,Dn takes a plain Line-F | Unsupported-datatype trap, "even if destination EA is Dn or An" | `fpu040_test` asserts the Line-F | the same instruction on a real 68040 |
| **D26** | FMOVE.P FPn,(An)+{Dn} and -(An){Dn} step An by 12 | An unchanged: the size table holds 0 for the dynamic k-factor format | a packed operand is twelve bytes whatever its k-factor (MC68881/MC68882 UM); the oracle's value is a table slot, not a decision | An after that instruction's datatype trap on a real 68040 |
| **D27** | FMOVE(M) control registers to a PC-relative destination: Line-F at once | Line-F after evaluating the EA, so a memory-indirect one marks a page Used (or faults in the walk) | three vectors; the instruction does not exist either way | — |
| **D28** | FBcc / FDBcc taken to an odd address: address error inside the branch, as for Bcc | Sets the PC and raises nothing, then or later | the manual: a fetch from an odd address is an address error. The oracle's FPU branches simply lack the test its integer branches have | — (the oracle has no frame to compare) |
| **D29** | EA fields $3D-$3F on a 68030: the coprocessor decides — register-to-register runs, a memory form clears the status byte then takes a Line-F | Plain Line-F from the opcode table | on a 68030 the main processor validates an EA only when the coprocessor asks for it; nothing at hand states it for these fields. 63 vectors | `$F23D $0000` (FMOVE FP0,FP0 with EA field $3D) on a real 68030 + 68882 |
| **D31** | Failed 040 CAS2 writes only its second read operand | No failed-comparison write (`gencpu.c i_CAS2` @ Hatari e77819f7) | MC68040UM-1993 §7.4.5 explicitly requires this terminal transfer; held by `cache040_test` bus traces and write-only /BERR | manual settles the transfer; precise CAS2 fault register/PC ordering still needs a real 040 capture |
| **D32** | A later-fragment physical 040 data/fill /BERR retains the initial logical FA with MA clear; second-page ATC faults set MA | `cpummu.c misalignednotfirst:989–997` @ Hatari e77819f7 unconditionally sets MA | MC68040UM-1993 §§8.4.6.2/8.4.6.4 independently define MA and initial FA; `cache040_test` holds 8 592 cases, 2 016 pre-fix frame failures | primary-manual ruling; no new corpus exception or oracle payload |
| **D33** | Root/pointer U writes follow the next successful descriptor read | `cpummu.c:695–742` @ Hatari e77819f7 writes U before fetching the next descriptor | MC68040UM-1993 Figure 3-10 (PDF p. 60) explicitly schedules the delayed write; `cache040_test --descriptor-faults` pins traces and partial history | primary-manual/source-reading ruling; no oracle patch or corpus exception |
| **D34** | MOVEM with CM reuses stacked EA only for indexed/PC-relative modes | `gencpu.c start_mmu040_movem` @ Hatari e77819f7 reuses it for all modes | MC68040UM-1993 §8.4.6.2 lists modes 110 and 111/010–011; §8.4.6.7 repeats the operand prefix. Bare-core, state and JIT continuation witnesses | primary-manual/source-reading ruling; no patched oracle or corpus exception |
| **D39** | 68040: an A-line, illegal or CHK instruction on a page's last word before a non-resident page reports the ATC access error | Takes the instruction's own exception (`gencpu.c` @ Hatari e77819f7 fetches the next opcode only at the next instruction) | MC68040UM-1993 §3.5 names exactly this case; `moira_rules_test` (`deferredPrefetchFault`) holds it on both models | primary-manual ruling; the `pagend` cells of `tests/cpu_vectors` leave the three opcodes out, no corpus exception |
| **D41** | STOP that begins with T0 set is traced, whatever SR value it loads, and does not stop | Not traced when the loaded SR changes none of T1, T0, S, M and the mask: `MakeFromSR_x` returns before its `oldt0` test (`newcpu.c` @ Hatari e77819f7), and the processor stops | MC68030UM-1989 §8.1.7 (status register manipulations are traced in this mode) and MC68040UM-1993 §8.2.6 (STOP is in the list; a traced STOP "never enters the stopped condition"); the oracle's exception is a shortcut for an unchanged SR, not a rule | STOP #$6700 under SR = $6700 on a real 68030 or 68040 |
| **D42** | `LINK A7` pushes the decremented A7 on every model | The 68030 table (`cpuemu_32.c`, both sizes) pushes the undecremented A7; the 68040 table (`cpuemu_31.c`) the decremented one | M68000PRM-1992, LINK (PDF p. 215): "SP – 4 → SP; An → (SP); SP → An; SP + dn → SP", the register stored after the stack pointer moved, for the whole family; the 68000 measured by SingleStepTests/680x0; the oracle's own 68040 table. 24 targeted vectors of 2026-11-20, in no pinned corpus | `LINK A7` on a real 68030 |
| **D45** | 68030 RTR moves A7 once both pops are done: a fault on the return address leaves A7 at its entry value | Steps A7 over the CCR word before the second pop and arms no fixup (`cpuemu_32.c op_4e77_32`): the handler sees A7 + 2, and the oracle's own continuation after RTE takes the logged CCR, steps A7 again and reads the return address two bytes too far | MC68030UM-1989 §8.2.1: an RTE from a format-$B frame continues the faulted instruction; the oracle's order cannot. `moira_rules_test` (`stackFaultRoundTrip`) holds the round trip against a run that never faulted. The oracle's 68040 table leaves A7 intact | RTR with its return address on a non-resident page, on a real 68030: A7 in the handler |

D23b — FRESTORE of a 68040 frame accepted by a 68882 — left this table on
2026-11-07: the acceptance was WinUAE's permissive setting, not the
hardware, and the strict oracle sides with the manual (`POM68K_VENDOR.md`
row 53; the four permissive vectors are listed in `tests/sst68030.known`).
D24 — an enabled 68040 FPU exception delivered at once instead of by the
next FPU instruction — stood in this table for a few hours on 2026-11-07:
the manual (M68040UM § 9.7) sided with the oracle and the fork now follows
both (`extern/moira/POM68K_VENDOR.md` row 50, gate `sst68040_fpu2`).

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
`--mmu tt` fixes the windows (ITT0 = everything, DTT0 = supervisor only);
`--mmu ttvar` draws all four at random over the same identity tree — off,
matching or missing, any S field, CM, U1/U0 and write protection — so a
window hit, a window miss that walks the tree and a W-window write fault
are all reached.
There is no `loop.sh` equivalent: the 040 corpus under
`tests/data/sst68040/` is pinned and replayed by the `sst68040` gate.

## Fresh-seed sweeps, both CPUs

A pinned corpus is a floor, not a coverage: fresh seeds keep finding what
the pinned vectors never drew. `fuzz/sweep.sh SEED040 SEED030 [N] [OUT]
[BUILD]` generates every family of `fuzz040.py` and `fuzz030.py` over every
MMU setup each accepts (strict 6888x on the 68030), N vectors a cell
(default 1 000), then replays each family with the interpreter
(`--strict-ram`) and with the JIT lockstep harness, and writes
`summary.txt`. JIT replay sets `POM68K_JIT_PROFIT_SCORE=0` so short vectors
compile on AArch64/68030 too; inspect the native counters, or set
`POM68K_JIT_REQUIRE_NATIVE=1` on a native host to require native execution.
Every divergence is read against `fuzz/disputes/NOTES.md`
before it is called new; the sweep itself is not pinned, a fix pins what
holds it. The script accepts Linux `.so` and macOS `.dylib` oracle builds
(or an explicit `POM68K_ORACLE_SO`) and uses at least one generation worker.
Build `sst68030`, `sst68040`, `sst68030_jit` and `sst68040_jit` first.
Use a Python environment with NumPy on `PATH` (for example
`PATH="$PWD/build-oracle/venv/bin:$PATH" bash oracle/fuzz/sweep.sh 151100 151200`).
Without NumPy, each vector scans 16 MB through the pure-Python fallback.
Set `POM68K_SWEEP_JOBS=1` to generate serially on a shared host; prefix
the command with `nice -n 15` to lower priority for generation and replay.
The 2026-11-14 sweep (NOTES.md) is the reference run.

The shared `earaw` family targets full-format indexed effective addresses
on both CPUs: MOVE.L loads/stores, LEA and PEA, including PC-relative loads
and LEA. BD size, bit 3, I/IS and suppression bits are drawn independently,
including reserved encodings (see D17). It compares emulator behavior, not
silicon-defined behavior for reserved words. The sweep appends these cells
after the older families so their existing seed assignments stay stable.

`opcells.py` complements the families where a uniform draw starves an
instruction: `generate --set forms --trace t0|t1` runs fixed instruction
forms behind a lead-in (the trace sweep of 2026-11-20), `generate --set
classes [--trace t0|t1]` draws N states per small encoding class with
random register fields, extension words and operands, `generate --set ea`
draws them per (instruction, addressing mode) for the 68020 additions and
the system instructions, with extension words kept inside their defined
encodings (`--mmu identity` makes the members fault, `--user` runs them
unprivileged, `--user --stack data` puts the user stack on a data page so
that pushes and pops fault while the frame goes to a sound supervisor
stack, `--edge` puts the address registers a few bytes below a page
boundary for split accesses, `--only <regex>` keeps the matching classes),
and `report <dir>`
tabulates a runner's `--dump` per form. The runners' `--dump` prints USP,
ISP and MSP when they differ: a faulting user-mode push shows nowhere else.
A 100-state `ea` file is 1.5 GB: 25 states per class is the working size. `fuzz030.py`/`fuzz040.py --trace t1|t0` set a
trace bit behind a lead-in instruction, and `--trace-handler rte` turns the
vector into a single-step loop.

`fuzz030.py --geometry 4k-2level` selects 4 KB pages and two levels of
short descriptors (IS=8, TIA=TIB=6). Its tables occupy $200000–$204FFF;
the generator protects them and the full code region from holes/remaps.
Fault offsets and page-end placement use the selected page size. The
legacy `32k` geometry remains the default and keeps its recipes unchanged.
The sweep appends identity-MMU cells for every family with this geometry.
The `4k-2level-long` geometry uses eight-byte descriptors at both levels,
with a type-3 root pointer, unlimited ascending limits, separate flags and
addresses, and leaves extending through $208FFF. The sweep appends its
identity cells after the short-descriptor geometry.
`4k-limits` adds upper and lower bounds to four long table descriptors,
leaving infrastructure reachable. Its `limits` family probes each bound's
predecessor, equality and successor with MOVE.L reads/writes; leaf pages
are made valid and writable so only the parent bound decides. The public
32-vector recipe covers all twelve direction/position/access combinations.
The `rootlimits` family uses the `4k-2level` and `4k-2level-long` geometries
with SRE enabled: supervisor code/frames use the unlimited SRP, while
MOVES.L with SFC/DFC=1 and PTEST with FC=1 use a bounded CRP. Upper/lower
root bounds are probed immediately before/on/after the inclusive boundary,
with reads/writes and PTEST levels 1–7. The target leaf is valid/writable;
probe data never overlaps code, stacks or translation tables. The two
64-vector public recipes each cover all 24 bound/position/instruction/
direction combinations. These cells are appended after the table-limit
cells so existing sweep seeds do not change.
The `fcl` family prepends an eight-entry function-code lookup table to
the same short/long geometries. Private address and leaf tables map one
logical operand to eight distinct physical pages; MOVES.L SFC/DFC and
PTEST immediate FC select codes 0–7, with valid, write-protected or invalid
target leaves. FC=7 MOVES is a translation-bypass control. Root upper-zero
and lower-seven limits exercise their omission at the initial FC lookup.
Supervisor code and exception stacks retain valid mappings. Each 64-vector
public recipe (seed 2026111615) includes all 24 FC/leaf-kind combinations
and both instruction kinds for every FC. The sweep appends these cells
after `rootlimits`, preserving earlier seeds. Reserved FC values pin oracle
behavior without making a silicon claim.
The focused `sre`, `early`, `indirect`, `mixed` and `ttprobe` families
in `mmu030_probes.py` extend both geometries. `sre` maps user/supervisor
operands through distinct CRP/SRP trees, bounds either root in either
direction and includes accesses through the other, unlimited root.
`early` places a page descriptor in the root or first address table;
`indirect` varies pointer/target sizes, non-16-byte-aligned pointers and
valid/invalid target DT; `mixed` changes descriptor size between levels.
All protect high-alias code, vectors, stacks and handlers with a correctly
encoded TT1 window. These new families omit explicit zero initial RAM
bytes; the oracle and replay buses are zero-filled.

`ttprobe` distinguishes a transparent operand from its translated mapping
with different data, including paged WP/invalid leaves. TT0 varies enable,
address and FC base/masks, CI and R/W(M). MOVES, ordinary MOVE, PTEST at
level zero or 1–7, TAS and CAS exercise direction and locked matching.
The legacy `--mmu tt` recipe was misdescribed as a universal bypass: its
low FC mask is zero, FC base is 7, RWM is clear and CI is set. Ordinary
FCs still walk the tree; FC=7 already bypasses ordinary translation.
Its bytes remain unchanged for old corpus reproducibility. Use `ttprobe`
for true TT successes/misses. Oracle patch 15 fixes a direct-root write
retry that formerly indexed the ATC with -1; root mapping itself follows
MC68030UM §9.7.1, rather than that undefined oracle result.
`earlyfcl` adds early pages with FCL active. A DT=1 root has no FC table;
its limit remains active as §9.7.1 explicitly specifies. A table root uses
an additional FC lookup before the existing early page. These cells append
after the other focused families and keep their seed assignments stable.

## Licensing / provenance

The WinUAE core is GPL-compatible (GPLv2+). `uae/VENDOR.md` carries the
upstream URL, commit, extracted file list and every local patch — same
discipline as `extern/moira/POM68K_VENDOR.md`. Oracle sources are
**test-time only**: nothing here links into the POM68K binary.
