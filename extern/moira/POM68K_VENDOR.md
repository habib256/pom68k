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
2026-11-10 with rows 79–86 (memory bit fields touch only their bytes,
MOVES fixup slot, MOVEM extension log, JMP odd-target PC, FScc and ADD/SUB
buffers, access-counted cache replacement, 68030 CACR strobes read as
zero); before that 2026-11-10 with rows 76–78 (compile-time acceleration seam, plain-020
RTE completion and instance-owned FPU arithmetic); before that
2026-11-09 with rows 60 to 75: ATC operations, MOVEP fault restart,
data-bearing cache snapshots, implicit-NC coherency, failed-CAS terminal
writes, MOVE16 fault context, read-fill aborts, pending write-allocate stores,
push-fault frames, split-page MA, descriptor errors and failed-translation
retention, MOVEM continuation and access-driven four-way ATCs; before that
2026-11-08 with rows 44 to 59: FPU corrections, portable continuation
state, manual evidence and physical cache operands; before that
2026-11-06 with rows 38 to 43: the DIVS absolute values back-ported from
NeoST, the 68040 cache switch taken out of the environment, three NeoST
68000 prefetch fixes, the 68000 bus-cycle order, three NeoST host hooks
with a 68000 external bus error, and two defects a fresh fuzz seed found;
before that
2026-10-19 with row 37, the MOVES fault cleanup; before that
2026-10-09 with row 35, the debugger's access-stop delegate; earlier
2026-09-05 with rows 33 and 34, the fused 68030 `ird`/`irc` fetch and the
68030 interpreter data window; earlier counts predated the 040 FPU/cache
completion, the JIT memory-contract work and the peripheral-phase alignment
pair):

| | | how to re-measure |
|---|---|---|
| distinct `pom*` extension identifiers | **112** (65 of them `pomJit*`) | `grep -rhoE '\bpom[A-Za-z0-9_]*' Moira/ \| sort -u \| wc -l` |
| `POM68K`-marked lines | **539** | `grep -rn POM68K Moira/ \| wc -l` |
| source files carrying a marker | **16 of 26** | `grep -rln POM68K Moira/ \| wc -l` |
| patch groups in the inventory below | **93** | this file |
| files POM68K *adds* outright | `MoiraCache040.h`, `MoiraState.h` | — |

Eighty-six patch groups, two of which (the JIT seam, row 22, and the ATC
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
against a *measured* estimate of porting this patch set onto it. Not by the
general discomfort of being forked.

The "Before re-syncing from upstream" note further down is now the exception
path, not the plan.

## How to read this file

> **The other half lives in `POM68K_SURFACE.md`.** This file says what was
> changed and why; that one says what the core exposes and what a host owes
> it — the contract to re-implement the host side in another emulator.

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
  the current marked-line and marked-file counts are the ones in the table
  under § Status, which `docs_test` recomputes (391 across 13 of 25 on
  2026-08-19, 361 across 13 of 25 on 2026-08-12 and 336 across 12 of 24 on
  2026-07-31; the 25th file is `MoiraCache040.h`, added).
  The NeoST patches carry `NEOST` markers instead, not `POM68K` ones — the two
  greps are disjoint.
- **Every comment that cites WinUAE or SingleStepTests carries its grade
  and revision**. WinUAE:
  `[D] … @ Hatari e77819f7`, the tree pinned under `oracle/uae/upstream`
  (CLAUDE.md § Evidence grades — another emulator is grade D, and a grade-D
  citation without its revision designates nothing). `core_grades_test`
  (`tests/core_grades_test.py`, including `tools/core_grades.py --check`'s
  checks) holds it: a WinUAE-citing comment block
  without `[D]` and a revision, or a `file.c:N` outside the pinned file,
  fails. Until 2026-11-08 the core cited two Hatari revisions without saying
  which — most of the FPU's `fpp.c` lines were read in NeoST's copy
  (`f0736b24`, 9 lines off in places) — and every such line is now
  retargeted to `e77819f7`. A `[D]` claim never overrules a `[B]` one; where
  the fork follows WinUAE against a manual, `oracle/README.md` names the
  ruling.
  SingleStepTests comments have their own header, `[D] SingleStepTests/680x0
  68000/v1 @ e0d5ece9670205cc84a0101081837deb446f86a3`, matching the fetcher
  and `tests/sst_corpora.lock`. The gate checks that agreement and rejects
  an unrelated oracle's grade/revision as evidence for the corpus. Manual
  citations now name an edition and a locator reviewed against the original
  PDF, held in `tests/core_manuals.lock`. Their own `[B]` header establishes
  primary-document evidence; an implementation approximation stays `[F]`
  with a `Settled by:` witness. Historical Musashi comparisons whose
  patched local tree was lost are F, not unpinned D. The remaining pass is
  over hardware claims without any source (`TODO.md` § Moteur › Surface).
- Sections are never rewritten when superseded, only annotated. § *Model support
  in this copy* (last section) is the current state; the journal is history.
- The vector counts quoted throughout (`sst68000` 1 000 058, `sst68030` 3 082,
  `sst68040` 7 200, and every historical figure below) come from corpora
  that live outside the repo, in git-ignored `tests/data/` (or wherever
  `POM68K_SST_DIR` / `POM68K_SST030_DIR` / `POM68K_SST040_DIR` point); without
  them those gates soft-skip. Their identity is pinned since 2026-11-06:
  `tests/sst_corpora.lock` records every file's size and SHA-256 and each
  corpus's provenance, and `sst_corpora_lock_test` holds a present corpus to
  it. The 68000 one can be re-fetched at its pinned upstream commit
  (`tests/fetch_sst_68000.sh`); the 68030 and 68040 ones were generated here
  and are not yet published anywhere (`TODO.md § Moteur › Socle`).

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
| 13 | **IRQ-recognition delay** after a mask-lowering SR write (`irqDelay`) | `Moira.cpp`, `Moira.h` | SimCity 2000 re-entered its VBL task with a clobbered A5 | `moira_rules_test` (`irqDelayAfterMaskLowering`), `lcii_boot_etalon` |
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
| 27 | **Integrated MC68040 FPU** — sparse native map, forced S/D precision, FPSP traps, revision-$41 FSAVE and BUSY resume | `MoiraExecFPU_cpp.h`, `Moira.h`, `MoiraTypes.h` | full-040 machines no longer masquerade as an attached 68882 | `fpu040_test`, `sst68040_fpu` |
| 28 | **MC68040 data-bearing caches** — I/D line contents, WT/CB/NC policy, writeback, CPUSH/CINV, snooping and hit/fill/push timing | `MoiraCache040.h`, `MoiraExecMMU_cpp.h`, `Moira.h` | tags alone could not expose stale copyback data or alternate-master coherency | `cache040_test` |
| 29 | **MC68881/2 mid-instruction interrupts** — protocol checkpoints, format $9, BUSY FSAVE/FRESTORE resume | `MoiraExecFPU_cpp.h`, `MoiraExceptions_cpp.h`, `MoiraExec_cpp.h`, `Moira.h`, `MoiraTypes.h` | long coprocessor commands are interruptible between null/come-again responses | `fpu_sanity` |
| 30 | **Saturating 68030 access-log counters** — `pomMmuBumpIdx`/`pomMmuBumpIdxDone` replace the raw `++` at all twelve log sites (ten in `MoiraExecMMU_cpp.h`, two in `MoiraExec_cpp.h`'s MOVES last-write path) | `Moira.h`, `MoiraExecMMU_cpp.h`, `MoiraExec_cpp.h` | `mmuIdx`/`mmuIdxDone` are per-INSTRUCTION state reset by `mmuExecuteStart()`; generated 68030 code enters through `pomJitWriteData`/`pomJitReadData` and never passes that loop head, so they grew without bound. Signed overflow is UB, and the store guard `mmuIdxDone < 10` is signed: at INT_MAX the counter wrapped to INT_MIN, the guard passed again and the next logged access wrote `mmuAd[-2147483648]` | `jit_lockstep_030_test`, `jit_lockstep_030_x64_experimental_test`, `jit_lockstep_030_x64_alignment_test` |
| 31 | **Identity-sized probe bound on the 68030** — `pomIdentityProbeBound()`, applied in the TT-match and TC.E-off branches of `pomJitProbeCode` and `pomJitProbeData` | `Moira.h`, `MoiraExecMMU_cpp.h` | both branches answer *identity* and return before the ATC, but carried `pageLen` from TC.PS — 0 until the OS programs TC, and the legal values are 8..15. The engine was handed a **one-byte page** and refused every window arm: 965 013 refusals per LC II boot, 95.6 % of all of them, every one before the MMU is enabled. Interpreter untouched (it never calls a probe), ATC paths untouched (only reached with TC.E set). Measured on the 120 000-step lockstep: interpreter fallback 46.8 M → 15.0 M, refusals 1 009 036 → 44 024, degenerate → 0 | `jit_lockstep_030_blocks_test`, `sst68030`, `lcii_boot_etalon` |
| 32 | **Post-PMOVE pipe visibility** — `pomMmuPipeLive()` accessor; the engine refuses to arm the code window while the pre-switch fetch pipe is live | `Moira.h` | mmuFetchWord serves pipe fetches BEFORE the i-cache overlay counters, so the interpreter counts nothing in a PMOVE's three-word shadow while a native block's folded charge counts its traced fetchWords — a +2 fetches/+2 hits/0 miss/0 cycle lockstep divergence in the System's self-patched MMU-init loop. The engine-side refusal (`ArmFail::Pipe`) makes both engines run the shadow interpreted, not-counting included | `jit_lockstep_030_test`, `jit_lockstep_030_x64_experimental_test`, `jit_lockstep_030_x64_alignment_test` |
| 33 | **Fused 68030 `ird`/`irc` fetch** — `mmuExecuteStart` serves both opcode words from ONE `pomJitFetch(pc, 4)` with one pipe test and one in-flight stamp, and the i-cache overlay moves into `pomIcacheFetch<Words>`, the single body `mmuFetchWord` now calls too | `Moira.h`, `MoiraExecMMU_cpp.h` | the mode-5 loop head paid two full `mmuFetchWord` calls for a pair the window already covers, while `mmu040InstrStart` had done the fused thing since the seam was written; on x86-64 the 030's automatic backend was then `threaded` (`X64Backend::caps().autoFamilies` carried 68040 only; it carries 68030 as well since the 2026-09-06 re-promotion), so EVERY guest instruction paid the duplicated pipe test, four-store stamp, window probe and call. The fold is refused when the pre-switch pipe is live (row 32 — a shadow fetch must keep returning before the counters), when a watchpoint is armed, and when the window does not cover all four bytes: a word reaching `read16` can clear the ROM overlay or flush a device's ticks, and nothing may sit between the two i-cache charges. ONE overlay body is deliberate — a second copy of the tag/valid/line arithmetic is how the 2026-08-19 retained-cache divergence presented, agreeing counters over parted cache content | `jit_lockstep_030_test`, `jit_lockstep_030_blocks_test`, `jit_lockstep_030_x64_alignment_test` |

| 34 | **68030 interpreter data window** — `mmuRead`/`mmuWrite` consult `pomJitData` for naturally aligned Byte/Word/Long, behind `POM68K_DATA_WINDOW`; `pomJitData030Ok()` owns the refusals and `pomJitData030Hits`/`pomJitData030Refusals` the instrument | `Moira.h`, `MoiraExecMMU_cpp.h` | J3 (point 11) wired the window from `mmu040Read`/`mmu040Write` only, so the knob was a **dead path** on the 68030 — knob on and knob off produced identical fingerprints and identical *zero* fills (`docs/JIT_BRINGUP.md` § C.2), which also left the C.2/C.3 030 probe and thunks without a direct interpreter-side exercise. The 030 owes a **wider** refusal set than the 040: `mmuRead` serves program space as well (`read<C, AddrSpace::PROG, …>` — prefetch, JSR/JMP targets) where `mmu040Read` takes `data` as an argument, and the DTLB is filled from a DATA-space probe whose `fc` the 030 ATC matches exactly, so `fcl != FC::USER_DATA` is refused alongside `fcSource != 0` and `mmuRmw`. `POLL_IPL` is reproduced at each size's own position — before a byte, after a word, **between** the halves of an aligned long — or interrupt recognition moves; unaligned and straddling forms keep the long path because they are the only ones that touch `mmuState[1]`/`mmuDataBuffer`. No in-flight stamp on a hit, same argument as `mmu040Read`, exactness inherited from J3b. **Default ON for this family since 2026-09-05** — the opposite of the 68040, on three independent admissions: bit-identical fingerprints knob on and off across three engines at two budgets, `-L m030` green 56/56 with the window on, and −5.5 % on `threaded` / −5.7 % on the interpreter arm. `POM68K_DATA_WINDOW=0` still refuses it, and `jit_lockstep_030_no_data_window_test` is the gate that keeps that path exercised now that it is no longer the default | `jit_lockstep_030_test`, `jit_lockstep_030_no_data_window_test`, `jit_lockstep_030_blocks_test`, `jit_lockstep_030_x64_experimental_test`, `jit_lockstep_030_x64_alignment_test` |
| 35 | **Debugger access-stop delegate** `pomDidReachWatchpoint(addr, S, write, program)` at all six watchpoint sites (default forwards to `didReachWatchpoint`), and `Debugger::reset` re-arming `CHECK_CP` | `Moira.h`, `MoiraDataflow_cpp.h`, `MoiraExecMMU_cpp.h`, `MoiraDebugger.cpp` | the bare address could not say read from write nor data from program space, so no consumer could keep a write watchpoint from stopping on reads or on an opcode fetch; and a reset left catchpoints listed with `CHECK_CP` clear, so the accelerated engine no longer stepped aside for them | `debug_session_test` |
| 36 | **Defined last-level test in the 68030 table walk** — `bitpos <= 0` replaces `!bitpos` in the `indirect` predicate of `mmuWalkTables` | `MoiraExecMMU_cpp.h` | once TID has been consumed `bitpos` is −4, and `!bitpos` let `bits >> -4` evaluate: undefined behaviour, flagged by the nightly UBSan job in the test-side mirror `src/Mmu030Peek.h` (red from 2026-10-09). x86-64 and AArch64 mask a 32-bit shift count to 28, `bits` holds 16 bits, so the old expression read 0 and answered "last level" — exactly what the new one answers. No behaviour change | `sst68030`, `debug_inspection_test` |
| 37 | **MOVES releases alternate FC on a bus fault** — clear `fcSource` before exception stacking | `Moira.cpp` | A faulting MOVES left SFC/DFC selected for handler fetches and data accesses. The LC II optional-FPU probe exposes this with SFC=7 and an empty 68882 socket; the fault's FC remains in the recorded access/SSW | `v8_ramsize`, `lcii_nofpu_boot_etalon` |
| 38 | **DIVS absolute values in unsigned arithmetic** — `cyclesDiv<C68000\|C68010, DIVS>` and `setUndefinedDIVS` no longer call `abs()` on the 32-bit dividend (back-port of NeoST `74b2f2d`, 2026-10-20) | `MoiraALU_cpp.h` | `abs(INT32_MIN)` is undefined behaviour: on `$80000000 / $8000` NeoST saw GCC and clang take different branches of the 68000 cycle count (+136 cycles on one compiler). In this fork the two `cyclesDiv` sites are unreachable for that dividend — `execDivsMoira` leaves on V before asking for the count (row 2's early overflow exit) — but `setUndefinedDIVS` is reached by every 68020/68030 `DIVS.W` of `$80000000`. No behaviour change on the compilers the project builds with; the 8 065 DIVS vectors of `sst68000` contain no such dividend, hence a hand-written gate | `moira_rules_test`, `sst68000`, `sst68030` |
| 39 | **The 68040 cache switch and its CM histogram are host-injected** — the constructor no longer reads `POM68K_040_DCACHE` and `mmu040AtcFill` no longer reads `POM68K_040_CM_STATS`; `setPomCache040()` (existing) and `setPomCmStats040()` (new) are the only way in | `Moira.cpp`, `Moira.h`, `MoiraExecMMU_cpp.h` | a behavioural switch read with `getenv` in a CPU core's constructor is invisible to the host's configuration, breaks the project's own rule that only `ProcessEnvironment` reads the process environment, and would have followed the core into any other emulator. POM68K declares both names in `StartupOptions.h` (JIT domain, like `POM68K_DATA_WINDOW`), `jit::ResolvedConfig` carries them and `MoiraCpu` applies them, so the product and every test rig still honour the same two environment variables. Three `NEOST_EXC_DIAG` reads remain (`TODO.md § Moteur › Surface`) | `jit_lockstep_x64_test`, `jit_lockstep_a64_coarse_test` (both now fail if the requested cache is not armed), `cache040_test` |
| 40 | **Three NeoST 68000 prefetch fixes, back-ported** — MOVE to SR reloads both prefetch words (`fullPrefetch`, NeoST `ce1cc52`); ANDI/ORI/EORI #,SR and #,CCR and MOVE to CCR do the same (`d8d33bb`); DIVU/DIVS spend their internal cycles BEFORE the final prefetch (`262f63a`, upstream `f90fc50`) | `MoiraExec_cpp.h` | after a write to SR or CCR the 68000 re-reads the word at PC and then PC+2, in the NEW function code; the old code read PC+2 twice and, for the immediates, read the first in data space, keeping a stale opcode in IRD. NeoST took all three from the microcode transcription and Hatari CE; the SingleStepTests transaction lists say the same on every non-exception vector, which makes three independent readings. Cycle totals unchanged. In `execDivsMoira` the port keeps row 2's early overflow exit by computing the quotient first | `sst68000` (bus cycles) |
| 41 | **68000 bus-cycle order and idle placement** — group-0 frame stacked PC low, SR, PC high, IR, address low, status word, address high, with its two idle cycles before the frame instead of after; RTE and RTR unstack PC high, SR/CCR, PC low; JSR fetches the first target word before pushing the return address; a trapping CHK spends four of its idle cycles on a discarded prefetch, a non-trapping one prefetches before idling; `MOVEM.l <ea>,list` reads its extra word after the last transfer; `MOVEM.l list,-(An)`, ADD/SUB/ADDQ/SUBQ/ADDI/SUBI long to memory write the low word first; ADDX.l/SUBX.l read both operands low word first (new `REVERSE` in `read<>`); `BTST Dn,#imm` idles after its prefetch | `MoiraExceptions_cpp.h`, `MoiraExec_cpp.h`, `MoiraDataflow_cpp.h` | every one of these left registers, memory and the cycle TOTAL right, so `sst68000` was green at 1 000 058 / 1 000 058 while only 736 983 vectors had the corpus's bus sequence. A bus-order error matters where the bus is watched cycle by cycle — video contention on the compacts, a blitter or a Shifter in another emulator — and where a write lands on what the next read fetches (JSR onto its own target). All are `C == Core::C68000` only; 68010 and 020+ keep upstream's order, for which there is no corpus. Evidence is grade D throughout: SingleStepTests/680x0 v1 `transactions` | `sst68000`, which compares bus cycles since this row: order and timing 1 000 058 / 1 000 058 |
| 42 | **Three NeoST host hooks and a 68000 external bus error** — `illegalOpcodeHook(opcode)` (a handled illegal opcode is consumed like a NOP; NeoST `fdbce2e`), `willProcessFault(frame)` on every address or bus error including a double fault, `setBusLock(bool)` bracketing the TAS read-modify-write (both NeoST `ce1cc52`); and `extBusError000(addr, write)`, new here | `Moira.h`, `Moira.cpp`, `MoiraExec_cpp.h`, `MoiraExecMMU_cpp.h` | the hooks are no-ops by default and off every hot path (illegal opcode, fault, TAS); they are what NeoST's wrapper overrides, so the two copies no longer differ there. `extBusError000` exists because `extBusError()` stacks a 68020/68030 frame and throws an `MmuBusError` that `processException<C68000>` has no branch for: called from a 68000 board it left `execute()` as a C++ exception. `PortableMemory::busError` did exactly that on a SCSI pseudo-DMA access with DRQ inactive (Macintosh Portable, PowerBook 100); it now passes the address and direction and gets a group-0 frame built with NeoST's recipe. Not ported: NeoST's per-bus-cycle hooks, which add two virtual calls to every access | `moira_rules_test` (frame fields for a read and a write fault, the lock bracket `LrwU`, the released lock on a faulting TAS, handled and unhandled illegal opcodes) |
| 43 | **DIVU.L/DIVS.L no longer swallow an operand fault; CHK2/CMP2 leave N and V alone on the 68040** — the `catch(...) { return false; }` around `readOp` in `execDivlMoira` is gone; `execChkCmp2` applies `setUndefinedCHK2` below the 68EC040 only | `MoiraExec_cpp.h` | the catch-all was dead in stock Moira, where a 68020+ data read never throws; here it swallowed every access fault on the operand — a 68030 or 68040 page fault, or an external /BERR — so the instruction took no exception and stopped half-way. CHK2/CMP2's N and V are undefined; the 68020/68030 follow WinUAE's table and the 68040 follows the same oracle's early return, which upstream's port of that table lacked. Both were found by replaying fresh fuzz seeds (405, 507, 509, 605) against the oracle; the 7 200 pinned vectors contain neither | `moira_rules_test`, `sst68040`, `sst68040_8k` |
| 44 | **68040 FPU: an unimplemented instruction outranks an unsupported datatype** — `fpuGetSource` raises the datatype trap (vector 55: packed, denormal, unnormal) only for the opmodes the chip executes itself; an X, D or dynamic-format source in Dn and an An source take the unimplemented frame when the opmode is unimplemented, a Line-F otherwise | `MoiraExecFPU_cpp.h` | the caller raises vector 11 right after the fetch, so the datatype trap pre-empted it: FMOD with a packed source reached the FPSP's datatype handler instead of its unimplemented-instruction one. Oracle order (`fault_if_unimplemented_680x0` ahead of every `fp_unimp_datatype`). Found by the first differential run of the integrated FPU | `moira_rules_test`, `sst68040_fpu` |
| 45 | **68040: no FPU instruction ends on a "last write"; the (An)± fixup and FPIAR are in place before the operand access** — `execMmu040BusError` restarts every `$F200-$F3FF` instruction; `fpuArmFixup` arms on the 68040 too; `execFGeneric` latches FPIAR before the source fetch | `MoiraExecMMU_cpp.h`, `MoiraExecFPU_cpp.h` | a faulting FMOVE/FMOVEM/FSAVE store stacked the NEXT instruction's PC and kept (An)± stepped, as an integer store does; a faulting source read left An stepped and FPIAR stale. A handler that maps the page and returns then skipped the store, or re-ran it one operand further. Decided from the opcode at fault time, so the store path carries no extra test | `moira_rules_test`, `sst68040_fpu` |
| 46 | **FPU-less 68040: what is no instruction is refused before the "no FPU" frame** — opclass 001, the opmodes with nothing behind them, a packed source in Dn, X/P/D to Dn, An/PC-relative/immediate destinations and an immediate destination of a control-register move take a plain format $0 Line-F; opmodes $78-$7F take the illegal vector with the PC past the extension word (`execFpuIllegalOpmode040`, with or without an FPU); EA fields $3D-$3F are not FPU opcodes on the 040 family; `FMOVE #imm` with no control register selected consumes one long | `MoiraExecFPU_cpp.h`, `Moira.h` | the hook answered format $4 to nearly everything in the window. The 68LC040 is the product default of the LC 475/575/580 and Centris 610/650: format $4 tells its system "an FPU instruction met no FPU", format $0 "this is not an instruction". The `random` family reaches this window once in a few hundred vectors — four pinned vectors in 7 200; the `fline` family written for it started at 3 074 / 4 500 | `moira_rules_test`, `sst68040_fline` |
| 47 | **FBcc evaluates five predicate bits on every FPU; an An operand is refused after the status byte is cleared** — FBcc registered over the six-bit field on every FPU-bearing model, the `$20-$3F` Line-F gone; the early Line-F on an An EA kept for FMOVEM only | `MoiraInit_cpp.h`, `MoiraExecFPU_cpp.h` | the Line-F came from reading the dead `#if 0` block of the oracle's `fpp_cond`, whose live code opens on `condition &= 0x1f`; `fpuCondEval` already masked for FScc/FDBcc/FTRAPcc, so FBcc alone disagreed with its siblings. An source or destination: the oracle clears the FPSR exception byte before it finds the EA invalid. No manual at hand rules on bit 5 (grade F, stated at the site) | `moira_rules_test`, `sst68040_fpu`; fuzz seeds 808 and 809 on the 68030 |
| 48 | **68040 bus accessors: the common translation inlined, the TTR test from a table** — `mmu040TranslateHot` (TTR, MMU off, or the last-hit ATC line, with `mmu040Translate` behind it for everything else), `mmu040FetchWord` for instruction words, `mmu040BusRead`/`mmu040BusWrite` for the cache-model-off bus call, and `mmu040MatchTTR` answered from a 512-entry table rebuilt from the reference test when a register pair has changed | `MoiraExecMMU_cpp.h`, `MoiraDataflow_cpp.h`, `Moira.h` | the 2026-11-06 profile put 63 % of the 68040 interpreter in address translation, behind three out-of-line calls per access. No new cache: the last-hit probe already existed, this stops paying the calls to reach it. Same decisions in the same order; the ATC's pseudo-LRU is touched only once the access is known to succeed in the hot path. Interpreter −31.7 % wall on the fixed-cycle bench (32.17 s → 21.97 s), fingerprint `a2036281a5c2ac55` unchanged; JIT mode flat. The table is derived state: no hook on the registers, never serialized | `moira_rules_test` (TTR table), the four `sst68040*` corpora, `jit_bench` fingerprint |
| 49 | **The core reads no environment variable** — the three `getenv("NEOST_EXC_DIAG")` of NeoST's exception trace (`setIPL` `[PIN]`, `execInterrupt<C68000>` `[EXC]`, `jumpToVector` `[JTV]`) test a member set through `setPomExceptionDiag(bool)` | `Moira.h`, `Moira.cpp`, `MoiraExceptions_cpp.h`, `MoiraDataflow_cpp.h` | the last process-global input of `extern/moira/Moira/` after row 39. A core that reads its host's environment cannot be embedded twice with two answers, and three function-local `static const bool` froze the first one for the life of the process. The name stays `NEOST_EXC_DIAG`: POM68K declares it in `StartupOptions.h` and carries it in `jit::ResolvedConfig`, the one configuration all fourteen CPU wrappers receive; NeoST's wrapper calls the same setter from wherever it reads its own settings | `docs_test` (no `getenv` under `extern/moira/Moira/`) |
| 50 | **68040: an FPU exception the user enabled is posted, not taken** — `fpuCheckArithException` reports an exception as immediate only when it is pending without being enabled (the nonmaskable OVFL/UNFL, SNAN/OPERR on a move to an integer); an enabled one leaves `expPend` set and the next FPU instruction takes it as a pre-instruction exception, format $0. `fpuMakeStatus` keeps the destination untouched on any 68040 overflow or underflow; the BUSY resume of FRESTORE leaves what it raises posted | `MoiraExecFPU_cpp.h` | ruling D24. The fork delivered every exception at once with a format $3 frame, on the word of `fpu040_test`, which cited nothing. M68040UM § 9.7.2.1, 9.7.3.1 and 9.7.6 say the exception is posted and "the next floating-point instruction that is encountered takes a pre-instruction exception"; the oracle does exactly that. A handler written for a 68040 — the FPSP's — reads the pre-instruction frame and the E1/E3 state frame; given a format $3 frame for an FADD it finds an effective-address field that means nothing. The pending machinery was already there, inherited from the 68882 | `sst68040_fpu2` (6 000 two-instruction vectors), `sst68040_fpu`, `fpu040_test` |
| 51 | **68040 state frames carry the intermediate result** — `fpuCheckArithException` fills WBTEMP and the guard bits of a BUSY (E3) frame from softfloat's internal result, the rounded FPTEMP/ETEMP of a trapped FMOVE/FABS/FNEG, quiets the signaling bit of a move-out's ETEMP and tags it as extended; `execFpuDatatype040` shows FSQRT as opmode $05 in CMDREG1B and tags a move-out's source as extended | `MoiraExecFPU_cpp.h` | the FPSP finishes a trapped operation from these fields — it denormalizes WBTEMP after an underflow, rounds it after an overflow — so zeros there made an emulated Quadra flush to zero what a 68040 returns as a denormal. Invisible until row 50 posted exceptions and a family put FSAVE in the handlers: `fpuh`, three steps, 5 750 / 6 000 before, 5 988 after. [D] WinUAE fpp.c "fsave data for 68040" @ Hatari e77819f7, the getters being softfloat's own | `sst68040_fpuh`, `sst68040_fpu2` (its three listed vectors now pass) |
| 52 | **68030: the last write of every read-modify-write instruction is marked; CLR is a pure store from the 68020 up** — `pomLastWrite030` before the direct `writeM` of ADD/SUB to memory, ABCD/SBCD/NBCD, ADDX/SUBX, the memory shifts and BCHG/BCLR/BSET; `execClr` takes its no-read branch for the whole `C68020` core, not only from the 68EC040 | `MoiraExec_cpp.h`, `Moira.h` | `writeOp` marks the 68030's last write (a fault there stacks the short frame $A with the next instruction's PC and the flags already updated); seven handlers call `writeM` themselves and stacked the long $B with the old flags, restarting an instruction the oracle only completes. CLR read its destination first on the 68020 and 68030 as the 68000 does: only "the MC68000 and MC68008" do (M68000PRM), and on a device register that read is a side effect no 68020 Macintosh produces. The `fault` family has its own opcode list and emitted none of these forms; a `rmw` family written for them started at 5 052 / 6 000 | `sst68030_rmw`, `moira_rules_test` (CLR bus cycles), `sst68000` (bus cycles unchanged) |
| 53 | **A 68882 refuses a 68040 FSAVE frame** — the FRESTORE `$41` branch is taken by `FPUModel::M68040` only; a 6888x given that frame takes the format error | `MoiraExecFPU_cpp.h` | ruling D23b, settled 2026-11-07: the acceptance mirrored WinUAE's "horrible hack to support on the fly 6888x <> 68040 FPU switching", which exists only in the oracle's permissive `fpu_no_unimplemented` setting — the one every 68030 corpus before that day was generated in. The strict oracle and MC68881/MC68882 UM § 6 agree on the format error. The four permissive vectors of the July corpus are listed in `tests/sst68030.known`; `sst68030_solo` was regenerated strict (six vectors changed, all of them this). The 68LC040 + software 68882 hybrid of the LC 475/575/580 and Quadra 605 was the risk: a system that believes it has a 68040 FPU restores only frames it saved itself | `sst68030` (listed), `sst68030_solo`, `sst68030_fpuraw`, `sst68030_fpu2`, the LC 475 and Quadra 605 etalons |
| 54 | **68030 PTEST reports an invalid descriptor fetched at its level limit** — `mmuWalkTables` sets MMUSR.I when the loop leaves on the limit with the last descriptor invalid | `MoiraExecMMU_cpp.h` | the descriptor read in one pass was examined at the top of the next, which a level limit equal to the depth reached prevents: PTESTR level 1 into a DT=0 descriptor answered N=1 and no I. MC68030UM (Freescale edition) § 9.7.4, Table 9-3: I is set if a descriptor encountered during the search is invalid (cited as § 9.7.6 until 2026-11-08 — no such section); WinUAE examines each descriptor as fetched. One fuzz vector in 21 000 ("mmu/tt seed=1006 #1188") | `moira_rules_test` |
| 55 | **The interpreter arms the fetch window itself** — `pomSetInterpWindow(on, codeSpanFn, ctx)` and `pomInterpArm`, called on a window miss by the 68030 and 68040 loop heads and by `execute()` for the 68000/68010/68020; `jit::Engine` hands the window over when it is disabled and takes it back when it runs. Also drops a stale `assert(S == Word)` in `divlsMoira`: DIVS.L of `$80000000:00000000` by −1 overflows legitimately | `Moira.h`, `MoiraExecMMU_cpp.h`, `MoiraALU_cpp.h` | the window, its fetch path and its eviction hooks were the core's; only the decision to arm lived in the engine, so `POM68K_CPU_ENGINE=interp` — and any bare host — paid a translated bus read per opcode word. The arm is `Engine::armWindow` decision for decision, minus the block guards it has no use for. Fixed-cycle benches, interpreter: Quadra 605 19.75 s → 12.71 s (−36 %), LC II 13.26 s → 11.06 s (−17 %), Mac II 5.83 s → 5.06 s (−13 %), Plus within noise, fingerprints unchanged; JIT mode flat (it never ran this path) | `jit_bench` / `jit_bench_lcii` fingerprints, the lockstep gates (interpreter arm under the window now), `jit_asset_free_lockstep_test` |
| 56 | **68030: a /BERR on a table-search descriptor fetch ends the search with MMUSR B\|I** — `extBusError()` raised while `mmuInTableSearch` throws the side-effect-free `MmuTableSearchBusError`; `mmuBusWalk` (now a wrapper over `mmuBusWalkBody`) and `mmuWalkTables` catch it: the bus walk adds B\|I to the status it had accumulated, so the ATC entry carries B and the retried access faults on its logical address; PTEST reports B\|I with the faulted descriptor not counted in N, and takes no exception | `MoiraExecMMU_cpp.h`, `Moira.h`, `MoiraTypes.h` | the walk used to throw the access's own fault from inside the search: same frame for a normal access, but no B entry left behind, and PTEST/PLOAD took an exception instead of reporting. MC68030UM (Freescale edition) § 9.5.2 p. 9-49, the ATC B bit p. 9-11, § 9.7.4 Table 9-3; WinUAE mmu030_table_search agrees. Was an "unverified" line of `POM68K_SURFACE.md` | `moira_rules_test` (fails on the old core: MMUSR $0400 after the fault, and PTEST level 7 taking vector 2) |
| 57 | **Portable instruction-boundary state visitor** — `pomVisitState(Ar&)` owns the CPU inventory, adds the pending 6888x result, integrated-040 frame micro-state, 030 pipe and DF-cleared retry; an armed public MOVEM latch is carried with inactive EA normalized | `Moira.h`, `MoiraState.h` | the host-side visitor omitted state reachable between executed instructions; a fresh CPU lost SQRT completion, retried a software-completed bus access, refetched patched code, and emitted different FSAVE payloads. POM68K adapter delegates; format v29 refuses old payloads | `moira_state_test` |
| 58 | **Manual evidence audit** — explicit editions and reviewed locators; model approximations and lost historical Musashi comparisons have an F witness | `Moira.cpp`, `Moira.h`, `MoiraALU_cpp.h`, `MoiraCache040.h`, `MoiraDasm_cpp.h`, `MoiraDataflow_cpp.h`, `MoiraExceptions_cpp.h`, `MoiraExecFPU_cpp.h`, `MoiraExecMMU_cpp.h`, `MoiraExec_cpp.h`, `MoiraTypes.h` | nonexistent or misattributed sections and unsupported silicon claims could be carried into another host as established rules. Comments only; CPU executable tokens unchanged | `core_grades_test`, optional `tools/core_grades.py --verify-manual-files DIR`, `docs_test` |
| 59 | **Physical CINV/CPUSH operands** — `pomCacheOp040` takes An directly; the logical resolver and its private peek walk are removed | `Moira.h`, `MoiraExecMMU_cpp.h` | with paging enabled, cache management selected an unrelated translated alias or skipped a resident physical line; cold operands also read guest descriptors. Motorola defines the operand as physical | `cache040_test` (128 physical-operand combinations, RAM writeback and descriptor access checks) |
| 60 | **FC2-tagged 68040 I/D ATCs** — lookup/fill, hot translation, JIT probes and cache-line publication use the access privilege | `Moira.h`, `MoiraExecMMU_cpp.h` | one logical page could reuse the other root's mapping. MOVES uses SFC/DFC, not current SR; a JIT publication must not borrow the opposite domain's ATC index | `mmu040_atc_test` (44 CPU sequences; 192 pre-fix failures) |
| 61 | **68040 MMU register writes retain ATCs** — TC/URP/SRP setters revoke only derived windows/TLBs/publications | `Moira.h` | MC68040UM-1993 § 3.7.4 leaves the I/D entries resident until an explicit flush; misses use the new roots | `mmu040_atc_test` (24 added sequences; 192 pre-fix failures) |
| 62 | **68040 PFLUSH/PTEST ATC effects** — page flushes select DFC.FC2; PTEST renews only its selected I/D entry, including negative results | `Moira.h`, `MoiraExecMMU_cpp.h`, `MoiraTypes.h` | MC68040UM-1993 §§ 3.7.2/3.7.3 and M68000PRM-1992 PFLUSH/PTEST; table /BERR during PTEST reports B without an access exception | `mmu040_atc_test` (1 216 added sequences; 3 616 pre-fix failures) |
| 63 | **68040 MOVEP store faults restart the unfinished instruction** — all byte positions bypass the last-write/next-PC branch | `MoiraExecMMU_cpp.h` | WinUAE @ Hatari e77819f7 keeps mmu_restart true for the whole byte sequence; generic store marking skipped its remaining bytes after RTE. Hardware partial-transfer/writeback policy remains unverified | `moira_rules_test` (192 MOVEP sequences + 36 ordinary-store controls; 558 pre-fix failures), seed 845 #1967 |
| 64 | **040 snapshots retain data-bearing caches** — sparse valid-line payloads, per-longword dirtiness, replacement phase and cache-model mode, including disabled retained lines | `MoiraCache040.h`, `MoiraState.h` | flushing caches on load lost copyback bytes absent from RAM and stale I-cache bytes newer RAM cannot reconstruct; save has no bus/time effects, load drops old ATC/JIT state before restoring caches (host format v30) | `moira_state_test` (six initial cases: 24 pre-fix failures), `savestate_040_test` (five rigs × two engines, rollback and v29 refusal) |
| 65 | **040 implicit-NC operands preserve coherency** — inhibit the access through CM while retaining CACR enable, and arm CAS2's locked operand marker | `MoiraExecMMU_cpp.h`, `MoiraExec_cpp.h` | MOVES/TAS/CAS bypassed dirty push and clean invalidation; CAS2 used cacheable reads without write permission. MC68040UM-1993 §§4.3.2/7.4.1/7.4.3/7.4.5 | `cache040_test` (630 initial sequences, 1 351 pre-fix failures; three disabled-cache controls; sixteen CAS2 protection cases) |
| 66 | **040 failed-CAS terminal transfers** — CAS writes its read operand; either failed CAS2 comparison writes only the second operand | `MoiraExec_cpp.h` | MC68040UM-1993 §7.4.5 requires the final locked transfer even when memory is unchanged. Uses the ordinary final-write fault path. WinUAE omits CAS2's transfer (D31); CAS2 fault register/PC ordering remains an explicit model assumption | `cache040_test` (1 814 sequences, 3 752 pre-fix failures; includes 78 legacy controls, terminal /BERR, WB3 completion and RTE) |
| 67 | **MOVE16 owns its fault descriptor; successful pushes restore the caller** — line-aligned logical source/destination, data FC, direction and line size, plus destination EA for WB2 | `MoiraExecMMU_cpp.h`, `Moira.h` | MOVE16 inherited a program-fetch stamp; destination faults could use stale WB2 addresses. A successful dirty push also left its physical longword descriptor over the following operand read. Fill faults and faults inside a push remain open | `cache040_test` (3 810 MOVE16 + 70 post-push read faults; 5 230 pre-fix failures) |
| 68 | **040 read-fill demanded versus unused faults** — defer frame construction, keep logical read size/space/address, return completed bytes without caching an aborted line, and read before pushing a dirty victim | `MoiraExecMMU_cpp.h`, `Moira.h` | MC68040UM-1993 §§4.6.1/4.6.2/7.6.1/8.4.6.2/8.4.6.4: unused fill beats must not fault the guest; an aborted replacement read restores dirty data without a premature push. Write-allocate and push-fault frames remain separate | `cache040_test` (4 872 data + 224 instruction sequences; 7 936 pre-fix failures) |
| 69 | **040 write-allocate pending-store frames** — retain the original logical store descriptor, read before pushing, and expose a reported fill-read fault's outstanding store through WB3 | `MoiraExecMMU_cpp.h`, `Moira.h` | MC68040UM-1993 §4.6.2 and §8.4.6/Table 8-6: read-before-push, logical read fault and register-aligned pending data. WB3 stage assignment and unused write-fill beat delivery remain F pending silicon capture | `cache040_test` (14 616 MOVE store sequences; 35 112 pre-fix failures; software WB3 completion and RTE) |
| 70 | **040 physical push-fault frames and transfer width** — one dirty longword versus a full line, full PD/physical FA=WB1A, independent logical WB3A, retained new line and faulted CPUSH invalidation | `MoiraExecMMU_cpp.h`, `MoiraCache040.h`, `Moira.h`, `Moira.cpp` | MC68040UM-1993 §§4.6.2/7.6.1/8.4.6.2/8.4.6.4–8.4.6.6, Table 8-6. Keep the synchronous pending-store policy; asynchronous pipeline combinations and NC-fault invalidation need more evidence | `cache040_test` (initial 5 733 sequences, 15 848 pre-fix failures; expanded 12 285 sequences with B/W/L pending writes, software PD/WB3 completion and RTE) |
| 71 | **040 split-page MA requires an ATC fault** — keep the initial logical FA for every split fault, but do not set MA for an external data/fill /BERR | `MoiraExecMMU_cpp.h` | MC68040UM-1993 §§8.4.6.2/8.4.6.4 outrank the unconditional later-fragment MA in WinUAE `cpummu.c:989–997` @ Hatari e77819f7 (D32) | `cache040_test` (8 592 sequences; 2 016 pre-fix frame failures; physical and ATC controls, partial stores and software WB3/RTE completion) |
| 72 | **040 descriptor-bus faults and deferred U history** — qualify ordinary walk errors as original-access ATC faults, retain an R=0 entry, clear the search context and delay table U writes until the next descriptor read | `MoiraExecMMU_cpp.h` | MC68040UM-1993 §3.5/Figure 3-10/§8.4.6.2; PTEST keeps its B-only outcome. D33 records the manual's deferred U order over the eager pinned oracle. Shared table reads explicitly sequence host words | `cache040_test --descriptor-faults` (initial 13 632 cases, 30 144 pre-fix failures; expanded 14 112 cases including M re-walk hits) |
| 73 | **040 ordinary failures retain R/W/S and PA in the ATC** — publish before permission/validity rejection, and preserve a supervisor denial before any M=0 write re-walk in both lookup paths | `MoiraExecMMU_cpp.h` | MC68040UM-1993 §§3.3/3.5: retry uses the created entry; a repaired descriptor alone cannot replace cached attributes. Only an otherwise permitted unmodified write re-walks | `mmu040_atc_test --failure-retention` (8 192 sequences; 33 488 pre-fix failures; PTEST controls, direct/scanned lookup, FC2 isolation, M controls and PFLUSH/WB3/RTE recovery) |
| 74 | **040 MOVEM continuation after RTE** — restore CM/EA from format $7, reuse the saved EA only for indexed/PC-relative modes, and bypass repeated memory indirection | `MoiraExec_cpp.h`, `MoiraDataflow_cpp.h` | MC68040UM-1993 §§8.4.6.2/8.4.6.7; D34 limits the oracle’s blanket EA reuse. Repeat all operand accesses, preserve current source values and consume the latch on success or another fault | `moira_rules_test --movem-restart` (4 848 sequences, 5 952 pre-fix failures), `moira_state_test`, `jit_asset_free_lockstep_test` |
| 75 | **040 ATCs have sixteen four-way sets and access-driven replacement** — 64 entries per side, invalid-way priority, counter effects committed through windows, DTLBs and native cache hits, and speculative ATC rollback | `Moira.h`, `Moira.cpp`, `MoiraDataflow_cpp.h`, `MoiraExecMMU_cpp.h` | MC68040UM-1993 §3.3 defines geometry, indexing and the access counter. Last-hit memos are accelerators, not replacement state. Exact seed/clock/PTEST phase and I/D coupling remain F; ordinary native JSR also reproduces the interpreter's target-word read with caches off | `mmu040_atc_test` (768 geometry + 2 688 replacement sequences), `jit_asset_free_lockstep_test` (432 access/fault/TC-TTR + 1 152 victim/U-bit cases, both native ISAs) |
| 76 | **Compile-time acceleration seam** — `MOIRA_ENABLE_JIT=0` removes fetch/data windows, native publications, probes, layout and timing instrumentation; the shared interpreter dispatch and architectural cache/MMU paths remain | `MoiraConfig.h`, `Moira.h`, `Moira.cpp`, `MoiraDataflow_cpp.h`, `MoiraExecMMU_cpp.h`, `MoiraState.h` | an interpreter host must not require JIT storage or branches; the compatibility default stays enabled | `moira_interp_test` compares state/cycles/bus/RAM across builds; `moira_interp_berr030_test`, `moira_interp_fpu_sanity`, `moira_interp_fpu040_test`, `moira_interp_callm_rtm_test` reuse CPU proofs without JIT |
| 77 | **Plain 020/EC020 RTE software completion** — seed restart scratch state before prefetch, restore CCR/EA on a data fault, consume cleared-DF reads/writes and second-half longwords without repeating the faulted cycle, carry the existing latch through snapshots, preserve original PCs across nested/redirected frames and cancel it on reset | `Moira.h`, `Moira.cpp`, `MoiraDataflow_cpp.h`, `MoiraExec_cpp.h`, `MoiraExecMMU_cpp.h`, `MoiraExceptions_cpp.h` | MC68020UM-1992 §§6.2.1–6.2.3 define DF and right-justified buffers; bit 9 is reserved. The plain model keeps its format-$B restart representation; internal silicon state, prior accesses and DF-cleared RMW remain unproved | `berr020_test`, `moira_interp_berr020_test` (404 sequences each), base corpora and Mac II/LC boots |
| 78 | **Per-instance SoftFloat context** — move rounding, exception flags, conversions and intermediate-result captures out of process globals; opaque ownership keeps C types out of the public header | `Moira.h`, `Moira.cpp`, `MoiraExecFPU_cpp.h`, `extern/softfloat/softfloat.c/.h` | FPCR rearming alone does not protect a suspended operand callback or parallel CPUs. Existing pending/FSAVE state remains the archived boundary image; arithmetic formulas and the oracle copy stay unchanged | `fpu_context_test`, `moira_interp_fpu_context_test`, `moira_state_test`, FPU corpora |
| 79 | **Memory bit fields touch only the bytes they span, reads before writes** — `execBitFieldEa` reads 1, 2, 2+1, 4 or 4+1 bytes (byte, word, word+byte, long, long+byte), finishes both reads before the first write and writes the same shape back | `MoiraExec_cpp.h` | the longword-always access read and rewrote up to three bytes outside the field — a spurious ATC fault, /BERR or device access past a page or register end, and the wrong SSW size in a fault frame (seed 405 #174/#281 of `fuzz040.py --family random --mmu identity --page 8k`) — and wrote the first four bytes before reading the fifth, so a fault on that read left them written and the restarted BFCHG inverted them twice. [B] MC68030UM-1989 §11.6.14: one operand cycle below five bytes, two at five; [D] WinUAE `get_bitfield`/`put_bitfield` and `i_BF*` @ Hatari e77819f7 for the decomposition; [F] the three-byte field, one operand cycle with SIZ = 3 in the manual, a word and a byte here and in the oracle. Valid for the 68020, 68030 and 68040. The JIT contract publishes the narrower shapes as longword proof spans (plain RAM only, no exact thunk or cache token, `src/jit/JitIr.h`) and the five-byte RMW as read4/read1/write4/write1; a three-byte field counts its 040 data-ATC route twice | `moira_rules_test` (bus traces on 020/030/LC040, /BERR on the fifth byte), `jit_backend_test`, `jit_asset_free_lockstep_test` (three-byte ATC counts), `sst68040`, `sst68030` |
| 80 | **68030 MOVES memory-to-register (An)± fixup in the second slot** — the pending fixup moves to `mmuFixupReg[1]`, so a format-$B frame carries it in the wb3 status byte ($34) | `MoiraExec_cpp.h` | WinUAE's generator emits the destination EA of both MOVES directions, so the read direction's fixup is `mmufixup[1]`; the fork put it in wb2 ($36). Internal words the manual leaves undefined: [D] `gencpu.c` i_MOVES @ Hatari e77819f7, [F] a real 68030's frame. The restore walks both slots, so only the frame byte changes. Found by a fresh sweep (`fuzz030.py --family core --mmu identity --strict-fpu --n 1000 --seed 9117`, #575) | `moira_rules_test` (MOVES fixup slot), `sst68030*` |
| 81 | **68030 MOVEM to memory logs its EA extension words** — `execMovemRgEa` turns access logging off after `computeEA`, not before | `MoiraExec_cpp.h` | WinUAE fetches the mask and every EA extension word through the logged `get_iword_mmu030c_state` and leaves only the transfers unlogged ([D] `cpuemu_34.c` op_48e8_34_ff @ Hatari e77819f7); the fork stopped logging before the d16 fetch, so a format-$B frame reported idx/idx_done $11 instead of $22 — at even and odd addresses alike, though the sweep that found it (seeds 802 #139, 803 #635, 9123 #401) only met odd ones. The RTE retry replays the extra logged word | `moira_rules_test` (MOVEM extension log: frame and retry, even and odd), `sst68030*` |
| 82 | **68030 JMP to an odd target: indexed modes stack the running pc + 2** — `execJmp` applies the 68040 rule of the same oracle mechanism to the 68030 | `MoiraExec_cpp.h` | the oracle's indexed EA (`get_disp_ea_020_mmu030`) advances the pc per extension word before the address error, so it stacks PC+6 for JMP (d8,An,Xn), PC+8 for (bd.W,An,Xn); plain modes stay PC+2. [D] WinUAE @ Hatari e77819f7; [F] MC68030UM-1989 §8.1.3/§8.2 leave this PC unstated and the oracle's two rules read as its bookkeeping (abs.L and (bd.W,An,Xn) are both six bytes). Reproducer "random/off seed=807 #681" | `moira_rules_test` (JMP odd-target PC), `sst68030*` |
| 83 | **68030 FScc stacks its pending byte unsigned** — new `MMU_RAWBUF` write flag (named `MMU_ZEXT` until row 84); FScc's memory store uses it, so the 030 data buffer (wb3_data) holds $000000FF, not $FFFFFFFF | `MoiraTypes.h`, `MoiraExecMMU_cpp.h`, `MoiraExecFPU_cpp.h` | the integer core sign-extends byte and word operands into the buffer because gencpu passes them signed; fpp.c's `fpuop_scc` passes `cc ? 0xff : 0x00` as an unsigned int, while its FMOVE .B/.W stores keep signed casts ([D] fpp.c @ Hatari e77819f7; the buffer contents are internal, [F] silicon). The last 60 000-sweep leftover ("fpu/identity seed=811 #1003") and the listed open defect `fpuraw/identity seed=722 #1890` | `moira_rules_test` (FScc write buffer), `sst68030_fpuraw` (194 known) |
| 84 | **68030 ADD/SUB to memory stack their unmasked result** — `execAddRgEa`, `execAddiEa` and `execAddqEa` store with `MMU_RAWBUF`, so a faulting store's data output buffer is the 32-bit `addsub` result | `MoiraExec_cpp.h`, `MoiraExecMMU_cpp.h`, `MoiraTypes.h` | gencpu's flag_add/flag_sub form `uae_u32 newv = (uN)dst ± (uN)src` and pass it to the store unmasked ([D] @ Hatari e77819f7; internal, [F] silicon); the fork re-sign-extended the operand-sized value. Closes the eleven SUB/SUBI vectors of `tests/sst68030_rmw.known` (now empty); a fresh rmw sweep (seeds 9301-9302, 6 000 vectors) passes, including 12 ADD faults whose raw and sign-extended buffers differ | `moira_rules_test` (ADD/SUB write buffer), `sst68030_rmw` |
| 85 | **68040 cache replacement counted on accesses** — `Cache040::allocate` only reads `rot`; reads count the half-lines they touch (I and D), copyback writes their lines, write-through writes their bus transfers, a miss counts after selecting its victim and even if its fill aborts; the interpreter's fetch window and both native generators (I: fetch words per retired instruction; D: line-hit deltas, deferred for cache-only probes) count the accesses they bypass | `MoiraCache040.h`, `MoiraExecMMU_cpp.h`, `Moira.h`, `src/jit/backends/*` | [B] MC68040UM-1993 §4.1 (counter incremented per access, rules per cache) and §7.3 (aligned transfers); [F] the victim/count order, the fork's word-granular instruction fetch and the greedy transfer split. Previously the counter advanced on replacement only. The model stays opt-in (`POM68K_040_DCACHE`). The real Quadra 605 boot exposed three more native gaps, now closed: a conditional branch's stream words differ per path (`branchFetchWords040`, also correcting the I-ATC count), a thunk's own I-side access (JSR target, program-space read) must see the instruction's stream words first (staged in `PomJitAtcReplay040::cacheFetches`, committed by `pomJitAtcReplayBegin040`, restored on replay), and a block's I-cache residency proof must include the lookahead word after its last instruction | `cache040_test` part 5 (per-rule deltas, victim choice), `jit_atc040_cases` (I/D counters compared through windows, threaded and native on A64 and x64; each native count mutation-tested), `jit_lockstep_a64_coarse_test` (counters compared every checkpoint; `POM68K_JIT_LOCKSTEP_CACHE=1` compares every line), `moira_state_test` |
| 86 | **68030 CACR clear strobes read as zero** — `cacrMask()` for M68030/M68EC030 is $3313, not $3F13: CD (bit 11) and CED (bit 10) join CI/CEI as write-only command strobes; the raw MOVEC value still reaches `didChangeCACR()` | `Moira.cpp` | [B] MC68030UM-1989 §6.3.1 (§6.3.1.3/.4: "always read as a zero"); [D] WinUAE `m68k_movec2` @ Hatari e77819f7 reads through $3313; cherry-picked deliberately from upstream Moira `f7844ac` (2026-08-25), the first upstream change taken under the fork contract. No corpus vector writes a clear bit and reads CACR back | `moira_rules_test` (CACR strobes) |
| 87 | **A fault on a JSR's target fetch belongs to the next instruction (68030/68040)** — `execJsr` and the JIT helper `pomJitJsr040` still read the target after the push (the I-cache fill, ATC accounting and queue the JIT gates pin are unchanged), but an `MmuBusError` on that read no longer aborts the retired JSR: the next instruction start refetches the target and takes the fault with the target as stacked PC. Taken inside the JSR, the 68040 format-$7 frame stacked the return address, so the handler's RTE skipped the call | `MoiraExec_cpp.h`, `MoiraExecMMU_cpp.h` | [D] WinUAE @ Hatari e77819f7, found by a fresh-seed sweep (`040 random/identity seed=3945 #676`) and 962 + 950 targeted JSR/BSR vectors (1 and 2 steps, mapped/unmapped, even/odd targets): 68040 853 → 962 / 962. the three access-log bytes of the 68030 two-step frame are ruling D36 (fork kept) | `moira_rules_test` (jsrTargetFetchFault), `jit_copyback_write_040_test` |
| 88 | **A long displacement is one entry of the 68030 access log** — `readExt<C, Long>` (the base and outer displacements of a full-format EA) logs its value once, as ABS.L and a long immediate already do, instead of two word entries. Visible only when the memory-indirect fetch faults before `computeEAdisp030` rewinds the log: the format-$B frame then held one entry too many and a wrong idx/idx_done byte | `MoiraDataflow_cpp.h` | [D] WinUAE `cpummu030.c get_disp_ea_020_mmu030` @ Hatari e77819f7 (`next_ilong_mmu030_state`); found by a fresh-seed sweep (`random/identity seed=6107 #1726`, MOVE.W ([bd.L,A2,D5.W*4],od.W),(A1)). The five pinned 68030 corpora are unchanged | `moira_rules_test` (fullEaLongDisplacementLog030) |
| 89 | **The 68040 ATC replay snapshot is copy-on-write** — `pomJitAtcReplayBegin040` saved both 64-entry ATCs (1 KB a side) at every exact native access; it now saves the access counter and the last-hit indices only, and `pomAtcCow040Save` copies a side's entries the first time something is about to change one (`pomAtcCow040Touch` in front of the two re-walk invalidations of `mmu040AtcLookup`, the fill, and the three flushes). Bits 2-3 of `PomJitAtcReplay040::active` record which sides were copied; the generated code's existing clear of `active` retires them. Host time only: `_platform_memmove` was 5.7 % of the Quadra 605 bench's steady state | `Moira.h`, `MoiraExecMMU_cpp.h` | POM68K host-time measurement (CHANGELOG 2026-11-11 (sixth)); no guest-visible claim | `moira_rules_test` (atcReplayCopyOnWrite040, through the `PomAtcProbe` friend seam) **Lifetime (2026-11-12):** Begin keeps a pointer to the replay object; an owner whose object dies (the x64 backend's is a local of `run()`) calls `pomJitAtcReplayRelease040` first. Without it a later ATC mutation wrote 1 KB through the dead stack slot — two x64 ROM gates crashed. Gate: the same cell, "a released replay object was written to". |
| 90 | **The fetch window's cache-model test is inline** — `pomJitCache040Match` answers `true` at once when the cache model is off (every product default) or the CPU is below the 68EC040, and only then calls `pomJitCache040MatchLive`, the former body. The compiler kept the whole function out of line, so every windowed fetch of every machine paid a call for that answer. Host time only | `Moira.h` | POM68K host-time measurement (CHANGELOG 2026-11-11 (eleventh)); no guest-visible claim | bench fingerprints; `cache040_test` and the cache-on lockstep gates for the live path |
| 91 | **68000 STOP, back-ported from NeoST** — `execStop` takes its operand from IRC and makes no bus access on the 68000, charging its 4 cycles itself (NeoST `2fc53a8`, `9f20046`); `stopGraceUntil` holds an interrupt the operand unmasks until STOP+8 and `execute()` takes a later one on a 4-cycle boundary counted from the STOP, advancing the clock to it (`993ce5e`, `2fc53a8`); `<cstdlib>` is included explicitly (`c9925da`). `stopGraceUntil` is serialized (`MoiraState.h`, host format v31) and cleared by reset. The 68010 and the 68020 family keep upstream's paths | `Moira.h`, `Moira.cpp`, `MoiraExec_cpp.h`, `MoiraState.h` | [B] M68000UM Rev 8 § 8.11 (STOP 4(0/0)); [D] fx68k @ 0602ee46 and Hatari @ f0736b24 as NeoST read them. [F] Hatari takes the pending-interrupt case at STOP+12 where fx68k gives STOP+8: settled by a capture. `sst68000` has no STOP vectors, so nothing else in the tree holds this. | `moira_rules_test` (stop68000) |
| 92 | **68000 reset timing, back-ported from NeoST** — `Moira::reset` spends 6 cycles, not 4, between its two prefetch reads on the 68000 (NeoST `9f20046`). Every 68000 machine's clock is two cycles later from power-on | `Moira.cpp` | [D] fx68k @ 0602ee46 as NeoST read it (microcode trap8/trap9/b); the M68000 User's Manual gives only the total, 40(6/0). Could not land before the Portable board stopped depending on host slicing (CHANGELOG 2026-11-11 (fourteenth), (fifteenth)) | `moira_rules_test` (reset68000) |
| 93 | **The FP data registers are value-initialised** — `fpu.fp[8]` had no initialiser; `fpuResetState()` writes the reset NaNs, but a core that has not been reset yet is serialized as well (the Portable and PowerBook 100 are held in reset by their Power Manager, and `PortableCpu::hardReset` defers the first `reset()` to the release edge), so a save state taken there carried 80 heap bytes. No guest can observe it: every path to an FPU instruction passes a reset | `Moira.h` | POM68K determinism defect, found by `portable_slicing_etalon` failing at frame 0 under a loaded host (CHANGELOG 2026-11-12 (seventh)); no guest-visible claim | `portable_slicing_etalon`; `moira_rules_test` (unresetFpZero) |

Rows 2-21, 25-34, 36, 38, 40, 41, 43-47, 50-54, 56, 59-75 and 79-86 are the accuracy work; rows 22-24, 35, 39, 42, 49 and 57 are seams (inert
when nothing arms them); rows 48 and 55 are performance alone, with no architectural effect. Row 58 is evidence documentation only. The ten files carrying no `POM68K` marker at all —
`MoiraDasm.h`, `MoiraDasmFPU_cpp.h`, `MoiraDasmMMU_cpp.h`, `StrWriter*` (2), `MoiraDebugger.h`, `MoiraMacros.h`,
`MoiraALU.h`, `MoiraExceptions.h`, `MoiraInit.h` — are where an upstream fix can
still be taken as-is; everything else is ported by hand, per the fork decision
above. One caveat on that list: `MoiraMacros.h` carries a **NeoST** patch
(`POLL_IPL` → `pollIpl()`, `NEOST_VENDOR.md`), so it is unmarked by the
`POM68K` grep but not pristine.

## Build configuration (`Moira/MoiraConfig.h`)

- `MOIRA_ENABLE_JIT` → **1** by default; define **0** consistently in all
  CPU/client translation units to omit the acceleration seam. The CMake
  `moira_interp` target propagates that ABI choice. The interpreter retains
  CPU/MMU/FPU execution, architectural caches/timing and the state visitor.
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

> **Annotation, 2026-11-06.** "The oracle wins" was the rule of this date
> and is superseded by the evidence grades (`CLAUDE.md`): SingleStepTests
> is grade D. The patches below stand — reverting one turns `sst68000`
> vectors red — and the two that contradict a higher-grade reading (the
> early DIVS overflow exit, ASR past the operand width) are listed as
> known exceptions in `oracle/README.md` § *Rulings taken against a
> manual*.

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
post-instruction external-FPU exceptions then stacked format $2 plus the
instruction address (pre-instruction exceptions remain format $0).
**Superseded by row 26, ruling D23a:** the 020/030 currently stack format
$3 to retain the pinned oracle vectors; this known exception to the manual
and the RTE inconsistency remain open (`TODO.md` § Moteur › Justesse
68030). On the 68020, the empty CALLM/RTM bodies are replaced by the complete 24-byte
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

Gate since 2026-11-13: `moira_rules_test`, cell
`irqDelayAfterMaskLowering` — the landing after MOVE to SR, ANDI to SR
and RTE on five models (two instructions on the 68020/030/040, none on
the 68000/010), no delay without an unmasking, and none left after reset.

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

> **Register-write correction, 2026-11-09 (row 61):** the Q8 TC/URP/SRP
> flush policy recorded below was an implementation defect. MMU register
> writes now retain the ATCs and revoke only derived fast mappings.

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
- **Q8 — Mac PACK 4 vs format $4: REMOVED 2026-11-08.** The
  `fpuDisabledSaneFline` escape hatch (stack format $0 Line-F so the guest
  PACK 4 glue accepts it) had been dormant since 2026-07-21: nothing in
  `src/` or `tests/` ever called its setter, and the bare-FPU boot it was
  written for was solved from the other end — the XPRAM `$AE`
  ROM-resource combo binds the **integer** PACK 4, so `FPUModel::NONE`
  reaches the Finder with no F-line rewriting (`q605_barefpu_boot_etalon`,
  CHANGELOG 2026-07-21 "Bare no-FPU solved"). An unbound branch in a core
  meant to be read by another host is a claim nobody holds; it went in the
  clean-up before the NeoST port (`TODO.md` § Objectif principal). Every
  040 model without an FPU takes vector 11 with format $4.
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
  `mmu040_movem`): a fault consumes the start EA into SSW.CM and the
  format-$7 EA; RTE now restores CM (not CT). Indexed/PC-relative modes
  reuse the stacked EA without another indirect pointer read; other modes
  recalculate from current registers (primary-manual ruling D34, row 74).
  All operand transfers repeat. -(An) stores base-in-list as initial − S
  with An written only at the end, and there are no 68000 guard reads.
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

## Debugger access and exception stops (2026-10-09)

The product debugger (`src/DebugCpuTarget.h`) needs three facts the bare
`didReachWatchpoint(addr)` does not carry: the access width, its direction,
and whether it is a program-space access. Every site — `read`/`write` in
`MoiraDataflow_cpp.h` and the four translated entry points of row 20 — now
calls `pomDidReachWatchpoint(addr, S, write, program)`, whose default
implementation forwards to the original delegate, so a consumer of the old
hook is unchanged. `program` is the template `AS` on the plain cores, the
function-code pins (`fcl`) in `mmuRead`/`mmuWrite` (which serve both
spaces) and `!data` on the 040 paths. The 68030 fetches its opcode words
through `mmuFetchWord`, which has no hook: opcode fetches never reach the
debugger there, by construction.

`Debugger::reset` re-armed `CHECK_BP` and `CHECK_WP` after `Moira::reset`
cleared `flags`, but not `CHECK_CP`. `catchpointMatches` is evaluated
unconditionally at the vector sites, so the stop still fired; what was lost
is the flag that takes the CPU off the JIT's `pomJitIdle()` fast path while
an exception stop is armed. `MoiraDebugger.cpp` therefore carries its first
`POM68K` marker.

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
   `pomJitMapMoved()`, see point 7 — at **fifteen** sites. Fourteen of them
   are the whole set of things that can move a logical→physical mapping:
   the four 030 ATC flushes (`mmuAtcFlushAll/FlushFc/FlushPage/FlushPageFc`),
   the three 040 ones (`mmu040AtcFlushAll/FlushNonGlobal/FlushPage`), the
   four TTR setters (`setITT0/ITT1/DTT0/DTT1`) and — since 2026-08-06 — the
   three **030 `PMOVE` register writes** (`TC`, `SRP`, `CRP` in
   `execPMove`); `setTC040`/`setURP040`/`setSRP040` reach it through
   `mmu040AtcFlushAll`, and `pomFlushAtcs()` (§ *Save-state seam*) through
   both flush-alls. The fifteenth moves no mapping: the 68040 `CINV`/`CPUSH`
   instruction-cache path (`pomCacheOp040`, reached only with the opt-in
   data-bearing cache model armed), where the instruction bytes a native
   block embedded may architecturally change. A window whose generation no
   longer matches is refused, which is the one staleness an address-range
   test cannot see.

   > **2026-11-09 correction (row 61):** the three 040 register setters
   > now call `pomJitMapMoved()` directly, retaining architectural ATC
   > entries. They no longer reach it through a flush.

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
   `pomJitJsr040(instructionPc, returnAddress, target, opcode)`
   (2026-09-03) owns the cache-active JSR's ordered push plus target read.
   The completed push makes pristine replay architecturally impossible, so
   the door never replays; a fault on the target read leaves the JSR retired
   and is taken by the next instruction start (row 87, 2026-11-11).

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

    **68030, 2026-09-04 (patch 34).** `mmuRead`/`mmuWrite` now consult the
    same two levels, so the knob is no longer inert on that family. The
    refusal set is wider there (`pomJitData030Ok`) because `mmuRead` also
    carries program-space accesses, and the counters
    `pomJitData030Hits`/`pomJitData030Refusals` exist so that "the knob is
    off" and "the path is dead" can never look the same again. **And the
    default diverges here (2026-09-05):** measured on the 68030 the window is
    −5.5 % on `threaded` and −5.7 % on the interpreter arm — the arm where
    the 68040 lost. Two differences carry it: the 030's ATC has 22 entries
    against the 040's 32 but a far larger page on an LC II, so per-entry
    coverage is much wider; and the 030's `mmuRead` is also the exact-thunk
    target from generated code, where a hit replaces a whole C++ chain rather
    than a hot MRU probe. So point 11's conclusion stands for the 68040 and
    is reversed for the 68030, per family, in `JitEngine.cpp`.

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
    (`pomJitDataR1`/`pomJitDataW1`, one `PomJitDataPage` each) carries a
    tag and a host and nothing else, so
    `pomJitDataSlow` deliberately keeps the OLD coarse rule (`if (w &&
    e.codeMask) return nullptr;`): the mask exists for **generated** code,
    and the interpreter's use of these tables is opt-in on the 68040
    (`POM68K_DATA_WINDOW`) and measured a net loss there, so widening level 0
    would be paying for a path nobody runs. (The 68030 interpreter has used
    the window by default since row 34; its level 0 keeps the same coarse
    rule.)

14. **`Moira.h` — `PomJitTiming` + `pomJitBeginTiming/EndTiming` +
    `pomJitTimingProbe`.** Per-instruction cycle attribution for the JIT
    tracer, split three ways: `baseCycles` (handler, data bus included),
    `icacheCycles` (the `PomIcache` overlay's contribution, excluded from
    the base) and `postExceptionCycles` (exception/trace work after the
    handler stopped). Read by `JitEngine.cpp:924-947` when it decides what a
    compiled block owes `pomJitSync`. The struct has since grown the traced
    instruction's `fetchWords` (the `PomIcache` fetch delta) and, for the
    68040, where its IPL polls sat relative to its data accesses
    (`iplPolls`, `dataAccesses`, `iplPollPositions`, filled by `pollIpl` and
    `mmu040Read`/`mmu040Write`).

    Opt-in on purpose: the three-way cycle split is taken only on the
    **68030** branch of `pomJitExecOne`, and every
    other probe site is one predictable `[[unlikely]]` false branch when no
    trace is armed. The 030 is the family where the i-cache overlay makes
    "how many cycles did that instruction cost" a question the clock delta
    alone cannot answer.

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

> **2026-11-09 correction (row 61):** no MMU register setter now flushes
> either ATC. Snapshot restore and debugger MMU edits explicitly use this
> helper; neither depends on the former setter side effect.

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

**Superseded by row 57 (2026-11-08):** the state inventory now lives in
`Moira::pomVisitState` (`MoiraState.h`), and `MoiraSnapshot::visitCpuCommon`
is only the POM68K adapter. The archive implementation, RAM and versioned
container stay on the host side. The visitor also carries the 030 pipe,
DF-cleared retry, suspended 6888x result, 040 FSAVE micro-state and an
armed public MOVEM latch. The initial `moira_state_test` proved the first
four across executed boundaries and only host-armed MOVEM state. Row 74
now fixes RTE continuation and adds a natural fault/RTE/re-fault/RTE/save
sequence followed by fresh-CPU restore. Dirty copyback cache lines were
still lost at row 57 (corrected next).

> **Superseded by row 64 (2026-11-09):** format v30 restores both
> data-bearing 040 caches and their mode after invalidating old derived
> state. Saving does not push dirty lines or advance guest time.

Free side effect worth knowing: both flush-alls route through
`pomJitMapMoved()` (§ *JIT seam*, points 1 and 7), so a restore also
invalidates the JIT code window and data TLB. The host must also invalidate
its code guard when RAM is replaced, so cached translated blocks are dropped
(`POM68K_SURFACE.md` § 8).

At that point nothing else in the vendored tree changed for save states;
row 57 adds the portable visitor described above.

## 68040 cache-TAG model — historical M1 (2026-08-05)

`POM68K_040_DCACHE` (env, read once in the constructor — until 2026-11-06,
row 39: host-injected through `setPomCache040()` since — default off;
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
  **Superseded for data-bearing caches by row 64 (2026-11-09):** v30
  restores their bytes, dirty masks and replacement phase after the flush.

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
(`MoiraExceptions_cpp.h:135/140`, asserted `C == Core::C68010`). Upstream
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

## Manual evidence audit (2026-11-08, row 58)

The code's manual aliases designate specific documents, not a processor
family with unspecified pagination. `tests/core_manuals.lock` owns the URLs,
editions, SHA-256 digests and one-based PDF pages for the checked locators:

| Alias | Reviewed primary document |
|---|---|
| `MC68030UM-1989` | Motorola second edition, MC68030UM/AD REV 1 (1989), complete scan hosted by Vintage Apple |
| `MC68040UM-1993` | NXP-hosted Motorola M68040 User's Manual, cover 1990, revised 1992/1993 |
| `MC68881UM-1989` | NXP-hosted MC68881/MC68882 User's Manual, second edition (1989) |
| `M68000PRM-1992` | NXP-hosted M68000 Family Programmer's Reference Manual (1992) |

This pass reviewed 116 manual-containing comment units against those PDFs.
It repaired both nonexistent references (030 §§ 9.7.2.2, 9.7.2.6, 9.7.5.5,
9.7.6) and valid section numbers pointing at the wrong subject (TT, ATC,
exception frames, PTEST/PFLUSH, rounding and FPU state/timing). The NXP
third-edition 030 parts were cross-checked: the nonexistent paragraphs
were not explained by an edition change. The complete 1989 scan avoids
mixing the split third-edition parts' different PDF pagination.

A primary citation does not bless a stronger implementation claim. The
040 ATC remains 32 fully associative entries without a supervisor tag,
against the manual's 64 four-way entries. The cache replacement counter
advances only on allocation, while hardware also counts cache accesses.
`Moira::reset` is a host cold initialization, not the 030 RESET pin's
ATC-retaining behavior. The old `Cache040::touch` policy helper does not
implement the resident-line handling required for cache-inhibited accesses;
it is not the live data-access routine. Uniform FPU format surcharges and
i-cache miss penalties remain estimates. Each such cited approximation
carries F and its missing witness rather than borrowing the adjacent B.
Locked and MOVES accesses being implicitly noncacheable are primary-backed
by 040 §§ 7.4.1 and 7.4.3. CINV/CPUSH operands being physical is confirmed by
040 § 4.2. The translator correction follows in row 59 below.

The retired Musashi oracle was a locally patched tree deleted before this
repository's history. Historical version labels cannot identify its exact
bytes: those comparisons stay F until a pinned replay exists. Unqualified
Clock Signal aliases attached to SST observations were removed; the actual
SingleStepTests revision remains their D evidence.

`core_grades_test` holds the reviewed edition/locator headers, independent
source attribution and explicit F witnesses. Its 11 checker controls include
wrong editions/locators, lost oracle revisions and mismatching PDF bytes.
The new rules reject 113 manual comment units from the pre-audit tree.
Normal CI needs no manual download; reviewers can check the original bytes:

```sh
python3 tools/core_grades.py --check
python3 tools/core_grades.py --verify-manual-files /path/to/manuals
```

The lock is an identity/locator register, not an automated reading of the
manual or proof of uncited hardware claims. `--apply` does not promote
manual references to B. This pass changes comments in eleven core files;
comparison with the pre-audit tree preserves every executable C++ token.
No CPU algorithm or hardware behavior is changed by row 58.

## Physical cache-management operands (2026-11-08, row 59)

The audit's confirmed rule now has an executed regression: CINV/CPUSH
line/page operands are physical addresses, including when paging is on
(MC68040UM-1993 § 4.2; M68000PRM-1992 CINV). `pomCacheOp040` takes An
directly. The old `pomCache040Phys` resolver and its sole-use
`mmu040PeekWalk` are gone. CPUSH still drains dirty physical data through
the host bus, which may raise a real writeback fault; only operand
translation and its descriptor accesses are removed.

`cache040_test` drives the actual opcodes with different resident physical
and translated-alias lines. Its 128 combinations cover CINV/CPUSH, all four
cache fields (including no-op), line/page scope, 4K/8K pages, cold/warm ATC
and caches enabled/disabled. It checks the selected lines, physical RAM
writes and unchanged/unread page tables. The existing CPU sequence now
also checks physical lines with invalid logical mappings and descriptor
chains that would have entered the bus-error hole. The final regression
fails 196 checks on the pre-fix core; the old gate had instead required the
incorrect translation. All-scope, privilege/decode, TTR and ordinary cache
transactions retain their existing coverage.

The earlier M1 descriptions are history: the non-faulting
logical resolver and its tests were deliberate but based on the wrong
operand rule. The debugger's independent `src/Mmu040Peek.h` remains a
logical-address peek; cache management no longer needs a copy of it.

## FC2 in the 68040 ATC tag (2026-11-09, row 60)

MC68040UM-1993 § 3.3 includes FC2 in each ATC tag, independently of the
page descriptor's supervisor-protection bit. Each entry now records that
access privilege. Lookup/fill, the hot translated path and read-only I/D
JIT probes match it. Replacing the same logical page from the other
privilege also revokes derived mappings. The tag comes from the access's
function code: MOVES SFC/DFC can differ from current SR.

`pomJitCache040Publish` additionally refuses to bind a supervisor publication
to the last user ATC entry of a MOVES store. Otherwise its cached bytes could
be correct while the native guard validated the wrong memo/MRU state.

The former standalone FC2 repro becomes the registered `mmu040_atc_test`.
It now has 44 CPU sequences: both models, both page sizes, cache on/off,
either mode first, instruction/data reads and writes, retained translations
after descriptor edits, MOVES and read-only probes. Four copyback/MOVES
sequences check the publication's ATC association. The pre-fix core fails
192 checks. Cache geometry/replacement, PFLUSH/PTEST and register-write
retention remain open; row 60 does not claim full ATC fidelity.

> **2026-11-09 follow-up:** row 61 below closes register-write retention.

## 68040 MMU register writes retain the ATCs (2026-11-09, row 61)

MC68040UM-1993 § 3.7.4 (printed p. 3-34, PDF p. 83) specifies that MMU
register reads and writes do not flush either ATC. TC/URP/SRP writes had
called `mmu040AtcFlushAll()`. They now call `pomJitMapMoved()` directly:
resident translations survive, while fetch windows, data TLBs and native
cache-line publications must be revalidated. A new root applies on an ATC
miss; guest PFLUSH makes it apply to formerly resident pages too.

`mmu040_atc_test` adds 24 executed MOVEC/PFLUSHA sequences across 68LC040
and 68040, 4K/8K pages and cache off/on. Both privilege domains have
resident I/D mappings. Two alternative roots disagree on those pages and on
fresh pages, distinguishing retention from ignoring the root write. TC
tests cover same-value writes and disable/re-enable after descriptor edits;
P is changed and restored while disabled, without assuming the translation
of an old entry at the other page size. The gate also holds generation and
fetch-window invalidation. The pre-fix core fails 192 checks; the original
44 FC2/MOVES sequences remain in the same gate.

Reset, snapshot restoration and debugger MMU edits keep their explicit
flush policy. The ATC geometry/replacement and PFLUSH/PTEST instruction
effects remain open (`TODO.md` § Moteur › Justesse 68040).

> **2026-11-09 follow-up:** row 62 closes PFLUSH/PTEST selection/refill;
> ATC geometry/replacement remains open.

## 68040 PFLUSH/PTEST select and renew ATC entries (2026-11-09, row 62)

MC68040UM-1993 §§ 3.7.2/3.7.3 and M68000PRM-1992 PFLUSH/PTEST (printed
pp. 6-35/6-36 and 6-70/6-71) distinguish page flushes from PTEST. PFLUSH
page forms select FC2 from DFC but invalidate both I/D ATCs. All-page
forms ignore DFC; N forms spare G entries. PTEST instead flushes/refills
only the DFC-selected I/D entry and privilege, even if global. The other
ATC/domain remains resident. WinUAE `cpummu.c mmu_op_real/mmu_flush_atc`
@ Hatari e77819f7 flushes both I/D ATCs for PTEST; the PRM takes precedence.
Single-instruction vectors cannot observe that difference.

The refill includes negative results: invalid descriptors and table /BERR
leave an R-clear entry. A later access must still fault if software merely
repairs the descriptor without flushing; a write may not invalidate that
negative entry to set M. A TTR hit flushes the old selected entry without
allocating another. PTEST still performs its existing U/M updates and
MMUSR reporting. Descriptor /BERR on any search read or U/M write reports
MMUSR.B alone without building an access frame. This uses the existing
side-effect-free `MmuTableSearchBusError` unwind, armed only around PTEST
on the 040; ordinary accesses retain their prior bus-fault path.

`mmu040_atc_test` adds 512 PFLUSH and 704 PTEST sequences: both qualified
040 models, 4K/8K pages, cache off/on, four defined DFC values, both access
directions, global/nonglobal pages, warm and cold positive refills, negative
entries, WP retention, read/write failures at all three descriptor levels,
and TTR hits. The old core fails 3 616 checks (192 PFLUSH cases and all
704 PTEST cases). The earlier 68 FC2/MOVES/register-retention sequences
remain. ATC geometry/replacement is still approximate; undefined DFC values
and the 68EC040 are not qualified by this gate.

## MOVEP store faults retain the instruction PC on 040 (2026-11-09, row 63)

The default 040 write funnel marked MOVEP's first byte as a last write.
Every fault then stacked PC+4 and RTE skipped the unfinished transfer.
WinUAE `generated/cpuemu_31.c op_0188_31_ff/op_01c8_31_ff` @ Hatari
e77819f7 never clears `mmu_restart` on a MOVEP output byte. The fault
dispatcher now excludes MOVEP stores from the next-PC branch, as it already
does for FPU stores. No successful-path write or other CPU family changes.

This is a correction of the fork's existing re-execution model on grade-D
evidence. MC68040UM-1993 § 8.4's generic format-$7 diagram labels the PC as
the next instruction but does not explain an incomplete MOVEP byte stream.
A real fault capture is needed to settle the hardware PC, queued writebacks
and whether earlier peripheral writes repeat. The code retains an F witness
for that gap; this change does not claim cycle/pipeline fidelity.

`moira_rules_test` adds 192 MOVEP sequences and 36 ordinary MOVE controls:
both 040 models, .W/.L loads/stores, every failed byte and successful
controls, external /BERR and 4K/8K page faults, data-window prefixes and
cache-active WT stores. It checks frame PC/SSW/FA/WB3, untouched gaps,
completed prefixes, CCR/registers, and completion after repair/RTE.
Ordinary MOVE.B/W/L stores retain next-PC semantics. The old core fails
558 checks; an independently rebuilt pinned oracle confirms 26 targeted
MOVEP/MOVE cells and the original `random/identity/8k seed=845 #1967`.
`scratchpad/2026-11-09/movep040-repro.py` regenerates that one vector without
running the preceding vectors through the oracle.

## 040 cache contents survive save/load (2026-11-09, row 64)

The tag-only M1 restore rule became destructive with M2's data-bearing
caches. RAM can lag dirty copyback data or lead a clean stale instruction
line. Pushing dirty lines on save would change RAM, bus effects and guest
time, and still would not preserve stale I-cache instructions.

`Cache040::visit` archives replacement phase and every valid line's tag,
dirty mask and 16 bytes, omitting invalid garbage and diagnostic counters.
`Moira::pomVisitState` appends both caches and `pomCache040On` on EC/LC/full
040, including retained disabled lines. The existing load seam first
invalidates old ATCs, JIT windows/TLBs and cache lines; the appended payload
then restores the architectural caches. Saving does not change them, RAM
or the clock. The host bumps its strict format to v30; v29 is refused
before any mutation. CPU/FPU and timing configuration still must match.

The initial six `moira_state_test` regressions failed 24 checks before the
fix: 68LC040/68040, active/CACR-disabled/model-disabled, stale instruction
fetch, dirty cross-longword data, nonzero replacement phase and the next
dirty eviction. A separate case excludes invalid garbage and counters.
`savestate_040_test` checks full containers on Q605, Centris, Q700, Q900 and
Q630 in interpreter and JIT modes, exact continued execution, rollback of
a truncated CPU payload over different live dirty data, and v29 refusal.
This fixes software persistence without asserting new hardware fidelity.

## MOVES and locked operands resolve cached data before bypass (2026-11-09, row 65)

The initial 630 executed sequences fail 1 351 checks: an enabled dirty
copyback line does not reach memory before MOVES/TAS/CAS, clean stale lines
remain valid, and CAS2 is not marked locked at all. MC68040UM-1993 §§7.4.1
and 7.4.3 make these operands implicitly noncacheable; §7.4.5 requires the
RMW to begin after dirty data is pushed and clean data invalidated.

`pomCache040Touch` now inhibits through CM instead of clearing the CACR
activation marker. This reaches the existing resident-line resolution path
in `pomCache040ReadValue/WriteValue`. CAS2 arms `mmu040Lrmw` before either
operand read, sharing TAS/CAS's write-permission and SSW behavior even in
cacheless mode. The following ordinary instruction sets its own attributes.

`cache040_test` executes EC/LC/full 040, transparent and 4K/8K aliases,
missing/clean/dirty lines, MOVES.B/W/L both ways at SFC/DFC 1/2/5/6, TAS and
successful CAS.B/W/L and CAS2.W/L. Bus traces require two dirty longwords
pushed before exact external operand reads/writes, current data and dirty
neighbours, invalidation without allocation, then normal cache service.
Three ordinary CACR-off/on controls retain dirty bytes without a push;
sixteen CAS2 protection cases fault on either operand before any store,
with both page sizes and cache-model modes. Failed-comparison terminal
writes, the disabled-cache locked-hit quirk and dirty-push fault frames
remain open. No bus-lock waveform or pipeline claim follows from these tests.

> **Follow-up, row 66:** failed-comparison terminal transfers are implemented
> and tested below; CAS2's precise write-fault pipeline ordering still needs
> hardware evidence.

## Failed CAS/CAS2 still terminate with a write on 040 (2026-11-09, row 66)

MC68040UM-1993 §7.4.5 requires one final write when comparison fails:
CAS writes its read operand back, CAS2 writes only the second read operand,
even if the first comparison failed. The old fork emits neither transfer.
Memory usually stays identical, so a RAM-only single-instruction vector
cannot observe this omission; MMIO and write faults can.

The failed branches now use `writeM` before committing compare registers,
only on EC/LC/full 040. CAS matches WinUAE `cpuemu_31.c op_0ad0/op_0cd0/
op_0ed0` @ Hatari e77819f7, including its final-write/next-PC policy.
WinUAE's `gencpu.c i_CAS2` omits the required transfer; D31 follows the
primary manual. CAS2 uses the same final-write path by analogy: its precise
register-commit/frame-PC ordering on /BERR is F at the site until a real
040 capture exists. The bus transfer itself is B, not an inferred rule.

The initial **1 814 sequences fail 3 752 checks**; all 78 68020/68030
controls already pass and keep no terminal write. The 1 736 040 sequences
cover supervisor/user, flat or 4K/8K aliases, cache off or absent/clean/dirty
lines, CAS.B/W/L with (An)/(An)+/-(An), CAS2.W/L with either mismatch,
and /BERR on each host beat of the final write. Exact bus traces hold
original read data rather than update-register data, ordering, width and
preservation of dirty neighbours. Fault cases pin the chosen format-$7
policy (LK, write size/FC, logical FA/WB3 address, read value in WB3,
next PC, deferred Dc commit), then complete WB3 and return through RTE
without repeating operand reads. No LOCK/LOCKE waveform claim follows.

## MOVE16 fault context and return from a successful push (2026-11-09, row 67)

MC68040UM-1993 §§8.4.6.2/8.4.6.4 specify the original transfer size and
initial logical FA for non-push accesses. MOVE16 used to inherit whatever
instruction-fetch stamp preceded it: a physical bus error looked like a
program word/longword read, even on a destination write. It also left
`mmu040EffAddr` stale, so the existing WB2 model could point at an unrelated
address on a translated destination fault.

`mmu040GetMove16/PutMove16` now establish all five bus-stamp fields and the
aligned logical EA before translation, preserving that line descriptor
through all eight host-word callbacks. Source and destination get separate
descriptors. A successful `pomCache040Writeback` now restores the saved
address/value/size/direction/space before the caller's operand transfer;
a fault inside the push keeps its previous path and is explicitly F for
the missing physical-TM/WB1A/PD contract.

The initial **3 810 MOVE16 sequences plus 70 post-push read faults fail
5 230 checks**. MOVE16 covers EC/LC/full 040, supervisor/user, flat/4K/8K
aliases, all five forms with unaligned cursors, ordinary fetch/window/cache
miss paths, every source/destination host beat, and source/destination MMU
faults. Traces hold the completed prefix; frames hold data FC, read/write,
line size, initial logical FA, destination WB2 address and complete PD0-3.
The window variant proves it skips the instruction-fetch bus. Repair,
PFLUSHA where supported, RTE and retry on safe RAM preserve the copy and
postincrement behavior. The 70 MOVES.B/W/L/TAS cases push two dirty
longwords successfully before faulting the operand read and retain the
original logical descriptor without a spurious pending push writeback.

This preserves the existing WinUAE-derived WB2/restart model, not a full
silicon pipeline or peripheral-safe replay proof. Cache-fill fault context,
faults inside dirty pushes and MOVE16 resident-line coherency still need
separate gates; the cache-on MOVE16 matrix exercises misses only.

## Read fills keep the original descriptor and distinguish unused beats (2026-11-09, row 68)

The initial **4 872 data-read and 224 instruction-fetch sequences fail
7 936 checks**. Every fill beat used to replace the caller's stamp with a
physical longword address/size, every /BERR raised immediately, and dirty
victims were pushed before a replacement read that might abort.

A read fill now probes host words with a private size sentinel and catches
its private bus-abort signal before `mmu040Fault` changes any architectural
fault or continuation state. If the failed longword intersects the requested
chunk, the original logical address, size and I/D space construct the read
fault. Otherwise, the completed requested bytes return through a caller-owned
line buffer, the attempted cache line stays invalid, and no guest exception
is raised. The private signal never escapes the controller. No new CPU
member or snapshot field is needed; the sentinel is scoped to bus probes.

Read replacement now fills before pushing a dirty victim, as §4.6.2 specifies.
An aborted read restores the dirty victim without first modifying its RAM;
a clean victim remains discarded. Copying the completed bytes into the
caller's buffer before restoring a victim prevents the returned pointer
from exposing victim bytes. An unused fill error also cannot publish a valid
native line. Existing fixed fill-latency charges and replacement policy stay.

Data witnesses cover byte/word/long reads, within-line and cross-line
misalignment, wrapped first-longword positions, either host half, absent/
clean/dirty victims, WT/CB mappings, both privileges and flat/4K/8K aliases.
Instruction witnesses hold demanded logical program-word faults and unused
beats for both words of the fork's lookahead. Frames, exact transfer order,
RAM/victim state, requested values and safe-RAM repair/RTE retries are pinned.
The primary locators are §§4.6.1/4.6.2/7.6.1 (PDF pp. 95/97/179) and the
already reviewed §§8.4.6.2/8.4.6.4.

Row 69 subsequently carries the pending store for `readBytes=0`; its
unused-beat delivery remains F. Row 70 subsequently builds push-fault PD
frames and retains the new line after a successful replacement read. The instruction lookahead/deferred-prefetch
pipeline is not upgraded by these fill-buffer rules. No pin timing claim.

## Write-allocate fill errors preserve their pending store (2026-11-09, row 69)

The initial **14 616 MOVE store sequences fail 35 112 checks**: 13 104
reported faults lose their pending store and original frame descriptor,
while dirty victims are pushed before a replacement read can fail.

Write-allocate reads now use the same private host-bus probe as ordinary
fills. The logical store stamp survives every successful beat and any
successful victim push. On a reported fill-read error, the controller
restores the dirty victim, constructs the read SSW with the original
logical address and B/W/L size, then exports the outstanding store in WB3
with its register-aligned value. WB1/WB2 remain invalid. §8.4.6/Table 8-6
permits this read-error/pending-write frame (reviewed PDF p. 244). Choosing
WB3 for this synchronous store remains an F pipeline decision until a
real 040 write-miss fault frame is captured. Generic final-store PC/CCR/EA
handling remains the existing fork policy.

Replacement reads precede dirty pushes for writes too (§4.6.2). A failed
read cannot prematurely update victim RAM; successful fills no longer leave
a physical read stamp over the store. The regression covers B/W/L,
`(An)`, `(An)+`, `-(An)`, cross-line writes, both host halves, missing/clean/
dirty victims, both privileges and flat/4K/8K aliases. It completes the
actual stacked WB3 value in software, then executes RTE and the next NOP.
Bus order, cache-visible bytes, untouched neighbours, committed flags and
single EA adjustment are held together. Safe RAM is the completion target.

The controller still conservatively reports every failed write-allocation
beat. Suppressing unused beats requires a separate pending-store completion
policy when no valid line was allocated; the read scratch-buffer shortcut
cannot preserve that store. That unresolved delivery policy remains a TODO;
row 70 subsequently supplies push PD frames and new-line retention. No snapshot
member, format, timing table or native admission change is involved.

## Push faults export a physical frame and keep a completed replacement (2026-11-09, row 70)

The initial **5 733 sequences fail 15 848 checks**. Each push beat was
stamped as an ordinary FC=1/5 write: FA and WB3A shared a physical address,
WB1A and the full PD image were absent, and an outstanding logical store
was replaced by the victim's data. A failed push rolled back a successful
replacement read; explicit CPUSH incorrectly kept its faulted line valid.
Only dirty longwords were transferred even when §4.6.2 requires a line.

A private push probe catches /BERR before constructing a generic write
fault. The ordinary caller establishes its existing continuation, LK and
pending-store state; the controller then replaces the bus transaction with
write/TM=0/TT=0, longword or line size, physical FA=WB1A and all four PD
longwords. WB1 remains invalid as Table 8-6 requires. WB3A has its own
fault-to-frame scratch field, distinct from a physical push FA. This field
never carries state past an instruction boundary; reset clears it and the
portable snapshot payload/version stay unchanged.

One dirty longword transfers that longword; two or more transfer the full
line, including clean longwords and their bus/timing charges. A completed
replacement stays valid after a subsequent push failure (§7.6.1, PDF p. 180).
The drain helpers invalidate selected lines before calling the bus sink,
while a local copy retains the victim image. A failed explicit CPUSH is
therefore invalidated, with its data exported in PD. The reviewed frame
locators add §§8.4.6.5/8.4.6.6 (PDF pp. 239/240).

The expanded **12 285 sequences** cover CPUSH line/page/all, replacement
reads/B/W/L stores, NC reads/B/W/L stores and locked TAS, all host beats,
each single-dirty position and multi-dirty masks, both privileges and
flat/4K/8K aliases. Software consumes actual stacked PD first, then WB3
through architectural memory so the retained cache line also sees the
completed store. RTE must not repeat a completed push/fill or an EA update.
Earlier coherency matrices now expect full-line multi-dirty transfers.

This preserves the synchronous model's ordinary-store WB3 assignment; it
does not add asynchronous WB2/WB3 or pending MOVE16 stages. NC-hit push
fault invalidation uses the same drain policy, while §7.6.1 explicitly
names replacement and CPUSH: a hardware NC capture remains the witness.
Row 71 subsequently qualifies ordinary data/fill page-split MA. Unused
write-fill delivery remains open. No pin-waveform claim or native-admission change.

## Split-page MA distinguishes ATC faults from external data errors (2026-11-09, row 71)

The initial **8 592 sequences fail 2 016 frame checks**, all on physical
later-fragment faults. The old builder restored the initial logical FA
and set MA in the same unconditional block. MC68040UM-1993 §8.4.6.2
(PDF p. 238) reserves MA for an ATC fault on the second page; §8.4.6.4
(PDF p. 239) independently keeps the first byte's logical address for all
misaligned faults.

The builder still restores FA on every later fragment. Only the MA bit
now requires `atc`. This deliberately differs from WinUAE `cpummu.c`
`misalignednotfirst` @ Hatari e77819f7, which unconditionally sets MA on
later-fragment exceptions (D32). No oracle patch, corpus exemption or
regenerated vector hides that distinction.

The matrix covers EC040 flat controls and LC/full 040 flat/4K/8K aliases,
user/supervisor, .W/.L reads/stores, `(An)`, `(An)+`, `-(An)`, aligned
first-page controls and every crossing alignment. Physical callbacks fault
each requested host transfer, or either host half of a demanded fill
longword. Nonresident, write-protected and supervisor-only descriptors
supply first-/second-page ATC controls. Cache-off, active NC, active WT
read fills and active CB write allocation reach the same frame builder.
Exact bus prefixes, trap origins, SSW/FA/WB3, already committed store
fragments, register/CCR state and single EA updates are checked together.
Software completes full pending WB3 on safe RAM, then RTE advances a store
or retries a read; operand bytes and neighbours survive.

This does not change the existing final-store/restart policy or pending
WB3 stage assignment, does not add a CPU member or snapshot field, and
leaves cache timing/replacement/native admission unchanged. Row 72
subsequently qualifies descriptor-bus errors during ordinary walks; this matrix's ATC controls use descriptor validity/protection. Unused write-fill delivery and asynchronous push buffers
remain separate TODOs.

## Ordinary descriptor errors fault the access, with deferred U history (2026-11-09, row 72)

The initial **13 632 cases fail 30 144 checks**: 7 344 ordinary faults
lack the original-access ATC/MA outcome, 7 344 lose their R=0 translation
after repair without a flush, 11 808 table traces use eager U writes, and
3 648 history images include a U update before a failed next read. The
baseline uses MMUSR's defined R/M fields; the descriptor's U bit is not
reported in MMUSR.

Ordinary `mmu040Translate` now scopes `mmuInTableSearch` around the raw
walk. The private descriptor-bus signal unwinds without changing operand
metadata. The catch clears that context, creates an R=0 entry in the
selected I/D/privilege domain, and calls the normal ATC fault builder with
the original address, value, size, direction and FC. PTEST retains its
existing B-only result and creates no exception. Unexpected exceptions
also clear the context. A subsequent TTR-served physical /BERR before any
other walk proves there is no leaked table-search mode.

MC68040UM-1993 Figure 3-10 (PDF p. 60) schedules a root/pointer U write
after the next descriptor read completes. The walker now follows that
order, retaining the previous descriptor/address explicitly. This is D33's
primary-manual ruling over WinUAE's eager writes @ Hatari e77819f7.
`mmuRead32` also sequences its high and low host words explicitly; a
first-half exception cannot touch the second. That shared helper also
serves the 030, whose existing gates remain applicable.

The expanded **14 112 sequences** cover LC/full 040, 4K/8K pages, both
privileges, cache-off/NC/WT-read/CB-write paths, B/W/L data accesses, actual
program fetches, PTEST R/W in data/code domains, direct/indirect pages and
either host half of every descriptor read and U/M write. First-page and
post-split faults are distinguished, with 480 additional M re-walk cases
starting from a resident unmodified target entry. Checks hold exact table
prefixes, partial history, target suppression, SSW/FA/WB3 or MMUSR, negative
translation lifetime, cleanup and safe-RAM repair/PFLUSH/WB3/RTE together.

The regression's diagnostic physical-fault frame is discarded explicitly;
the original stacked frame drives the actual repair/RTE. The ordinary
pending-store stage/restart policy is unchanged. Descriptor LOCK/RMW pin
waveforms are not newly modeled. Row 73 subsequently holds ordinary
validity/protection failure retention. No CPU member, snapshot payload,
cache replacement rule or native admission changes.

## Ordinary validity/protection failures retain their ATC attributes (2026-11-09, row 73)

The initial **8 192 sequences fail 33 488 checks**, including cascading
repair/recovery consequences. Initial frames and positive M controls pass.
The cold walker validated the returned R/W/S bits before creating an entry,
so repairing a descriptor immediately removed the failure or changed its
physical mapping. A second defect appears even with a PTEST-created entry:
a user write to S=1, M=0 discards the entry to set M before checking S.

MC68040UM-1993 §§3.3/3.5 require the created entry to supply attributes on
retry, and permission denial to precede an M-bit re-walk. Normal walks now
publish their status before testing validity/protection. Root/pointer/page
invalidity and invalid/double-indirect pages create R=0; protected resident
pages keep their PA, R and W/S fields. Both the last-entry and scanned
lookup paths now retain S denial instead of treating it as an allowed
M=0 write. Valid, permitted M=0 writes still re-walk; modified writes retain
their cached mapping. The pinned oracle already follows this ordering, so
no new ruling or oracle patch is required.

`mmu040_atc_test --failure-retention` covers LC/full 040, 4K/8K pages,
cache-off/NC/WT/CB modes, both privileges, B/W/L reads/stores, code fetches,
PTEST R/W data/code and locked TAS. Root/pointer/page WP, S with M=0/M=1,
all invalid levels and indirect cases are repaired to a different PA.
A same-domain detour selects the scan path; the opposite FC2 domain sees
its own repaired translation. No table cycles or operand stores may bypass
retained denials. WP reads keep their original readable PA. Positive controls
distinguish legitimate M=0 re-walks from modified writes. PFLUSH then exposes
the repaired mapping, with software WB3/RTE or read/TAS/code restart on safe
RAM and a single EA update. The registered gate still runs all older ATC
cases as well as this matrix.

This changes neither existing fault-frame/pending-store stage policy nor
snapshot payload, table history order, cache replacement or native admission.
No corpus exception or regenerated vector. Hardware capture of the existing
pipeline choices and descriptor LOCK/RMW waveforms remains separate.

## MOVEM resumes from a guest-produced CM frame (2026-11-09, row 74)

The initial 4 848-sequence matrix fails 5 952 checks. Fault creation already
saves EA and sets SSW.CM, but RTE discarded both. Merely restoring the latch
would still repeat full-index memory indirection before overriding its result,
and reuse EA for modes outside the manual’s explicit list.

MC68040UM-1993 §8.4.6.2 (PDF p. 238) restricts saved-EA reuse to indexed An
and PC-relative modes; §8.4.6.7 (PDF p. 240) restores continuation and repeats
all operand transfers, including the completed prefix. Successful format-$7
unstack now publishes its saved EA/CM. MOVEM uses the stacked EA only in
those modes; its full-EA decoder consumes extensions without rereading the
pointer. Other modes use current An. Source register values are current,
predecrement’s base-in-list value remains initial − size, and completion
clears the latch. A subsequent fault consumes it into CM again.

`moira_rules_test --movem-restart` covers EC/LC/full 040, W/L, user/supervisor,
cache-off/active NC, physical errors and 4K/8K page faults. AI, displacement,
brief/full indexed, PC-relative, postincrement and predecrement controls
change An/index/pointers, clear CM or edit stacked EA. The witness checks
frame provenance, exact completed prefixes, no repeated indirection, all
replayed operands, source values, sign extension, flags and final An.
`moira_state_test` adds a naturally faulted/refaulted MOVEM, RTE and fresh-CPU
restore at the live continuation boundary. The JIT gate retains a trained
native MOVEM block across interpreter fault/RTE boundaries, resolves RTE’s
known-empty IRQ-check request, and checks the existing latch guard and
threaded replay under both CCR layouts. Fault setup uses instruction counts
because the coarse model may charge zero cycles before the fault. No backend
admission changes. Both AArch64 and x86-64/Rosetta execute the focused
continuation witness; the full asset-free JIT executable also passes on
x86-64/Rosetta. No x86 hardware performance claim.

D34 records the manual’s mode restriction over WinUAE’s blanket saved-EA
reuse. The pinned oracle already restores CM and skips indexed indirection;
it is not patched. No corpus exception, new CPU field, archive-version change,
cache policy or cycle calibration is introduced.

<a id="atc040-sets-access-phase"></a>
## 68040 ATCs use four-way sets and commit access effects (2026-11-09, row 75)

MC68040UM-1993 §3.3 (PDF pp. 75–78) defines separate 64-entry I/D ATCs,
sixteen four-way sets, FC2 tags, LA15:12/LA16:13 indexing for 4K/8K pages,
invalid-way priority and an access-driven two-bit replacement counter.
The former 32-entry fully associative pseudo-LRU policy supplied none of
that geometry. The new policy selects within the indexed set; last-hit memos
remain lookup accelerators. PFLUSH invalidates entries without resetting the
model's phase. Reset and snapshot restore clear this derived state; the
guest archive format and payload are unchanged.

The access path includes interpreter-owned code windows, engine code/data
windows, native DTLBs and native architectural-cache hits. Publications carry
ATC provenance. The tracer records actual 040 instruction-stream word counts,
separately from 030 timing; native code consults live window provenance so
unchanged physical blocks can survive TC/TTR changes. Semantic access slots
carry deferred operand weights, including MOVEM spans and optional bitfield
tails. Retirement commits them. Exact thunks debit preceding operands and
their own real access; an abandoned attempt restores both private ATCs,
counters and memos, then revokes derived publications before Moira replay.
Descriptor U/M writes are not undone. This establishes architectural
boundaries, not an exact external bus waveform for speculative faults.

The counter proof also exposed an existing cacheless native JSR omission:
it skipped the interpreter's target-word read and left the wrong IRC. Both
emitters now use the ordered push/target-read helper for ordinary 040 JSR
with either cache mode. A64 full-indirect JSR retains its existing preflight
transaction. Native cache entries keep their 32-byte stride without an MRU
pointer or memo guard; their selected ATC index remains diagnostic. Data-TLB
entries retain their 16-byte stride with a 16-bit code mask and provenance
byte. The architectural cache and interpreter data window retain their
existing opt-in defaults.

`mmu040_atc_test` adds 768 geometry and 2,688 relative-phase sequences.
Read-only probes inspect all 64 mappings, set conflicts, FC2 isolation,
victims and repaired descriptors. Hits in another set and cached denials
advance the selected counter; probes, transparent/disabled translation and
the opposite ATC do not in this model. Existing PFLUSH cases also check phase
retention. `jit_asset_free_lockstep_test` adds 432 access/fault/TC-TTR cases
and 1,152 victim/U-bit cases under native, threaded and interpreter windows.
It requires actual native cache read/write hits, rather than block entry
alone. Pure-RAM cases exercise the optional positioned-poll admission while
the rich loop retains the production fallback arm. The tests train cache
residency before compilation, resolve generation admission, then publish
the measured hit; the old-policy write memo is selected after conflict fills
so the identical before/after fixture reaches its native hit on both models.
The isolated 32-entry baseline fails 98,256 geometry checks, 6,240 relative-
phase checks and all 1,152 pipeline victim scenarios. The identical fixtures
pass on the four-way model. Its 432 access comparisons also pass before the
policy change, isolating accounting from victim selection.

Grade F remains explicit: independent zero-seeded phases, exact debit edges
and PTEST installation phase have no silicon capture. TC/TTR reuse tests keep
TC.P fixed; mixed-size entries without the manual's advised flush sequence
are not established by these witnesses. No oracle patch or corpus exception.

## Model support in this copy (`MoiraTypes.h`)

- 68000 / 68010 — cycle-exact execution ✓ (Mac Plus phase)
- 68EC020 / 68020 — functional execution ✓, including CALLM/RTM module
  frames and optional CPU-space access-control transactions.
- 68EC030 — the plain-020 execution path, without PMMU translation,
  i-cache timing overlay or code-window admission. `moira_rules_test`
  holds that basic contract against a faulting 68030 control; there is no
  instruction corpus for this model (`POM68K_SURFACE.md` § 1.2).
- 68030 — functional execution of the **MMU instruction set** ✓ (O4
  slice 1) and of **bus-level address translation + ATC + bus-fault
  frames** ✓ (O4 slice 3, see above), plus 6888x format-$3
  post-instruction exception frames (row 26, ruling D23).
- 68881/68882 — **execution ✓** when attached via `setFPUModel()` (O5
  slice 2, see above; softfloat-backed, 68882 = LC II PDS FPU).
- 68LC040 / 68EC040 / 68040 — **integer execution ✓** (Q2: MOVE16,
  CINV/CPUSH, MOVEC 040 set, format $2 address errors, 040 trace and
  undefined-CCR rules), **no-FPU F-line ✓** (Q4, format $4) and the
  **040 MMU bus translation ✓** (Q3, section above: TTR + URP/SRP walk
  with U/M, page-split accesses, format $7 faults with the last-write
  dichotomy, MOVEM restart, PTEST), with **64-entry, sixteen-set/four-way
  I/D ATCs ✓** (row 75), and an **integrated FPU ✓** (native sparse
  opmodes, FPSP traps, revision-$41 FSAVE/FRESTORE frames and BUSY resume).

  The architectural cache model implements data-bearing transactions:
  physically indexed/tagged I/D line data, WT/CB/NC policy, requested-
  longword-first four-beat fills, dirty replacement/CPUSH, CINV, MOVE16
  bypass, alternate-master snooping and configurable hit/fill/push charges.
  `POM68K_040_DCACHE=1` opts in. While armed, ordinary JIT page-to-RAM data
  windows are fenced; a code block's whole embedded range must be resident
  and byte-identical in I-cache, and direct links cannot bypass the next
  residency guard. Native sole-access cache-line reads and writes have
  their own admission/guard contract (`src/jit/POM68K_JIT.md` § 8,
  `POM68K_SURFACE.md` § 9). Architectural gaps remain: exact 040 ATC
  replacement phase/coupling, cache replacement-counter cadence, fault stamps
  during fills/writebacks
  (`TODO.md` § Moteur › Justesse 68040). Pin-level BCLK/TA waveform
  simulation is also outside scope; the existing transaction gates do not
  establish full cache fidelity. The 68EC040 currently takes this full
  paging path despite `hasMMU()` being false; it has no profile/corpus and
  must not be treated as a hardware-accurate EC040 (`TODO.md` § Moteur ›
  Surface).

  The 8K-page (TC.P) cell — `mmu040PageMaskI()` and the walk's
  `if (reg.tc040 & 0x4000)` branch, with the ATC following suit — is
  exercised since 2026-11-06 by `sst68040_8k` (4 000 vectors, every
  `core`, `mmu` and `fault` cell at 100 %). Mac OS uses 4K throughout.

## 2026-10-18 — MOVES bus faults release SFC/DFC

`processException` clears `fcSource` before entering a 020/030/040 bus-error
handler. The in-flight access already records the fault's FC, so its SSW
still reports that FC; stacking and handler execution use normal spaces.
The V8 wrapper now faults FC=7 coprocessor-1 accesses when the 68882 socket
is empty. Previously its MOVES probe read RAM at `$22000`, which hid this
exception-path bug and made the LC II ROM claim an FPU was fitted.
`v8_ramsize` executes the probe under 020 and 030, with/without FPU and
SFC=5/7, checking both the stacked FC and the handler's released FC.

The CIR-select reference is MAME 0.287's
[`m68ki_read_16_fc`](https://github.com/mamedev/mame/blob/mame0287/src/devices/cpu/m68000/m68kcpu.h):
FC=7 plus coprocessor-1 addresses selects the 6888x interface separately
from program/data RAM.
