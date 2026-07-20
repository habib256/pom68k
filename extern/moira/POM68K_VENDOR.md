# Moira — vendored copy (POM68K)

Provenance: copied from **NeoST** (`/home/gistarcade/src/neost/extern/moira`,
NeoST commit lineage `0b96cab` → `47d6c39`), itself vendored from upstream
**Moira** by Dirk W. Hoffmann (<https://github.com/dirkwhoffmann/Moira>, MIT).
The NeoST patches (documented in `NEOST_VENDOR.md`, kept alongside) are
included: deferred IPL recognition (`setIplDelay`/`pollIpl`), STOP
level-sensitive IRQ re-check, exception-handling robustness, watchpoint
24-bit address masking.

## POM68K local changes

Unlike NeoST (which patches `MoiraConfig.h` at build time), POM68K edits the
vendored files directly — every divergence from the NeoST copy is listed here:

- `Moira/MoiraConfig.h`
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
(`oracle/musashi`, MAME 0.276 `m68kmmu.h`) — 2 900/2 900 `family=mmu
--mmu off` vectors across 8 seeds, gated by `ctest -R sst68030`
(`tests/data/sst68030/mmu_off.json`). Address translation on the bus is a
LATER slice; the registers round-trip (TC keeps E=1 values). Oracle-vs-
manual conflicts are logged in `oracle/fuzz/disputes/NOTES.md` (D1-D7)
for re-arbitration when oracle B (WinUAE) lands. Changes:

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
Musashi oracle itself was patched to converge (`oracle/musashi/
VENDOR.md`). Gate: `ctest -R sst68030`, 520/520 two-oracle-agreed
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
order (still open); FRESTORE of a 68040 BUSY frame ($41/$60) skips it
instead of resuming the interrupted op (see below).

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
  MC68882" footnote applied) — FPcr single moves 30-35, FPcr lists
  29+6n, FP-register FMOVEM 39+25n out / 37+31n in, +14 dynamic list.
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

**FSAVE BUSY frames — decision: never generated.** WinUAE's own 6888x
support has no busy-save path (fpuop_save, fpp.c:2374-2580 emits NULL
or IDLE only; the mid-instruction save window doesn't exist in its
coprocessor model), and the oracle is the convergence target. FSAVE
therefore stays NULL/IDLE-only, documented in `execFSave`.

**FRESTORE acceptance matrix** — mirrors `fpuop_restore`
(fpp.c:2593-2812) exactly under the oracle's config
(`fpu_model = 68882`, `fpu_no_unimplemented = false`,
`fpu_revision = 0` — oracle/uae/glue.c:60-84), oracle-verified:

| frame (1st long)  | behaviour                                        |
|-------------------|--------------------------------------------------|
| version $00       | NULL: full FPU reset (fpu_null)                  |
| $1F, size $18/$38 | 68881/68882 IDLE: reload ccr/eo/BIU micro-state; BIU bit 27 clear re-arms the pending exception (fpp.c:2781-2787) |
| $1F, size $B4/$D4 | 68881/68882 BUSY: skipped, `ad += size` (fpp.c:2788-2790) |
| $1F, other size   | format error, vector 14 (D21 PC convention)      |
| $41, size $00     | 68040 IDLE via WinUAE's version hack (fpp.c:2799-2802; get_fpu_version(68040) == $41): state = non-null, expState cleared |
| $41, size $28/$30 | 68040 UNIMP: skipped                             |
| $41, size $60     | 68040 BUSY: skipped (WinUAE resumes the op when CU_SAVEPC == $FE — not modelled, TODO in `execFRestore`) |
| $41, other size   | format error, vector 14                          |
| anything else     | format error, vector 14 (incl. $40 — it mismatches $41) |

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

## Odd-SP interrupt frames: no A0 masking on 010/020, single vector
## scaling (2026-07-17, Lode Runner launch freeze)

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
   faulted). Minimal repro: `scratchpad/oddframe.cpp` (bare Moira,
   odd SSP, one autovector IRQ; frame bytes now `[SR][PC][$0064]`
   byte-exact).

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
- **Q8 — Mac PACK 4 vs format $4:** `fpuDisabledSaneFline` (Cpu040 may set
  it) rewinds to the opcode and stacks classic format $0 Line-F so guest
  PACK 4 glue accepts the frame. Architectural default remains format $4
  (`sst68040`). Bare NONE + PACK 4 still ends in SysError 90 (dsNoFPU)
  without FPSP; Quadra NOFPU uses soft 68882 instead.
- **Q8 — 040 I/D ATC:** 32-entry separate I/D ATC overlay on
  `mmu040Translate` (flush PFLUSH*/TC/URP/SRP; `POM68K_MMU040_WALK=1`
  disables). U/M/WP semantics preserved vs walk-per-access.
  `POM68K_Q605_CACHE_BOOST` stays default 1 (boost 2+ fails SCSI
  bring-up); machine-cycle stall / VIA sync / SWIM C15M are boost-invariant.

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
  modelled: the oracle flushes its ATC on every state load, so
  walk-per-access is observably identical (idempotent U/M rewrites);
  PFLUSH stays a no-op, PTEST reports the walk (or TTR hit) in MMUSR040.
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

## Model support in this copy (`MoiraTypes.h`)

- 68000 / 68010 — cycle-exact execution ✓ (Mac Plus phase)
- 68EC020 / 68020 — functional execution ✓
- 68030 — functional execution of the **MMU instruction set** ✓ (O4
  slice 1) and of **bus-level address translation + ATC + bus-fault
  frames** ✓ (O4 slice 3, see above).
- 68881/68882 — **execution ✓** when attached via `setFPUModel()` (O5
  slice 2, see above; softfloat-backed, 68882 = LC II PDS FPU).
- 68LC040 / 68EC040 / 68040 — **integer execution ✓** (Q2: MOVE16,
  CINV/CPUSH, MOVEC 040 set, format $2 address errors, 040 trace and
  undefined-CCR rules), **no-FPU F-line ✓** (Q4, format $4) and the
  **040 MMU bus translation ✓** (Q3, section above: TTR + URP/SRP walk
  with U/M, page-split accesses, format $7 faults with the last-write
  dichotomy, MOVEM restart, PTEST). Remaining: a perf ATC overlay and
  the 8K-page (TC.P) cell, untested by the fuzzer (Mac OS uses 4K).

Do **not** re-sync from upstream blindly: diff against this file and
`NEOST_VENDOR.md` first.
