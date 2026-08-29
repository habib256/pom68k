# Moira — vendored copy (POM68K)

Provenance: copied from **NeoST** (`/home/gistarcade/src/neost/extern/moira`,
NeoST commit lineage `0b96cab` → `47d6c39`), itself vendored from upstream
**Moira** by Dirk W. Hoffmann (<https://github.com/dirkwhoffmann/Moira>, MIT).
The NeoST patches (documented in `NEOST_VENDOR.md`, kept alongside) are
included: deferred IPL recognition (`setIplDelay`/`pollIpl`), STOP
level-sensitive IRQ re-check, exception-handling robustness, watchpoint
24-bit address masking.

## Status: this is a permanent fork (decided 2026-08-09)

**The copy in `extern/moira/` is no longer a vendored library with a re-sync
path. It is a fork, and the project owns it.** Said explicitly because the
alternative had become a third choice nobody had made: the exit strategy
towards upstream had quietly expired while the file still read as if a rebase
were the plan.

What forced it, measured on this tree rather than remembered (re-measured
2026-08-21; earlier counts predated the 040 FPU/cache completion, the JIT
memory-contract work and the peripheral-phase alignment pair):

| | | how to re-measure |
|---|---|---|
| distinct `pom*` extension identifiers | **88** (56 of them `pomJit*`) | `grep -rhoE '\bpom[A-Za-z0-9_]*' Moira/ \| sort -u \| wc -l` |
| `POM68K`-marked lines | **398** | `grep -rn POM68K Moira/ \| wc -l` |
| source files carrying a marker | **13 of 25** | `grep -rln POM68K Moira/ \| wc -l` |
| patch groups in the inventory below | **32** | this file |
| files POM68K *adds* outright | `MoiraCache040.h` | — |

Twenty-nine patch groups, two of which (the JIT seam, row 22, and the ATC
performance work, row 16) are not patches over upstream's design but a second
consumer of it. A re-sync is no longer a merge with conflicts; it is a port.

**The rejected alternative** was regrouping the hooks behind a single
extension surface to keep a rebase cheap. It is rejected because it buys back a
path the project does not intend to walk, and it charges for it in the exact
currency the hooks exist to save: row 14 records that turning the i-cache
overlay into a virtual hook cost **~11 %**, and rows 16 and 22 are inlining work
by construction. Paying a permanent throughput tax for an optional future merge
is the wrong trade for an emulator whose accuracy claims all rest on the
interpreter's speed being tolerable.

**What the decision obliges instead** — the maintenance contract:

1. **This file is the fork's design record**, not a re-sync aid. The journal
   sections keep carrying the *why* (oracle ruling, ROM behaviour, guest bug);
   losing one is losing the reason, which the code cannot restate.
2. **Upstream is a source of reports and ideas, not of merges.** A fix landing
   in Dirk Hoffmann's Moira is read, and cherry-picked deliberately as its own
   change with its own gate — never applied wholesale.
3. **The MIT notice and attribution stay** exactly as they are (`Moira/LICENSE`,
   the provenance paragraph above). Forking changes the maintenance posture, not
   one line of the licence obligation, and the lineage above stays accurate.
4. **`grep -rn POM68K extern/moira/Moira/` stays the machine-checkable
   inventory.** It is what makes this file auditable instead of aspirational,
   and it is the reason the numbers in the table above could be measured at all.

**Reopening condition** — reversed only by upstream landing something the fork
cannot cheaply reproduce (a full 68040 FPU, a rewritten dispatch core), and only
against a *measured* estimate of porting the 29 groups onto it. Not by the
general discomfort of being forked.

The "Before re-syncing from upstream" note further down is now the exception
path, not the plan.

## How to read this file

Unlike NeoST (which patches `MoiraConfig.h` at build time), POM68K **edits the
vendored files directly**. This file is therefore the patch set: without it an
upstream re-sync silently deletes work that took a differential fuzz loop to
find.

- **§ Inventory** below is the scannable index — one row per patch group, with
  the files it touches and the gate that would catch its loss. Start here.
- The sections after it are the **journal**, in chronological order. They carry
  the *why*: the oracle ruling, the ROM behaviour, the guest bug. A rationale
  lost here cannot be recovered from the code.
- Every local change is marked `POM68K` (often `POM68K <slice>:`) in the source,
  so `grep -rn POM68K extern/moira/Moira/` is the machine-checkable inventory —
  391 marked lines across 13 of the 25 source files, as of 2026-08-19 (was 361
  across 13 of 25 on 2026-08-12 and 336 across 12 of 24 on 2026-07-31; the
  25th file is `MoiraCache040.h`, added).
  The NeoST patches carry `NEOST` markers instead, not `POM68K` ones — the two
  greps are disjoint.
- Sections are never rewritten when superseded, only annotated. § *Model support
  in this copy* (last section) is the current state; the journal is history.
- The vector counts quoted throughout (`sst68000` 1 000 058, `sst68030` 3 082,
  `sst68040` 7 200, and every historical figure below) are **not reproducible
  from a clean clone**: the corpora live outside the repo, under
  `POM68K_SST_DIR` / `POM68K_SST030_DIR` / `POM68K_SST040_DIR`
  (`cmake/Pom68kComponentGates.cmake:308-332`, included by the repository
  root — not either CMake file beside this document — defaulting to
  `tests/data/sst680*`, absent here).
  Without them those gates soft-skip. Treat the counts as a record of what was
  pinned, not as something a reader can re-check today.

**Before re-syncing from upstream:** diff against this file and
`NEOST_VENDOR.md` first, then re-apply patch group by patch group, running the
gate in the last column of each row.

## Inventory of local patches

| # | Patch | Vendored files | Why | Gate |
|---|---|---|---|---|
| 1 | Build configuration: 3 macros | `MoiraConfig.h` | cycle-exact Mac Plus + oracle parity | `sst68000` |
| 2 | **SST 680x0 convergence** — address-error machinery, DIV/CHK/ASR/LINK rules | `MoiraExceptions_cpp.h`, `MoiraDataflow_cpp.h`, `MoiraExec_cpp.h`, `MoiraALU_cpp.h` | 1 000 058/1 000 060 vectors; the oracle wins over the manual | `sst68000` |
| 3 | **68030 MMU instructions** PMOVE/PTEST/PFLUSH(A)/PLOAD (stubs → real) | `MoiraExecMMU_cpp.h`, `Moira.h`, `MoiraTypes.h` | LC II ROM enables the PMMU at `$A416AA` | `sst68030` |
| 4 | 68030 MMU **two-oracle arbitration** D1-D6b (decode, EA order, Line-F, vector 56) | `MoiraExecMMU_cpp.h`, `MoiraExec_cpp.h` | WinUAE won every dispute; Musashi retired | `sst68030` |
| 5 | **68030 MMU bus layer** — translation, 22-entry ATC, $A/$B frames, mode-5 fetch model | `MoiraExecMMU_cpp.h`, `MoiraExceptions_cpp.h`, `MoiraDataflow.h`, `MoiraDataflow_cpp.h`, `MoiraExec_cpp.h`, `MoiraALU_cpp.h`, `Moira.cpp`, `MoiraTypes.h` | every LC II access is translated | `sst68030` |
| 6 | 68030 **integer arbitration** D11-D17 (CHK/DIV CCR, format $2, odd-PC AE) | `MoiraExec_cpp.h`, `MoiraALU_cpp.h`, `MoiraExceptions_cpp.h`, `MoiraDataflow_cpp.h` | WinUAE rulings; all `C == C68020`-gated | `sst68030` |
| 7 | **68882 FPU execution** (empty stubs → full), `setFPUModel()` | `MoiraExecFPU_cpp.h`, `Moira.h`, `Moira.cpp`, `MoiraTypes.h`, `MoiraInit_cpp.h` | LC II PDS FPU; softfloat-backed | `fpu_sanity`, `sst68030` |
| 8 | FPU **timing tables + FRESTORE acceptance matrix** | `MoiraExecFPU_cpp.h` | MC68881/882UM § 8; placeholders billed a 570-cycle FTWOTOX as 20 | `fpu_sanity` |
| 9 | **External /BERR (030)** `extBusError()` + RTE of format $A | `MoiraExecMMU_cpp.h`, `MoiraExec_cpp.h`, `Moira.h` | V8 unmapped I/O + SCSI pseudo-DMA timeout | `berr030_test` |
| 10 | RTE $B honours a **software-cleared SSW.DF** (one-shot completion latch) | `MoiraExec_cpp.h`, `MoiraExecMMU_cpp.h`, `Moira.h` | Mac OS slot-probe recovery, else a 6.8M-deep vector-2 storm | `berr030_test`, `lcii_boot_etalon` |
| 11 | **Prefetch-pipe carry** across a translation switch + mode-5 **IPL polling** | `MoiraExecMMU_cpp.h`, `Moira.h`, `Moira.cpp` | the LC II ROM banks on the real 030's 3-word pipe; TimeDBRA needs prompt IRQs | `lcii_boot_etalon` |
| 12 | `isStopped()` accessor | `Moira.h` | debug-only; STOP vs spin loop | — |
| 13 | **IRQ-recognition delay** after a mask-lowering SR write (`irqDelay`) | `Moira.cpp`, `Moira.h` | SimCity 2000 re-entered its VBL task with a clobbered A5 | `lcii_boot_etalon` |
| 14 | **68030 i-cache timing overlay** `PomIcache` (folded inline from a virtual hook) + exact CACR CI/CEI strobes (`pomInvalidateIcache030`) | `Moira.h`, `MoiraExecMMU_cpp.h` | 020 cycle model over-charges cached 030 code; the virtual hook cost ~11% | `lcii_boot_etalon`, `berr030_test` |
| 15 | **Odd-SP interrupt frames**: no A0 masking on 010/020, single vector scaling | `MoiraExceptions_cpp.h` | Lode Runner launch freeze (spurious FORMAT ERROR) | `lcii_launch_etalon`, `sst68000` |
| 16 | **ATC performance**: O(1) pseudo-LRU, last-hit probe, `MOIRA_HOT_INLINE` | `MoiraExecMMU_cpp.h`, `Moira.h`, `Moira.cpp` | 38% of LC II machine time was in two 22-entry scans | `sst68030` |
| 17 | **68040 integer core** + 040 MMU registers, MOVEC/MOVE16/CINV/CPUSH, 040 trace, no-FPU format $4 | `MoiraExec_cpp.h`, `MoiraALU_cpp.h`, `MoiraExceptions_cpp.h`, `MoiraTypes.h`, `Moira.h`, `Moira.cpp`, `MoiraInit_cpp.h` | Quadra/Centris/LC 475 family | `sst68040` |
| 18 | **68040 MMU bus translation** (Q3) + **32-entry I/D ATC** (Q8) | `MoiraExecMMU_cpp.h`, `Moira.h`, `Moira.cpp` | 7 200/7 200 pinned; format $7 last-write dichotomy | `sst68040`, `q605_boot_etalon` |
| 19 | **External /BERR (040)** `extBusError040()` | `Moira.h`, `MoiraExecMMU_cpp.h` | the O6 twin for MEMCjr/djMEMC/F108 unmapped I/O | `q605_boot_etalon` |
| 20 | **Watchpoints under the MMU** — logical-address hooks on the 4 translated paths | `MoiraExecMMU_cpp.h` | `readM`/`writeM` are bypassed on the 030/040 | — (debug) |
| 21 | **External /BERR on the plain 68020** (queue refill, access capture, guarded restores) | `MoiraExceptions_cpp.h`, `MoiraDataflow_cpp.h`, `MoiraExecMMU_cpp.h` | the Mac LC ROM's 32-bit probe died in a DS-1 Sad Mac | `lc_boot_etalon` |
| 22 | **JIT seam** — fetch window (040/030/020 + the cycle-exact 68000 flavour), data TLB (with the per-slice `codeMask`), probes, `pomJitExecOne`, layout, ATC-eviction hook, bus-stall hook, `PomJitTiming` probe | `Moira.h`, `Moira.cpp`, `MoiraExecMMU_cpp.h`, `MoiraDataflow_cpp.h` | the second execution engine drives this object from `src/jit/` | `jit_lockstep_test`, `jit_system_boot_etalon` |
| 23 | **Save-state seam** `pomFlushAtcs()` | `Moira.h` | a restored snapshot replaces the page tables under live ATCs | `savestate_030_test`, `savestate_040_test` |
| 24 | **68010-only `readBuffer`/`writeBuffer`** — `pomSetRB<C>`/`pomSetWB<C>` setters; the store compiles away off the C68010 core, argument side effects (the address-error dummy `readM`) preserved | `Moira.h`, `MoiraDataflow_cpp.h`, `MoiraExec_cpp.h` | the buffers are 68010 format-$8 frame state and no Mac is a 68010; maintained on every core they made snapshot bytes depend on JIT arming history | `savestate_040_test` |
| 25 | **MC68020 CALLM/RTM** — type-$00/$01 module frames, CPU-space access-control protocol, stack switching and argument copy | `MoiraExec_cpp.h`, `Moira.h`, `MoiraTypes.h` | the final two integer stubs in the 020 instruction map | `callm_rtm_test` |
| 26 | **MC68030 FPU post-instruction frame** — format $3 with operand EA, like the 040 (a one-day format-$2 excursion on MC68030UM reading was REVERTED by ruling D23, 2026-08-18: the oracle pushes $3 on the 030 too, and oracle wins over spec) | `MoiraExecFPU_cpp.h` | `fpu_sanity`, `sst68030` pin it | `fpu_sanity` |
| 27 | **Integrated MC68040 FPU** — sparse native map, forced S/D precision, FPSP traps, revision-$41 FSAVE and BUSY resume | `MoiraExecFPU_cpp.h`, `Moira.h`, `MoiraTypes.h` | full-040 machines no longer masquerade as an attached 68882 | `fpu040_test`, `sst68040` |
| 28 | **MC68040 data-bearing caches** — I/D line contents, WT/CB/NC policy, writeback, CPUSH/CINV, snooping and hit/fill/push timing | `MoiraCache040.h`, `MoiraExecMMU_cpp.h`, `Moira.h` | tags alone could not expose stale copyback data or alternate-master coherency | `cache040_test`, `sst68040` |
| 29 | **MC68881/2 mid-instruction interrupts** — protocol checkpoints, format $9, BUSY FSAVE/FRESTORE resume | `MoiraExecFPU_cpp.h`, `MoiraExceptions_cpp.h`, `MoiraExec_cpp.h`, `Moira.h`, `MoiraTypes.h` | long coprocessor commands are interruptible between null/come-again responses | `fpu_sanity` |
| 30 | **Saturating 68030 access-log counters** — `pomMmuBumpIdx`/`pomMmuBumpIdxDone` replace the raw `++` at all ten log sites | `Moira.h`, `MoiraExecMMU_cpp.h`, `MoiraExec_cpp.h` | `mmuIdx`/`mmuIdxDone` are per-INSTRUCTION state reset by `mmuExecuteStart()`; generated 68030 code enters through `pomJitWriteData`/`pomJitReadData` and never passes that loop head, so they grew without bound. Signed overflow is UB, and the store guard `mmuIdxDone < 10` is signed: at INT_MAX the counter wrapped to INT_MIN, the guard passed again and the next logged access wrote `mmuAd[-2147483648]` | `jit_lockstep_030_test`, `jit_lockstep_030_x64_experimental_test`, `jit_lockstep_030_x64_alignment_test` |
| 31 | **Identity-sized probe bound on the 68030** — `pomIdentityProbeBound()`, applied in the TT-match and TC.E-off branches of `pomJitProbeCode` and `pomJitProbeData` | `Moira.h`, `MoiraExecMMU_cpp.h` | both branches answer *identity* and return before the ATC, but carried `pageLen` from TC.PS — 0 until the OS programs TC, and the legal values are 8..15. The engine was handed a **one-byte page** and refused every window arm: 965 013 refusals per LC II boot, 95.6 % of all of them, every one before the MMU is enabled. Interpreter untouched (it never calls a probe), ATC paths untouched (only reached with TC.E set). Measured on the 120 000-step lockstep: interpreter fallback 46.8 M → 15.0 M, refusals 1 009 036 → 44 024, degenerate → 0 | `jit_lockstep_030_blocks_test`, `sst68030`, `lcii_boot_etalon` |
| 32 | **Post-PMOVE pipe visibility** — `pomMmuPipeLive()` accessor; the engine refuses to arm the code window while the pre-switch fetch pipe is live | `Moira.h` | mmuFetchWord serves pipe fetches BEFORE the i-cache overlay counters, so the interpreter counts nothing in a PMOVE's three-word shadow while a native block's folded charge counts its traced fetchWords — a +2 fetches/+2 hits/0 miss/0 cycle lockstep divergence in the System's self-patched MMU-init loop. The engine-side refusal (`ArmFail::Pipe`) makes both engines run the shadow interpreted, not-counting included | `jit_lockstep_030_test`, `jit_lockstep_030_x64_experimental_test`, `jit_lockstep_030_x64_alignment_test` |

Rows 2-21 and 25-32 are the accuracy work; rows 22-24 are pure seams (inert
when nothing arms them). The twelve files carrying no `POM68K` marker at all —
`MoiraDasm*` (4), `StrWriter*` (2), `MoiraDebugger.*` (2), `MoiraMacros.h`,
`MoiraALU.h`, `MoiraExceptions.h`, `MoiraInit.h` — are where an upstream fix can
still be taken as-is; everything else is ported by hand, per the fork decision
above. One caveat on that list: `MoiraMacros.h` carries a **NeoST** patch
(`POLL_IPL` → `pollIpl()`, `NEOST_VENDOR.md`), so it is unmarked by the
`POM68K` grep but not pristine.

## Build configuration (`Moira/MoiraConfig.h`)

- `MOIRA_PRECISE_TIMING` → **true** (cycle-exact Mac Plus: `sync()` before
  each bus access, required for video/RAM contention later)
- `MOIRA_EMULATE_ADDRESS_ERROR` → **true** (Mac software relies on address
  errors; also needed for oracle parity)
- `MOIRA_MIMIC_MUSASHI` → **false** (accuracy over Musashi compatibility)

## SingleStepTests/680x0 convergence patches (2026-07-14)

The 68000 core was reconciled against the full `SingleStepTests/680x0`
corpus (124 files × 8 065 vectors — registers, SR, USP/SSP, PC + prefetch
queue, RAM, exact cycles). Result: **1 000 058 / 1 000 060** (the 2 misses
are upstream-documented bad data, SST issue #4, skipped in the harness).
Per the project rule, **the oracle wins** on every spec conflict. Patches,
all marked `POM68K:` in the code:

Address-error (AE) machinery:
- `MoiraExceptions_cpp.h execAddressError`: 68000 idle = 0 for data faults,
  2 for instruction-flow faults (discriminated by the frame's I/N bit);
  upstream had 8 for all.
- `MoiraDataflow_cpp.h makeFrame`: 68000 data-fault frames stack
  `getPC() - 2` uniformly (upstream had per-mode ±2 corrections;
  `STD_AE_FRAME` in `MoiraExec_cpp.h` is now 0).
- `MoiraDataflow_cpp.h readOp`: `(An)+` is post-incremented BEFORE the
  access — the register updates even when the access faults. `writeOp`
  keeps the upstream order (write faults leave `(An)+` unmodified).
- `MoiraDataflow_cpp.h readM`: operand reads always drive FC = data (101),
  including PC-relative modes.
- Instruction-flow faults (Bra/Bcc/DBcc/BSR/JMP/JSR/RTS/RTR/RTE): frame =
  `AE_PROG|AE_SET_CB3` with stacked PC = **target − 4**; BSR pushes the
  return address before faulting (SSP −18); DBcc decrements Dn before the
  fault.
- `execAddxEa` (ADDX/SUBX −(An),−(An)): a faulting `.l` source/dest leaves
  An at **init−2** (two word steps); plain reads keep the full −4.
- MOVE family: dest-fault frames carry the MOVE's own opcode in IRD/code
  (no CB3, independent of S/C); interim CCR before a faulting write =
  N(high word)/Z(full long)/V=0/C=0 on `.l`, N/Z(word) on `.w`; abs.l dest
  fault +2 idle; `-(An).l` dest fault leaves An at init−2 (+2 idle).
- MOVEM: +2 idle on AE, uniform PC−2 frames, `(An)+` leaves An advanced by
  one word.
- CMPM / DIVS / DIVU / CHK AE catches: no extra idle (upstream +2).

Instruction behavior:
- DIVU/DIVS overflow: V=1, C=0, **N/Z preserved** (upstream N=1/Z=0); every
  DIVS overflow exits early at 16/18 cycles total (upstream ran the full
  loop on late overflow — ijor's paper says hardware does too; SST/CLK
  disagrees and wins here, flagged for oracle #2).
- Divide-by-zero: N=Z=V=C=0; stacked PC = the DIV's own address.
- CHK: C=V=0, Z=(value==0) always; N=1 if value<0 (wins), N=0 if
  value>bound, else preserved.
- ASR: shifting past the operand width clears C and X even for negative
  values (**flagged for re-verification against oracle #2** — contradicts
  the common reading of hardware behavior).
- ADDQ/SUBQ `.l` to An: 6 cycles (upstream 8).
- BTST Dn,#imm: 10 cycles (upstream 8).
- LINK A7: pushes the decremented SP unconditionally (upstream only in
  Musashi mode).

## 68030 MMU-instruction convergence patches (2026-07-15, O4 slice 1)

The 68030 MMU *instructions* (PMOVE/PTEST/PFLUSH/PFLUSHA/PLOAD) now
execute, converged by differential fuzzing against the **Musashi oracle**
(then `oracle/musashi/`, MAME 0.276 `m68kmmu.h`; that tree was **retired and
deleted 2026-07-15** — see the next section and `oracle/fuzz/disputes/
NOTES.md`, which is where its rulings survive) — 2 900/2 900 `family=mmu
--mmu off` vectors across 8 seeds, gated by `ctest -R sst68030`
(`tests/data/sst68030/mmu_off.json`). Address translation on the bus is a
LATER slice; the registers round-trip (TC keeps E=1 values). Oracle-vs-
manual conflicts are logged in `oracle/fuzz/disputes/NOTES.md` (D1-D7)
for re-arbitration when oracle B (WinUAE) lands.

> **Superseded in part** — every "no ATC modelled" below is true of this
> slice only. O4 slice 3 (§ *68030 MMU bus layer*) makes the 22-entry ATC
> real, and PFLUSH/PLOAD/PTEST level 0 act on it. Read this file
> chronologically; § *Model support in this copy* is the current state.

Changes:

- `Moira/MoiraTypes.h Registers`: added 68030 MMU registers `crp`, `srp`
  (u64), `tc`, `tt0`, `tt1` (u32), `mmusr` (u16) — MC68030UM § 9.7.2;
  cleared on reset via the existing `reg = { }` (§ 9.7.2.2: TC.E off).
- `Moira/Moira.h`: public `get/setCRP·SRP·TC·TT0·TT1·MMUSR` accessors
  (getVBR style) + private MMU helper declarations (`mmu*`,
  `execMmuConfigError`).
- `Moira/MoiraExecMMU_cpp.h`: replaced the five `throw`ing exec stubs with
  real implementations + helpers (`mmuTranslate`, `mmuWalkTables`,
  `mmuMatchTT`, `mmuUpdateSR`, `mmuUpdateDescriptor`, `mmuFCFromModes`,
  `mmuDecodeEA`, `mmuRead/WriteEA16/32/64`, `mmuRead32/mmuWrite32`):
  - PMOVE: all three formats (TT0/TT1 32-bit, TC 32-bit + SRP/CRP 64-bit,
    MMUSR 16-bit), both directions, FD bit stored-only (no ATC). Writing
    an invalid TC (E=1 with PS+IS+TIA..TID ≠ 32 or PS < 8) or a DT=0
    root pointer keeps the (E-cleared) value and takes the **MMU
    configuration exception, vector 56, format-2 frame**
    (`execMmuConfigError`, mirroring Musashi `m68ki_exception_trap`).
  - PTEST: real translation-table walk (§ 9.5.3, short/long descriptors,
    indirection, early termination) with MMUSR reporting (§ 9.7.2.6) and
    descriptor address → An (A bit). Level 0 = ATC search, always a miss
    (no ATC modelled — the oracle flushes its ATC on every state load).
  - PLOAD: same walk with U/M history updates written to RAM
    (§ 9.5.3.5); MMUSR untouched.
  - PFLUSH/PFLUSHA: no ATC → no-ops that still consume the extension
    word (+ EA decode side effects for the fc|ea form).
  - **Oracle-followed quirks** (disputes D1-D5) — *superseded by the
    2026-07-15 two-oracle arbitration, next section*: no privilege
    check, MMUSR→Dn full-register write, DT=0 overwriting MMUSR,
    long-indirect second word at +0, walks with TC.E=0.
  - Invalid encodings keep falling to `execIllegal`/Line-F exactly as
    `execPGen` routes them (stricter than Musashi — disputes D6/D6b).
- Table-walk memory traffic uses the raw physical `read16/write16` bus
  interface (like the oracle's `m68k_read_memory_32`), untranslated until
  the bus slice lands.

## 68030 MMU two-oracle arbitration (2026-07-15, O4 arbitration turn 1)

The WinUAE oracle (`oracle/uae`, PRIMARY) arbitrated disputes D1-D5 and
won every one (rulings + probe evidence in
`oracle/fuzz/disputes/NOTES.md` § Arbitrated). The MMU exec layer was
moved off the Musashi-solo quirks onto the WinUAE/manual truth, and the
Musashi oracle itself was patched to converge (its `VENDOR.md`, gone with
the tree — Musashi won 0 arbitrations and was retired the same day, ruling
in `oracle/fuzz/disputes/NOTES.md`). Gate: `ctest -R sst68030`, 520/520 two-oracle-agreed
vectors (`mmu_off` 210 / `core_off` 160 / `random_off` 150); `ctest -R
sst68000` unchanged at 1 000 058/1 000 058.

- `Moira/MoiraExecMMU_cpp.h` — decode rewritten to mirror WinUAE's
  `mmu_op30` + gencpu MMUOP030 handlers:
  - D1: `execPGen` starts with `SUPERVISOR_MODE_ONLY` (vector 8 before
    the extension word); PC-relative/immediate/`Mode::IP` opcodes
    ($F03A-$F03F) are Line-F even in user mode;
  - D2/D6b: the EA is computed up front (`mmuDecodeEA`, WinUAE order —
    extension words consumed, `(An)+`/`-(An)` adjustments survive the
    trap); `Dn/An/(An)+/-(An)` EAs and every reserved extension-word
    field now raise Line-F (`execLineF`, no longer `execIllegal`);
    extension formats 101/110/111 are silent no-ops;
  - D3: long-indirect second word read at +4, and indirection detected
    on "next TI nibble zero" (both walks previously replicated Musashi's
    unmasked test that only fired at TID);
  - D4: DT=0 ORs I into the accumulated MMUSR (WP kept);
  - D5 refinements: PTEST level 0 = TT match or I; PTEST level > 0 and
    PLOAD walk the tree directly via the new `mmuRootPointer` — no TT
    match, no fc=7 bypass (`mmuTranslate` removed);
  - `mmuFCFromModes` now returns bool with WinUAE's loose/strict decode
    (00xxx → SFC/DFC on bit 0; bits 4-3 = 11 → Line-F);
  - vector 56 pushes a format $0 frame with the next PC
    (`writeStackFrame0000`; the format-$2 frame lost the arbitration);
  - `mmuReadEA16/32/64`, `mmuWriteEA16/32/64` removed (PMOVE uses
    `readM/writeM` at the pre-decoded address).
- `Moira/Moira.h` — declarations updated accordingly.
- `Moira/MoiraExec_cpp.h execMovemRgEa` (-(An) path) — on 020+ the value
  stored for a base register in the list is the **initial value minus
  one operation size** (M68000PRM, WinUAE-confirmed); upstream's
  per-iteration `writeA` stored the running EA. 68000 path untouched.

## 68030 MMU bus layer (2026-07-15, O4 slice 3)

Bus-level address translation (MC68030UM § 9.5) now runs on every memory
access when `cpuModel == M68030` and TC.E is set, modeled byte-for-byte
on the **primary oracle** (WinUAE `cpummu030.c`, hatari e77819f7) and
converged by the O4 fuzz loop: **875/875** pinned gate vectors
(`ctest -R sst68030` — mmu-off 520, duo-agreed identity/tt 250,
WinUAE-solo fault corpora 105, arbitration D9) plus 2 300+ scratch
vectors across seeds; `sst68000` unchanged at 1 000 058/1 000 058 and
all Mac Plus boot etalons green (the hooks are `if constexpr (C ==
Core::C68020)`-gated and compile out of the 68000/68010 cores; the only
68000-visible cost is one predictable `cpuModel` branch in `execute()`).

- `Moira/MoiraTypes.h` — `MmuBusError` exception; `MMU_NOFIXUP` flag.
- `Moira/Moira.h` — 22-entry ATC (`MmuAtcEntry mmuAtcArr[22]`, § 9.5.2:
  pseudo-LRU history bit, busError/WP/M/CI flags) + per-instruction
  restart/fault bookkeeping mirroring WinUAE's globals (`mmuState[3]`,
  `mmuAd[]` access-value log, `mmuIdx/mmuIdxDone`, data buffer, disp
  store, opcode, (An)± fixup encodings, fault capture for the frames).
- `Moira/MoiraExecMMU_cpp.h` — the translation core:
  - `mmuTranslateAccess`: fc=7 bypass, TT OK-match (wrong-direction TT
    falls through; locked RMW needs RWM=1), ATC lookup (write hits on
    unmodified pages are invalidated to force an M-updating walk), fill
    on miss, fault on busError/WP-write;
  - `mmuBusWalk`: WinUAE `mmu030_table_search` level 0 — FCL, short/long
    descriptors, limit checks, early termination (unused index bits into
    the page address), indirection, U on traversed descriptors and U+M
    on the page (skipped on supervisor violation);
  - `mmuRead/mmuWrite`: 68030 bus splitting (odd word = B+B, unaligned
    long = W+W or B+W+B) with per-sub-access translation, SUBACCESS
    flags, progressive data buffer, sign-extended pending-write buffer,
    and the ACCESS_CHECK/EXIT access log (`_state` accessors);
  - `mmuPageFault`: SSW per § 8.2.1 (WinUAE encodings incl. the double
    DF bit; FB|RB for instruction-stream faults), pending-fixup
    application (the (An)± adjustment survives the fault), wb3
    data/stage-B/state capture, `throw MmuBusError`;
  - PLOAD = page flush + ATC-filling level-0 search; PFLUSH/PFLUSHA
    flush for real (by fc/mask, fc+page, all); PMOVE register writes
    with FD clear flush the whole ATC (not on a vector-56 trap, never
    for MMUSR); PTEST level 0 searches the real ATC (raw-EA compare —
    WinUAE quirk kept, hit reports B/WP/M).
- `Moira/MoiraExceptions_cpp.h` — `writeStackFrameShortBusFault` ($A)
  and `writeStackFrameLongBusFault` ($B) replicate WinUAE
  `Exception_build_stack_frame` byte-for-byte (access log with the
  pending write value parked at idx_done, version/fixup word, MOVEM
  counter, disp stores, stage-B address, SSW, pipe words = 0 in mode 5);
  `execMmuBusError` mirrors the run-loop CATCH + `Exception_mmu030`:
  $A on last-write faults (PC = next instruction, CCR kept), $B
  otherwise (PC = instruction, CCR + fixups restored; prefetch-phase
  faults set bit 31 of the pipeline-status long), vector fetched through
  translation *before* the frame is pushed, odd vector or nested fault
  → HALT (double fault). Exception stacking/RTE pops are excluded from
  the access log (`mmuLogging`).
- `Moira/MoiraDataflow_cpp.h` — the hooks: `read/write` funnel diverts
  to `mmuRead/mmuWrite` under `mmuActive()`; mode-5 instruction-stream
  model for M68030 (opcode + irc refetched through translation at every
  `execute()` start, `prefetch/fullPrefetch/jumpToVector` queue refills
  suppressed — WinUAE fetches the handler/jump-target opcode at the
  NEXT step); `readExt` logs the CONSUMED extension word (`skipExt` for
  SKIP_LAST_RD sites); ABS.L and long immediates log as ONE long entry;
  IX/IXPC EAs go through `computeEAdisp030` (WinUAE
  `get_disp_ea_020_mmu030`: inner accesses rewound from the log, result
  in `mmuDispStore` + DISP flag + word count in state[2]); `computeEA`
  arms the (An)± fixups; `writeOp` marks LASTWRITE and updates (An)+
  BEFORE the last write (gencpu order, 68030 only).
- `Moira/MoiraExec_cpp.h` — MOVE handlers set the FINAL flags before the
  destination write on the 68030 (gencpu order — last-write faults stack
  the updated CCR; the SST-68000 interim-flag rules stay on the 68000
  path); `execMove4` 030 path (An decremented before the write, no
  REVERSE); MOVEM: MOVEM1 flag + `mmuState[0]` transfer counter +
  unlogged transfers with the value in the data buffer, EA logged for
  loads (`state_store`), final store marked LASTWRITE with the base
  register updated first, no 68000 guard reads, `-(An)` base-in-list
  value = initial−S without touching An mid-list; TAS/CAS drive locked
  RMW cycles (SSW RM, RWM-only TT match, write-probed ATC — data
  accesses only); MOVES Rg,Ea is a last-write with the FULL source
  register in the data buffer, Ea,Rg reads through SFC.
- `Moira/MoiraALU_cpp.h` — D10: the "ASR past width clears C/X" SST-
  68000 rule is now 68000/68010-only (both 68030 oracles keep C/X).
- `Moira/Moira.cpp` — `execute()` calls `mmuExecuteStart` for M68030
  (state reset + translated opcode fetch, may fault); `processException`
  routes `MmuBusError` → `execMmuBusError` (nested fault → halt);
  `reset()` invalidates the ATC and the bookkeeping (§ 9.5.2).

Known model limits (logged, not gate-blocking): Moira reads the two
queue words at instruction start and one word ahead on `readExt`
(WinUAE reads exactly the consumed word) — indistinguishable unless an
instruction sits within 6 bytes of a page boundary (never in the
corpora: code pages are identity-mapped and protected); RTE of $A/$B
frames does not re-run the faulted access (WinUAE restarts the
instruction; Musashi raises format error — three-way divergence, no
oracle agreement to converge on; gen030's zeroed stacks never produce
such frames).

## 68030 integer-family arbitration (2026-07-15, O4 slice 4)

The remaining integer disagreements between the oracles were arbitrated
(WinUAE won every ruling, D11-D17 + D6-remainder in
`oracle/fuzz/disputes/NOTES.md` § slice 4) and Moira converged on the
rulings. Every ALU change is gated `if constexpr (C == Core::C68020)`
(D10 pattern) — `sst68000` stays at 1 000 058/1 000 058 cycle-exact and
the Mac Plus boot etalons are untouched. Gate: `ctest -R sst68030` =
**1 040/1 040** (random_off re-pinned at 250, random_identity at 121,
both fresh seeds 81/91 and both containing D11 WinUAE-solo odd-PC
vectors); scratch: 2 672/2 672 across the full 3×3 grid × 3 seeds plus
2 000/2 000 on the slice-4 sweep seeds.

- `MoiraExec_cpp.h execChk` — C68020 routes to `setUndefinedCHK`
  (WinUAE `setchkundefinedflags`); `MoiraALU_cpp.h setUndefinedCHK` —
  upstream's port of that table had **dropped `SET_NFLG(dst < 0)`**
  (N always refreshed on the 020/030) — restored (D14).
- `MoiraExec_cpp.h execDivsMoira/execDivuMoira` — C68020 div-zero CCR
  via `setDivZeroDIVS/DIVU` (D13); `MoiraALU_cpp.h divsMoira/divuMoira`
  — C68020 overflow CCR via `setUndefinedDIVS/DIVU` (the SST-68000
  "N/Z preserved" rule stays 68000/68010-only); `execDivlMoira` case
  0b10 (DIVS.L 32/32) — $80000000/-1 is an overflow: registers stay
  unchanged, `setUndefinedDIVSL` flags (upstream wrote 0 to both);
  `divlsMoira<Word>`'s overflow branch is reachable now (assert gone).
- `MoiraExceptions_cpp.h execException` — the C68020 format-$2 frame
  (zero divide / CHK / TRAPcc) stacks the **next** instruction's
  address in the instruction-address field, same as the PC field
  (WinUAE-probed, D12; was `reg.pc0`).
- `MoiraDataflow_cpp.h computeEAfull` — the reserved full-extension
  I/IS=100 encoding no longer dereferences: memory indirection only
  when `(iis & 0b011) != 0` (WinUAE `get_disp_ea_020`, D17).
- `MoiraExec_cpp.h execLineF` — user-mode cpSAVE/cpRESTORE shapes
  (`(op & $F1C0) == $F100/$F140` with a valid cpSAVE/cpRESTORE EA) take
  a privilege violation before Line-F on the 020/030 (WinUAE
  `privileged_copro_instruction`, D6-remainder).
- **D11 — odd-PC address errors** (WinUAE-solo ruled vectors now enter
  the corpora, so Moira must replay the frames):
  - `MoiraExceptions_cpp.h execAddressError030` + `mmuCheckOddPc`:
    vector 3, format $B frame (SSW $0066, fault address = target,
    restart state zeroed, vector fetched before the frame), stacked PC
    per WinUAE's per-instruction conventions;
    `writeStackFrameLongBusFault` gained a vector parameter.
  - odd-target checks in `execBra/execBcc/execBsr/execDbcc/execJmp/
    execRts/execRtd/execRtr/execRte` (M68030 only): BSR decrements A7
    without writing the return address; DBcc faults even when the loop
    counter expired (stacked PC = the odd TARGET); JMP stacks
    instruction+2 for every EA mode, RTR instruction+2; RTR/RTE apply
    the popped CCR/SR first; pops stay popped; **JSR intentionally has
    no check** (WinUAE defers the fault to the next opcode fetch — the
    vector ends with an odd PC, Musashi-compatible). The handlers
    rebuild the access log where Moira's consumption differs from
    WinUAE's (`mmuLogReset` + `mmuLogExtWord`: Bcc/BSR displacement as
    one entry, RTD = disp + popped long, RTE = SR/PC/format).
  - `MoiraDataflow_cpp.h` read/write funnel — taken for **every**
    M68030 access, TC.E on or off (WinUAE's `_mmu030_state` accessors
    always log and split; translation is the only conditional part —
    `mmuTranslateAccess` returns the address untouched when TC.E = 0).
    This is what puts RTS/RTR/RTE pops into the $B frame's access log
    with the MMU off; behaviour of non-faulting accesses is unchanged.

## MC68882 FPU execution (2026-07-15, O5 slice 2)

Full 68882 instruction execution behind the coprocessor interface,
softfloat-backed. The Mac LC II's optional PDS FPU is a 68882 — that is
the model implemented. **Attach/detach follows the CPU-model mechanism**:
`FPUModel` enum (`MoiraTypes.h`), `fpuModel` member next to `cpuModel`,
`setFPUModel()` rebuilds the jump table (`MoiraInit_cpp.h` populates the
coprocessor-id-1 window for 020/030 when a 6888x is attached, in an
`if constexpr (C == Core::C68020)` block). With `fpuModel == NONE`
(default) the table is **byte-identical to stock Moira** — F2xx = Line-F —
which the FPU-less gate corpus pins: `ctest -R sst68030` = 1 040/1 040
unchanged, `sst68000` = 1 000 058/1 000 058, all Mac Plus etalons green.

**Softfloat provenance**: `extern/softfloat/` (new top-level vendored
dir, see its `VENDOR.md`) — John Hauser's SoftFloat-2a with the
Previous/WinUAE `SOFTFLOAT_68K` extensions and the FPSP transcendentals,
copied from the oracle vendor tree (`oracle/uae/upstream/softfloat/`,
never included at build time). Same softfloat family as the primary
oracle, so numerical convergence is by construction. GPLv2+ — Moira stays
MIT and only **links** against it (separate static lib `softfloat68k`).

Semantics are ported from WinUAE `fpp.c`/`fpp_softfloat.c` (6888x
branches, accurate mode) with file:line citations in the code:

- `Moira/MoiraTypes.h` — `FPUModel`, `FpuExtended` (raw floatx80 layout,
  keeps `Moira.h` softfloat-free).
- `Moira/Moira.h` — `fpu` state block (fp[8], fpcr/fpsr/fpiar, frame
  micro-state: null/idle, pending-exception vector, FSAVE ccr/eo);
  accessors `getFP/setFP` (SST030 3×u32 word contract),
  `getFPCR/setFPCR` (mask $FFF0), `getFPSR/setFPSR` (mask $0FFFFFF8),
  `getFPIAR/setFPIAR`, `setFPUModel/getFPUModel`; private helper decls.
- `Moira/MoiraExecFPU_cpp.h` — the implementation (was empty stubs):
  - FMOVE in/out all 7 formats (B W L S D X **P** incl. static/dynamic
    k-factor out; the Dn-destination packed quirk — conversion updates
    FPSR, then Line-F — kept, fpp.c:1810);
  - FMOVECR: 22-entry constant ROM, exact bit patterns + INEX2/rounding
    nudges + the undefined-offset garbage table (fpp.c:169-236);
  - dyadic FADD FSUB FMUL FDIV FMOD FREM FSCALE FSGLMUL FSGLDIV FCMP
    FTST; monadic FABS FNEG FSQRT FINT FINTRZ FGETEXP FGETMAN and the
    whole FPSP transcendental set incl. FSINCOS; the undocumented alias
    opmodes ($05, $07, $0B, $13, $17, $1B, $29-$2F, $39, $3B-$3F)
    execute their base op; $40-$7F (68040 S/D variants) = Line-F;
  - FPCR rounding modes RN/RZ/RM/RP and precisions X/S/D applied per op
    (incl. the quirk: invalid precision $C0 = double);
  - FPSR: cc byte from every result, FMOD/FREM quotient byte, exception
    byte + accrued byte per MC68881UM tables; FPIAR updated per WinUAE's
    rule (only when a non-BSUN exception is enabled — the oracle's
    economy, replicated on purpose);
  - FMOVEM: control registers any combination (Dn single-only, An
    FPIAR-only, #imm multi, ±(An) address fixups per fpp.c:3300-3400)
    and FP registers static+dynamic lists, raw 96-bit transfers, with
    WinUAE's EA-direction Line-F rules;
  - conditionals FBcc (W/L, FNOP = FBF.W #0), FDBcc, FScc, FTRAPcc: the
    32-predicate 6888x condition table (cputester-verified), BSUN +
    accrued IOP on the IEEE non-aware predicates over NaN, enabled BSUN
    traps (vector 48); FTRAPcc raises the integer TRAPcc exception
    (vector 7, format $2);
  - FSAVE/FRESTORE: supervisor-only; 68882 NULL frame ($00380000 —
    WinUAE's exact word, fpp.c:2512) and $3C-byte IDLE frame (version
    $1F length $38, BIU flags $540EFFFF|state); on the 68030 the eight
    internal longs are **skipped, not zeroed** (WinUAE's MMU build,
    fpp.c:2532 — the primary oracle runs that path); FRESTORE per the
    full fpuop_restore acceptance matrix (see the follow-up section
    below);
  - pending-exception model: enabled arithmetic exceptions latch a
    vector + capture the 68882 FSAVE eo/ccr data and trap
    pre-instruction at the *next* FPU instruction (the 68882 keeps the
    vector armed after the trap — WinUAE fpp.c:387).
- `Moira/MoiraInit_cpp.h` — FPU registration condition (see above).
- `Moira/Moira.cpp` — softfloat include (extern "C"), `hasFPU()` counts
  an attached 6888x, `setFPUModel()`, `reset()` resets the FPU
  (MC68881/882UM § 6.1: control regs 0, FP regs = NaN
  $7FFF FFFFFFFF FFFFFFFF).

Gates: `fpu_sanity` (hand-computed FMOVECR/2+2/DZ/OPERR/FCMP/FMOVEM +
detached-F-line) and the first differential FPU corpus
(`tests/data/sst68030_fpu`, 41/41 at the time of writing).

**Solo-corpus convergence pass (2026-07-15, same day)** — a WinUAE-solo
FPU corpus (Musashi is too weak on these classes, D9 precedent) exposed
three failure classes at 617/700; all converged to 700/700 with the duo
corpora (41/41 pinned + 90/90 fresh seed) and every hard gate unchanged:

1. **FSAVE frame state after external restore** — the WinUAE glue forces
   `regs.fpu_state = 1` after `oracle_set_state`, so its FSAVE always
   emits the IDLE frame. Convention adopted: `setFP/setFPCR/setFPSR/
   setFPIAR` leave the FPU in the non-null state (`Moira.h`); a freshly
   reset FPU still FSAVEs the NULL frame ($00380000 — WinUAE's exact
   word, version $00 + length $38, fpp.c:2512, verified).
2. **D21 — FRESTORE invalid-frame format error** (WinUAE wins, ruling
   logged by the fuzz loop): vector 14 stacks `m68k_getpc()` — the PC
   past ALL consumed words — not Moira's generic `reg.pc - 2`
   convention. Dedicated `execFRestoreFormatError` path; the shared
   FORMAT_ERROR path (RTE) is untouched.
3. **Post-instruction FP exception frame** — enabled exceptions on
   FMOVE-out trap with the **format $3** floating-point post-instruction
   frame (SR, next PC, $3xxx word, operand effective address = fp_ea;
   WinUAE `Exception_build_stack_frame_common` newcpu_common.c:1616 +
   frame case 0x3), replacing the format $0 stub. `fpu.ea` now tracks
   the operand EA (WinUAE `regs.fp_ea`), latched by
   `fpuCheckArithException`. Pre-instruction traps stay format $0 with
   PC = the re-executing FP instruction (matches WinUAE).

Fresh-seed re-verify (seeds 17/19, same day) closed two more classes,
both FPU accesses faulting through the MMU:

4. **FMOVEM restart bookkeeping** — the mmu030 build's FMOVEM FP-block
   transfers are **unlogged** (WinUAE fpp.c:2810-2841/2875-2910 uses the
   non-state `x_put_long/x_get_long`) with manual bookkeeping, exactly
   like Moira's integer MOVEM: MOVEM1 ($4000) in state[1] — plus FMOVEM
   ($2000) for memory->FP — a completed-long counter in state[0], the
   pending write value placed in the data buffer before each put, and
   memory->FP parking the first two longs of the register in flight in
   `mmu030_fmovem_store`, which a $B frame stacks in access-log padding
   slots 7/8 (`writeStackFrameLongBusFault`, new `mmuFmovemStore[2]`).
   A fault therefore stacks idx == idx_done (only the ext-word
   consumption is in the log).
5. **Plain (An)± fixups for FPU operands** — `get_fp_value`/
   `put_fp_value2` modes 3/4 and FScc -(An) arm a WinUAE `mmufixup`
   without the 0x300 flag bits: on a non-lastwrite fault the register is
   **restored to its pre-instruction value** (`cpu_restore_fixup`) while
   the frame's wb2/wb3 status byte stays 0 (`mmu030fixupreg` returns 0
   for such fixups). Moira encodes this as bit 7 of `mmuFixupReg[]`
   (restore-only), armed by `fpuArmFixup` in `fpuGetSource`/`fpuPutDest`
   (An)+/-(An) and the FScc -(An) store; `mmuPageFault` masks bit-7
   fixups out of the status byte and skips the ± adjustment.

Numbers after the pass: solo seeds 11/17/19/23 = 700/700, 211/211,
100/100, 100/100; pinned gate `ctest -R sst68030` = 1 871/1 871 (the
fpu corpora are pinned in `tests/data/sst68030/`); sst68000 and the
full 15/15 ctest unchanged.

Known model limits (for the differential loop): FMOVEM
full-extension-format EAs consume their words before a direction-rule
Line-F but memory-indirect side reads may differ from WinUAE's exact
order (still open). The historical 68040-BUSY limitation named here was
closed by the distinct integrated-040 model on 2026-08-16 (see below).

### O5 follow-ups closed (2026-07-15): FPU timing + FRESTORE frames

**FPU instruction timing** — the `CYCLES_68020` placeholders in
`MoiraExecFPU_cpp.h` are replaced by the MC68881/MC68882UM Section 8
figures, all in one table/section at the top of the file:

- **Table 8-3** (MC68882 Overall Execution Times) — per-opmode
  FPn-to-FPm totals (`fpuCk882Op[64]`, e.g. FADD 56, FMUL 76, FDIV 108,
  FSQRT 110, FSIN 394, FATANH 696), the per-format column spread for
  memory/Dn/#imm sources (`fpuCk882Fmt[]`: S +13, D +19, X +25,
  integers +38, P +855), the FMOVE-to-memory row (`fpuCk882MoveOut[]`:
  integer 110, S 38, D 44, X 50, P 2006 static k / +14 dynamic k), and
  FMOVECR 32.
- **Table 8-6** (control moves/FMOVEM, cache case, "+2 clocks if
  MC68882" footnote applied) — FPcr single moves 30 (`Rn,FPcr`) / 33
  (`FPcr,Rn`), FPcr lists 29+6n (27+6n from `#imm`), FP-register FMOVEM
  39+25n out / 37+31n in, +14 dynamic list.
- **Table 8-7** (conditionals, cache case) — FBcc 20/18, FDBcc
  20/20/24, FScc 18/22/20, FTRAPcc 39-43 taken / 18-22 not.
- **Table 8-8** (FSAVE/FRESTORE by frame, MC68882/MC68881 rows) —
  FSAVE NULL 16 / IDLE 100 ($38) / 52 ($18); FRESTORE NULL 21 /
  IDLE 105/57 / BUSY 339/291; invalid frames charge the NULL figure
  (documented estimate, no manual row).

EA-calculation cycles reuse Moira's integer mechanism verbatim
(`computeEA` accumulates the 68020 per-mode penalty into `cp`;
`CYCLES_68020(c)` syncs `c + cp` — MoiraMacros.h:25). The 68000/68010
paths are untouched. Cycles stay **advisory** in Phase 2 (SST030
`length` is not compared), but `emuCycles` orders events, so the 20-ish
placeholders (a 570-cycle FTWOTOX billed as 20) are gone. Gated by two
`fpu_sanity` timing smokes (FADD.X = 56, FMOVECR = 32, exact).

**FSAVE BUSY frames — superseded 2026-08-16.** The original oracle-parity
decision was to emit NULL/IDLE only because WinUAE has no mid-instruction
window. The later requirement explicitly targets the hardware behaviour:
long external-6888x operations now expose interrupt checkpoints and FSAVE
therefore emits a resumable BUSY frame when one is suspended.

**FRESTORE acceptance matrix** — mirrors `fpuop_restore`
(fpp.c:2593-2812) exactly under the oracle's config
(`fpu_model = 68882`, `fpu_mode = 1` softfloat,
`fpu_no_unimplemented = false` — `oracle/uae/glue.c` `pom_default_prefs`,
:80-105), oracle-verified:

| frame (1st long)  | behaviour                                        |
|-------------------|--------------------------------------------------|
| version $00       | NULL: full FPU reset (fpu_null)                  |
| $1F, size $18/$38 | 68881/68882 IDLE: reload ccr/eo/BIU micro-state; BIU bit 27 clear re-arms the pending exception (fpp.c:2781-2787) |
| $1F, size $B4/$D4 | 68881/68882 BUSY: resume POM2-authored state; opaque hardware frames remain accepted |
| $1F, other size   | format error, vector 14 (D21 PC convention)      |
| $41, size $00     | 68040 IDLE via WinUAE's version hack (fpp.c:2799-2802; get_fpu_version(68040) == $41): state = non-null, expState cleared |
| $41, size $28/$30 | 68040 UNIMP: skipped                             |
| $41, size $60     | historical 68040 hack: skipped; the integrated model added later resumes `CU_SAVEPC == $FE` arithmetic |
| $41, other size   | format error, vector 14                          |
| anything else     | format error, vector 14 (incl. $40 — it mismatches $41) |

> **Corrected 2026-08-16:** this was one mixed compatibility matrix. The
> distinct `M68881/M68882` models now accept only revision `$1F`; the
> integrated `M68040` model accepts only revision `$41` and implements its
> BUSY resume path.

Note the version byte of **both** 6888x frame flavours is $1F
(get_fpu_version, fpp.c:1432-1449) — there is no $1E.

The fuzzer now exercises this: `gen030.py` plants a well-formed frame
image at ~60 % of FRESTORE operand addresses (`_plant_frestore_frame`:
NULL / IDLE $38+$18 with plausible internals and a flipping BIU bit 27 /
BUSY $D4+$B4 / $41 hack frames / wrong-version / wrong-length).
Fresh-seed verification (seeds 29/31, n=200, fpu × off/identity,
WinUAE-solo): **800/800** at first replay, 93 FRESTOREs covering every
matrix row. New `fpu_sanity` cases pin NULL reset, IDLE $38 acceptance
(+$3C postincrement), BUSY $D4 skip (+$D8 postincrement) and
garbage → vector 14.

## Integrated MC68040 FPU + 020/030 closure audit (2026-08-16)

Full-040 machines now select `FPUModel::M68040`, distinct from an external
68882. The model executes the 040 hardware subset, including all sixteen
forced-single/forced-double opmodes, applies the 040 FPCR mask, predicate
table and immediate exception rules, and sends valid non-hardware operations
to vector 11 for the guest FPSP. Denormal/unnormal and packed operands take
the software-datatype path with the 040's packed/subnormal BUSY payload.
FMOVEM uses the integrated core's distinct list and 96-bit word ordering.
FSAVE emits revision-$41 NULL/IDLE, UNIMP and BUSY frames; FRESTORE resumes a
planted BUSY arithmetic command when `CU_SAVEPC` is `$FE`, including the
FPTE15/ET15 denormalization bits. `fpu040_test` pins the sparse decode map,
precision boundaries, nonmaskable integer conversion exceptions, datatype
payloads, FMOVEM, every native `$40-$7F` opcode, FSAVE and BUSY resume.

The family audit found two older gaps outside the 040. On the 68030,
post-instruction external-FPU exceptions now stack format $2 plus the
instruction address (pre-instruction exceptions remain format $0). On the
68020, the empty CALLM/RTM bodies are replaced by the complete 24-byte
type-$00/$01 module-frame protocol, including the optional CPU-space
access-control hooks and stack/argument transfer. `fpu_sanity` and
`callm_rtm_test` pin those corrections.

The external 6888x boundary closed later the same day. Arithmetic and
transcendental commands keep their completed image private while the MPU
waits, sample IPL at protocol checkpoints, and on an eligible interrupt
stack the 68020/030 format-$9 continuation frame. FSAVE writes a complete
$1F/$B4 (68881) or $1F/$D4 (68882) BUSY frame and idles the coprocessor;
FRESTORE reloads the staged result and remaining cycles, and RTE resumes at
the following CPU instruction. `fpu_sanity` interrupts FSQRT, saves/restores
inside the handler, verifies the frame and the exact final FP0 value.

## External /BERR + RTE $A (2026-07-15, O6 slice 1)

The LC II machine needs bus errors the CPU core cannot see coming from
translation alone: unmapped I/O in `$F00000+` (the ROM's address-map
probe builds `AddrMapFlags` from them) and the SCSI pseudo-DMA DRQ
timeout (`docs/LCII_HARDWARE.md` § SCSI). Three additions, M68030-only:

- **`Moira::extBusError()`** (public, `[[noreturn]]`,
  MoiraExecMMU_cpp.h): called from *inside* a `read8/16`/`write8/16`
  bus callback when the machine asserts /BERR. It replays the recorded
  in-flight sub-access into `mmuPageFault`, so the stacked frame is
  byte-identical to a translation fault at the same point: format **$B**
  (PC = faulted instruction, restart) for reads/fetches, format **$A**
  (PC = next instruction) when the fault lands on the instruction's
  last write. A nested fault while stacking → HALT (double fault).
- **Access-context capture**: `mmuTranslateAccess` and `mmuFetchWord`
  record `{addr, sswFlags, fc, write}` into `mmuAccAddr/Ssw/Fc/Write`
  before every physical access — including with TC.E off (the funnel
  routes every M68030 access through them since O4 slice 4).
- **RTE format $A** (MoiraExec_cpp.h): the C68020 RTE previously knew
  only $0/$1/$2/$B and took a FORMAT_ERROR on $A frames. It now pops the
  16-word short bus-fault frame and continues at the stacked PC. Policy
  matches the existing $B path (and Musashi/MAME, which boots this ROM
  family): the pending access is **not** re-run — the restart model.
  Handlers that clear SSW.DF expect exactly this; handlers that want a
  retry restart the whole instruction ($B) or re-issue the access.

Gate: `tests/berr030_test.cpp` (read → $B + fault address + restart via
handler PC fixup; write → $A + RTE continuation; fetch → $B with
FB|RB SSW; vector-2 counts). `sst68030` unaffected (capture is
record-only; no vector reaches RTE-of-$A/$B).

## RTE $B honors a software-cleared SSW.DF (2026-07-15, O6.9)

Mac OS's slot-probe recovery (`GISTPERSO` System, RAM routine at
$1313E/$1315E; also the ROM's own probes) uses the documented 68030
protocol: on a data-read bus fault it RTEs the $B frame **with DF still
set** up to 63 times ("retry the cycle"), then clears DF with `bclr #0`
on the stacked SSW high byte and RTEs — "the cycle is done, complete
the instruction with the frame's data input buffer". Moira's $B RTE
discarded the frame and resumed at the stacked PC unconditionally, so
the probe re-ran and re-faulted forever (6.8M-deep vector-2 storm,
TODO O6.9).

Fix (MoiraExec_cpp.h RTE $B + MoiraExecMMU_cpp.h + Moira.h): the RTE
pops now *capture* SSW, fault address and data input buffer (bus access
pattern unchanged). `SSW & $0200 && !($0100)` — the bit-9 "frame
carried DF" marker WinUAE encodes and our `mmuPageFault` already stacks
($0300) — arms a one-shot latch `{addr, read/write, data, stacked pc}`.
When the restarted instruction re-issues that exact access,
`mmuRead`/`mmuWrite` complete it without a bus cycle: reads return the
data input buffer (CLIPped to size, logged in `mmuAd` like a real
completion), writes are skipped. Any new fault voids the latch. RM
(locked RMW) frames are excluded, as in WinUAE.

Oracle status: WinUAE's `m68k_do_rte_mmu030` implements the same
semantics via full mid-instruction continuation (access-log replay);
Moira expresses it in its restart model as re-execution + substitution.
The mode-5 oracle cannot testify byte-for-byte on the interleaving (its
RTE performs retried accesses *inside* the RTE step), so this is pinned
by a machine-level gate instead: `berr030_test` § DF-cleared RTE, plus
the GISTPERSO boot etalon. `sst68030`'s 3 082 vectors are unaffected
(none reaches RTE-of-$B; re-verified green).

## Prefetch-pipe carry + mode-5 IPL polling (2026-07-15, O6 machine debug)

Two divergences from the real 030 surfaced only when the LC II ROM ran
on the real machine (both invisible to single-instruction fuzzing):

- **Prefetch pipe across a translation switch.** The mode-5 loop
  (O4 slice 3) refetches `ird`/`irc` through translation at every
  instruction start. The real 030 pipe holds the next ~3 words fetched
  under the OLD mapping, and Apple's ROMs bank on it: the LC II enables
  the MMU with `pmove (A3),tc; nop; bne; jmp (A5)` ($A416AA-$A416B6) —
  exactly the pipe depth — where the post-switch 32-bit map translates
  the logical addresses of those very instructions to RAM garbage.
  Fix: `mmuCapturePipe()` (called by the PMOVE handlers for TC/SRP/CRP
  with the old registers still in force) snapshots 4 words at `reg.pc`;
  `mmuFetchWord` serves linear fetches from the snapshot and drops it on
  the first out-of-window fetch. Capture is fault-safe (a short pipe
  just ends) and read-only — fuzz vectors are unaffected.
- **IPL sampling.** Mode-5 suppressed the end-of-instruction prefetches
  that carried `POLL_IPL`, so an instruction stream without data
  accesses (`dbra D0, .` …) never sampled the IPL lines and interrupts
  were delivered only after the loop fell through. The LC II ROM's
  TimeDBRA calibration ($A00820: level-1 autovector hijack + VIA1 T2
  one-shot + unbounded dbra) requires prompt delivery — the late
  interrupt double-stored a result and derailed the boot into the POST
  debug console. Fix: `POLL_IPL` at the top of `mmuExecuteStart` (every
  instruction boundary). Fuzz vectors never drive the IPL pins — no
  behavioural change there.

## `isStopped()` accessor (2026-07-16, review fix)

- `Moira.h`: added `bool isStopped() const { return flags &
  State::STOPPED; }` next to `isHalted()`. Debug-only surface (no
  behavioural change): `tests/lcii_trace.cpp` printed its "stopped"
  end-of-run flag from `(SR & 0)` — constant false — because no public
  accessor for `State::STOPPED` existed. Distinguishes a STOP-parked
  CPU (waiting on an IPL that never rises) from a spin loop.

## IRQ-recognition delay after a mask-lowering SR write (2026-07-16, O6.12)

68020+ only (`Moira.cpp` `setSR` + the run loop; `Moira.h` new `int
irqDelay`). When an SR write (MOVE-to-SR / RTE / ANDI/ORI/EORI-to-SR)
LOWERS the interrupt mask, interrupt recognition is deferred by two
instructions: `setSR` arms `irqDelay = 2` when `ipl < reg.sr.ipl`, and the
run loop decrements it instead of calling `checkForIrq()` while it is
nonzero. The 68k does not sample interrupts until after the instruction
following a mask change (M68000 PRM); modelling it guarantees the
interrupted program makes forward progress before the next IRQ.

Why: SimCity 2000 on the LC II redraws its screen from a per-VBL task;
the QuickDraw blit lowers IPL at the instruction just before it restores
A5 ($A4B414 MOVE-to-SR → $A4B416 → $A4B418 movem). With interrupts
sampled immediately, a VBL/timer IRQ fired in that window, its (long,
mouse-cursor-heavy) redraw handler overran the frame, and by RTE the next
IRQ was already pending → taken before A5 was restored → the task
re-entered with A5 = the blit working value → `jsr (A5+$14AA)` into
garbage ("coprocesseur absent" Line-F). See CHANGELOG 2026-07-16,
memory `pom68k-simcity-crash`. A depth of 2 (not the strict 1) is what
empirically clears the worst case (redraw + continuous mouse motion);
paired with the `Cpu030` i-cache throughput model (kCacheBoost). Gates
unaffected: sst68030 (3082 vectors), lcii_boot_etalon, cpu_smoke, all
green — SST vectors are single instructions with no IRQ, and the guard is
68020+ so the cycle-exact 68000 path is byte-identical.

## `willFetchInstr` delegate — 68030 i-cache overlay hook (2026-07-17, O6.13)

`Moira.h` new `virtual void willFetchInstr(u32 addr, bool super) { }`, called
in `MoiraExecMMU_cpp.h::mmuFetchWord` (the sole 68030 instruction-word fetch
choke point — mode-5 has no prefetch queue, so opcode/lookahead/extension all
fetch through here) with the LOGICAL address (pre-translation; the 030 caches
are logical) and the supervisor flag.

Why: Moira runs the 68030 on its `Core::C68020` cycle model — 68020 cycle
placeholders, no i-cache, no d-cache — so it charges more cycles per
instruction than the real cached 030, worst on tight loops. The wrapper
(`Cpu030`) uses this hook to model the on-chip 256-byte instruction cache and
charge a fetch-bus penalty only on a MISS: cache-resident code (SimCity's
redraw measured 95% hit) runs near the throughput ceiling while miss-heavy
cold code is throttled toward real speed — the per-code-path behaviour of the
real cache instead of a flat global boost. Zero cost when not overridden
(empty virtual). Gates unaffected: the hook only fires on the 68030 MMU fetch
path; sst68030 (3082 vectors) is state-based (no timing compared) and stays
green, as do lcii_boot_etalon and the 68000/68010 paths (never reach
mmuFetchWord). Not a new `Core::C68030`: the 020/030 share Moira's execution
core by design, and the cache is a timing overlay, not a different instruction
set. See `src/Cpu030.*`, CHANGELOG 2026-07-17.

**Folded inline (2026-07-17, perf):** the virtual hook is GONE — the
per-instruction-word indirect call + out-of-line model measured ~11% of the
whole emulator (TODO § Performance). The cache model now lives as a
`protected` member struct `Moira::PomIcache` (`Moira.h`, same MC68030UM §6
16×4-LW logical direct-mapped model, same CACR-bit-0 gate and miss penalty)
executed inline at the same spot in `mmuFetchWord`, guarded by
`pomIcache.armed` (default off — bare-Moira users and the 68000 wrapper pay
one predictable branch, nothing else). `Cpu030` arms it in its constructor
(`missPenalty` = `POM68K_ICACHE_MISS`), flushes it from `didChangeCACR`/
`hardReset`, and re-exports the hit/miss counters via `icacheStats()`.
Behaviour byte-identical (lcii_boot_etalon: same 0.09/0.48 metrics, same
9583 SCSI commands); boot etalon wall time 143 s → 122 s (-15%).

**Exact cache-clear commands (2026-08-12):** `CACR.CI` still invalidates all
sixteen lines, while `CACR.CEI` now invalidates only the longword selected by
`CAAR[7:4]` (line) and `CAAR[3:2]` (word), matching the WinUAE oracle
(`newcpu.c`). The earlier conservative implementation flushed the whole timing
overlay for either strobe. `berr030_test` pins the selected-word invalidation,
preservation of its three line neighbours, and preservation of another line.

**Closed 68030 restart/fault oracle gaps (2026-08-12):** the local WinUAE
mode-5 oracle now pins three previously missing paths in `berr030_test`.
Format `$A` RTE restores the frame's fault address, SSW and data-output buffer
and completes the pending last-write bus cycle before continuing at the next
PC. Format `$B` RTE applies the inverse WB2/WB3 `(An)±` fixups before freshly
decoding the faulted instruction, preventing a double postincrement. PMOVE
operand translation faults match WinUAE's format `$B` frames in both
directions (SSW `$0345` read / `$0305` write). Finally, a translated FMOVEM
full-extension preindexed-indirect EA reads its pointer before the three
ascending longs of the selected FP register; the resulting raw 96-bit image
is byte-identical to WinUAE. The Darwin oracle build was also made portable:
Mach-O uses `-dead_strip` and the existing `ORACLE_EXPORT` visibility markers
instead of ELF-only `--gc-sections` and `--version-script`.

## Odd-SP interrupt frames: no A0 masking on 010/020, single vector scaling (2026-07-17, Lode Runner launch freeze)

`MoiraExceptions_cpp.h`, two related fixes on the interrupt-frame path:

1. **`writeStackFrame0000`, C68010/C68020 branch: the four frame writes
   dropped their `& ~1` address masks.** Masking A0 is 68000 bus
   behaviour (and stays in the C68000 branch, validated by the 1M-vector
   sst68000 corpus); the 010/020/030 write misaligned frames byte-exact
   — the mode-5 `mmuWrite` already splits odd word/long accesses
   correctly — and `execRte` reads the frame back at the TRUE addresses.
   The asymmetry meant an interrupt accepted while SP was odd (legal on
   the 020/030; QuickDraw's 3-byte-per-pixel stack temps make odd SPs
   routine in the LC II blit engine) pushed the whole frame one byte
   low; the later RTE then read a garbage format nibble and took a
   spurious FORMAT ERROR → ROM system error → the Lode Runner
   launch-time freeze (cascading bus errors until an odd SSP double-
   faulted). Minimal repro (a scratch file, not kept in the tree): bare
   Moira, odd SSP, one autovector IRQ — frame bytes are now
   `[SR][PC][$0064]` byte-exact.

2. **`execInterrupt<C68020>` passes the RAW vector to
   `writeStackFrame0000/0001`** instead of `4 * queue.ird`: the frame
   writers scale by 4 themselves (`4 * nr` / `nr << 2`), as every
   `execException` call site relies on. The double scaling stacked
   vector offset $190 instead of $64 for autovector 25 — format nibble
   still 0, so RTE never objected, but any handler reading the stacked
   offset saw a wrong vector. (The C68010 direct-write path already
   scaled once; unchanged.)

Gates: 24/24 CTest including sst68000 (C68000 untouched), sst68030
3082 vectors (the fuzz corpus exercises `execException`, whose raw-
vector contract is unchanged; frame bytes at even SPs are identical),
and both boot etalons. Machine-level: Lode Runner launches to its
title screen; the SC2K repro stays crashes=0.

## ATC performance: O(1) pseudo-LRU + last-hit probe (2026-07-17)

`MoiraExecMMU_cpp.h` + `Moira.h`/`Moira.cpp`, motivated by a gprof
profile showing 38% of LC II machine time inside the two 22-entry ATC
scans executed on every translated access:

- `mmuAtcTouch` keeps a counter (`mmuAtcMruCount`) equal to the number
  of set history bits instead of re-scanning all 22 entries for a clear
  bit; the "all used → clear all, keep current" reset is unchanged.
  Every mru transition goes through touch or the reset paths, so the
  counter cannot drift.
- `mmuAtcLookup` probes `mmuAtcLast[fc][rw]` — the line that satisfied
  the previous lookup for that function code and direction — before the
  full scan. The probe performs the identical validity/fc/page checks,
  the identical write-upgrade invalidation (`e.valid = false` on an
  unmodified writable page probed for write), and the identical LRU
  touch, so architectural behaviour (incl. PTEST level-0 searches and
  replacement order) is preserved; a stale line simply falls through.

Both are exactness-preserving optimizations, confirmed by the sst68030
gate (3082 pinned WinUAE-differential vectors incl. the mmu/fault
families) and the boot etalons. Speed: with the V8 bus word fast paths
and -march=native/LTO, the LC II went from 0.40× to 1.91× realtime.

Follow-up (same day): `Moira.h` declares `mmuAtcLookup`, `mmuAtcTouch`
and `mmuTranslateAccess` with `MOIRA_HOT_INLINE`
(`__attribute__((always_inline))` on GCC/Clang, plain `inline`
elsewhere). Callgrind showed GCC keeping them out of line (the 22-entry
fallback scan trips its size heuristics) at ~35 Ir per access of pure
call overhead — ~9% of the whole emulator. All three live in the
Moira.cpp translation unit, so forcing the inline changes no behaviour.

## 68040 integer core + no-FPU F-line (2026-07-18, Q2/Q4)

The 68LC040 (LC 475 / Quadra 605 CPU) now **executes** on the shared
C68020 core, converged by the Phase-3 WinUAE-solo loop (`oracle/fuzz/
fuzz040.py`, SST040 vectors) — 5 400/5 400 across core/random/mmu × off
on two seed sets (101-103 pinned in `tests/data/sst68040`, 777-779
fresh-seed re-verify). Every change is runtime-gated on
`cpuModel >= Model::M68EC040` inside the C68020 blocks — `sst68030`
(3 082) and `sst68000` (1 000 058) are byte-identical.

- `MoiraTypes.h Registers` — the eight 040 MMU registers (`urp040
  srp040 tc040 itt0 itt1 dtt0 dtt1 mmusr040`), distinct from the 030
  set; `Moira.h` accessors apply the WinUAE MOVEC masks (TC & $C000,
  ITT/DTT & $FFFFE364, URP/SRP/MMUSR full 32 bits).
- **MOVEC** (`execMovecRcRx/RxRc`): model-dependent legality (WinUAE
  `movec_illg`): 040 = $000-$007 + $800-$807 minus CAAR ($802); the
  040 rows route to the new registers. `cacrMask()` = $80008000.
- **MOVE16** (5 forms) executes: lines masked `& ~15`, abs.l consumed
  first, 4 long reads then 4 long writes, post-increment after the
  transfer, shared register increments once (WinUAE op_f6xx_31).
- **CINV/CPUSH**: scope 00 = Line-F even in supervisor mode (WinUAE
  cpudefs mask $FF38); otherwise supervisor-checked no-ops (caches not
  modelled architecturally).
- **PFLUSH40/PTEST40**: supervisor-checked; PFLUSH is a no-op until the
  Q3 ATC lands, PTEST does not touch MMUSR040 yet (Q3).
- **Odd instruction-flow targets** — `execAddressError040` (vector 3,
  **format $2**, address = target & ~1, WinUAE `Exception_mmu` nr 3)
  with WinUAE's per-instruction conventions: Bcc/DBcc check the odd
  displacement BEFORE the condition (and DBcc before the Dn decrement);
  BSR checks before pushing (A7 untouched); RTS re-pushes (A7 -= 4);
  RTD/RTR restore A7 fully (RTR keeps the popped CCR); RTE keeps A7
  popped with the new SR applied; JMP stacks instruction + 2 (indexed
  EAs: the running pc + 2); JSR stays uncheck (fault at the next fetch).
- **RTE formats**: 040 accepts $0/$1/$2/$3 (+12)/$4 (+16)/$7 (+60);
  $A/$B are now 020/030-only (format error on the 040). The $7 pop
  replays WinUAE `m68k_do_rte_mmu040`: SSW.CT copies the frame's raw
  SR/PC/+8-long to the popped position; SSW.CM (MOVEM restart) is
  machine-level and deferred to Q3.
- **68040 trace machinery** (`trace040Pending`/`tracePc040`,
  Moira.cpp): an SR write whose OLD Tx bits were set traces once after
  the instruction even if the write cleared them (WinUAE MakeFromSR_x
  one-shot; covers RTE throwaway chains); a staged TRACE_EXC on the 040
  no longer preempts — the next instruction runs first and vector 9
  follows (WinUAE SPCFLAG_DOTRACE order); the trace frame is **format
  $2** with address = WinUAE's `trace_pc`. Any exception cancels the
  pending trace (`exception_check_trace`, 040 row).
- **Undefined CCR, 040 rows** (WinUAE newcpu_common.c helpers):
  DIVS/DIVU/DIVSL/DIVUL overflow = V=1 C=0 N/Z untouched; divide-by-
  zero = C=0 only; CHK = C computed from the out-of-range shape,
  N = value<0, Z/V/X untouched (trap or no trap); ABCD/SBCD leave N and
  V untouched.
- **Q4 — F2xx with no FPU** (68LC040/68EC040): the FPU window is
  registered for the FPU-less models too; each handler behind
  `!hasFPU()` consumes the shape's words and takes **vector 11 with the
  format $4 frame** {SR, pc-after-consumed-words, $402C, EA,
  instruction PC} (`execFpuDisabled040`, WinUAE `fault_if_no_fpu`).
  EA conventions mirror fpp.c call sites: 0 for FGen register forms/
  immediates/FBcc/FTRAPcc, `(ext<<16)|disp` for FDBcc, the computed
  address for FScc (with the -(An) byte adjust), FSAVE/FRESTORE and
  FGen/FMOVEM memory forms; FMOVEM with Dn/An/#imm stays Line-F
  (get_fp_ad failure). FBcc pseudo-conditions $20-$3F are registered on
  the 040 family only (format $4 without FPU, Line-F with).
- **Q8 — Mac PACK 4 vs format $4:** `fpuDisabledSaneFline`
  (`setFpuDisabledSaneFline`) rewinds to the opcode and stacks classic
  format $0 Line-F so guest PACK 4 glue accepts the frame. Architectural
  default remains format $4 (`sst68040`).
  **Dormant since 2026-07-21:** nothing in `src/` or `tests/` calls the
  setter any more, so every 040 profile takes the architectural format $4.
  The bare-FPU boot it was written for was solved elsewhere — the XPRAM
  `$AE` ROM-resource combo makes `InitResources` fall back to
  `UniversalInfo` `defaultRSRCs` 4 and bind the **integer** PACK 4, so
  `POM68K_Q605_NOFPU=2` (true `FPUModel::NONE`) reaches the Finder with no
  F-line rewriting at all (`q605_barefpu_boot_etalon`, CHANGELOG
  2026-07-21 "Bare no-FPU solved"). Kept because it is one branch on a cold
  path and it is the only lever if an FPSP-less guest ever needs it again;
  delete it if a second release passes with no caller.
- **Q8 — 040 I/D ATC:** 32-entry separate I/D ATC overlay on
  `mmu040Translate` (`Moira.h` `MMU040_ATC_ENTRIES`, pseudo-LRU, flush
  PFLUSH*/TC/URP/SRP; `POM68K_MMU040_WALK` — set to anything, the
  wrappers test only its presence — disables it via `setMmu040AtcArmed(false)`
  and restores
  walk-per-access for oracle comparison). U/M/WP semantics preserved vs
  walk-per-access. Page size follows TC.P (4K/8K).
  `POM68K_Q605_CACHE_BOOST` defaulted to 1 here ("boost 2+ fails SCSI
  bring-up"); **superseded 2026-07-25** — that pin was a stale symptom.
  Re-measured, the whole 040 family is green at boost 4 and all **nine**
  wrappers that carry the overlay (`Cpu040`, `CentrisCpu`, `Q630Cpu`,
  `Q700Cpu`, `SonoraCpu`, `VaspCpu`, `RbvCpu`, `Cpu030`, `MscCpu`) now
  default to `cacheBoost_ = 4` (CHANGELOG "The Quadra's
  boost-1 pin was stale"). The two unit tests that were measuring wait
  states on the boosted clock read `machineClock()` now, so machine-cycle
  stall / VIA sync / SWIM C15M are boost-invariant.

Oracle-glue fixes found by this loop (see `oracle/uae/VENDOR.md`):
stale `regs.t1/t0` at `oracle_set_state` armed WinUAE's one-shot
DOTRACE on a plain state load (phantom vector-9 corpus vectors carrying
the previous vector's `trace_pc`), and the `mmu040_movem` restart latch
leaked a faulted MOVEM's saved EA into the next vector's MOVEM.

## 68040 MMU bus translation (2026-07-18, Q3)

Bus-level 040 translation runs on every access for `cpuModel >=
M68EC040`, modelled on WinUAE `cpummu.c` (the oracle's mmu040 build) and
converged by the Phase-3 loop over the full 4×3 family×mmu grid —
**7 200/7 200 pinned** (`tests/data/sst68040`, 11 cells) plus 6 400/6 400
on fresh seeds 301-308; sst68030 (3 082), the Q2 corpora, and sst68000
are unregressed. New section at the end of `MoiraExecMMU_cpp.h`:

- **Translation core** (`mmu040Translate`/`mmu040Walk`/`mmu040MatchTTR`):
  ITT/DTT pair match first (S-field decode, WP faults writes even with
  TC.E off), then the URP/SRP 3-level walk (WinUAE `mmu_fill_atc`:
  supervisor-fc physical descriptor fetches, U maintenance on the upper
  levels, one indirection, U+M on the page — U only when the write will
  fault — WP accumulated over all levels). No architectural ATC is
  modelled *in this slice*: the oracle flushes its ATC on every state
  load, so walk-per-access is observably identical (idempotent U/M
  rewrites); PFLUSH stays a no-op, PTEST reports the walk (or TTR hit)
  in MMUSR040. **Superseded by Q8** (§ *68040 integer core*, bullet "Q8 —
  040 I/D ATC"): the 32-entry I/D ATC is real, PFLUSH* flushes it, and
  `POM68K_MMU040_WALK` restores this walk-per-access behaviour for
  oracle comparison. The walk itself is unchanged — including its
  4K/8K TC.P branch.
- **Access model** (`mmu040Read/Write`): page-boundary splitting exactly
  like the WinUAE accessors (word straddling = byte+byte; long = word
  halves when even, four bytes when odd; each part translated with the
  ORIGINAL size in the SSW); later parts fault with FA = the base
  address and SSW.MA (`misalignednotfirst`); split write faults report
  the FULL value in WB3D. Locked RMW (TAS/CAS) translates DATA accesses
  as writes from the read on and faults with SSW.LK, RW stripped; MOVES
  translates under SFC/DFC with the `ismoves` SSW fc mangling.
- **No prefetch queue** (mode-5 pattern, shared with the 030):
  `mmu040InstrStart` fetches opcode + irc through translation at every
  instruction start; `readExt` refetches at consumption; queue refills
  and jump-target fetches are suppressed (a tail refill would fault
  pages WinUAE only reads at the next step). Same one-word-lookahead
  known limit as the 030.
- **Format $7 frame + the last-write dichotomy** (`execMmu040BusError`,
  gencpu `gen_set_fault_pc`, oracle-probed): a fault on the
  instruction's LAST write stacks PC = NEXT instruction with no restore
  — the CCR keeps the just-computed flags and (An)± keeps its
  adjustment (gencpu adjusts before the final store and disarms the
  fixup); every other fault restarts — CCR restored to the
  pre-instruction snapshot, (An)± fixups undone (`cpu_restore_fixup`;
  fixups stay armed across the whole instruction — a CMPM second-read
  fault un-does the first (An)+), stacked PC = the instruction. Marking
  is default-on in `mmu040Write` (data space, MOVEM latch excluded)
  with per-site pre-arms where a word is consumed after the store
  (MOVE to ABS.L). Frame fields per `Exception_build_stack_frame` case
  0x7: EA (`mmu_effective_addr` — only MOVEM/MOVE16 faults set it),
  SSW, WB status/data, FA twice, MOVE16 line buffer as PD0-3.
- **Instruction reorderings for the gencpu order** (all 040-gated):
  readOp (An)+ increments AFTER the read; writeOp/execMove4 (An)±
  adjust BEFORE the write; execClr is a pure store (no destination
  read); the D6-remainder user-mode cpSAVE/cpRESTORE privilege rule is
  now 020/030-only (040 `op_illg` = straight Line-F); FGen with an An
  EA on a FPU-less 040 = format $4, not Line-F.
- **MOVEM restart latch** (`mmu040MovemArmed/Ea`, WinUAE
  `mmu040_movem`): armed with the start EA for every MOVEM, resumed
  from the saved EA when re-armed (fault → SSW.CM + EA = saved EA →
  handler RTE with SSW.CT re-arms), -(An) stores base-in-list as
  initial − S with An written only at the end, no 68000 guard reads.
- **Third oracle-glue leak fixed** (`oracle/uae/VENDOR.md`): WinUAE only
  writes `regs.mmu_effective_addr` on MOVEM/MOVE16 faults and keeps
  `mmu040_move16[]` across vectors — ordinary faults stacked the
  PREVIOUS vector's values in the frame's EA and PD0-3 fields;
  `oracle_set_state` zeroes them (with `wb2_address`/`wb3_data`).

## External /BERR on the 68040: `extBusError040()` (Q5)

The 040 twin of § *External /BERR + RTE $A*, needed for the same reason on the
Quadra/Centris/Q630 boards: the ROM probes an address map by faulting on
unmapped I/O, and translation alone cannot see that coming.

- `Moira.h` (public, `[[noreturn]]`) — called from *inside* a `read8/16` /
  `write8/16` bus callback when the machine asserts /BERR.
- `MoiraExecMMU_cpp.h extBusError040` — replays the captured in-flight access
  (`mmu040AccAddr/Val/Sz/Write/Data`, stamped by `mmu040Read`/`mmu040Write`
  before every physical access) into `mmu040Fault`, so the frame is identical
  to a translation fault at the same point **except** that SSW.ATC is clear
  (WinUAE `mmu_hardware_bus_error`, `nonmmu = true`). MOVES supplies the
  privilege from `mmu040Moves` rather than `sr.s`.

The access stamp is what the JIT seam's `pomJitStampAccess` has to reproduce
(§ *JIT seam*, point 5): a fetch served from the window performs no
`mmu040Read`, so nothing else would leave the context this function reads back.

## Watchpoints under the MMU: logical-address hooks (2026-07-21, Q8.2)

`readM`/`writeM` host the debugger watchpoint checks, but the 030 and
040 translated bus paths branch away before reaching them
(`mmuRead`/`mmuWrite` in `MoiraExecMMU_cpp.h`, and
`mmu040Read`/`mmu040Write`), so `debugger.watchpoints` never fired on
those models. Each of the four entry points now runs the same
`CHECK_WP` → `watchpointMatches(addr & addrMask, S)` →
`didReachWatchpoint` sequence on the **logical** address before
translation. Found (and used) while tracing who binds `_FP68K` ($15AC)
during the Quadra bare no-FPU boot; debug-only, `flags & CHECK_WP` is
clear unless a watchpoint is armed.

## External /BERR on the plain 68020 core (2026-07-24, Phase C — Mac LC)

Three linked fixes let a device raise `extBusError()` correctly on the
**plain 68020/EC020 core** (previously "M68030-only" by `assert` — the
Mac II shim soft-failed around it, and the Mac LC died in a DS-1 Sad
Mac the first time its ROM's 32-bit probe faulted):

1. **`execMmuBusError` refills the prefetch queue** for non-mode-5
   models (`MoiraExceptions_cpp.h`). The 030/040 loops refetch the
   opcode at the loop head, but the plain-queue models kept executing
   the STALE `queue.ird` — the faulted instruction re-ran at the
   handler PC and re-faulted until HALT (the LC ROM's AddrMapFlags
   MOVEM probe from `$50FC0000`). Same `fullPrefetch<C, POLL>` as
   `execAddressError040`; a nested fault propagates to the caller's
   catch → `halt()` = true double fault.
2. **The plain `read<>`/`write<>` paths record the in-flight
   sub-access** (`MoiraDataflow_cpp.h`, `if constexpr (C == C68020)`
   after the 030/040 branches): `mmuAccAddr/Ssw/Fc/Write` per size,
   per sub-word on Longs. `extBusError()` drops its assert and stacks
   a $B frame with the TRUE fault address — the LC ROM's probe catcher
   compares that field before resuming; a stale address made it
   forward every expected fault to SysError (DS 1).
3. **`execMmuBusError` guards its mode-5 state restores** — on the
   plain core `mmuCcrSave`/`mmuFixup*`/`mmuState`/`mmuOpcodeV` are
   stale; restoring them corrupted live CCR/An. Plain core: frame $B
   at `pc0`, no fixups, no CCR restore.

Gates: `lc_boot_etalon` (68020 + V8), `classic2_boot_etalon`; the SST
68000 corpus and the 030/040 fuzzer pins are unaffected (68000/010
paths untouched; 030/040 take the mode-5 branches before the new
code).

## JIT seam (2026-07-27, J0/J1)

POM68K's **second execution engine** lives outside this vendored core, in
`src/jit/` (design: `src/jit/POM68K_JIT.md`, which owns everything above the
seam — backends, block discovery, invalidation policy, measurements). It
drives a `moira::Moira` object and never replaces it.
Five additions in this first slice, all marked `POM68K JIT`, all inert until a
`jit::Engine` arms them.

> **Default engine, 2026-08-12:** the seam was off everywhere when this was
> written. It no longer is — `jit::defaultEngine` (`src/jit/JitConfig.h`)
> takes the per-family policy, which is **JIT on the 68040** and interpreter
> on every other family; `POM68K_CPU_ENGINE=interp|jit` overrides it. Read
> "off by default" below as "off unless the family policy or the env knob
> arms it"; the seam itself is still inert code until an engine arms it.

> **Read the four sub-sections below as one patch group.** J0/J1 was 68040-
> only; J2 (2026-07-28) added the code-generator surface *and* extended the
> seam to the 68030 and the plain 68020; J3/J3b (2026-07-28) added the
> data path and the ATC-eviction contract that makes every derived cache
> exact (those three in commit `b2c4e19`; the 2026-07-30 `movemArmed` addition
> came with the compiled MOVEM); J4 (2026-08-10) widened the data TLB and
> added the timing probe.
> `grep -rn "POM68K JIT\|POM68K J3" extern/moira/Moira/` is
> the authoritative site list: `Moira.h`, `Moira.cpp`, `MoiraExecMMU_cpp.h`,
> `MoiraDataflow_cpp.h`.

**Why public rather than protected.** `jit::Engine` holds a `moira::Moira&`,
not a machine wrapper, so one engine serves every machine profile that carries
one — **twelve** CPU wrappers today: `Cpu040`, `CentrisCpu`, `Q630Cpu`,
`Q700Cpu` (68040), `Cpu030`, `RbvCpu`, `SonoraCpu`, `VaspCpu`, `MscCpu`
(68030, plus the Macintosh LC's 68020 flavour of `Cpu030`), and since
2026-08-06 `Cpu020` (Mac II family), `Cpu68k` (the compacts) and `IIfxCpu`
(the IIfx). That is every `src/*Cpu*.h` in the tree.
A public non-virtual member is the only shape that stays a **direct** call
from another translation
unit — and this file already records what an indirect call on the
per-instruction path costs (§ *willFetchInstr … Folded inline*: ~11% of the
whole emulator). No `friend` was added, and the instruction handler table
`exec[]` stays private: the engine has no business calling a handler itself,
because that would skip `POLL_IPL` and the per-instruction MMU resets.

1. **`Moira.h` — `PomJitWindow pomJitWindow` + `u32 pomJitMmuGen`.**
   An instruction-fetch window: while armed, opcode, lookahead and
   extension-word fetches inside `[base, base+len)` are served from a host
   pointer instead of walking the ATC and re-entering the machine's memory
   map. (68040-only in this slice; J2 extended it to the 030 and the plain
   020 — point 3.) Same shape as `PomIcache` — a plain struct, checked inline, one
   predictable branch when disarmed. `pomJitMmuGen` is bumped — through
   `pomJitMapMoved()`, see point 7 — at **fourteen** sites, which is the
   whole set of things that can move a logical→physical mapping: the four
   030 ATC flushes (`mmuAtcFlushAll/FlushFc/FlushPage/FlushPageFc`), the
   three 040 ones (`mmu040AtcFlushAll/FlushNonGlobal/FlushPage`), the four
   TTR setters (`setITT0/ITT1/DTT0/DTT1`) and — since 2026-08-06 — the
   three **030 `PMOVE` register writes** (`TC`, `SRP`, `CRP` in
   `execPMove`); `setTC040`/`setURP040`/`setSRP040` reach it through
   `mmu040AtcFlushAll`, and `pomFlushAtcs()` (§ *Save-state seam*) through
   both flush-alls. A window whose generation no longer matches is refused,
   which is the one staleness an address-range test cannot see.

   *The three `PMOVE` sites were missing until 2026-08-06.* `TC.E` is the
   switch between "translation off, logical == physical" and a page-table
   walk, and `SRP`/`CRP` are the roots the walk starts from — but none of
   them touches the ATC, so none of them reached a bump. Every 030 machine's
   window and block cache could therefore survive a change of translation
   root. Masked in practice because Apple's ROMs issue a `PFLUSHA` right
   after, which does bump; found while wiring the Mac II family, whose
   `MacIIMemory::physAddr` switches remap mode on `TC` bit 31 and so made
   the gap reachable without a flush.

   Note the window points **into the guest RAM/ROM buffer**, so guest writes
   are visible to it immediately: self-modifying code needs no invalidation
   here. What can go stale is the *translation*, hence `super` + `gen`.

2. **`Moira.cpp` — `bool pomJitExecOne()`.** The MMU cores' fast path of
   `execute()`, factored out so there is exactly **one** copy of it:
   `execute()` calls it for `cpuModel == M68030 || cpuModel >= M68EC040`
   (same TU, still inlined) and the block replayer calls the same function.
   A hand-copied twin in `src/jit/` would have rotted against the
   instruction-start contract — `POLL_IPL`, the eight per-instruction MMU
   resets, `reg.pc += 2` before the handler, `processException`, the staged
   `trace040Pending`. Returns false whenever the instruction did not retire
   normally, which the engine always reads as "go back through `execute()`".

   *2026-07-28, three branches now:* `M68030` runs `mmuExecuteStart` as its
   loop head (the 030's mode-5 equivalent of `mmu040InstrStart`), `>=
   M68EC040` runs `mmu040InstrStart`, and the **pre-030 models** have no
   per-instruction MMU loop head at all — the queue was refilled by the
   previous instruction's prefetch, where its `POLL_IPL` lives, so the
   boundary contract is just the dispatch. That third branch is reached only
   from the engine (`execute()` keeps its own generic fast path); the six
   duplicated lines are deliberate, because making `execute()` delegate
   would put a model test in the hottest loop in the emulator for no
   behavioural gain.

   *2026-08-06:* the same third branch now also carries the **cycle-exact
   68000/68010**, and it needed no change to do so — it is byte-for-byte
   `execute()`'s generic fast path, and `exec[]` is already the per-model
   table. What makes it correct for a cycle-exact core is not here but in
   the fetch (point 3, `pomJitFetch000`), which charges the cycles `read<>`
   would have. The earlier wording claimed routing these models through here
   "would be wrong"; that was never demonstrated and is not true of this
   branch.

3. **`MoiraExecMMU_cpp.h` + `MoiraDataflow_cpp.h` — the window fast path**
   at every instruction-fetch site, per core:
   - **68040**, two sites: `mmu040InstrStart` (`ird` at `pc` and `irc` at
     `pc+2` — **both or neither**, because serving only `ird` would change
     which page the lookahead touches for an instruction on a page's last
     word, and with it the fault frame) and the 040 branch of `readExt`
     (every extension word past the lookahead — where multi-word
     instructions actually spend their fetch budget).
   - **68030**, one site: `mmuFetchWord`, which the mode-5 loop funnels
     `ird`, `irc` and every extension word through. Placed **after** the
     `PomIcache` overlay (its miss penalty is cycle-visible and must keep
     charging) and after the `mmuAccAddr` stamp that `extBusError()` reads
     back.
   - **plain 68020**, three sites in `MoiraDataflow_cpp.h` (`prefetch`,
     `fullPrefetch`, `readExt`) through `pomJitFetch020`, which replaces
     only the *tail* of a `read<PROG,Word>`: the head is behavioural state
     that must be replicated, not skipped — the `POLL_IPL` riding on the
     prefetch, the FC pins, and the in-flight access stamp an external
     /BERR reads back (§ *External /BERR on the plain 68020 core*).
   - **68000/68010** (2026-08-06), the same three sites through
     `pomJitFetch000` — and the one flavour that replaces *neither* head
     nor tail but only the **bus read**. On `Core::C68020` the `SYNC(x)`
     macro expands to nothing, which is why the three flavours above cost
     no cycle accounting; on `Core::C68000` it really calls `sync(x)`, and
     the compacts are the one POM68K family whose timing claim is
     cycle-exact. So this one reproduces `read<>` step for step in
     `read<>`'s own order — leading `SYNC(2)`, address-error bail-out (it
     returns false and charges *nothing*, letting the ordinary path both
     charge and throw), FC pins, `POLL_IPL`, the machine's bus model,
     trailing `SYNC(2)` — and skips only the `read16()` virtual and the
     machine's address decode.

   All of them sit **after** `POLL_IPL` and after the per-instruction MMU
   resets, never before.

3b. **`Moira.h` — `pomJitSetBusStall(fn, ctx)`** (2026-08-06). The Mac Plus
   charges video/RAM contention wait states from *inside* its `read16()`
   (`Cpu68k::applyContention`), so a windowed fetch — which performs no
   `read16()` — would silently stop paying them and run the JIT on a
   different clock than the interpreter. This hands the wrapper's charge
   back to `pomJitFetch000`, at exactly the point `read16()` would have
   applied it. A plain function pointer, not a virtual, for the reason this
   file already documents twice: it sits on the per-fetch path.
   Null by default, which is every non-compact board.

4. **`MoiraExecMMU_cpp.h` — `bool pomJitProbeCode(...) const`.** A
   side-effect-free translation probe: TTR match, MMU-disabled identity,
   then a read-only scan of the instruction ATC. It deliberately does *not*
   call `mmu040Translate`, because a walk writes the descriptor U/M bits
   back into guest RAM through `mmuWrite32` and faults by throwing — neither
   of which may happen between instructions. A miss simply returns false;
   the interpreter then fetches normally, fills the ATC, and the next probe
   succeeds.

   Four branches: 040 (above), 030 (ATC scan), plain 020 (identity), and
   **68000/68010** (2026-08-06, identity **with a refusal**). These cores
   drive a 24-bit bus and `read<>` masks every access with `addrMask<C>()`,
   while the window is keyed on the *unmasked* pc — so a pc above `$FFFFFF`
   would name one byte to the interpreter and another to the window. It is
   refused rather than masked. Real Mac code never leaves the low 24 bits,
   so nothing that was going to be hot is lost.

5. **`Moira.h` — `pomJitStampAccess(u32)`.** The window fast path leaves
   behind exactly the in-flight access context (`mmu040AccAddr/Val/Sz/Write/
   Data/Base/First/Split`) that the `mmu040Read` calls it replaced would have
   left. Only `extBusError040()` reads those back, and only `MOVE16` inherits
   them rather than setting its own — but without the stamp an external bus
   error during a `MOVE16` would stack a differently-shaped frame under the
   JIT than under the interpreter, which invariant 1 does not allow.

### J2 additions (2026-07-28) — what a CODE GENERATOR needs on top

The x86-64 backend (`src/jit/backends/JitBackendX64.cpp`) emits host machine
code that touches this object directly. Five more additions, same rules: all
marked `POM68K JIT`, all inert until armed.

6. **`Moira.h` — `PomJitDtlb pomJitDtlbR/W` + `pomJitDtlbFlush()`.** A
   256-entry direct-mapped data-translation cache (`PomJitDtlb::kEntries`,
   16 bytes/entry = 4 KB per table, two tables), the data-side twin of the
   fetch window. Generated code cannot call `mmu040Read`: that path throws
   on a fault, and a C++ exception may not cross a JIT frame — there is no
   unwind information for bytes we emitted ourselves. So a data address is
   translated *inline*, by tag compare, and a miss bails out of the compiled
   block at an instruction boundary with nothing committed. Entries are 16
   bytes so one `lea` reaches them. Read and write are separate caches
   because a page can be readable and write-protected, and because a write
   to an unmodified page has to re-walk so the table search sets M.

7. **`Moira.h` — `pomJitMapMoved()`.** Every site that bumps
   `pomJitMmuGen` now goes through this instead, because it must also empty
   the data TLB. The generation counter alone suffices for the code window,
   which re-checks it on every fetch; the data TLB is read by generated code
   that checks nothing but the tag, so it has to be emptied at the source.

8. **`MoiraExecMMU_cpp.h` — `bool pomJitProbeData(...) const`.** The data
   twin of `pomJitProbeCode`, held to the same three rules (no guest memory
   touched, no throw, no CPU state disturbed) plus the two a write adds: the
   page must not be write-protected, and it must already be marked modified,
   because a write to an unmodified page owes the descriptor an M-bit
   write-back and a probe may not perform a guest store.

9. **`MoiraExecMMU_cpp.h` — `pomJitReadData/pomJitWriteData`, `noexcept`.**
   The other half of that data path: what generated code calls when the TLB
   refuses — an I/O register, an access straddling two pages. They perform
   the REAL access through the same `mmu040Read`/`mmu040Write` the
   interpreter uses, and convert the one thing a JIT frame cannot survive, a
   thrown fault, into `false`. A false answer means nothing was committed on
   the guest side, so the caller leaves the instruction alone and the
   interpreter re-runs it from the boundary and faults identically.
   `pomJitReadProg(addr, u16&)` (2026-08-23) is the program-space twin. On
   the 030 it reproduces `execJsr`'s
   `queue.irc = read<PROG, Word>(ea)` — mode-5 has no prefetch queue, so a
   taken JSR must read its target's first word at run time
   (`setFC(USER_PROG)` + `mmuRead<Word, 0>`, verbatim), never predict. On the
   040, full-index memory-indirect JSR/JMP use the ordinary
   `read<PROG, Word>` funnel so ITT/ATC/cache/fault behaviour stays the
   interpreter's rather than becoming a backend approximation.

10. **`Moira.h` — `pomJitLayout()`, `pomJitSync(int)`, `pomJitSimpleIpl()`.**
    The register file stays private; `pomJitLayout()` hands back the byte
    offsets a code generator needs, measured from a live object rather than
    with `offsetof` on a polymorphic type. `pomJitSync` routes generated
    cycles through the machine's virtual `sync()` — a block that merely
    added to `clock` would run the guest forward with the VIA, the ASC,
    SWIM and the Cuda/Egret MCU frozen behind it. `pomJitSimpleIpl()` lets
    the backend refuse to compile at all if the deferred IPL-recognition
    feature (`setIplDelay`) is ever armed, since generated code models
    `POLL_IPL` as the plain assignment.
    2026-07-30: `PomJitLayout` gained `movemArmed` — the offset of
    `mmu040MovemArmed`, the 68040 MOVEM restart latch. A compiled MOVEM
    tests it and bails to the interpreter while it is set: a restart after
    a fault must resume from the SAVED ea (`mmu040MovemEa`), which a
    recomputation would silently ignore if the fault handler changed the
    base register.
    2026-08-21: `pomJitIcachePeekPenalty(pc, words)` +
    `pomJitBiasClock(i64)` — the peripheral-phase alignment pair
    (`docs/JIT_BRINGUP.md` § C.4nonies). The peek is the emitted i-cache
    charge's walk, read-only (one-line local override, no mutation); the
    bias is a bare clock adjustment, deliberately NOT `sync()`. The x64
    access thunks bracket a device access with them so the peripheral
    flush the access forces sees the clock the interpreter's same access
    would — `mmuFetchWord` charges the fetch penalty before exec, emitted
    code after the body, and the difference is exactly where the VBL pin
    slipped one delivery window in the 120k reproducers.

### J3 (2026-07-28) — the interpreter reads the data TLB too

11. **`Moira.h` — `pomJitData<N,W>()` + `pomJitDataR1/W1` +
    `Moira.cpp pomJitDataSlow()` + the fill callback
    `pomJitDtlbFillFn/Ctx`.** The same two tables of point 6, now consulted
    by the **interpreter**: `mmu040Read` and `mmu040Write` try them before
    the ATC/translate/split chain. Three levels, because the shape is
    measured, not aesthetic — level 0 is **one entry per direction**
    (`pomJitDataR1/W1`) checked inline in the hot part of the object; levels
    1 (the 256-entry table) and 2 (the engine's fill callback) both live
    behind `pomJitDataSlow`, deliberately out of line, because level 0 is
    inlined into every `mmu040Read/Write` instantiation and the
    interpreter's hot loop pays for those bytes hit or miss. Consulting the
    big tables directly from the interpreter measured **~10 % slower**:
    streaming guest data evicted their own cache lines, adding an L1 miss to
    an access the long path served from hot machine state.

    `pomJitData` refuses up front for correctness, not speed: an armed
    watchpoint (`CHECK_WP` must see every access), MOVES' alternate FC
    space (`mmu040Moves`), a locked-RMW read (`mmu040Lrmw` — it translates
    as a write), and any access straddling a 4 KB boundary. The write side
    replicates the last-write marker (`mmu040LastWrite/Pc`) bit for bit,
    because the format $7 frame stacks that dichotomy and a fast path that
    skipped it would change which PC a two-write instruction's second fault
    reports.

    `pomJitDtlbFillFn` is null unless a `jit::Engine` binds it, so the whole
    path is one dead branch by default — and the engine binds it **only**
    under `POM68K_DATA_WINDOW=1` (`JitEngine.cpp`). It is opt-in because
    J3b made it exact and exactness is what killed it: a TLB entry may not
    outlive the ATC entry it derives from, so its coverage is capped at the
    040 ATC's 32 pages, and under Mac OS VM the eviction/refill churn costs
    more than the remaining hits save. The x86-64 backend keeps its inline
    use of the same tables (same cap, but there it replaces a C++ call
    chain, not a hot MRU probe).

### J3b (2026-07-28) — derived state dies with the ATC entry it derives from

12. **`Moira.h` — `pomJitAtcEvict(logicalPage, pageLen, code)`**, called from
    every ATC eviction site on **both** cores: the write-M invalidation in
    `mmuAtcLookup` (030, last-hit probe *and* full scan) and
    `mmu040AtcLookup`, plus the replacement in `mmuAtcFill` /
    `mmu040AtcFill`. **This is the exactness contract for the whole seam**,
    and it is worth being explicit about why, because it was invisible for
    weeks.

    A window or TLB hit skips a walk — which is exactly what an ATC hit
    does, so it is bit-exact *while the backing ATC entry is resident*. Once
    that entry is evicted, the interpreter's next access re-walks and
    re-sets the descriptor's **U bit** in guest RAM. A derived entry that
    outlived the eviction would skip that guest-visible write. Harmless
    until Mac OS VM started *reading* the U bits for page aging (it does,
    once the System is up), at which point each engine's different survival
    pattern became a different guest **execution**: three engines, three
    futures, all "correct" and none comparable.

    The eviction is **per space**: `code` says whether the evicted entry
    backed the fetch window (instruction-space residency) or the data TLB
    (data-space residency). Killing both on either eviction — the first cut
    — threw the code window away every time an unrelated data page sharing
    its logical page churned.

**Known divergence, by design:** serving a fetch from the window skips the
instruction ATC's pseudo-LRU update for that access, so ATC *replacement
order* still differs from a pure-interpreter run — but with J3b a hit can
only happen while the interpreter's own ATC would also have hit, so no
guest-visible descriptor write is ever skipped. Architecturally invisible
(the same class as the `PomIcache` timing overlay) — which is what lets the
JIT be the conformant default on the 68040 rather than an opt-in mode.
On every core that runs through the seam `SYNC(x)`
expands to nothing (`MoiraMacros.h:19`: `if constexpr (C != Core::C68020)`,
and 020/030/040 all *are* `Core::C68020`), so the window changes **no** cycle
accounting at all.

### J4 (2026-08-10) — the data TLB stops refusing whole code-bearing pages

13. **`Moira.h` — `PomJitDtlbEntry::codeMask` + `PomJitDtlb::kSliceShift`.**
    One bit per 256-byte slice of the entry's 4 KB page, set when some
    compiled block was translated out of that slice. **Write table only**,
    zero everywhere else.

    Before it, a write entry was refused outright whenever the page held any
    translated code at all — an entry maps 4 KB while `jit::CodeGuard` works
    at 256 bytes, a 16× mismatch, and 68k code shares pages with its stack
    constantly. That single refusal was **95.6 % of every remembered
    data-TLB refusal on an idle Finder** (63 998 of 66 922); lifting it
    *unsafely* was worth −9.8 % of wall clock. The mask buys the same win
    safely: a store into a marked slice must go through the memory map,
    where the guard can see it, and the branch is never taken on a page with
    no code. `kSliceShift` mirrors `jit::CodeGuard::kShift`; the
    `static_assert` lives in `JitEngine.cpp`, the one place that sees both.

    It cannot go stale: `jit::Engine::markPages()` flushes both tables when a
    slice gains its first block, and the 1 → 0 direction only ever leaves the
    mask *conservative*. The interpreter's own level-0 page
    (`pomJitDataPage`) carries a tag and a host and nothing else, so
    `pomJitDataSlow` deliberately keeps the OLD coarse rule (`if (w &&
    e.codeMask) return nullptr;`): the mask exists for **generated** code,
    and the interpreter's use of these tables is opt-in
    (`POM68K_DATA_WINDOW`) and measured a net loss, so widening level 0 would
    be paying for a path nobody runs.

14. **`Moira.h` — `PomJitTiming` + `pomJitBeginTiming/EndTiming` +
    `pomJitTimingProbe`.** Per-instruction cycle attribution for the JIT
    tracer, split three ways: `baseCycles` (handler, data bus included),
    `icacheCycles` (the `PomIcache` overlay's contribution, excluded from
    the base) and `postExceptionCycles` (exception/trace work after the
    handler stopped). Read by `JitEngine.cpp:717-719` when it decides what a
    compiled block owes `pomJitSync`.

    Opt-in on purpose, and only on the **68030** branch of `pomJitExecOne`
    (`Moira.cpp:328`): ordinary interpretation pays one predictable
    `[[unlikely]]` false branch and nothing else. The 030 is the family
    where the i-cache overlay makes "how many cycles did that instruction
    cost" a question the clock delta alone cannot answer.

**Gate for the whole seam:** `jit_lockstep_test` runs two machines from one
ROM, one interpreted and one JIT-driven, and compares D0-D7/A0-A7/PC/SR/USP/
ISP/MSP, the clock and the low 2 KB of guest RAM at every instruction boundary
— and fails if the JIT never actually replayed a block. That binary carries
**five** CTest registrations (`jit_lockstep_test`, `_blocks_`, `_x64_`,
`_x64_fine_`, `_noaccess_`) varying backend, block cache and comparison
granularity, plus `jit_lockstep_a64_coarse_test` when configuring on AArch64
with `POM68K_JIT_BACKENDS=auto`. Two sibling harnesses cover the other seam
flavours the same way: `jit_lockstep_030_test` (+ `_030_blocks_`, and
`_030_a64_experimental_` on AArch64) and `jit_lockstep_68000_test`
(+ `_68000_blocks_`). `ctest -L jit` runs all of them together with the
`jit_*_boot_etalon` re-registrations.

## Save-state seam: `pomFlushAtcs()` (2026-07-29)

`Moira.h`, one protected one-liner beside the 030 ATC declarations:

```cpp
protected:
    void pomFlushAtcs() { mmuAtcFlushAll(); mmu040AtcFlushAll(); }
private:
```

**Why.** Restoring a save state replaces RAM — and therefore the page
tables — underneath a CPU whose ATCs still hold the *previous* machine's
translations. Both have to be dropped or the first post-restore access
translates through a stale entry, which surfaces as rare corruption rather
than a clean failure. `mmuAtcFlushAll()` (030) and `mmu040AtcFlushAll()`
(040) are both **private**, and while `setURP040()`/`setSRP040()`/
`setTC040()` reach the 040 one as a side effect, **nothing public flushes
the 030 ATC**. Rather than serialize the ATCs (they are pure caches,
re-derivable from RAM) or widen the private section, this adds the single
protected entry point `src/MoiraSnapshot.h` needs.

Everything else about save states is on the POM68K side of the seam: the
whole CPU chunk lives in `MoiraSnapshot::visitCpuCommon()` (`src/MoiraSnapshot.h`,
which calls `pomFlushAtcs()` after the reload), and it works only because
Moira's execution state (`clock`, `reg`, `queue`, the IPL history, `flags`,
`fpu`) is already `protected` — the CPU wrappers derive from it.

Free side effect worth knowing: both flush-alls route through
`pomJitMapMoved()` (§ *JIT seam*, points 1 and 7), so a restore also
invalidates the JIT code window and data TLB. Restoring under an armed
engine needs no extra call.

Nothing else in the vendored tree changed for save states.

## 68040 cache-TAG model — historical M1 (2026-08-05)

`POM68K_040_DCACHE` (env, read once in the constructor, default off;
`setPomCache040()` overrides for tests) arms an **architectural tag
model of the two on-chip caches** — 4 KB, 4-way, 64 sets, 16-byte
lines, physically indexed/tagged (M68040UM § 4). **Tags only: every
access is still served by the bus**, so the model can observe but
never interfere; that is the whole point of the milestone.

- `MoiraCache040.h` (new) — the self-contained `Cache040` store:
  per-longword dirty bits (UM Fig. 4-4), invalid-ways-first + 2-bit
  counter replacement, CINV (discard) vs CPUSH (push-count +
  invalidate) scopes, MOVE16 no-allocate / write-hit-invalidate.
- `mmu040Translate` touches the model on **all four** of its return
  paths (TTR match, MMU disabled, ATC hit, walk); the
  CM mode comes from the matched TTR (`mmu040MatchTTR` gained an
  optional `u32 *cm` out-param — all existing callers unchanged), the
  descriptor status (bits 6-5, as the M0 probe read), or the MMU-off
  default (cachable/writethrough, UM § 3.5.1). CACR DE/IE gate lookup
  and allocation at touch time (UM § 4.4 — a disabled cache keeps its
  contents).
- `execCinv`/`execCpush` (previously supervisor-checked no-ops, § Q2)
  call `pomCacheOp040` when armed: cache field bits 7-6, push bit 5,
  scope bits 4-3, An bits 2-0. Line/page operands resolve via
  `pomCache040Phys` — TTR, then a side-effect-free ATC scan, then
  `mmu040PeekWalk`, a **read-only** twin of `mmu040Walk` (no U/M
  descriptor write-back, no fault): flag ON must not disturb guest
  state flag OFF would not have touched. Unmapped operand = skip.
- `pomFlushAtcs()` (save-state seam above) also invalidates both tag
  stores — caches are flushed on restore, never serialized.

Hardened the same day by an adversarial bughunt (three fixes, detail
in `CHANGELOG.md` § 2026-08-05 (later)): the touch's span walk runs in
u64 with an exclusive end (u32 `a <= hi` was a tautology — an infinite
loop — for accesses ending at PA $FFFFFFFF); `pomCache040Phys` catches
`MmuBusError` from the peek walk, so a garbage-but-resident descriptor
chain landing in unmapped space is "unmapped, skip" instead of a
guest-visible format $7 with stale fault context; and `extBusError040`
rolls back the last stamped touch span (`pomCacheLastPa/Bytes/Data`),
because the touch runs at translate time and a TEA-terminated fill
must leave no valid line (UM § 7) — the stamp is cleared by
non-allocating touches and by the peek walk so the rollback can never
hit an older span.

Known M1 approximations (documented in `docs/CACHE_040.md` § M1, both
M2 work): split sub-accesses reuse the full access's SSW size so a
page-crossing misaligned write can over-mark one longword dirty; the
JIT's inline DTLB bypasses `mmu040Translate`, so tag state under the
JIT is approximate. Gated by `tests/cache040_test.cpp` (44 checks,
incl. MMU-on resolver paths against real page tables and a /BERR
descriptor chain); `sst68040` and the JIT lockstep suite run green
with the flag armed.

## 68040 cache contents, copyback, snooping and timing — M2/M3 (2026-08-16)

The M1-only boundary above is superseded. `Cache040::Line` carries all 16
bytes; translation records the CM/CACR/MOVE16 attributes and the access site
performs a requested-longword-first fill, WT/CB/NC policy and failed-fill
rollback. Dirty victims and CPUSH write physical longwords back before
invalidation. `pomSnoop040Read/Write` cover dirty supply, invalidate and
write-sink/MI, including disabled-cache and cross-line hits. Hit, four-beat
fill and dirty-longword-push costs are separate runtime values.

The JIT page-to-host-RAM data TLB is refused while this model is active. A native code block
requires its complete embedded range to be resident in I-cache and to match
the host span byte for byte; blocks return through that guard instead of linking directly. Thus
generated execution can neither fetch memory newer than a stale I-cache line
nor bypass dirty D-cache data. `POM68K_040_DCACHE=1` enables the model; the
product default remains cacheless until this guarded JIT regains its
real-ROM speed budget. `cache040_test` and the cache-on 040 JIT locksteps are
the gates.

Performance follow-up: cache-active DTLB refusals are remembered as tagged
null entries, sending subsequent native accesses straight to the exact
D-cache thunk instead of repeating a side-effect-free probe. On the Q605
gate this reduced refusal calls from 321,187,389 to 727,456 and wall time
from 60.63 s to 59.19 s, without changing guest state. A last-cache-line memo
measured +4.4 % and was removed. The native physical-line read path then
landed in A64/x64: logical privilege, DATA-ATC generation, live valid bit,
physical tag and line boundary are checked on every hit; stores remain exact.
The permanent cache-on native locksteps require a non-zero hit count (18.6 M
on the measured A64 run). Its paired fixed-budget Q605 result is 61.94 s to
48.78 s (-21.2 %), but cacheless remains 11.03 s and cache-on still misses
the Finder budget, so opt-in policy is unchanged.

## 68010-only exception-frame buffers (2026-08-12, savestate determinism)

`readBuffer`/`writeBuffer` have exactly one architectural consumer:
`writeStackFrame1000`, the 68010 format-$8 bus/address-error frame
(`MoiraExceptions_cpp.h:134/140`, asserted `C == Core::C68010`). Upstream
nevertheless maintains them on **every** core — one store per memory write in
`writeOp`, one per extension-word fetch — because a single dataflow serves
all three cores.

That convenience broke save-state determinism the day the JIT became the
68040 default (2026-08-10): the JIT's contract deliberately excludes the
buffers (they are guest-invisible on every Mac model), so their serialized
value depends on **when the JIT was armed** — a restore calls
`pomJitDisarm()`, the post-restore interpreted warm-up window stamps
`writeBuffer` with live loop data, and `savestate_040_test` § determinism
failed on all five 040 families at CPU-chunk offset 256 (the `writeBuffer`
field; `readBuffer` only matched by accident, tracking the loop-invariant
extension word). Interpreter-forced runs were green — the divergence was the
engine asymmetry, not the container.

The fix removes the asymmetry at its root: two `pom*` hooks in `Moira.h`,

    template <Core C> void pomSetRB(u16 v) { if constexpr (C == Core::C68010) readBuffer = v; }
    template <Core C> void pomSetWB(u16 v) { if constexpr (C == Core::C68010) writeBuffer = v; }

and every producer site (12 in `MoiraDataflow_cpp.h`, 19 in
`MoiraExec_cpp.h`, each marked `// POM68K: 68010-only`) calls the hook
instead of assigning. Two properties matter:

- **C68010 is bit-for-bit unchanged** — the hook performs the identical
  store there, so the 68010 frame keeps its exact contents.
- **Side effects survive on every core.** Several sites read the bus into
  the buffer (`readBuffer = (u16)readM<...>(ea & ~1)` — the address-error
  dummy read). A hook *argument* is always evaluated; only the dead store is
  elided, so bus traffic and cycle accounting are untouched on 68000/020+.

Gate: `savestate_040_test` (all five 040 rigs green under the default
`jit/auto` **and** under `POM68K_CPU_ENGINE=interp`). `sst68000` pins that
the cycle-exact 68000 core is unaffected.

**Follow-up audit, 2026-08-13 — does `writeBuffer` have siblings?** The
defect's class is "a serialized field the interpreter maintains and the JIT
does not". Two things were established, both by experiment rather than by
reading:

- **The restore-determinism check IS the detector for this class, and every
  machine family has one** — `savestate_68k_test` (Plus, SE, **Mac II**),
  `savestate_v8_test` (LC II), `savestate_030_test` (Sonora, VASP, RBV,
  RBV-IIci, IIfx, Duo) and `savestate_040_test` (Q605, Centris, Q700, Q900,
  Q630). Restoring the pre-fix behaviour makes all five 040 rigs fail again
  at the same byte, so the detector demonstrably bites. `jit_lockstep_*`
  does **not** cover the class: it compares registers, the three stacks, the
  clock, 2 KB of RAM and a dozen IPL fields — not the chunk.
- **A direct interpreter-vs-JIT chunk diff finds nothing, and that is not
  the reassurance it looks like.** Running the two engines side by side for
  the same cycles produced identical chunks even with the defect restored,
  because the JIT's fallback path executes through the interpreter: a store
  that falls back still writes the buffer. The asymmetry only shows when the
  PROPORTION of interpreted execution differs between two runs — which is
  exactly what a restore creates by disarming the JIT. Do not replace the
  determinism check with an engine diff; the engine diff is the weaker
  instrument.

The residual risk is bounded by the same structural fact: the JIT only
diverges on instructions it emits **natively**, and those are the integer
moves, ALU ops and branches. Serialized fields reachable only through
exceptions, traces, the FPU or the PMMU are executed by the same interpreter
under both engines. `tests/CpuChunkMap.h` now names the field behind a
divergence offset, so the next occurrence reports "CPU chunk +256 =
writeBuffer" instead of "byte 312".

## Model support in this copy (`MoiraTypes.h`)

- 68000 / 68010 — cycle-exact execution ✓ (Mac Plus phase)
- 68EC020 / 68020 — functional execution ✓, including CALLM/RTM module
  frames and optional CPU-space access-control transactions.
- 68030 — functional execution of the **MMU instruction set** ✓ (O4
  slice 1) and of **bus-level address translation + ATC + bus-fault
  frames** ✓ (O4 slice 3, see above), plus 6888x format-$2
  post-instruction exception frames.
- 68881/68882 — **execution ✓** when attached via `setFPUModel()` (O5
  slice 2, see above; softfloat-backed, 68882 = LC II PDS FPU).
- 68LC040 / 68EC040 / 68040 — **integer execution ✓** (Q2: MOVE16,
  CINV/CPUSH, MOVEC 040 set, format $2 address errors, 040 trace and
  undefined-CCR rules), **no-FPU F-line ✓** (Q4, format $4) and the
  **040 MMU bus translation ✓** (Q3, section above: TTR + URP/SRP walk
  with U/M, page-split accesses, format $7 faults with the last-write
  dichotomy, MOVEM restart, PTEST), with the **32-entry I/D ATC overlay
  ✓** (Q8, section above), and an **integrated FPU ✓** (native sparse
  opmodes, FPSP traps, revision-$41 FSAVE/FRESTORE frames and BUSY resume).

  The architectural caches are now complete at the transaction level:
  physically indexed/tagged I/D line data, WT/CB/NC policy, requested-
  longword-first four-beat fills, dirty replacement/CPUSH, CINV, MOVE16
  bypass, alternate-master snooping and configurable hit/fill/push charges.
  `POM68K_040_DCACHE=1` opts in. While armed, ordinary JIT page-to-RAM data
  windows are fenced; a code block's whole embedded range must be resident
  and byte-identical in I-cache, and direct links cannot bypass the next
  residency guard. Native backends may serve a sole read from a published
  physical D-cache line after checking logical privilege, DATA-ATC epoch,
  live valid bit, physical tag and line boundary. Writes remain on the exact
  path so dirty masks and format-$7 restart bookkeeping stay centralized.
  What remains outside scope is pin-level BCLK/TA waveform simulation, not
  an architectural cache behaviour (`docs/CACHE_040.md`).

  Implemented but **not exercised by the fuzzer**: the 8K-page (TC.P)
  cell — `mmu040PageMaskI()` and the walk's `if (reg.tc040 & 0x4000)`
  branch handle it, and the ATC follows suit; no corpus generates
  TC.P = 1 because Mac OS uses 4K throughout. A coverage gap, not a
  missing cell — extending the generator is the work, not writing code.
