# Moira convergence — the fork's 158 changes against Dirk Hoffmann's rewrite

**Direction (owner, 2026-11-22).** Dirk's Moira is the base; POM68K's 68k
CPUs are built on top of it, adding only what the Macintosh needs. Dirk has
been rewriting Moira since 2026-11-02 on his `FPU` branch (own MMU030/MMU040,
an MIT FPU on `MFloat80`, a 020/030 cache, runners against UAE, MAME,
Musashi and SingleStepTests). This note sorts every row of
`extern/moira/POM68K_VENDOR.md` § *Inventory of local patches* against that
branch, so the size and shape of the POM68K layer is known before any
rebase.

**Pinned to** Dirk's `FPU` @ `70e7aff7` (2026-11-22) and POM68K `b4710b0c`.
His branch moves daily; a verdict is a reading of that commit.

## Method and confidence

Each row was read against Dirk's code (not his documentation) by five
parallel reviews of about 32 rows each, then spot-checked: the bit-field
width (row 121), `MMU040::setTC` (row 61), the packed-zero early return
(row 128) and the BKPT data read (row 105) were confirmed in his code. The
manual claims attached to UPSTREAM-GAP rows come from the fork's own
inventory; each must be read again in the locked PDF
(`tests/core_manuals.lock`) before it is reported to Dirk, as the three
findings posted in Moira discussion #36 were (CHANGELOG 2026-11-22,
eighth). The differential replay of the oracle corpora on his branch is
`tools/dirk_moira/`.

| Class | Meaning | Rows |
|---|---|---|
| COVERED | his branch has the equivalent | 41 |
| UPSTREAM-GAP | general 68k behaviour his branch lacks or does differently; candidates to report | 65 |
| MAC-LAYER | POM68K-specific (Mac needs, JIT, save states, debugger, host speed): the layer on top | 24 |
| OBSOLETE | cannot arise on his design, or records only | 27 |
| UNSURE | needs a capture | 1 |

## What decides most verdicts

1. **Restart, not continuation.** On the 020/030/040 his core snapshots
   the registers, restores them on a fault and reruns the whole instruction
   (`processException` → `restoreRegisters`; `readRteFrame68020` never
   reads the SSW). It is a deliberate choice, held by his `retrymoira`. The
   fork follows the 68030's continuation (format $B access log, DF honoured
   by RTE) after WinUAE and the manual. Twenty-odd fork rows exist only to
   make continuation exact and are OBSOLETE on his base; the rows that
   remain are the visible consequences: a handler that completes a write
   and clears DF (MC68030UM-1989 §8.2.2) is not honoured (rows 9, 10, 77),
   and a host-raised bus error is completed into a frame only on the
   68000 (rows 9, 19). **This matters for the Mac:** the ROMs' slot probing
   and NuBus time-outs go through exactly this path (`berr020_test`), so
   the Mac layer depends on how this is resolved upstream.
2. **No FPU exception delivery.** No vectors 48-54, no BUSY/E1/E3 FSAVE
   frames, no FRESTORE validation, no format $2 for a 68040 unimplemented
   instruction (rows 8, 26, 27, 29, 44, 46, 50, 51, 53, 113). A 68040
   FPSP cannot run on it, and an emulated Quadra does run one (row 51:
   it finishes trapped operations from the FSAVE state frame). Reported in
   part (discussion #36, finding 3).
3. **No 68040 caches** (`Cache/Cache.h`: "The caches of the 68040 models
   are not emulated"). Rows 28, 59, 64, 65, 67-70, 85 belong to a 040
   cache model the fork keeps optional (`POM68K_040_DCACHE`); they only
   matter upstream if he wants that model.

## Candidates to report first

Small, general, checkable — each still to be confirmed in the manual and
by a reproducer on his branch before posting:

| Row | Finding | Where in his branch |
|---|---|---|
| 121 | bit-field width from Dn held in a signed `int`: `Dn = $80000000` overflows (undefined behaviour) | `execBitFieldDn/Ea` |
| 128 | FMOVE.P of a zero returns before the k > 17 OPERR check | `FpuFormats.cpp toPacked` |
| 61 | every TC write flushes the 68040 ATCs | `MMU040::setTC` |
| 105 | BKPT makes a data read at PC on the 020+ | `execException(BKPT)` |
| 35 | catchpoints are not re-armed by reset; watchpoint delegate gets no access kind | `Debugger::reset`, `didReachWatchpoint` |
| 137 | CAS2 with Dc1 = Dc2 writes in the 040 order on the 020/030 | `execCas2` |
| 158 | 68040 FBcc/FDBcc with an odd displacement faults only when taken | `execFBcc/execFDbcc` |
| 144, 152, 154 | SR/CCR refill, DBcc and TRAPV make raw program reads past the i-cache | `execMoveToSr`, `execDbcc`, `execTrapv` |
| 66 | a failed 68040 CAS makes no terminal write | `execCas/execCas2` |
| 56, 62 | a /BERR on a descriptor fetch escapes PTEST instead of reporting B | `MMU030/MMU040::walk` |

## The Mac layer

The MAC-LAYER rows are the size of what POM68K adds on top: the JIT seam
and its probes (rows 22, 31-34, 55, 76, 89, 90, 119, 120, 123, 125), save
states (23, 24, 57, 64, 118, 124, 130), the debugger's side-effect-free
inspection (112), host-speed ATC structures (16, 48), the 68030 IRQ delay
tuned on SimCity 2000 (13, grade F) and the optional 040 cache model. None
of these has a hook in his API today: no fetch/data window, no serializer,
no CACR-change delegate. Those hooks are what to ask him for, once the
correctness rows are settled.

## All rows

Classes: C = COVERED, G = UPSTREAM-GAP, M = MAC-LAYER, O = OBSOLETE,
U = UNSURE. "Split" marks a row whose items fall in different classes.

| Row | Class | Note |
|---|---|---|
| 1 | C | config macros are `#ifndef`; `MOIRA_MIMIC_MUSASHI` became `MOIRA_COMPATIBILITY` |
| 2 | C | SST 100 % in his MAME mode; DEFAULT differs in a few places |
| 3 | C | PMOVE/PTEST/PFLUSH/PLOAD on `MMU030` |
| 4 | G | malformed PMMU words take illegal, not Line-F; no vector-56 configuration exception |
| 5 | C | 030 ATC, TT, walk, $A/$B frames — restart model |
| 6 | C | split: D17 (reserved IIS=100 dereferenced) still open |
| 7 | C | his own FPU, compared with UAE's softfloat |
| 8 | G | no FPU timing; FRESTORE accepts any frame |
| 9 | G | 030 /BERR from the host: SSW not completed; RTE $A restarts |
| 10 | G | RTE ignores a software-cleared DF |
| 11 | G | PMOVE to TC: no prefetch-pipe carry (LC II ROM, grade C) |
| 12 | C | `isStopped()` |
| 13 | M | IRQ delay after mask lowering, grade F |
| 14 | C | real 020/030 caches replace the fork's timing overlay; Mac etalons to re-measure |
| 15 | C | odd-SP frames, vector scaling |
| 16 | M | O(1) ATC lookup (host speed) |
| 17 | C | split: no format $4 frame on the FPU-less 040 |
| 18 | C | 040 MMU, 64-entry I/D ATCs |
| 19 | G | 040 /BERR from the host: SSW040 not completed |
| 20 | G | watchpoints match physical addresses under the MMU |
| 21 | C | plain 020 /BERR frame (UAE model) |
| 22 | M | JIT seam |
| 23 | M | save-state ATC flush |
| 24 | M | 68010 read/write buffers |
| 25 | G | CALLM/RTM are stubs |
| 26 | G | no external-FPU exception, no post-instruction frame |
| 27 | G | split: 040 FPU partly covered; no unimplemented-data-type trap, UNIMP/BUSY frames |
| 28 | G | no 040 data-bearing caches |
| 29 | G | no 6888x mid-instruction state (format $9, BUSY) |
| 30 | O | access-log counters |
| 31 | M | JIT probe bound |
| 32 | M | post-PMOVE pipe accessor |
| 33 | M | fused fetch / i-cache overlay seam |
| 34 | M | interpreter data window |
| 35 | G | catchpoints not re-armed at reset; watchpoint delegate without access kind |
| 36 | O | rewritten walk |
| 37 | C | MOVES releases SFC/DFC on fault |
| 38 | C | DIVS without undefined behaviour |
| 39 | O | no environment reads, no 040 cache |
| 40 | C | NeoST 68000 prefetch fixes (SST bus-level) |
| 41 | C | 68000 bus order and idle placement (SST bus-level) |
| 42 | C | split: NeoST-only hooks are O |
| 43 | G | split: 040 CHK2/CMP2 N/V |
| 44 | G | 040 unimplemented-vs-unsupported ordering moot: no FPSP traps |
| 45 | C | 040 FPU faults restart (by his restart model) |
| 46 | G | FPU-less 040 Line-F frame rules |
| 47 | G | FBcc predicate bit 5 (grade F) |
| 48 | M | 040 hot translation (host speed) |
| 49 | O | no environment reads |
| 50 | G | no 040 enabled FPU exception |
| 51 | G | FSAVE writes NULL/IDLE only |
| 52 | G | split: CLR covered; last-write completion is the restart-model difference |
| 53 | G | FRESTORE does not validate frames |
| 54 | C | PTEST invalid at level limit |
| 55 | M | split: interpreter-armed window; assert part O |
| 56 | G | /BERR on a 030 descriptor fetch escapes PTEST |
| 57 | M | state visitor |
| 58 | O | comment audit |
| 59 | M | physical CINV/CPUSH (040 cache model) |
| 60 | C | FC2-tagged I/D ATCs |
| 61 | G | TC write flushes the 040 ATCs |
| 62 | G | split: PFLUSH covered; PTEST over-flushes, no B on /BERR |
| 63 | C | 040 MOVEP restart |
| 64 | M | 040 caches in save states |
| 65 | G | 040 implicit-NC operands; CAS2 lock (040 cache model) |
| 66 | G | failed 040 CAS makes no terminal write |
| 67 | G | split: MOVE16 fault descriptor; push half needs the 040 cache |
| 68 | G | 040 read-fill faults (040 cache model) |
| 69 | G | 040 write-allocate frames (040 cache model) |
| 70 | G | 040 push-fault frames (040 cache model) |
| 71 | G | MA never set |
| 72 | G | 040 descriptor /BERR; U written eagerly |
| 73 | C | 040 ATC keeps R/W/S and PA on failure |
| 74 | G | 040 MOVEM restarts (repeats memory indirection) |
| 75 | G | split: 040 ATC fully associative, not 16×4; JIT accounting is M |
| 76 | M | `MOIRA_ENABLE_JIT` |
| 77 | G | plain 020 RTE ignores DF |
| 78 | C | per-instance FPU environment |
| 79 | G | bit fields always access a longword (+ byte 5) |
| 80 | O | 030 MOVES fixup slot |
| 81 | O | 030 MOVEM log entries |
| 82 | U | 030 odd JMP target PC: his A1200 (020) recordings vs the fork's WinUAE rule |
| 83 | C | FScc data buffer |
| 84 | C | ADD/SUB data buffer |
| 85 | G | split: 040 cache replacement (cache model); JIT half M |
| 86 | C | 030 CACR mask |
| 87 | G | JSR target-fetch fault stacks the JSR (grade D) |
| 88 | O | long displacement log entry |
| 89 | M | ATC replay copy-on-write |
| 90 | M | inline fetch-window test |
| 91 | G | split: 68000 STOP wake window (grade D/F) |
| 92 | G | 68000 reset timing (grade D) |
| 93 | C | FP registers initialised |
| 94 | G | CAS/CAS2/TAS without locked RMW on the 030 |
| 95 | C | UNPK -(An); PACK -(An) read width unsettled |
| 96 | C | Scc/shift buffers |
| 97 | O | frame latches |
| 98 | G | split: FSAVE busy absent; WB3D and odd JSR covered |
| 99 | G | prefetch fault taken one instruction early |
| 100 | O | no lock flag |
| 101 | O | restart model |
| 102 | O | no FPU traps (see 113) |
| 103 | O | no lock flags |
| 104 | C | PACK/UNPK store buffer |
| 105 | G | BKPT reads at PC on the 020+ |
| 106 | C | 040 bit-field/JSR restart |
| 107 | G | ANDI/ORI/EORI to SR refill as a data read |
| 108 | G | trace at RTE into $7 (grade D) |
| 109 | C | PTEST long-descriptor limits |
| 110 | C | PTEST and bus share the walk |
| 111 | G | direct-root write does not mark the ATC entry M; FCL bound skipped |
| 112 | M | side-effect-free inspection |
| 113 | G | no 6888x exception traps |
| 114 | O | packed zero (no SoftFloat) |
| 115 | C | group-2 format $2 instruction address |
| 116 | O | odd-target checks already gated |
| 117 | G | 030/040 odd JSR target uses 020 semantics |
| 118 | O | no MOVE16 frame latches |
| 119 | M | CACR-change retirement (needs a hook) |
| 120 | M | JIT JSR helper |
| 121 | G | bit-field width signed overflow |
| 122 | O | no data-buffer latch |
| 123 | M | JIT fixup |
| 124 | M | restart state in save states |
| 125 | M | write probe on TT asymmetry |
| 126 | O | no stale WB3D |
| 127 | C | level-7 edge (also applied to the 68000/010, unlike the fork) |
| 128 | G | FMOVE.P zero misses OPERR for k > 17 |
| 129 | G | 020 interrupt sequence not billed |
| 130 | M | `fcl` in save states |
| 131 | G | RTE $B restarts instead of continuing (design divergence) |
| 132 | O | no continuation |
| 133 | C | 020/030 trace format $2 |
| 134 | G | 040 trace drops after TRAP/CHK/TRAPV and RTE into $7 |
| 135 | G | T0: the 040's extra flow list |
| 136 | G | 040 MULx.L register write order (grade D) |
| 137 | G | CAS2 same-register write order below the 040 |
| 138 | O | restart model |
| 139 | O | split: NEG/NEGX/ASL data buffer is a grade-D gap |
| 140 | O | restart model |
| 141 | O | no continuation |
| 142 | G | ADDX/SUBX data buffer (grade D) |
| 143 | C | RTE frame read fault keeps A7 |
| 144 | G | SR/CCR refill is an immediate read |
| 145 | C | M-set interrupt throwaway frame |
| 146 | O | translation-unit split already his |
| 147 | G | split: 020 i-cache covered; fetch timing double-counted; Mac parts M |
| 148 | O | his compatibility modes |
| 149 | O | CALLM/RTM stubs (see 25) |
| 150 | O | no SoftFloat |
| 151 | C | MOVES releases SFC/DFC |
| 152 | G | DBcc expired read bypasses the i-cache |
| 153 | C | DIVS.L INT64_MIN/−1 |
| 154 | G | other raw 020 program reads |
| 155 | G | split: 68EC040 ACU — PTEST/PFLUSH Line-F, IACR/DACR names |
| 156 | C | PC-relative reads in program space (his 040 too; the fork keeps data space there) |
| 157 | G | 020/030 PC-relative fault classified as instruction stream |
| 158 | G | 040 FBcc/FDBcc odd displacement only when taken |
