# Group 5 — MOVE to SR: fullPrefetch<POLL> on the 68000

## NeoST change

Commit `ce1cc52` (2026-10-19). Moira file: `MoiraExec_cpp.h`.

```diff
# NeoST ce1cc52 — extern/moira/Moira/MoiraExec_cpp.h
@@ -3854,8 +3854,16 @@ Moira::execMoveToSr(u16 opcode)
     SYNC(4);
     setSR((u16)data);

-    (void)read<C, AddrSpace::PROG, Word>(reg.pc + 2);
-    prefetch<C, POLL>();
+    if constexpr (C == Core::C68000) {
+        // Microcode 301 rstw1 → 321 stiw4 → 1e1 malw3 → 363 b :
+        // recharger les DEUX mots après le changement de SR (donc de FC).
+        // Une lecture fictive de pc+2 conservait l'ancien opcode et modifiait
+        // aussi les hits du cache externe MegaSTE.
+        fullPrefetch<C, POLL>();
+    } else {
+        (void)read<C, AddrSpace::PROG, Word>(reg.pc + 2);
+        prefetch<C, POLL>();
+    }

     //           00  10  20        00  10  20        00  10  20
     //           .b  .b  .b        .w  .w  .w        .l  .l  .l
```

**NeoST rationale (translated).** `execMoveToSr<C68000>`: reload both prefetch words after the SR
write, via `fullPrefetch<POLL>`. The transcription of the silicon microcode
(<https://og.kervella.org/m68k/all.txt>, entries `46c0` and `46fc`) gives `301 rstw1` (back two
bytes), `321 stiw4`, `1e1 malw3` (read and update IRC), then `363 b`/`34c mmrw3` (next read and
IR/IRD transfer). The old code read `pc+2` twice and kept the old opcode. Other CPU models kept.
**Evidence:** microcode transcription; a NeoST test modifies the already-prefetched opcode between
reset and `MOVE D0,SR` — the new opcode must execute, at 8 and 16 MHz (both cases also fail with the
experimental "read pc, ignore result" variant).

## POM68K target

| NeoST hunk | POM68K location now | Changed since the copy? |
|---|---|---|
| `execMoveToSr` tail | `MoiraExec_cpp.h` l.4576, `Moira::execMoveToSr` | **No** — function identical to NeoST@`47d6c39`. |

POM68K current tail:
```cpp
    SYNC(4);
    setSR((u16)data);

    (void)read<C, AddrSpace::PROG, Word>(reg.pc + 2);
    prefetch<C, POLL>();
```
**Conflicts flagged:** none textual.
- (a) SST convergence patches: none in this function; the `STD_AE_FRAME` operand read is untouched.
- (b) `irqDelay`: `setSR` arms it only for `cpuModel >= M68020`; the edit is `C68000`-only.
- (c) JIT: `fullPrefetch` already carries the `pomJitFetch000` window (same SYNCs as `read<>`), so
  both engines see the same timing; `jit_backend_test` keeps MOVE to SR a block boundary
  (`checkUnsafe(0x46C0, ...)`).
- **Independent evidence found during this audit:** the local SingleStepTests corpus
  (`tests/data/sst68000/MOVEtoSR.json`, 8 065 vectors) records, for **every** one of its 4 938
  non-exception vectors, the tail `n 4, r(final PC), r(final PC + 2)` — i.e. the word at `reg.pc` is
  re-read, then the next, in the **new** FC (e.g. FC 2 = user program after `MOVE #$0xxx,SR`). The
  current code reads `reg.pc + 2` twice. So SST's own transaction lists side with NeoST; the harness
  simply does not compare them (`tools/tail.py`).
- On a Mac the re-read address moves from pc0+4/… to pc0+2/… (same region, same cycle), so RAM
  contention is charged at the same instants; an observable difference needs self-modifying code
  between prefetch and execution (NeoST's test) or a region boundary between the two addresses.

## Ready-to-apply edit list for POM68K

Exact replacements against POM68K's current text; each OLD block occurs exactly once in its file.
Apply in the listed order.

**5.1 — `extern/moira/Moira/MoiraExec_cpp.h` — MoiraExec_cpp.h execMoveToSr: fullPrefetch on the 68000**

OLD:
```cpp
    SYNC(4);
    setSR((u16)data);

    (void)read<C, AddrSpace::PROG, Word>(reg.pc + 2);
    prefetch<C, POLL>();
```
NEW:
```cpp
    SYNC(4);
    setSR((u16)data);

    if constexpr (C == Core::C68000) {
        // POM68K (ported from NeoST ce1cc52): microcode 301 rstw1 -> 321 stiw4
        // -> 1e1 malw3 -> 363 b reloads BOTH prefetch words after the SR (so
        // FC) change: the word at reg.pc is re-read, then reg.pc + 2. The old
        // dummy read of reg.pc + 2 kept the stale opcode in IRD. The
        // SingleStepTests transaction lists show exactly this pair, in the new
        // FC (tests/data/sst68000/MOVEtoSR.json, every non-exception vector).
        fullPrefetch<C, POLL>();
    } else {
        (void)read<C, AddrSpace::PROG, Word>(reg.pc + 2);
        prefetch<C, POLL>();
    }
```


Mechanical check of this edit list: `tools/verify.py` (same directory) asserts
that every OLD block occurs exactly once in POM68K's current working tree
(`/home/gistarcade/src/pom68k/extern/moira/Moira/`, branch
`refactor/core-no-getenv`, uncommitted edits included), applies the group on a
scratch copy, and applies all eight groups cumulatively in the recommended
order — all OK on 2026-11-06. The same edits as a unified diff (paths
`a/extern/moira/Moira/...`, for `git apply` from the repository root):
`patches/group5.patch`. **Not compiled** (no build allowed during this audit).

## Predicted effect on `sst68000`

**(i) stays fully green.** Cycles: both forms are two 4-cycle reads after the same `SYNC(4)` —
`length` unchanged. IRD/IRC: the new form loads IRD/IRC from memory at the final PC and PC+2; on the
harness's flat bus the vector's initial RAM image holds exactly those words — checked for all 4 938
sequential MOVEtoSR vectors (`tools/refetch_check.py`: 4 938/4 938 consistent), so IRD/IRC equal the
expected `prefetch`. SR, registers, RAM untouched. Exception vectors (privilege, operand address
error) leave before the tail. What changes is only the bus-transaction list (addresses pc+2/pc+4
instead of pc+4/pc+4, FC of the new mode) — not compared by the harness, and now **matching** the
corpus.

## Other gates that exercise the area

- `sst68000` (MOVEtoSR.json).
- `jit_lockstep_68000_test` / `_blocks_test` (both engines run the same handler; a Mac Plus executes
  MOVE to SR constantly — interrupt masking — so this is the in-vivo guard).
- `compact_boot_etalon` (+ `se_`, `sefdhd_`, `classic_` registrations), `mac128k_boot_etalon`,
  `portable_boot_etalon`, `system_boot_etalon`, `compact_timing_etalon` — 68000 machines.
- `sst68030`/`sst68040`/`berr030_test` guard that the `else` branch (020+) is unchanged.

## Risk grade and recommendation

**Low.** Recommendation: **apply as-is** (SST transaction lists + microcode + Hatari agree; the gate cannot move).
