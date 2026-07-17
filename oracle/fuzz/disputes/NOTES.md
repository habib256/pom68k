# Oracle disputes — arbitration log

Cases where the two oracles — **WinUAE** (oracle/uae, hatari e77819f7
`cpummu030.c` + gencpu, PRIMARY) and **Musashi** (oracle/musashi, MAME
0.276 `m68kmmu.h` + kstenerud 4.60 core) — or the **MC68030 User's
Manual** disagree. Arbitration rule (CLAUDE.md): majority wins; WinUAE +
manual + real-hardware reasoning outrank Musashi alone. Every ruling
below was established by ctypes probes against both `.so`s
(`oracle_driver.py`) before any code changed.

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

### Observations / proposed WinUAE-solo classes (proposals, NOT rulings)

- **FSAVE/FRESTORE, supervisor forms** — Musashi writes a fixed
  68881-style $1C idle frame (`m68kfpu.c perform_fsave`: 7 longs,
  version $1F) with no BIU flags and only handles (An)+/-(An) (others
  `fatalerror`-fall-through); WinUAE stacks the real 68882 $3C idle
  frame (frame id `(version<<24)|0x380000`, fsave_data internals, BIU
  word) and every legal EA. Same architectural gap as D9/D11 →
  **propose a WinUAE-solo `fsave` class** once Moira's FPU frames exist
  to replay them. (User-mode FSAVE/FRESTORE priv violations are already
  duo-agreed.)
- **Packed decimal (P format, both directions)** — Musashi's
  `READ_EA_PACK/store_pack_float80` is a partial sprintf-based model
  (0/16 agreement, ram diffs on every FP→ea P vector, k-factor issues);
  WinUAE has the full softfloat_decimal implementation. **Propose
  WinUAE-solo for the P format** pending a Musashi rewrite.
- **FDBcc / FTRAPcc are unreachable in Musashi**: the slice-4 D6 stubs
  `cpdbcc`/`cptrapcc` (masks $F1F8) are MORE specific than `040fpu0`
  (mask $FF00) and win the generated jump table, so coprocessor-id-1
  FDBcc/FTRAPcc still raise Line-F (probes #2, #77: Musashi PC lands on
  the vector-11 pad, WinUAE traps 7 / falls through). FBcc and FScc DO
  dispatch into the FPU (`m68040_fpu_op0` cases 1-3). Fix is a small
  Musashi patch (route the id-1 shapes into the FPU conditional
  decoder + implement FDBcc/FTRAPcc over `test_condition`) — next
  slice, together with BSUN (Musashi never sets it for IEEE-nonaware
  predicates on a set NAN bit; the 2/7 FBcc fpsr-only disputes).
- **FMOVEM of FP registers**: 0/37 — Musashi mis-advances the address
  between registers in several mode/list combinations (probe #14: three
  target registers all loaded from the same address) and disagrees on
  dynamic-list decode (probe #58). Real convergence work, not solo-able.
- **FSGLDIV/FSGLMUL rounding**: mantissa LSB off-by-one vs WinUAE's
  proper single-precision rounding (probe #1). Not yet arbitrated
  against MC68881UM § single-precision semantics — left open.
- **FMOVE-to-FPCR masking**: Musashi stores all 16 bits (fpcr diffs in
  FMOVEM-ctrl disputes); WinUAE masks with fpcr_mask $FFF0 (6888x has no
  bits 3-0). Manual backs WinUAE; small Musashi patch, next slice.
- The glue layers already converge on state-restore semantics: both
  oracles mask FPCR/FPSR on set_state ($FFF0 / $0FFFFFF8) and treat the
  restored FPU as "in use" (WinUAE `regs.fpu_state = 1`, Musashi
  `fpu_just_reset = 0`) so FSAVE would emit idle frames on both.
- **Operational**: the `random` family now executes $F2xx/$F3xx through
  the FPUs instead of F-lining (probe: 99/100 duo at seed 99, the one
  dispute an FBcc predicate ≥ 32 / BSUN case). Regenerating the gated
  integer corpora via `loop.sh` will therefore mint random-family
  vectors Moira cannot replay until its O5 FPU exec layer lands — the
  pinned pre-O5 corpora still pass 1 040/1 040.

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

### D8. Translation-enabled stepping  [info; residue 1-3 % post-slice-4]
`--mmu identity`/`tt` cells keep a small disagreement: mid-instruction
bus-fault frame details on translated accesses (Musashi runs the
faulted instruction to completion and zero-fills the frame — same
architectural gap as D9, covered by the WinUAE-solo fault corpora) and
PMOVE-through-translation faults (Musashi halts, PC $FFFFFFFF). A
Musashi fault-model rewrite is the only fix; not a blocker (the fault
family + the duo identity/tt corpora gate the behaviour).

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

From now on **all cells are WinUAE-solo**: the oracle's word is law,
and Moira-vs-oracle disputes are arbitrated manually (MC68030UM /
MC68881-882UM reading; oracle wins over spec; real-hardware traces
welcome and outrank everything). The duo/solo naming split in the
pinned corpora is history — existing pinned files replay unchanged and
merge into the standard `${family}_${mmu}.json` names on the next
repin (`loop.sh`). `fuzz030.py --b` keeps the differential slot open
should a second oracle return. D1-D22 above stay as recorded.
