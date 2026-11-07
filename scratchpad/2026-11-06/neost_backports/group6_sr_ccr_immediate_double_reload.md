# Group 6 — ANDI/ORI/EORI #,SR and #,CCR, MOVE to CCR: double reload

## NeoST change

Commit `d8d33bb` (2026-10-20, "CPU : double préfetch des #,SR/#,CCR/MOVE to CCR, faute hôte,
seuils IPL"). Moira file: `MoiraExec_cpp.h`.

```diff
# NeoST d8d33bb — extern/moira/Moira/MoiraExec_cpp.h
@@ -801,7 +801,10 @@ Moira::execAndiccr(u16 opcode)
         u8  dst = getCCR();
         setCCR(u8(logic<C, I, S>(src, dst)));
         SYNC(8);
-        (void)read<C, AddrSpace::DATA, Word>(reg.pc + 2);
+        // NeoST — microcode 003c/023c/0a3c : 1cc stiw1 (recul de deux octets)
+        // → 321 stiw4 → 1e1 malw3 → 363 b : relire pc0+4 PUIS pc0+6, IRD
+        // reçoit le mot relu après le changement de CCR (cf. execMoveToSr).
+        fullPrefetch<C, POLL>();

     } else {

@@ -809,10 +812,9 @@ Moira::execAndiccr(u16 opcode)
         u32 src = readI<C, S>();
         u8  dst = getCCR();
         setCCR(u8(logic<C, I, S>(src, dst)));
+        prefetch<C, POLL>();
     }

-    prefetch<C, POLL>();
-
     //           00  10  20        00  10  20        00  10  20
     //           .b  .b  .b        .w  .w  .w        .l  .l  .l
     CYCLES_IM   (20, 16, 12,        0,  0,  0,        0,  0,  0)
@@ -836,8 +838,16 @@ Moira::execAndisr(u16 opcode)
     u32 result = logic<C, I, S>(src, dst);
     setSR((u16)result);

-    (void)read<C, AddrSpace::DATA, Word>(reg.pc + 2);
-    prefetch<C, POLL>();
+    if constexpr (C == Core::C68000) {
+        // NeoST — microcode 007c/027c/0a7c : même queue que MOVE to SR
+        // (1cc stiw1 → 321 stiw4 → 1e1 malw3 → 363 b). Les deux mots sont
+        // relus APRÈS l'écriture de SR, donc avec le nouveau FC : un
+        // ANDI #$DFFF,SR repasse en utilisateur et refait pc0+4 en FC user.
+        fullPrefetch<C, POLL>();
+    } else {
+        (void)read<C, AddrSpace::DATA, Word>(reg.pc + 2);
+        prefetch<C, POLL>();
+    }

     //           00  10  20        00  10  20        00  10  20
     //           .b  .b  .b        .w  .w  .w        .l  .l  .l
@@ -3733,8 +3743,14 @@ Moira::execMoveToCcr(u16 opcode)
     SYNC(4);
     setCCR((u8)data);

-    (void)read<C, AddrSpace::PROG, Word>(reg.pc + 2);
-    prefetch<C, POLL>();
+    if constexpr (C == Core::C68000) {
+        // NeoST — microcode 44c0..44fc : 301 rstw1 / 159 mstw1 (recul de deux
+        // octets) → 321 stiw4 → 1e1 malw3 → 363 b, comme MOVE to SR.
+        fullPrefetch<C, POLL>();
+    } else {
+        (void)read<C, AddrSpace::PROG, Word>(reg.pc + 2);
+        prefetch<C, POLL>();
+    }

     //           00  10  20        00  10  20        00  10  20
     //           .b  .b  .b        .w  .w  .w        .l  .l  .l
```

**NeoST rationale (translated).** Same fix as `execMoveToSr` (2026-10-19) for `execAndisr`
(ANDI/ORI/EORI #,SR), `execAndiccr` (#,CCR) and `execMoveToCcr`, `C68000` branch: `fullPrefetch<POLL>`
replaces the dummy read of `pc+2` followed by `prefetch`. The microcode transcription gives for
`003c/023c/0a3c` and `007c/027c/0a7c` the tail `1cc stiw1` (back two bytes) → `321 stiw4` → `1e1
malw3` (read, IRC) → `363 b` → `34c mmrw3` (IRD ← IR ← IRC), and for `44c0..44fc` `301 rstw1`/`159
mstw1` then the same tail. Accesses: pc0+4, **pc0+4**, pc0+6 (instead of pc0+4, pc0+6, pc0+6); IRD gets
the word re-read AFTER the SR/CCR write, in PROGRAM space (the old dummy read of #,SR/#,CCR was in
DATA space). Hatari CE does the same (`op_027c_13_ff`: `get_word_ce000_prefetch(0)` after
`MakeFromSR`). Cycle count unchanged. Observable consequence: `ANDI #$DFFF,SR` (back to user) re-reads
pc0+4 in user FC; placed at `$7FE` that word is GLUE-protected → bus error, as on a real 68000.
**Evidence:** microcode transcription, Hatari CE, NeoST tests (`tests/blitter_rmw_logic.cpp`, 8 and
16 MHz): MOVE D0,CCR executes the modified opcode after reset; ANDI/EORI #,SR to user at `$7FA` fault
on `$7FE`; ORI #,CCR keeps its duration (20/10 cycles).

## POM68K target

| NeoST hunk | POM68K location now | Changed since the copy? |
|---|---|---|
| `execAndiccr` | `MoiraExec_cpp.h` l.822 | **No** (identical to NeoST@`47d6c39`). |
| `execAndisr` | `MoiraExec_cpp.h` l.852 | **No**. |
| `execMoveToCcr` | `MoiraExec_cpp.h` l.4457 | **No**. |

POM68K current `execAndiccr` (68000 branch + shared tail):
```cpp
    if constexpr (C == Core::C68000) {

        u32 src = readI<C, S>();
        u8  dst = getCCR();
        setCCR(u8(logic<C, I, S>(src, dst)));
        SYNC(8);
        (void)read<C, AddrSpace::DATA, Word>(reg.pc + 2);

    } else {

        SYNC(8);
        u32 src = readI<C, S>();
        u8  dst = getCCR();
        setCCR(u8(logic<C, I, S>(src, dst)));
    }

    prefetch<C, POLL>();
```
**Conflicts flagged:** none textual.
- (a) POM68K's convergence rule "operand reads always use DATA space" (`readM`, row 2) is about
  `readM`; these dummy reads are explicit `read<C, AddrSpace::DATA, Word>` calls and are removed on the
  68000 only. The 68010/020+ branches keep them (DATA for #,SR/#,CCR, PROG for MOVE to CCR) exactly.
- (b) `irqDelay`: 68020+ only, untouched.
- (c) JIT: `fullPrefetch` carries the `pomJitFetch000` window; no seam conflict.
- **Independent evidence found during this audit:** the local SST corpus agrees for every
  non-exception vector — ANDItoSR/ORItoSR/EORItoSR (8 065 each), ANDItoCCR/ORItoCCR/EORItoCCR
  (8 065 each) end with `n 8, r(pc0+4), r(pc0+6)` in **program** FC (6 supervisor, 2 after a return to
  user mode), and MOVEtoCCR's 4 958 sequential vectors with `n 4, r(final PC), r(final PC+2)`. The
  current code reads pc0+6 (DATA FC 5/1) then pc0+6.
- Mac relevance: no 68000 Mac protects low memory by FC the way the ST GLUE does; the FC change is
  invisible on Mac hardware (MacMemory decodes no FC — unverified for `PortableMemory`).

## Ready-to-apply edit list for POM68K

Exact replacements against POM68K's current text; each OLD block occurs exactly once in its file.
Apply in the listed order.

**6.1 — `extern/moira/Moira/MoiraExec_cpp.h` — MoiraExec_cpp.h execAndiccr (ANDI/ORI/EORI #,CCR)**

OLD:
```cpp
        setCCR(u8(logic<C, I, S>(src, dst)));
        SYNC(8);
        (void)read<C, AddrSpace::DATA, Word>(reg.pc + 2);

    } else {

        SYNC(8);
        u32 src = readI<C, S>();
        u8  dst = getCCR();
        setCCR(u8(logic<C, I, S>(src, dst)));
    }

    prefetch<C, POLL>();
```
NEW:
```cpp
        setCCR(u8(logic<C, I, S>(src, dst)));
        SYNC(8);
        // POM68K (ported from NeoST d8d33bb): microcode 003c/023c/0a3c —
        // 1cc stiw1 (back two bytes) -> 321 stiw4 -> 1e1 malw3 -> 363 b:
        // re-read pc0+4 THEN pc0+6, in PROGRAM space; IRD receives the word
        // re-read after the CCR change (see execMoveToSr). Hatari CE does
        // the same; so do the SingleStepTests transaction lists.
        fullPrefetch<C, POLL>();

    } else {

        SYNC(8);
        u32 src = readI<C, S>();
        u8  dst = getCCR();
        setCCR(u8(logic<C, I, S>(src, dst)));
        prefetch<C, POLL>();
    }
```

**6.2 — `extern/moira/Moira/MoiraExec_cpp.h` — MoiraExec_cpp.h execAndisr (ANDI/ORI/EORI #,SR)**

OLD:
```cpp
    setSR((u16)result);

    (void)read<C, AddrSpace::DATA, Word>(reg.pc + 2);
    prefetch<C, POLL>();
```
NEW:
```cpp
    setSR((u16)result);

    if constexpr (C == Core::C68000) {
        // POM68K (ported from NeoST d8d33bb): microcode 007c/027c/0a7c, same
        // tail as MOVE to SR (1cc stiw1 -> 321 stiw4 -> 1e1 malw3 -> 363 b).
        // Both words are re-read AFTER the SR write, hence with the new FC:
        // ANDI #$DFFF,SR returns to user mode and re-reads pc0+4 as user
        // program (the old dummy read was pc0+6 in DATA space).
        fullPrefetch<C, POLL>();
    } else {
        (void)read<C, AddrSpace::DATA, Word>(reg.pc + 2);
        prefetch<C, POLL>();
    }
```

**6.3 — `extern/moira/Moira/MoiraExec_cpp.h` — MoiraExec_cpp.h execMoveToCcr**

OLD:
```cpp
    SYNC(4);
    setCCR((u8)data);

    (void)read<C, AddrSpace::PROG, Word>(reg.pc + 2);
    prefetch<C, POLL>();
```
NEW:
```cpp
    SYNC(4);
    setCCR((u8)data);

    if constexpr (C == Core::C68000) {
        // POM68K (ported from NeoST d8d33bb): microcode 44c0..44fc —
        // 301 rstw1 / 159 mstw1 (back two bytes) -> 321 stiw4 -> 1e1 malw3
        // -> 363 b, like MOVE to SR.
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
`patches/group6.patch`. **Not compiled** (no build allowed during this audit).

## Predicted effect on `sst68000`

**(i) stays fully green.** `length` unchanged (two 4-cycle reads after the same SYNC). IRD/IRC
loaded from memory at final PC / PC+2 equal the expected prefetch in all 48 390 + 4 958 sequential
vectors (`tools/refetch_check.py`). Flags/registers/RAM untouched. Only the (uncompared) transaction
list changes — towards the corpus.

## Other gates that exercise the area

- `sst68000` (ANDItoSR, ORItoSR, EORItoSR, ANDItoCCR, ORItoCCR, EORItoCCR, MOVEtoCCR).
- `jit_lockstep_68000_test` / `_blocks_test`; 68000 machine etalons as in group 5.
- `sst68010`: none exists; the 68010 branch is unchanged anyway.

## Risk grade and recommendation

**Low.** Recommendation: **apply as-is**, together with or right after group 5 (same mechanism).
