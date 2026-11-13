# Oracle disputes — arbitration log

**Read this first.** This file is the *permanent record* of rulings
D1-D22, taken in 2026-07 while POM68K still ran **two** oracles. Since
**2026-07-15 there is one**: the MAME/Musashi oracle was retired and
`oracle/musashi/` deleted (rationale at the bottom, § *Musashi retired*).
So:

- Every `oracle/musashi/VENDOR.md` reference below points at a **deleted
  tree** — read it as "the Musashi side was patched at the time", not as
  a file you can open.
- The arbitration rule quoted in the older sections (*majority wins;
  WinUAE + manual outrank Musashi alone*) has collapsed into the current
  one: **the WinUAE oracle's word is law**, Moira-vs-oracle disputes are
  arbitrated manually against the MC68030UM / MC68881-882UM, and a
  real-hardware trace outranks everything.
- The rulings themselves stand. They were established by ctypes probes
  (`oracle_driver.py`) before any code changed, and Moira still
  implements them; the "Musashi does X instead" halves are kept because
  they are *why* each ruling went the way it did.

> **Annotation, 2026-11-06 — evidence grades.** "The oracle's word is law"
> and "oracle wins over spec" below are the rule these rulings were taken
> under. It is superseded for NEW rulings by `CLAUDE.md` § *Evidence
> grades*: the oracle is grade D, a Motorola manual is grade B, and a lower
> grade never overrules a higher one. Rulings D12 and D23, which follow the
> oracle against a manual, are kept as known exceptions because a reversal
> turns pinned vectors red; `oracle/README.md` § *Rulings taken against a
> manual* says what would settle each. Where the manual is silent the
> oracle remains the best reading and nothing changes.

Oracle in service: **WinUAE** (`oracle/uae`, hatari e77819f7
`cpummu030.c` + gencpu). Retired: **Musashi** (MAME 0.276 `m68kmmu.h` +
kstenerud 4.60 core).

## D36. Access-log bytes of a format-$B frame after a fetch fault [fork kept, 2026-11-11]

Two-step vector, 68030: a JSR, then the opcode fetch at its target takes a
bus fault. The oracle's frame carries idx/idx_done = $11 and the JSR's
pushed return address in the first access-log slot; the fork stacks zeros
(three bytes differ: frame offsets $37 and $59-$5B). WinUAE
`m68k_run_mmu030` @ Hatari e77819f7 resets `mmu030_idx_done` and
`mmu030_idx` only AFTER `x_prefetch(0)` returned, and never clears
`mmu030_ad[]`: a fault on the opcode fetch stacks the log of the PREVIOUS
instruction. Those bytes describe no access of the faulting fetch, and the
oracle's RTE ignores them for this frame (opcode storage -1 restarts from
the fetch). The fork resets the log before the fetch, so the frame does
not depend on what ran before — which is also the only form the JIT can
keep exact, since native blocks do not maintain the log.

[F] MC68030UM-1989 §8.4 documents these words as internal; a real 68030
puts something there that neither side models. No oracle patch, no corpus
exception: single-step vectors are unaffected (`oracle_set_state` zeroes
the log, VENDOR.md patch 6) and no pinned multi-step vector has this shape.

## D35. A fault on a JSR's target fetch belongs to the next instruction [ruled: WinUAE, 2026-11-11]

The fork read the first word of a JSR's target after the push and took a
fault on that read inside the JSR; its 68040 format-$7 frame stacked the
return address (the "last write" PC), so an RTE skipped the call. The
oracle retires the JSR and faults at the next opcode fetch with the target
as stacked PC. Fork changed (POM68K_VENDOR.md row 87): the read stays for
its I-cache/ATC accounting, its fault is dropped and retaken by the next
instruction start. 962 + 950 targeted vectors; `moira_rules_test`
jsrTargetFetchFault. [D] only — no manual passage was consulted.

## D34. MOVEM CM reuses EA only for indexed/PC-relative modes [ruled: primary manual, 2026-11-09]

MC68040UM-1993 §8.4.6.2 (PDF p. 238) explicitly limits stacked-EA reuse
to mode 110 and mode 111/register 010 or 011. Section 8.4.6.7 (PDF p. 240)
restores CM continuation and repeats the completed operand prefix. WinUAE
`gencpu.c start_mmu040_movem` @ Hatari e77819f7 emits saved-EA reuse for
all MOVEM modes, including plain (An), displacement and predecrement. The
primary manual’s specific mode list wins over that blanket oracle rule.

The fork also fixes its missing RTE-to-CM publication and avoids repeated
full-index indirection. Those two agree with the pinned oracle’s
`cpummu.c m68k_do_rte_mmu040` and generated indexed MOVEM handlers.
A 4 848-sequence bare-core witness changes An/index/pointers, clears CM or
edits the frame EA; it checks exact replayed bus operands and final registers.
The initial matrix fails 5 952 checks. Natural re-fault and fresh-CPU restore
cover the instruction boundary; the JIT witness checks threaded continuation
and the running host’s existing native MOVEM latch guard.

This is a primary-manual/source-reading ruling with host callback witnesses,
not a patched-oracle differential capture. No oracle patch or corpus exception.
The normal coarse cycle model is retained; exact continuation timing and pin
waveforms are not claimed by these tests.

## D33. Table U writes follow the next descriptor read [ruled: primary manual, 2026-11-09]

WinUAE `cpummu.c:695–742`, `mmu_fill_atc` @ Hatari e77819f7, writes each
root/pointer U bit before reading the next descriptor. MC68040UM-1993
Figure 3-10 (PDF p. 60) explicitly schedules that write after the next
level's descriptor read. If the next read fails, its predecessor's U bit
must remain untouched. The primary rule wins.

The fork retains the previous table descriptor/address and commits its U
write only after the next read succeeds. Shared physical table reads also
sequence their two host words. The initial 13 632-case matrix has 11 808
incorrect table prefixes and 3 648 incorrect history images before this
change, alongside 14 688 ordinary fault/negative-retention failures. The
expanded 14 112 cases include first-/second-half read and U/M write faults,
indirect pages, PTEST data/code domains, actual program fetches, split data
and M re-walk hits. Ordinary descriptor errors also now use the table-search
unwind already used by PTEST: an R=0 entry and original-access ATC fault,
with context cleanup proved by a following physical /BERR before another
walk. PTEST keeps MMUSR.B without an exception.

This is a primary-manual/source-reading ruling with host callback witnesses,
not a new patched-oracle differential capture. No oracle patch, corpus
exception or regenerated vector hides the distinction. Descriptor LOCK/RMW
pin waveforms are not newly modeled by the U-order correction.

## D32. Split-page MA requires an ATC fault [ruled: primary manual, 2026-11-09]

WinUAE `cpummu.c:989–997`, `misalignednotfirst` @ Hatari e77819f7,
restores the initial address and unconditionally sets `MMU_SSW_MA`.
The unaligned read/write wrappers invoke it from later-fragment exception
handlers without checking the fault origin. MC68040UM-1993 §8.4.6.2
(PDF p. 238) reserves MA for a second-page ATC fault. §8.4.6.4 (PDF p. 239)
independently requires the initial logical FA for every misaligned fault.
The primary rule wins: keep FA, qualify only MA with the ATC origin.

The fork's initial 8 592 `cache040_test` sequences fail 2 016 frame checks,
all on external data/fill errors after an earlier fragment. Controls cover
first-/second-page nonresident, write-protected and supervisor-only ATC
faults; .W/.L reads/stores; flat/4K/8K aliases; both privileges; three EA
modes; cache-off/NC/fill paths; exact transferred prefixes and software
WB3/RTE completion on safe RAM. The existing pending-store and restart
policy is unchanged. Descriptor-cycle /BERR qualification during an ordinary
table walk needs a separate witness; these ATC controls use descriptor
validity/protection. This is a source-reading/manual ruling held by host
callbacks, not a new patched-oracle differential capture. No corpus
exception, regenerated vector or oracle patch hides the distinction.

## D31. Failed 040 CAS2 has a terminal write [ruled: primary manual, 2026-11-09]

WinUAE `gencpu.c i_CAS2` / generated `cpuemu_31.c op_0cfc/op_0efc` @
Hatari e77819f7 read both operands and update Dc when either comparison
fails, but issue no write. MC68040UM-1993 §7.4.5 (PDF p. 168) explicitly
requires a single write of the second operand read to terminate the locked
sequence, including when the first comparison failed. CAS already has this
transfer in the oracle. The primary rule wins for CAS2.

A same-value write does not change the bytes a RAM-only vector records.
`cache040_test` therefore records bus transfers, and faults each terminal
host beat. Its 1 814 sequences include 78 unchanged pre-040 controls and
1 736 040 cases with flat/4K/8K mappings, both privileges and cache states.
The old core fails 3 752 checks. The fork now emits the write through the
usual final-store funnel before Dc updates. That ordering and next-PC
fault policy follow pinned CAS; applying them to CAS2 is an explicit F
pending a hardware terminal-write /BERR capture. D31 establishes the bus
transfer, not that internal pipeline ordering. No corpus exception or
regenerated oracle payload is introduced.

## Arbitrated (2026-07-15) — first arbitration turn

All five O4-slice-1 disputes were re-run against WinUAE. WinUAE + the
manual won every one; the **Musashi oracle was patched to match**
(`oracle/musashi/VENDOR.md`) and **Moira was moved off the old
Musashi-solo quirks** (`extern/moira/POM68K_VENDOR.md` § arbitration).
The gate corpora (`tests/data/sst68030/{mmu,core,random}_off.json`) are
now **two-oracle-agreed**; the old Musashi-solo `mmu_off.json` and the
interim `mmu_off_duo.json` are gone.

### D1. MMU instructions ARE privileged  [ruled: WinUAE + manual]
User-mode PMOVE/PTEST/PFLUSH/PLOAD all take a privilege violation
(vector 8, format $0 frame, instruction address) on WinUAE — the S check
happens **before the extension word is fetched** (gencpu emits
`if (!regs.s) Exception(8)` at the top of every MMUOP030 handler).
Musashi's `pmmu` op had no FLAG_S check → patched; Moira's
`execPGen` now starts with `SUPERVISOR_MODE_ONLY`.

### D2. PMOVE MMUSR,Dn does not exist  [ruled: WinUAE + manual]
WinUAE raises **Line-F** (vector 11): register-direct — like An, (An)+,
-(An), #imm and PC-relative — is not a legal MMU-instruction EA
(`mmu_op30_invea`). The "full register replace vs low-word merge"
question is therefore unreachable on a real 68030. Both Musashi and
Moira now validate the EA (and the rest of the extension word, see D6b)
and trap Line-F. Musashi's quirky `WRITE_EA_16` is no longer reachable
from MMU ops.

### D3. Long-indirect walk: second long at +4, and indirection detection
[ruled: WinUAE + manual]
Two Musashi bugs, both fixed:
- the second long of an 8-byte indirect target was re-read from +0
  instead of +4 (`tbl_entry2 = read_32(*addr_out)` twice);
- indirection was detected with an **unmasked** `bits >> bitpos`, which
  still contains the already-consumed upper TI fields — so a descriptor
  with DT=2/3 at the last populated level was only treated as indirect
  at TID. WinUAE's `last_table` semantics = "next TI nibble is zero";
  both walks now mask the nibble.
Probe: long-format identity tree + indirect entry; a translated MOVE.B
lands at the page addressed by the long at target+4 on WinUAE.

### D4. Invalid descriptor ORs into MMUSR  [ruled: WinUAE + manual]
Hitting DT=0 sets I **without erasing** flags accumulated on the way
down: PTESTW through a write-protected table descriptor into DT=0 gives
MMUSR `$0C02` (WP|I|2 levels) on WinUAE, per § 9.7.2.6. Musashi assigned
(`= I`) and lost the WP bit → patched to `|=`; Moira likewise.

### D5. PTEST/PLOAD walk with TC.E=0  [ruled: both oracles agree]
WinUAE also runs the table search with translation disabled (TC=CRP=0 →
MMUSR `$0400`, no memory traffic). Musashi/Moira behaviour confirmed, no
change. (Refinements that DID land while converging the PTEST path:
level 0 searches TT + ATC only; level > 0 walks the tree directly with
**no TT match and no fc=7 bypass** — WinUAE `mmu030_table_search` has
neither; PLOAD identically walks directly.)

### D6 (window + cp stubs), D6b (extension-word strictness)  [ruled:
WinUAE + manual — Moira's strict reading confirmed]
- Only **$F000-$F03F** (EA field up to `(xxx).L`) decodes as a 68030 MMU
  op (WinUAE table68k `MMUOP030 1111 0000 00ss sSSS`); $F040-$F1FF and
  the mode-7-reg>1 encodings $F03A-$F03F are Line-F **even in user
  mode**. Musashi routed $F000-$F1FF to the MMU group → windowed.
- Musashi's five `cp*` stubs (cpBcc/cpDBcc/cpGEN/cpScc/cpTRAPcc) were
  silent no-ops on 020+; a 68030 with no coprocessor fitted takes
  Line-F → patched (`m68k_in.c`).
- Extension-word strictness (WinUAE `mmu_op30_*`): nonzero PMOVE low
  byte, rw+fd, fd on MMUSR, bad preg, undecodable FC field (bits 4-3 =
  11), PTEST level 0 with A=1, PFLUSH modes other than
  $00/$02/$04/$10/$18, PFLUSHA with nonzero fc/mask bits, PLOAD with
  nonzero bits $1E0 — all Line-F. Formats 101/110/111 are **silent
  no-ops** (WinUAE `mmu_op30` has no case for them).
- Decode order quirks, replicated everywhere: the EA is computed
  **before** validation (extension words consumed; `(An)+`/`-(An)`
  adjustments survive the Line-F trap — probes P10b/P12); the FC field
  decode is loose for `000xx` (SFC/DFC on bit 0 alone, so fc=`00010`
  reads SFC).

### Vector 56 frame  [ruled: WinUAE]
The MMU configuration exception pushes a **format $0** four-word frame
with the next PC (probe: SR/PC/$00E0). Musashi's `m68ki_exception_trap`
pushed format $2 → patched; Moira's `execMmuConfigError` now writes
`writeStackFrame0000`.

### MOVEM.L/W list,-(An) with An in the list  [ruled: WinUAE + M68000PRM]
On 020+ the value stored for the base register is the **initial value
decremented by one operation size** (initial-4 for .L, initial-2 for .W,
regardless of the register's position in the list). Musashi stored the
initial value → patched (`m68k_in.c` movem re/pd); Moira stored the
running EA → patched (`MoiraExec_cpp.h`). sst68000 (68000 semantics:
initial value) still passes 1 000 058/1 000 058.

## Arbitrated (2026-07-15) — O4 slice 3, bus-translation turn

### D9. Translated-access bus-fault frames  [ruled: WinUAE + manual;
Musashi architecturally incapable — solo-arbitrated corpus]
On a translated access fault WinUAE aborts the instruction at the
faulting (sub-)access and stacks a 68030 format $A frame (fault on the
instruction's LAST WRITE: PC = next instruction, CCR already updated,
(An)± kept) or a format $B frame (anything else: PC = faulted
instruction, CCR and pending (An)± fixups restored), with the real
internal-state words — access-value log, fixup encodings in the
wb2/wb3 status bytes, MOVEM counter, SUBACCESS flags for unaligned
splits, disp-store words (MC68030UM § 8.1.4 backs the architectural
fields). Musashi (MAME lineage) instead runs the faulted instruction to
completion (reads ~0, writes dropped), restores ALL D/A registers, and
pushes mostly-zero frames with its own $A/$B choice heuristic — every
fault vector is auto-quarantined and patching this would mean rewriting
its fault model. Ruling per the majority rule (WinUAE + manual outrank
Musashi alone): the **fault-coverage gate corpora
(`tests/data/sst68030/fault_{identity,tt}.json`, `--family fault`) are
generated from WinUAE solo** and are pinned as arbitrated truth; Moira
replays them 100 %. Musashi convergence on the fault model is a
follow-up, not a blocker.

Two determinism defects were fixed in the WinUAE oracle while ruling
(both `POM68K:` marked, `oracle/uae/VENDOR.md` patches 5-6):
- `m68k_run_mmu030`'s pre-instruction CCR capture (`struct flag_struct
  f`) is written between setjmp and longjmp without volatile — after a
  fault the restored CCR was the *register* value at setjmp time
  (always 0 in this build), not the captured one. C-standard clobber,
  not 68030 behaviour → `volatile`, so $B frames now stack the
  pre-instruction CCR the code intends.
- the instruction-restart globals that leak into $A/$B frames
  (mmu030_ad log, data buffer, disp store, wb* registers, stage-B
  address) persist across steps upstream; `oracle_set_state` now zeroes
  them so fault frames are deterministic from the initial state.

### D10. ASR past the operand width keeps C/X on the 020+  [ruled:
both 68030 oracles agree — 68000 rule was leaking]
The SST/680x0-derived 68000 patch "shifting past the width clears C and
X even for negative values" (flagged in POM68K_VENDOR.md for
re-verification) was applied to every core. Both WinUAE and Musashi
keep C/X = the last bit shifted out (the sign) on the 68030. The clear
is now 68000/68010-only (`MoiraALU_cpp.h`); the SST 68000 corpus stays
at 1 000 058/1 000 058.

### MOVES through translation  [covered]
`--family core` now generates MOVES.B/W/L in both directions
((An)/(An)+/-(An)/d16); both oracles agree through translation (SFC/DFC
select the walk's function code and root pointer; MOVES Rg,Ea is a
last-write $A fault with the FULL source register in the data output
buffer). The fault family aims MOVES at bad pages too.

## Arbitrated (2026-07-15) — O4 slice 4, integer-family stragglers

Sweep: 3 000 duo `random/off` + `core/off` states (seeds 101/202/303,
n=500) → 88 disputes, all categorized by the initial-RAM opcode at PC
and reproduced with minimal ctypes probes before any patch. Every
category below was arbitrated per the standing rule; **WinUAE won every
call** (its integer undefined-flag tables in `newcpu_common.c` are
hardware-verified and self-consistent). Both the **Musashi oracle**
(`oracle/musashi/VENDOR.md` § slice 4) and **Moira**
(`extern/moira/POM68K_VENDOR.md` § slice 4) were converged; the 68000
rules pinned by SingleStepTests are untouched (D10 gating pattern).

### D11. Odd-PC address errors  [ruled: WinUAE; Musashi architecturally
incapable — standing A-solo arbitration in the fuzzer]
A control-flow instruction with an odd target (Bcc/BRA/BSR/DBcc/JMP/
RTS/RTD/RTR/RTE) raises **vector 3 with a 68030 format $B frame** on
WinUAE: SSW = $0066 (RW|SIZE_W|FC 6 — constant, S is already set when
evaluated), fault address = the odd target, restart state zeroed, and
the per-instruction access log (consumed extension words / popped
longs) in the internal words. The stacked PC follows each handler's
convention (probed): Bcc/BSR/RTS/RTD/RTE = the instruction, JMP = the
instruction + 2 (every EA mode), RTR = the instruction + 2, DBcc = the
odd TARGET. Quirks replicated in Moira: BSR decrements A7 *without
writing* the return address; DBcc faults **even when the loop counter
expired** (the odd check precedes the counter test); RTR/RTE apply the
popped CCR/SR first; pops stay popped; **JSR does not fault at all**
(WinUAE mmu030 defers the fault to the next instruction's opcode fetch,
so a single-step vector simply ends with an odd PC — Musashi agrees).
Musashi is built without 68030 address errors (68000-frame model only,
its own doc'd limitation) and jumps to the odd address; replicating the
WinUAE frame builder + per-instruction conventions inside Musashi would
be a rewrite → per the D9 precedent, `fuzz030.py` now carries a
**standing-ruling classifier** (`ruled_for_a`): a dispute where B ends
at an odd PC and A took vector 3 is emitted from **WinUAE alone**,
tagged `[D11 odd-PC address error]` in the vector name. Moira replays
these frames byte-for-byte (odd-target checks in the nine handlers +
`execAddressError030`).

### D12. Group-2 format $2 frames stack the NEXT instruction address
[ruled: WinUAE vs manual reading — oracle wins]
Zero divide, CHK/CHK2 and TRAPcc/TRAPV push a format $2 frame whose
"instruction address" field holds the **next** instruction's address on
WinUAE (same value as the stacked PC; `Exception_cpu` oldpc after the
extension words — probed for DIVU.W #0, DIVS.L, CHK.W, TRAPV, TRAPcc).
MC68030UM reads as "the instruction that caused the exception" and
Musashi stacked REG_PPC → Musashi's `m68ki_stack_frame_0010` and
Moira's C68020 `writeStackFrame0010` call now push the next PC.

### D13. DIV zero/overflow undefined CCR  [ruled: WinUAE tables]
Probed matrices (both CCR-in all-clear and all-set):
- **DIV.W by zero** (`divbyzero_special` 020/030): CZNV cleared, then
  signed: Z=1; unsigned: V=1, N/Z from the SIGN/ZERO of the dividend's
  HIGH word ($1234 → Z; $80000000 → N; $7FFFFFFF → neither).
- **DIVS.L/DIVU.L by zero**: signed N=0 Z=1 C=0 **V untouched**;
  unsigned N/Z from the LOW 32 dividend bits, V=1, C=0.
- **DIVU.W overflow** (`setdivuflags`): V=1, N set if dividend<0;
  Z/C/N-if-positive UNTOUCHED.
- **DIVS.W overflow** (`setdivsflags`): CZNV cleared, V=1; if not an
  "absolute overflow" (|dividend|>>16 < |divisor|), N/Z from the low
  BYTE of |quotient|. **$80000000/-1 is a plain overflow** (register
  unchanged) — Musashi wrote quotient 0/Z; also fixed in Moira's
  `divlsMoira<Word>` (DIVS.L 32/32 wrote 0 to both registers).
- **DIVS.L/DIVU.L overflow**: WinUAE `divsl/divul_overflow` rules
  (64-bit form has its own Z/N table). Musashi only set V; its 64-bit
  signed path also divided INT64_MIN/-1 (C UB → SIGFPE) — guarded.
Musashi patched with the full table; Moira already carried the
UAE-derived helpers (`setDivZero*`, `setUndefined*`) but its exec paths
used the SST-68000 rules for every core → now `if constexpr
(C68020)`-routed to the helpers (68000 path untouched).

### D14. CHK / CHK2 / CMP2 undefined CCR  [ruled: WinUAE tables]
- CHK (020/030 `setchkundefinedflags`): CZNV cleared, Z=(value==0),
  N=(value<0) **always refreshed** (Musashi left N untouched on the
  no-trap path); on a trap V = overflow of bound−value (operand-sized)
  and C = value<0 ? (value>bound || bound>=0) : (bound>=0).
  Moira's imported copy of this very table had **dropped WinUAE's
  `SET_NFLG(dst < 0)` line** — caught by fresh-seed replay (six SR
  fails), fixed in `setUndefinedCHK`.
- CHK2/CMP2: Musashi gained WinUAE's `setchk2undefinedflags` N/V rules
  (it computed only Z/C); an ADDRESS-register compare operand is used
  full-width (Musashi masked it to the operand size) and byte bounds
  are sign-extended (pcdi/pcix variants read them unsigned).
- Both oracles are now built with **`-fwrapv`**: the CHK/CHK2 tables
  subtract bounds that overflow `sint32` by design; WinUAE's build
  already wrapped, GCC folded Musashi's copy into `bound < val` and the
  V bit diverged (probe: CHK.L D2,D3 with bound $825D428F).

### D15. BCD undefined results/flags  [ruled: WinUAE]
- **SBCD digit correction fires on BORROW** (low-nibble bits 4-7 set),
  not on digit>9: SBCD $3D−$22 = $1B on the 68030 (Musashi's >9 rule
  gave $15). ABCD digits agreed; flags didn't:
- 020/030 flag rule: **V always cleared**, N from the result, Z sticky
  (Musashi kept its 68000 `~res & res` V quirk and NBCD N quirks).
- **NBCD has no $9A short-circuit**: NBCD $FF with X=1 gives $9A,
  X=N=C set (Musashi returned the operand untouched with N only, via
  its `res != 0x9a` special case). NBCD $00/X=0 leaves NZVC clear.
Musashi's 12 BCD handler bodies now call WinUAE-shaped helpers on
020+; Moira's `bcd()` was already UAE-derived (V=0 on 020 confirmed)
and needed nothing.

### D16. PACK/UNPK -(An) byte order  [ruled: WinUAE + big-endian sanity]
The memory forms transfer a big-endian WORD: PACK reads high byte at
Ay-2/low at Ay-1; UNPK writes low byte at the FIRST predecrement
(higher address). Musashi assembled/wrote both swapped (PACK of
$12 $34 packed $42 instead of $24, every UNPK mm wrote a byte-swapped
word). Musashi patched; Moira's non-Musashi paths were already correct.

### D17. Reserved full-extension-word I/IS = 100 does NOT dereference
[ruled: WinUAE]
3 000-case LEA probe: every full-format divergence had I/IS = 100
(reserved per M68000PRM Table 2-2). WinUAE's `get_disp_ea_020` only
fetches memory when the outer-displacement bits (I/IS & 3) are nonzero,
so 100 behaves as "no memory indirect" (base + bd + Xn); Musashi and
Moira treated it as postindexed-null and dereferenced. Both patched
(`m68ki_get_ea_ix`, `computeEAfull`); the legal 001-011/101-111
encodings agreed before and after.

### D6-remainder resolved: user-mode cpSAVE/cpRESTORE shapes are
privileged  [ruled: WinUAE `privileged_copro_instruction`]
Full $F000-$FFFF × user/super sweep: in USER mode the cpSAVE
($Fx00-$Fx3F window, `(op & $F1C0) == $F100`) and cpRESTORE ($F140)
shapes of EVERY coprocessor id take vector 8 before the no-coprocessor
Line-F — but only for the EA modes a real cpSAVE (An)/-(An)/d16/d8Xn/
abs) / cpRESTORE ((An)/(An)+/d16/d8Xn/abs/PC-rel) decodes; everything
else stays Line-F, and supervisor mode is always Line-F. Musashi's
F-line catch-all, its pmmu window rejects and Moira's `execLineF` now
run the same check.

### D7 resolved for the integer slices: the Musashi FPU is disabled
The LC II has no FPU and the WinUAE oracle is built `fpu_model = 0`,
but Musashi executed its compiled-in 68881 on $F2xx/$F3xx (and its
`move16`/68040-PFLUSH handlers leaked onto the 030). All now take the
F-line/priv path on the 68030 → the whole $F2xx/$F3xx space is
duo-agreed. The O5 FPU slice will re-enable both sides together.

## O5 slice 1 (2026-07-15) — 68881/68882 re-enabled in both oracles

Setup: WinUAE `fpu_model = 68882` + **softfloat** backend (`fpu_mode = 1`
→ `fpp_softfloat.c`, host-independent; oracle/uae/VENDOR.md), Musashi
slice-4 F-line gating reverted (`m68k_in.c 040fpu0/040fpu1`,
oracle/musashi/VENDOR.md § O5). New `--family fpu` in `gen030.py`
(arithmetic-weighted general ops in all 7 formats incl. packed, FMOVECR,
FMOVEM fp/ctrl, FBcc/FScc/FDBcc/FTRAPcc/FNOP, FSAVE/FRESTORE; FP
registers and operand memory seeded with ±0/±inf/qNaN/sNaN/denormal/
unnormal/normal specials). Corpora land in **`tests/data/sst68030_fpu/`
(pending dir)** — Moira has no FPU exec layer yet; they enter the gated
`tests/data/sst68030/` only when the O5 Moira slice replays them.

### Duo agreement, fpu family (seed 1)

| op class \ cell            | off (n=300) | identity (n=100) |
|----------------------------|-------------|------------------|
| basic arith (dyadic+mono)  | 0/90        | 0/33             |
| transcendental             | 0/26        | 0/8              |
| FMOVE ea→FP (L/S/W/B/X/D)  | 0/3         | —                |
| FMOVE FP→ea non-P          | 0/37        | 0/8              |
| FMOVE FP→ea packed (P)     | 0/11        | 0/5              |
| FMOVECR                    | 0/16        | 0/5              |
| FMOVEM ctrl regs           | 10/20       | 3/6              |
| FMOVEM FP regs             | 0/24        | 0/13             |
| FBcc/FNOP                  | 5/7         | 1/3              |
| FScc/FDBcc/FTRAPcc         | 4/13        | 1/4              |
| FSAVE                      | 6/27        | 3/7              |
| FRESTORE                   | 7/26        | 1/8              |
| **TOTAL**                  | **32/300**  | **9/100**        |

Musashi's FPU is as weak as expected. `fpsr` appears in ~95 % of all
disputes; most agreeing FSAVE/FRESTORE/conditional vectors are the
user-mode privilege-violation paths (duo-converged since D6-remainder).

### D18. Per-instruction FPSR/FPIAR bookkeeping  [ruled: WinUAE +
MC68881/68882UM — Musashi does neither]
MC68881UM § 4.5.2.2: the FPSR **EXC byte is cleared at the start of every
general instruction** and set per the operation; § 4.5.2.3: the **AEXC
byte accrues** (AEXC |= f(EXC)) at the end of each; § 4.6: **FPIAR is
loaded with the instruction address** by every general-type instruction
(FMOVE(M)-control and conditionals leave it alone). WinUAE implements
exactly this (`fpp.c fpsr_clear_status()` at each general-op entry,
`fpsr_make_status()` accrual tables, `regs.fpiar = oldpc` fpp.c:769);
Musashi leaves the EXC byte stale forever, never accrues, and never
writes FPIAR outside FMOVE-to-FPIAR (probe #4: FTST — WinUAE clears EXC
74→00 keeping quotient/AEXC, Musashi only refreshes the CC nibble).
**WinUAE softfloat wins** (manual backs it verbatim); this single defect
taints nearly every fpu-family vector, so it is the first Musashi
convergence target of the next slice.

### D19. FMOVECR constant ROM  [ruled: WinUAE — hardware-dumped ROM]
WinUAE carries the real 6888x constant ROM (`fpp.c fpp_cr[22]` with
per-rounding-mode LSB offsets + INEX2 flags, and `fpp_cr_undef[]` — "68881
and 68882 have identical undefined fields", i.e. hardware-dumped values
for the undocumented offsets). Musashi computes host-double
approximations, differing in the guard bits and on every undocumented
offset (10/16 off-cell FMOVECR disputes had fp-value diffs; the rest
were D18 fpsr-only). MC68881UM Table 4-15 only lists nominal values, so
the hardware dump is the strongest evidence: **WinUAE wins**.

### D20. Default-NaN sign on created NaNs  [ruled: WinUAE softfloat —
oracle wins per policy]
An operation that *creates* a NaN (e.g. FREM inf, 0×inf) returns
$7FFF|FFFFFFFF|FFFFFFFF on WinUAE (sign 0, all-ones fraction — fpp.c
`xhex_nan`, softfloat validated against real 6888x by the WinUAE
project); Musashi's softfloat default-NaN has the sign bit SET (probe
#5, FREM: 7fff… vs ffff…). MC68881UM's NaN wording does not pin the
created-NaN sign explicitly → per the standing rule the (primary,
hardware-tested) oracle wins: **sign 0**.

### Observations — the classes Musashi could not testify on

Proposals when written. D22 below **promotes four of them** into the
WinUAE-solo `fpu` corpus (FSAVE/FRESTORE supervisor frames, packed
decimal, FDBcc/FTRAPcc, FMOVEM-fp); the last two were never arbitrated
and were settled by the retirement, not by a ruling. What survives here
is the WinUAE-side fact each one pinned, because Moira has to match it:

- **FSAVE/FRESTORE, supervisor forms** — WinUAE stacks the real 68882
  **$3C idle frame** (frame id `(version << 24) | 0x380000`, fsave_data
  internals, BIU word) on every legal EA. (Musashi wrote a fixed
  68881-style $1C frame and handled only (An)+/-(An) — same architectural
  gap as D9/D11.) User-mode FSAVE/FRESTORE privilege violations were
  agreed by both.
- **Packed decimal (P format, both directions)** — WinUAE has the full
  softfloat_decimal implementation; Musashi's sprintf-based model agreed
  on 0/16 vectors (k-factor and ram diffs on every FP→ea P vector).
- **FDBcc / FTRAPcc, FMOVEM of FP registers** — unreachable or
  address-buggy in Musashi, so WinUAE-solo by necessity.
- **FSGLDIV/FSGLMUL rounding** — mantissa LSB off-by-one vs WinUAE's
  proper single-precision rounding (probe #1). Never arbitrated against
  MC68881UM § single-precision semantics; **settled by policy** once the
  oracle became solo, not by reading.
- **FMOVE-to-FPCR masking**: WinUAE masks with `fpcr_mask` **$FFF0** — the
  6888x has no bits 3-0. Manual backs it.
- State-restore semantics: `set_state` masks FPCR/FPSR ($FFF0 /
  $0FFFFFF8) and leaves the FPU **"in use"** (WinUAE `regs.fpu_state = 1`),
  so a restored state FSAVEs an *idle* frame, not NULL — the premise of
  fix 1 in the O5 slice-2 list below.
- **Operational**: the `random` family executes $F2xx/$F3xx through the
  FPU instead of F-lining, so regenerating the integer corpora via
  `loop.sh` mints random-family vectors that need Moira's FPU exec layer
  (landed in O5 slice 2).

## O5 slice 2 + convergence (2026-07-15) — Moira 68882 exec layer,
WinUAE-solo fpu corpus adopted

Moira now executes the full 6888x set (`extern/softfloat/` vendored from
the WinUAE tree — deliberately the same softfloat family as oracle A, so
numerical convergence is by construction; `extern/moira/POM68K_VENDOR.md`
§ MC68882). Replay results:

- **Duo fpu corpora: 100 % on first contact** — pending 41/41 (seed 1)
  and fresh-seed 90/90 (seed 7: 67 off + 23 identity).
- **WinUAE-solo fpu cells** (seed 11, 500 off + 200 identity): initial
  replay 617/700; three Moira-side fixes → **700/700**:
  1. FSAVE after a state restore emits the $3C 68882 IDLE frame, not
     NULL — both glues restore the FPU "in use" (WinUAE
     `regs.fpu_state = 1`, Musashi `fpu_just_reset = 0`), so Moira's
     `setFP*` now leaves the FPU non-null. A freshly *reset* FPU still
     FSAVEs a NULL frame ($00380000, WinUAE fpp.c:2512).
  2. **D21** below (FRESTORE bad-frame stacked PC).
  3. Enabled FP-exception **post-instruction traps push the format $3
     frame** (12 bytes: SR, next PC, $3xxx vector word, operand EA) per
     WinUAE `Exception_build_stack_frame_common` (newcpu_common.c:1616)
     — matches the MC68030UM coprocessor post-instruction protocol, so
     no ruling needed; Moira's stub had used format $0.

### D21. FRESTORE format-error stacked PC  [ruled: WinUAE]
A bad/garbage FRESTORE frame takes FORMAT_ERROR (vector 14, format $0).
WinUAE stacks `m68k_getpc()` = the PC **past all consumed extension
words**; Moira's generic FORMAT_ERROR convention (`reg.pc - 2`, correct
for RTE — proven by the 1 040-vector gate) is 2 low. Moira now uses a
dedicated `execFRestoreFormatError` path. Musashi cannot testify
(fatalerror fall-throughs on most EAs, $1C 68881 frames). **WinUAE
wins** per the standing solo policy.

### D22. WinUAE-solo `fpu` class adopted  [ruled — promotes the
slice-1 proposals]
The proposed solo classes (FSAVE/FRESTORE supervisor frames, packed
decimal, FDBcc/FTRAPcc-unreachable, FMOVEM-fp address bugs) are now
**effective as one WinUAE-solo fpu corpus** (`fpusolo_*.json`), same
status as the D9 fault family: Musashi's FPU cannot testify on these
classes and the primary oracle is hardware-validated. The duo fpu cells
stay in the loop — every Musashi convergence patch (D18 FPSR
bookkeeping first) grows the duo side and shrinks what only the solo
corpus covers.

Fresh-seed re-verify then surfaced two FPU-through-MMU fault classes
(both WinUAE-frame replication details under D22, fixed in Moira):
the $B-frame FMOVEM restart bookkeeping (`state[1]` $4000/$2000 flags,
unlogged FP-block transfers, `fmovem_store` in access-log padding —
seed 17) and the restore-only `mmufixup` for FPU (An)± operands (An
restored on fault, status byte 0 — seed 19). Closed at seeds 19 and 23
= 100/100 each.

### Gate corpora after O5
`fpu_off.json` 99 (duo, seeds 1/7), `fpu_identity.json` 32 (duo, seeds
1/7), `fpu_tt.json` 11 (duo, seed 13), `fpusolo_off.json` 600 +
`fpusolo_identity.json` 300 (WinUAE-solo, seeds 11/17, ruling D22) —
`ctest -R sst68030` total **2 082**.

**Post-retirement repin (2026-07-15, Musashi gone)**: the duo/solo split
merged into the standard names — `fpu_off.json` **1 099** (duo 1/7 +
solo 11/17 + FRESTORE-frame-planted 29/31), `fpu_identity.json` **732**
(same seeds), `fpu_tt.json` **211** (duo 13 + solo 41, first full
FPU-through-translation cell: 200/200 on first replay). 12 corpus
files, `ctest -R sst68030` total **3 082**.

## Still unresolved / oracle limitations

### D8. Translation-enabled stepping  [moot since the retirement]
The 1-3 % residue in the `--mmu identity`/`tt` cells was entirely
Musashi's: mid-instruction bus-fault frame details on translated
accesses (it ran the faulted instruction to completion and zero-filled
the frame — the D9 gap) and PMOVE-through-translation faults (it halted,
PC $FFFFFFFF). With one oracle there is no residue to measure; the
behaviour is gated by the fault family and the identity/tt corpora.

### PMOVE TC write, E=1 with invalid config  [Musashi-followed,
WinUAE cannot arbitrate]
WinUAE **halts** on this (it keeps E=1, enables the half-decoded tree
and double-faults on its own fake prefetch — `oracle_step` returns -1),
so it cannot testify. Musashi/Moira keep the manual-flavoured
behaviour: value kept with **E cleared**, vector 56. Also note WinUAE
sums TI fields only up to the first zero nibble when validating
(`shift - page.size != 0`), Musashi/Moira sum all six fields; only
exotic TCs (nonzero field after a zero one) can tell them apart.
Re-open if a real-hardware trace shows up.

## Agreement snapshot (seeds 51/61/71, n=100/cell, 2026-07-15, post-slice-4)

| family \ mmu | off          | identity     | tt           |
|--------------|--------------|--------------|--------------|
| core         | 300/300      | 298/300      | 298/300      |
| mmu          | 300/300      | 292/300      | 292/300      |
| random       | 300/300 *    | 296/300 *    | 296/300 *    |
| fault (solo) | —            | 150/150 ×2   | 150/150 ×2   |

\* including the D11-ruled WinUAE-solo odd-PC vectors (8 + 4 + 4).

The **off column is fully converged** (also 1500/1500 core and
1472/1500→1500/1500 random on the slice-4 sweep seeds 101/202/303 after
the patches). Every remaining identity/tt disagreement is the
documented D8/D9 category: translated-access fault frames mid-
instruction (Musashi runs to completion / zero-fills, incl. faulting
SBCD -(A7),-(A7) fixups) and PMOVE-through-translation faults (Musashi
halts, final PC $FFFFFFFF). Moira replays **100 % of every agreed
vector**: 2 672/2 672 grid scratch + 2 000/2 000 slice-4 sweep +
1 040/1 040 pinned.

Gate corpora (`ctest -R sst68030`, 1 040 vectors, ~30 MB):
`mmu_off.json` 210 (seeds 3/5/7), `core_off.json` 160 (11/13),
`random_off.json` **250 (81/91, post-slice-4** — DIV/CHK/BCD/F-line
categories unlocked, 7 D11 odd-PC WinUAE-solo vectors**)**;
`core_identity.json` 69 (31/37), `mmu_identity.json` 69 (31/37),
`random_identity.json` **121 (81/91, post-slice-4, 3 D11 vectors)**,
`core_tt.json` 56 (41/43) — duo-agreed, translation on;
`fault_identity.json` 70 (42/7), `fault_tt.json` 35 (42) — WinUAE-solo
(D9), every vector ends in a translated bus fault ($A/$B frames,
WP/invalid pages, unaligned page-straddling splits, MOVEM/MOVES/RMW).

## Musashi retired (2026-07-15)

Ruling: the MAME/Musashi oracle (`oracle/musashi/`) is **retired** and
its vendored tree deleted. Rationale:
- **0 arbitrations won across D1-D22** — every contested ruling went to
  WinUAE (+ manual reading).
- Its fault model is **architecturally divergent** (D8/D9: faulted
  instructions run to completion, zero-filled $A/$B frames, halt on
  PMOVE-through-translation) and it cannot model 68030 address errors
  (D11).
- Its 6888x reaches only **~13 % agreement** (D18-D20: FPSR/FPIAR
  bookkeeping, FMOVECR ROM, default-NaN sign) and cannot testify on
  FSAVE/FRESTORE frames, packed decimal, FDBcc/FTRAPcc or FMOVEM-fp
  (D22).
- Patching it to the rulings cost more than its testimony was worth.

### D23. 030 FPU post-instruction frame format, and the $41 FRESTORE
hack's reach  [ruled: WinUAE — a spec-based overrule REVERTED]
Two 2026-08-17 Moira changes (the 020/030/040-closure merge) contradicted
the pinned corpus, and the red sat invisible for a day under a STALE
`sst68030` binary — surfaced 2026-08-18 by `tools/check_binaries_fresh.py`
on the first fresh relink, 3068/3082.

(a) The 030 post-instruction FPU exception frame was moved to the
MC68030UM's format $2 ("coprocessor instruction", instruction address).
The oracle pushes **format $3 with the operand EA on the 030 too**
(fpu_tt #63: frame word $30C4, EA $0006B34E), and a full replay of all
2042 pinned FPU vectors through the UNCHANGED oracle .so reproduces every
final bit-for-bit (`replay030.py`, new with this ruling) — so the pin is
the oracle's living word, not a stale capture. 10 of the 14 reds.

(b) An `&& fpuModel == M68040` guard was added to FRESTORE's version-$41
branch. WinUAE's "horrible hack" (fpp.c:2799-2802) compares the frame
against the 68040's version regardless of the CURRENT fpu model, so $41
frames on a 68882 take the 68040 retry branch ($41/$00 IDLE, $41/$30 and
$41/$28 skipped) instead of format-erroring. 4 of the 14 reds (fpu_tt
#166, fpu_off #28/#83, fpu_identity #75 — the oracle takes no exception
on any of them).

Both reverted; `fpu_sanity` check 14 — written alongside change (a) and
asserting $2 — re-pinned to $3-with-EA. **WinUAE wins per the standing
solo policy**: overruling the oracle on manual reading alone is exactly
what this section exists to forbid. Reopen either half only with a real
68030's own stacked frame. 3082/3082, `sst68040` 7200/7200 untouched.

From now on **all cells are WinUAE-solo**: the oracle's word is law,
and Moira-vs-oracle disputes are arbitrated manually (MC68030UM /
MC68881-882UM reading; oracle wins over spec; real-hardware traces
welcome and outrank everything). The duo/solo naming split in the
pinned corpora is history — existing pinned files replay unchanged and
merge into the standard `${family}_${mmu}.json` names on the next
repin (`loop.sh`). `fuzz030.py --b` keeps the differential slot open
should a second oracle return. D1-D22 above stay as recorded.

## The integrated 68040 FPU and the F-line window (2026-11-07)

Three families were added to `gen040.py`/`fuzz040.py`: `fpu` (gen030's
assembler-shaped generator on a full 68040), `fpuraw` and `fline` (the raw
cpID-1 window `$F200-$F37F` with unconstrained extension words, on the full
68040 and on the FPU-less 68LC040). First replays: `fpu` 2 963 / 3 000,
`fline` 3 074 / 4 500, `fpuraw` 3 962 / 4 500.

**The oracle was wrong twice before the core was.** (1) `glue.c` left
`fpu_no_unimplemented` false: WinUAE then executes FSIN or a packed operand
on a 68040 like a 6888x (first `fpu` replay 1 783 / 3 000). (2) `mmufixup[]`
leaked across vectors: a 68040 access fault restores whatever fixup is
armed, several fpp.c paths arm one and never disarm it, and a later
faulting vector had an address register overwritten from an earlier one
(seventeen `fpuraw` vectors, e.g. "040 fpuraw/identity seed=962 #1007",
FRESTORE through A5 leaving A4 changed). Both fixed in the glue
(`uae/VENDOR.md` patches 9 and 10); `sst68030_solo` and `sst68040_8k`
regenerate hash-identically afterwards.

**Followed the oracle** (`extern/moira/POM68K_VENDOR.md` rows 44-47): the
unimplemented-instruction trap outranks the datatype trap; no FPU
instruction ends on a "last write", (An)± is restored and FPIAR set before
the operand access; on a FPU-less 68040 what is no instruction takes a
plain Line-F (or vector 4 for opmodes $78-$7F, PC past the extension word)
instead of the format $4 frame; FBcc evaluates five predicate bits. After
them: `fline` 4 500 / 4 500, `fpu` 2 986 / 3 000, `fpuraw` 4 448 / 4 500,
and fresh seeds (971-973, 911-913, 981-983; 6 000 vectors each) replay at
6 000, 5 979 and 5 944 with every residual in the four classes below.

### D24. Delivery of an enabled arithmetic exception on the 68040  [applied 2026-11-07: manual + oracle]
The oracle's `fpsr_check_arithmetic_exception` returns "nonmaskable" only
for SNAN/OPERR on an integer move-out; every other enabled exception of an
opclass 000/010 instruction stays pending and is taken by the NEXT FPU
instruction as a pre-instruction exception. The fork stacks a format $3
frame at once, and `fpu040_test` says so ("Enabled arithmetic exceptions
are immediate on the integrated 040") without a source. 22 pinned vectors,
all of the shape "A7 got $17F4 want $1800". Not changed: the hand-written
gate would go red.

**The manual sides with the oracle** (read the same night, M68040 User's
Manual, 1993 revision, § 9.7; grade B):
- § 9.7.2.1 b, SNAN, p. 9-27: "the processor posts an exception and
  another floating-point instruction is eventually encountered; a
  pre-instruction exception is reported at that time."
- § 9.7.3.1 b, OPERR, p. 9-30: "an OPERR exception is posted. The next
  floating-point instruction that is encountered takes a pre-instruction
  exception."
- § 9.7.6, divide by zero: "the exception is reported as a pre-instruction
  exception when the next floating-point instruction is attempted" (the
  manual's a/b labels for enabled/disabled are swapped there).
- § 9.7.4.2, overflow, register destination: "either a pre-instruction or
  a post-instruction exception is reported"; the post-instruction case is
  the one where an FMOVE OUT depending on the result is already executing
  (the FADD FP2,FP0 / FMOVE FP0,<ea> example). Memory or integer
  destination: "a post-instruction exception is taken immediately".
- § 9.7.3.2 and 9.7.2.2: SNAN/OPERR on a move to byte, word or long are
  nonmaskable and "taken immediately" — the oracle's one immediate case.
- State frame fields, T bit: "only an opclass 3 instruction can indicate a
  post-instruction exception".
So the ruling flips to the oracle: pending, format $0, at the next FPU
instruction.

**Applied the same night** (`POM68K_VENDOR.md` row 50), once two-instruction
vectors existed to hold the second half (`fuzz040.py --steps 2`, family
`fpu2`: an FPU instruction, then FNOP / FMOVE / FTST / FADD / FSAVE /
FMOVE FPSR,D0 / NOP / MOVEQ, FPCR enables armed seven times in ten).
6 000 vectors: 5 763 before, 5 997 after. The 22 single-instruction
vectors left `tests/sst68040_fpu.known`; fresh `fpu` seeds 911-913 replay
at 6 000 / 6 000. It turned out to be the delivery decision plus one rule
the immediate delivery had hidden — on a 68040 any overflow or underflow
leaves the destination register untouched — the pending machinery being
the 68882's. The three vectors that remain are not the ruling: an FSAVE
after a posted exception writes zeros where the oracle has the
intermediate result (WBT) and its guard bits (`tests/sst68040_fpu2.known`).

### D25. FMOVE.P to a data register on the 68040  [open — fork kept]
Oracle: unsupported-datatype trap ("68040+ generates unimplemented
effective mode exception even if destination EA is Dn or An", fpp.c
`put_fp_value`). Fork: Line-F, asserted by `fpu040_test` ("040
packed-to-Dn should take ordinary Line-F"). 27 pinned vectors.

### D26. (An)± step of a packed move-out with a dynamic k-factor  [open — fork kept, manual]
fpp.c steps An by `sz1[size]`, and `sz1[7]` is 0: FMOVE.P FPn,(An)+{Dn}
leaves An where it was, FMOVE.P FPn,(An)+{#k} adds 12. A packed operand
is twelve bytes in both forms (MC68881/MC68882 UM; M68040UM § 1, data
formats: "Packed BCD real data format (96 bits, 12 bytes)"); the fork
adds 12 in both. 16 pinned vectors. The manual outranks a table slot.

### D27. EA evaluated before a control-register Line-F  [open — fork kept]
`F23B A7B0`: FMOVEM control registers to a (d8,PC,Xn) destination. Both
sides take a Line-F; the oracle walks the memory-indirect EA first and the
page descriptor comes back Used. One vector.

### Not disputes: 68030 leftovers of the 60 000-vector sweep (seeds 801-846)
Recorded here so that they are not rediscovered; each is in `TODO.md`.
- `core/identity seed=802 #139`, `core/tt seed=803 #635`: MOVEM.L to an odd,
  faulting address — the $B frame's access counter reads $11, the oracle
  $22, and the saved-access slots differ. **Fixed 2026-11-10**: the fork
  stopped logging before the EA extension word (`POM68K_VENDOR.md` row 81);
  even addresses were affected too.
- `random/off seed=807 #681`: JMP (d8,A3,D4.L*8) to an odd target — the
  address-error frame stacks PC+2, the oracle PC+6. **Followed 2026-11-10**
  (`POM68K_VENDOR.md` row 82): the 68030 now uses the rule its 68040 path
  already had, running pc + 2 for the indexed modes; the manual is silent.
- `fpu/identity seed=811 #1003`: `F26E 0017`, an undefined opmode with a
  faulting EA — two bytes of the $B frame's tail differ. **Fixed 2026-11-10**: `F26E` is FScc
  (d16,A6), not an undefined opmode; fpp.c passes its byte unsigned, so the
  data buffer is $000000FF (`POM68K_VENDOR.md` row 83). With row 81 and
  row 82, seeds 802, 803, 807 and 811 now replay 8 000 / 8 000.
- `random/tt seed=809 #7, #1854, #1999`: FBcc taken to an odd target — the
  fork raises the address error inside the branch (as for Bcc), the oracle
  only sets the PC.

### Not disputes: the 68030 fresh-seed sweep of 2026-11-13 (seeds 5101-5114)
Strict oracle, 800 vectors per cell, the fourteen cells of `sst68030_solo`
in the same order: 11 171 / 11 174 on the first replay.
- `random/tt seed=5109 #261`: CAS2.W with a failing compare — the oracle
  marks both operand pages modified, the fork left U alone. **Fixed the
  same day** (`POM68K_VENDOR.md` row 94): on the 68030 CAS2 is now the
  locked read-modify-write CAS and TAS already were.
- `random/identity seed=5108 #157, #548`: FBcc taken to an odd target —
  ruling D28, the fork keeps the address error.
The sweep is not pinned: 11 172 / 11 174 after the fix, the two D28
vectors remaining.

### Open: the 68040 fresh-seed sweep of 2026-11-13 (seeds 5201-5208)
core, mmu, random and fault with identity and tt, 800 a cell, 4 KB pages:
6 399 / 6 400.
- `040 random/identity seed=5205 #483`: `$854B`, PACK -(A3),-(A2), access
  fault — the oracle leaves A3 at its entry value, the fork one byte
  lower. **Open** (`TODO.md`), not ruled.

## The raw F-line window on the 68030 + 68882, and the oracle's strict setting (2026-11-07)

`gen030.py` gained the `fpuraw` family (the generator gen040 used for
`fline`/`fpuraw`, moved there). First replay against the oracle as every
earlier corpus used it: 5 529 / 6 000.

**Two of the largest classes were the oracle's configuration, not the fork.**
WinUAE's `fpu_no_unimplemented` was false for the 68030 + 68882. In that
permissive setting a 6888x executes the 68040's FSxxx/FDxxx opmodes
($40-$77), sends $78-$7F to the illegal vector, and lets FRESTORE accept a
68040 frame — fpp.c's own "horrible hack to support on the fly 6888x <>
68040 FPU switching". With the option true, opmodes $40-$7F take a Line-F
("Only opmodes 0x40-0x7f generate F-line exception", fpp.c
`fault_if_unimplemented_6888x`) and a foreign frame a format error: what
the MC68881/MC68882 manual says, and what the fork already did for the
opmodes. `oracle_set_fpu_strict(1)` selects it (`uae/VENDOR.md` patch 12,
`fuzz030.py --strict-fpu`); the default stays permissive so that the pinned
corpora keep their recipe. Strict replay: 5 802 / 6 000.

**D23b, revisited.** The ruling "FRESTORE accepts a version-$41 frame on a
68882" was recorded as the oracle against the manual. It is the PERMISSIVE
oracle against the manual: the strict oracle takes the format error (three
vectors of this corpus, e.g. "fpuraw/identity seed=722 #311"). The four
pinned `sst68030` vectors that hold the fork's acceptance were generated in
the permissive setting. Manual (B) and strict oracle (D) agree. **Applied the same day**
(`POM68K_VENDOR.md` row 53): the `$41` branch is the 68040's only; the four
permissive vectors are listed in `tests/sst68030.known`, `sst68030_solo`
was regenerated strict, and the D23b blocks left `tests/sst68030_fpuraw.known`
and `tests/sst68030_fpu2.known`.

### D28. FBcc / FDBcc taken to an odd address  [fork kept, manual]
The oracle sets the PC and raises nothing — `fpuop_bcc`/`fpuop_dbcc` lack
the odd-target test gencpu gives Bcc/DBcc, and its fetch path never checks
— so a 68030 would run from an odd address. The fork takes the address
error inside the branch, as for Bcc (MC68030UM: an instruction fetch from
an odd address is an address error). 114 vectors; the frame itself cannot
be compared, the oracle has none.

### D29. EA fields $3D-$3F on the 68030  [open — fork kept]
WinUAE's opcode table holds no FPU opcode with those EA fields: plain
Line-F, nothing touched. The fork lets the coprocessor protocol decide: a
register-to-register operation never asks for the EA and runs; a memory
form clears the status byte and then takes the Line-F. On a 68030 the main
processor validates an EA only when the coprocessor asks for it, which is
the fork's reading; nothing at hand states it for these three fields. 63
vectors. (On the 68040, where the CPU decodes the instruction itself, the
fork follows the oracle — row 46.)

The corpus is pinned as `sst68030_fpuraw` (6 000 vectors, strict oracle)
with `tests/sst68030_fpuraw.known`: D23b 3, D26 15, D27 2, D28 114, D29 63,
and one open defect (two tail bytes of a $B frame).
