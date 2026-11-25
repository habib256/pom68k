# Moira convergence — the fork's 158 changes against Dirk Hoffmann's rewrite

**Direction (owner, 2026-11-22).** Dirk's Moira is the base; POM68K's 68k
CPUs are built on top of it, adding only what the Macintosh needs. Dirk
rewrote Moira from 2026-11-02 on his `FPU` branch (own MMU030/MMU040, an MIT
FPU on `MFloat80`, a 020/030 cache, runners against UAE, MAME, Musashi and
SingleStepTests), since merged into his `master`. This note sorts every row
of the fork's inventory (`extern/moira/POM68K_VENDOR.md` § *Fork inventory*,
the NeoST fork frozen on `main_old`) against his code, so the size and shape
of the POM68K layer is known.

**Sorted against** Dirk's `FPU` @ `70e7aff7` (2026-11-22); **re-read on
2026-11-25** against his `master` @ `bf98107f` (his `58e0405`, `186f904`,
`6946378`, `e9a083d`, `5fde078` and POM68K's PRs #42-#49) and the 15-patch
queue on it. A verdict is a reading of that commit.

## Method and confidence

Each row was read against Dirk's code (not his documentation) by five
parallel reviews of about 32 rows each, then spot-checked: the bit-field
width (row 121), `MMU040::setTC` (row 61), the packed-zero early return
(row 128) and the BKPT data read (row 105) were confirmed in his code
(121 and 128 have since been fixed upstream: `e9a083d`, PR #44). On
2026-11-25 six parallel passes re-read every row and the sections below
against `bf98107f` and the queue tree: 24 rows had gone stale (mostly fixed
by his FPU and bus-error work or by the merged PRs), six notes were wrong or
imprecise from the start (rows 15, 20, 96, 98, 104 and 119), and the queue
patch carrying a local fix is now named in the note. The manual claims attached to
UPSTREAM-GAP rows come from the fork's own inventory; each must be read
again in the locked PDF (`tests/core_manuals.lock`) before it is reported to
Dirk, as the three findings posted in Moira discussion #36 were (CHANGELOG
2026-11-22, eighth). The differential replay of the oracle corpora on his
branch is `tools/dirk_moira/`.

| Class | Meaning | Rows |
|---|---|---|
| COVERED | his `master` has the equivalent | 49 |
| UPSTREAM-GAP | general 68k behaviour his `master` lacks or does differently; candidates to report | 58 |
| MAC-LAYER | POM68K-specific (Mac needs, JIT, save states, debugger, host speed): the layer on top | 22 |
| OBSOLETE | cannot arise on his design, or records only | 27 |
| UNSURE | needs a capture | 2 |

A split row counts once, under the class of its first item.

## What decides most verdicts

1. **Restart, not continuation.** On the 020/030/040 his core snapshots
   the registers, restores them on a fault and reruns the whole instruction
   (`processException` → `restoreRegisters`; `readRteFrame68020` never
   reads the SSW upstream). It is a deliberate choice, held by his
   `retrymoira`. The fork follows the 68030's continuation (format $B
   access log, DF honoured by RTE) after WinUAE and the manual. About
   fifteen fork rows exist only to make continuation exact and are OBSOLETE
   on his base. A host-raised bus error now gets its SSW on the 020/030/040
   too (his `58e0405`; PR #42). What remains is a handler that completes a
   cycle and clears DF (MC68030UM-1989 §8.2.2), which upstream reruns (rows
   10, 77). **This matters for the Mac:** the ROMs' slot probing and NuBus
   time-outs go through exactly this path (`berr030_test`); POM68K resolves
   it locally with queue patch 3 (with 9 and 11), a completed-cycle record
   that RTE arms when DF is clear.
2. **FPU exception delivery is partial.** Since his `186f904` and
   `6946378` and PRs #48/#49: vectors 48-54 are taken pre-instruction
   (format $0) at the next FPU instruction, a 68040 FMOVE out takes a
   format $3 post-instruction exception, the 68040 unimplemented
   instruction takes vector 11 with format $2, FSAVE stores the UNIMP frame
   and FRESTORE validates frames. Still missing: BUSY/E3 FSAVE frames (FSAVE
   discards a pending exception), 6888x mid-instruction state (format $9),
   the vector-55 unsupported-data-type trap and FPU timing (rows 8, 27, 29,
   44, 51). The Quadra ROM's FPSP now completes unimplemented instructions
   (CHANGELOG 2026-11-25); its arithmetic-exception handlers, which read the
   E3 frame, still cannot run.
3. **No 68040 caches** (`Cache/Cache.h`: "The caches of the 68040 models
   are not emulated"). Rows 28, 59, 64, 65, 67-70, 85 belong to the fork's
   040 cache model, retired with its knobs on 2026-11-25; they only matter
   upstream if he wants that model.

## Candidates to report first

PRs #42-#49 are merged; the queue's 16 patches are all local
(`extern/moira/patches/queue.tsv`). Small, general, checkable — each still
to be confirmed in the manual and by a reproducer on his `master` before
posting:

| Row | Finding | Where in his `master` |
|---|---|---|
| 61 | every TC write flushes the 68040 ATCs | `MMU040::setTC` |
| 105 | BKPT makes a data read at PC on the 020+ | `execException(BKPT)` |
| 35 | catchpoints are not re-armed by reset; watchpoint delegate gets no access kind (carried by queue patches 5 and 6) | `Debugger::reset`, `didReachWatchpoint` |
| 137 | CAS2 with Dc1 = Dc2 writes in the 040 order on the 020/030 | `execCas2` |
| 158 | 68040 FBcc/FDBcc: an odd displacement faults only when taken and FDBcc decrements first (the 020 rule of PR #47); MC68040UM 8.2.2 faults taken or not | `execFBcc/execFDbcc` |
| 144, 154 | the ANDI/ORI/EORI to SR refill reads raw in DATA space, and `jumpToVector`'s first handler fetch passes the i-cache (MOVE to SR/CCR fixed by PR #43; DBcc and TRAPV carried by queue patch 1); the 030/040 SR-refill fault is not deferred | `execAndisr` (shared by ORI/EORI), `jumpToVector` |
| 66 | a failed 68040 CAS makes no terminal write | `execCas/execCas2` |
| 56, 62, 110 | a /BERR on a descriptor fetch escapes PTEST instead of reporting B | `MMU030/MMU040::walk` |

## The Mac layer

The MAC-LAYER rows are the size of what POM68K adds on top: the JIT seam
and its probes (rows 22, 31-34, 55, 76, 89, 90, 119, 120, 123, 125; the JIT
is compiled out until it is rebuilt), save states (23, 24, 57, 64, 118,
124, 130) and host-speed ATC structures (16, 48). The fork's IRQ delay (13)
and its 040 cache model left the layer on 2026-11-25, and the debugger's
side-effect-free inspection (112) is now served by his `MMU::peek()`. His
API has no fetch/data window and no serializer for the caches (queue patch
2 adds one); a CACR-change delegate does exist (`didChangeCACR`,
`MoiraInterface.h`). The remaining hooks are what to ask him for, once the
correctness rows are settled (`TODO.md` § Phase 2 and its staged plan).

## All rows

Classes: C = COVERED, G = UPSTREAM-GAP, M = MAC-LAYER, O = OBSOLETE,
U = UNSURE. "Split" marks a row whose items fall in different classes.

| Row | Class | Note |
|---|---|---|
| 1 | C | config macros are `#ifndef`; `MOIRA_MIMIC_MUSASHI` became `MOIRA_COMPATIBILITY` |
| 2 | C | SST 100 % in his MAME mode; in DEFAULT the 68000 address-error idle and DIV overflow flags/timing are queue patches 13 and 14 (local, SST evidence only) |
| 3 | C | PMOVE/PTEST/PFLUSH/PLOAD on `MMU030` |
| 4 | G | malformed PMMU words take illegal, not Line-F; no vector-56 configuration exception |
| 5 | C | 030 ATC, TT, walk, $A/$B frames — restart model |
| 6 | C | split: D17 (reserved IIS=100 dereferenced) still open |
| 7 | C | his own FPU, compared with UAE's softfloat |
| 8 | G | no FPU timing; FRESTORE refuses an unknown version or size since PR #48, but a BUSY frame restores nothing and a 68882 refuses a 68881-size frame (the fork and WinUAE accept it; unsettled) |
| 9 | C | split: the SSW of a host /BERR is completed on the 030/040 (Dirk's 58e0405) and the 020 (PR #42); RTE $A still restarts (restart model, see row 10) |
| 10 | G | RTE ignores a software-cleared DF upstream; queue patch 3 (with 9 and 11, local) honours it |
| 11 | G | PMOVE to TC: no prefetch-pipe carry (LC II ROM, grade C) |
| 12 | C | `isStopped()` |
| 13 | U | not carried: the inert `irqDelay` left the Mac layer on 2026-11-25; whether a guest needs an IRQ delay after mask lowering is open, grade F, settled by a hardware trace |
| 14 | C | real 020/030 caches replace the fork's timing overlay; Mac etalons to re-measure |
| 15 | C | 020/030/040 do not align SP and scale the vector once; his 68010 still aligns (no Mac is a 68010) |
| 16 | M | O(1) ATC lookup (host speed) |
| 17 | C | split: no format $4 frame on the FPU-less 040 |
| 18 | C | 040 MMU, 64-entry I/D ATCs |
| 19 | C | SSW040 of a host /BERR completed (Dirk's 58e0405); the logical first-byte fault address of a split or translated access is queue patch 12 (local) |
| 20 | G | watchpoints match the physical address on uncached accesses, the logical one on cache hits and 020-core fetches |
| 21 | C | plain 020 /BERR frame (UAE model) |
| 22 | M | JIT seam |
| 23 | M | save-state ATC flush |
| 24 | M | 68010 read/write buffers |
| 25 | G | CALLM/RTM are stubs |
| 26 | C | 6888x exceptions are taken pre-instruction, format $0, at the next FPU instruction (Dirk's 186f904); no post-instruction frame, which POM68K's 2026-11-24 ruling holds right (but see row 113 for FMOVE out) |
| 27 | G | split: vector 11 with format $2 (Dirk's 6946378) and the UNIMP FSAVE frame (PR #49) exist; the vector-55 unsupported-data-type trap and the BUSY/E3 frame are missing |
| 28 | G | no 040 data-bearing caches |
| 29 | G | no 6888x mid-instruction state (format $9, BUSY) |
| 30 | O | access-log counters |
| 31 | M | JIT probe bound |
| 32 | M | post-PMOVE pipe accessor |
| 33 | M | fused fetch / i-cache overlay seam |
| 34 | M | interpreter data window |
| 35 | G | catchpoints are not re-armed by reset; the watchpoint delegate gets no access kind; carried by queue patches 5 and 6 (local) |
| 36 | O | rewritten walk |
| 37 | C | MOVES releases SFC/DFC on fault |
| 38 | C | DIVS without undefined behaviour |
| 39 | O | no environment reads, no 040 cache |
| 40 | C | NeoST 68000 prefetch fixes (SST bus-level) |
| 41 | C | 68000 bus order and idle placement (SST bus-level) |
| 42 | C | split: NeoST-only hooks are O |
| 43 | G | split: 040 CHK2/CMP2 N/V |
| 44 | G | vector 11 exists (`execFUnimplemented`) but no vector 55: packed and denormal sources of implemented opmodes run in hardware; a Dn source in X/P/D or an An source with an unimplemented opmode takes Line-F before vector 11 (fork: vector 11, grade D) |
| 45 | C | 040 FPU faults restart; the snapshot omits FP registers, FPSR and FPIAR (queue patch 8, local) |
| 46 | G | FPU-less 040 Line-F frame rules |
| 47 | G | FBcc predicate bit 5 (grade F) |
| 48 | M | 040 hot translation (host speed) |
| 49 | O | no environment reads |
| 50 | G | enabled exceptions are posted and taken pre-instruction (format $0); left: OVFL/UNFL not nonmaskable on the 040, the destination kept only for SNAN/OPERR/DZ, FMOVE out takes an enabled exception at once with format $3, integer-destination SNAN/OPERR nonmaskable only with queue patch 7 |
| 51 | G | FSAVE writes NULL/IDLE/UNIMP (UNIMP since PR #49); no BUSY/E1/E3 frame with the intermediate result, and FSAVE discards a pending trap |
| 52 | G | split: CLR covered; the last write is the restart-model difference; a handler's DF-clear completion is honoured only with queue patches 3 and 9 (local) |
| 53 | C | FRESTORE takes a format error for a foreign version or an unknown size; a 6888x refuses $41 (PR #48) |
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
| 71 | G | MA never set; a page-crossing operand is translated once, at its first byte |
| 72 | G | 040 descriptor /BERR escapes and sets no ATC bit; U written eagerly at every level (queue patch 12 only fixes the FA of the original access) |
| 73 | C | 040 ATC keeps R/W/S and PA on failure |
| 74 | G | 040 MOVEM restarts (repeats memory indirection) |
| 75 | G | split: 040 ATC fully associative, not 16×4; JIT accounting is M |
| 76 | M | `MOIRA_ENABLE_JIT` |
| 77 | G | plain 020 RTE ignores DF upstream; queue patches 3 and 9 (local) serve a cleared-DF read or write, and the second word of a split long, from the frame |
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
| 96 | C | split: Scc, ABCD and the LSL/LSR/ROL/ROR/ROXL/ROXR/ASR memory shifts covered; G (D/F, internal buffer): ASL memory clips the result (bit 16 lost, fork and gencpu keep it), SBCD/NBCD memory can carry bit 16 where gencpu's u16 cannot |
| 97 | O | frame latches |
| 98 | G | split: no FSAVE busy frame; the 040 WB3D holds the unclipped value (ADDI.W/ADD.B carry-out, SBCD bit 16) instead of the store size (D/F); odd JSR covered (no read on 020+) |
| 99 | G | prefetch fault taken one instruction early |
| 100 | O | no lock flag |
| 101 | O | restart model |
| 102 | O | FPU arithmetic traps exist since Dirk's 186f904, but FSAVE discards the pending trap and writes an all-zero IDLE body, so no trapped operand reaches a frame; MFloat80 core, no softfloat intermediate state |
| 103 | O | no lock flags |
| 104 | C | split: PACK and UNPK without carry covered; G (D/F): an UNPK -(An) whose adjustment carries past bit 15 leaves $0001xxxx in the 030 buffer (fork and gencpu mask to 16 bits) |
| 105 | G | BKPT reads at PC on the 020+ |
| 106 | C | 040 bit-field/JSR restart |
| 107 | G | ANDI/ORI/EORI to SR refill as a data read |
| 108 | G | trace at RTE into $7 (grade D) |
| 109 | C | PTEST long-descriptor limits |
| 110 | C | split: PTEST and the bus share the walk (indirection, target size by DT) — C; a descriptor-read fault returning the preceding address/count is G (the /BERR escapes PTEST, as row 56) |
| 111 | G | split: a direct-root write leaves the ATC M bit clear — G; the DT=1 root limit with FCL is U [F]: MC68030UM 9.7.1 vs Figure 9-28, the core keeps Dirk's reading (CHANGELOG 2026-11-25) |
| 112 | C | Dirk's `MMU::peek()` (side-effect free, descriptors via `read16Dasm`); the POM68K debugger uses it since 2026-11-25 |
| 113 | G | 6888x arithmetic exceptions delivered since Dirk's 186f904: pre-instruction format $0 at the next FPU instruction (BSUN at once); the fork's post-instruction format $2 below the 040 cannot arise; FMOVE out to memory or Dn is mid-instruction format $9 in MC68881UM 6.1 (scan PC at +2, the FMOVE's address at +8, MC68030UM Figure 10-43): Dirk's core defers it, local queue patch 16 (not yet proposed) takes it at once and RTE resumes at the scan PC |
| 114 | O | packed zero (no SoftFloat) |
| 115 | C | group-2 format $2 instruction address |
| 116 | O | odd-target checks already gated |
| 117 | G | 030/040 odd JSR target uses 020 semantics |
| 118 | O | no MOVE16 frame latches |
| 119 | M | JIT 040 cache-line retirement on a CACR write; the hook exists (`didChangeCACR`), unused while the JIT and the 040 cache model are out |
| 120 | M | JIT JSR helper |
| 121 | C | bit-field width computed unsigned (Dirk's e9a083d) |
| 122 | O | no data-buffer latch |
| 123 | M | JIT fixup |
| 124 | M | restart state in save states |
| 125 | M | write probe on TT asymmetry |
| 126 | O | no stale WB3D |
| 127 | C | level-7 edge (also applied to the 68000/010, unlike the fork) |
| 128 | C | FMOVE.P of a zero raises OPERR for k > 17 (PR #44) |
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
| 144 | G | the SR/CCR refill fault is not deferred on the 030/040; MOVE to SR/CCR refill through the i-cache (PR #43) and MOVE to SR flushes the latch (PR #46); ANDI/ORI/EORI to SR still read raw |
| 145 | C | M-set interrupt throwaway frame |
| 146 | O | translation-unit split already his |
| 147 | C | split: the 020 i-cache and its fetch timing are covered (PR #45: hit 0, miss +2, narrow port +4); Mac parts (DSACK_32 longword, save-state visit, JIT refusal) M |
| 148 | O | his compatibility modes |
| 149 | O | CALLM/RTM stubs (see 25) |
| 150 | O | no SoftFloat |
| 151 | C | MOVES releases SFC/DFC |
| 152 | G | DBcc expired read bypasses the i-cache; carried by queue patch 1 (local) through `readStatusRefill` |
| 153 | C | DIVS.L INT64_MIN/−1 |
| 154 | G | left: the ANDI/ORI/EORI to SR refill (a DATA-space read at pc+2) and `jumpToVector`'s first handler fetch; MOVE to SR/CCR covered (PR #43), TRAPV by queue patch 1 |
| 155 | G | split: 68EC040 ACU — PTEST/PFLUSH Line-F, IACR/DACR names |
| 156 | C | PC-relative reads in program space (his 040 too; the fork keeps data space there) |
| 157 | G | split: on the 68020 a host /BERR on a PC-relative operand is a data cycle (PR #42, C); the 68030 still classifies by FC (FB/RB); continuation parts O |
| 158 | G | 040: a not-taken FBcc/FDBcc with an odd displacement still raises nothing (MC68040UM 8.2.2), FDBcc decrements first; the taken case faults at the instruction since PR #47 |
