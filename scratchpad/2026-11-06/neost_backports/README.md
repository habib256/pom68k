> **Archived copy (2026-11-06).** The `patches/`, `raw/` and `tools/` folders this
> README lists were working files of the session and are not kept: the group
> reports carry the complete OLD/NEW edit lists. Groups 5, 6 and 7 are applied
> (POM68K_VENDOR.md row 40); groups 1, 2, 3, 4 and 8 are open in
> `TODO.md § Moteur › Convergence`.

# NeoST → POM68K Moira back-ports — audit of the eight remaining groups

Date 2026-11-06. NeoST at `fd5ad5c`, POM68K at `529c353` + the uncommitted working tree of branch
`refactor/core-no-getenv` (its edits to `Moira.cpp`, `Moira.h`, `MoiraExecMMU_cpp.h` are the base
every OLD block was checked against). Read-only on both repositories; nothing was built or run
except Python over git output and over the local SingleStepTests JSON
(`tests/data/sst68000/`, 124 files, present on this host).

**Completeness check.** `git -C neost diff 47d6c39 HEAD -- extern/moira/Moira` (the copy point)
contains exactly the eight groups below, row 38's DIVS fix (`74b2f2d`, already ported) and the
`[EXC] pc=` diag field (`7672aea`) — no other NeoST Moira change exists since the copy.

## The eight groups

| # | Group | NeoST commit(s) | NeoST files touched (`Moira/`) | POM68K conflict | Predicted `sst68000` effect | Risk | Order |
|---|---|---|---|---|---|---|---|
| 1 | `illegalOpcodeHook` | `fdbce2e` 2026-10-01 | `Moira.h`, `MoiraExec_cpp.h` | No (code identical to the copy) | (i) green, by construction (default `false`) | Low | 1st |
| 2 | `willProcessFault` | `ce1cc52` 2026-10-19 | `Moira.cpp`, `Moira.h` | Minor: insertion point moved by row 25 (CALLM hooks); `processException<C>` gained 030/040 `MmuBusError` and FPU blocks above the edited lines; MMU faults not reported | (i) green, by construction | Low | 2nd |
| 3 | `willAccessBus`/`didAccessBus`, long split `SYNC(2)`+hooks+`SYNC(2)` | `ce1cc52` | `Moira.h`, `MoiraDataflow_cpp.h` | **Yes**: `read<>`/`write<>` carry POM68K's 030/040 MMU early returns, `pomDidReachWatchpoint`, plain-020 access recording; **JIT window `pomJitFetch000` bypasses `read<>`** (hooks mirrored there); per-access virtual = hot path → `MOIRA_BUS_CYCLE_HOOKS` switch, default OFF | (i) green: OFF = identical code; ON = `SYNC(2)+SYNC(2)` ≡ `SYNC(4)` on the test bus | Medium | 7th, behind the switch |
| 4 | `setBusLock` around 68000 TAS | `ce1cc52` | `Moira.h`, `MoiraExec_cpp.h` | No (68000 branch identical; POM68K changed only the 030/040 `else`) | (i) green, by construction | Low | 3rd |
| 5 | MOVE to SR: `fullPrefetch<POLL>` on 68000 | `ce1cc52` | `MoiraExec_cpp.h` | No (identical) | (i) green: same cycles; IRD/IRC identical (4 938/4 938 sequential vectors checked); transaction addresses change **towards** the corpus | Low | 4th |
| 6 | ANDI/ORI/EORI #,SR / #,CCR, MOVE to CCR: double reload | `d8d33bb` 2026-10-20 | `MoiraExec_cpp.h` | No (identical) | (i) green: same cycles; IRD/IRC identical (53 348/53 348 checked); corpus transactions now matched | Low | 5th |
| 7 | DIVU/DIVS internal cycles before the prefetch (68000) | `262f63a` 2026-10-20 (= upstream `f90fc50`) | `MoiraExec_cpp.h` | **Yes (a)**: DIVS early overflow exit 16/18 cycles (row 2) and row 38's "`cyclesDiv` unreached on overflow" — resolved by computing `divsMoira` first; DIVU verbatim | (i) green: totals unchanged; order now matches all 9 955 sequential DIV vectors | Low-medium (whole-machine wait-state drift on compacts) | 6th |
| 8 | STOP+8: `stopGraceUntil`, quantised wake, STOP 4(0/0) from IRC | `993ce5e` + `2fc53a8` 2026-10-22 | `Moira.h`, `Moira.cpp`, `MoiraExec_cpp.h` | **Yes (b)**: CHECK_IRQ block carries POM68K's `irqDelay`; NeoST's quantum would also hit 020/030/040 STOPs (guarded); new snapshot field | (i) green **by absence** — no STOP.json in the corpus | High | last — **hold** |
| — | `[EXC]` diag `pc=%x` | `7672aea` 2026-08-07 | `MoiraExceptions_cpp.h` | POM68K lacks the field (`MoiraExceptions_cpp.h` l.862); it is a `NEOST_EXC_DIAG` getenv diag slated for removal (row 39, TODO § Moteur › Surface) | none (stderr only) | — | no patch |

Recommendations per group: 1, 2, 4, 5, 6 apply as-is; 7 apply the POM68K variant (not NeoST's text);
3 apply only behind `MOIRA_BUS_CYCLE_HOOKS` (default false) and only if a consumer is planned;
8 hold for arbitration (MAME microcode-derived 68000 or fx68k simulation of `STOP #$2300` with a
pending level-4 IRQ), plus a new `moira_rules_test` section and the snapshot field.

What `sst68000` compares (`tests/sst68000.cpp`): D0-D7, A0-A6, USP, SSP, SR, PC (`getPC0`), IRD,
IRC, every final RAM cell, and the total cycle `length`. It **does not compare bus transactions**:
`parseVector` skips the `transactions` array. Groups 5, 6 and 7 change only transaction order or
addresses, which is why the gate is blind to them — and why the corpus itself, read with
`tools/tail.py`, is the arbiter: in every non-exception vector it shows NeoST's order.

## Files in this directory

| File | Content |
|---|---|
| `group1_illegal_opcode_hook.md` … `group8_stop_plus_8.md` | per-group report: NeoST hunks + rationale, POM68K target and conflicts, OLD/NEW edit list, `sst68000` prediction, other gates, risk |
| `patches/groupN.patch`, `patches/all_groups_in_recommended_order.patch` | the same edits as unified diffs against the current POM68K tree (`git apply` from the repo root) |
| `raw/<commit>.diff` | full `git show` of each NeoST commit restricted to `extern/moira/` |
| `tools/edits.py` | the edit lists (single source for the .md OLD/NEW blocks and the patches) |
| `tools/verify.py` | asserts each OLD is unique in POM68K's current files; applies each group alone and all cumulatively — ALL OK |
| `tools/tail.py`, `tools/refetch_check.py` | SST corpus analysis (transaction tails; IRD/IRC consistency of a re-fetch tail) |
| `tools/fncmp.py` | compares each touched function in POM68K against NeoST at the copy point `47d6c39` |
| `tools/gen.py`, `tools/mkpatches.py` | generators |

## `diff -rq` between the two `Moira/` directories

`diff -rq /home/gistarcade/src/neost/extern/moira/Moira /home/gistarcade/src/pom68k/extern/moira/Moira`
(line counts = lines `<` NeoST-only / `>` POM68K-only in `diff` of each pair):

| File | NeoST-only lines | POM68K-only lines |
|---|---:|---:|
| `Moira.cpp` | 30 | 385 |
| `Moira.h` | 31 | 1 636 |
| `MoiraALU_cpp.h` | 13 | 77 |
| `MoiraCache040.h` | — | 227 (POM68K-only file) |
| `MoiraConfig.h` | 3 | 3 |
| `MoiraDataflow.h` | 0 | 6 |
| `MoiraDataflow_cpp.h` | 41 | 381 |
| `MoiraDebugger.cpp` | 0 | 3 |
| `MoiraExceptions_cpp.h` | 12 | 330 |
| `MoiraExec_cpp.h` | 276 | 1 441 |
| `MoiraExecFPU_cpp.h` | 21 | 2 866 |
| `MoiraExecMMU_cpp.h` | 30 | 3 283 |
| `MoiraInit_cpp.h` | 1 | 36 |
| `MoiraTypes.h` | 0 | 62 |

Identical: `MoiraALU.h`, `MoiraDasm*.{h,cpp}`, `MoiraDebugger.h`, `MoiraExceptions.h`,
`MoiraInit.h`, `MoiraMacros.h`, `StrWriter*`, `LICENSE` and the other unlisted files.
NeoST-only lines are mostly upstream lines POM68K rewrote (FPU/MMU stubs, DIV/AE frames) plus the
eight groups; `MoiraALU_cpp.h`'s 13 are the French comments and pre-existing upstream lines around
row 38 (the logic is already ported). `MoiraConfig.h` differs on `MOIRA_PRECISE_TIMING`,
`MOIRA_EMULATE_ADDRESS_ERROR`, `MOIRA_MIMIC_MUSASHI` (NeoST patches these at CMake time instead).
