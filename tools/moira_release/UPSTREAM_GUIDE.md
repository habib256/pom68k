# Upstream guide — which fork changes concern stock Moira

This file sorts the fork's patch groups by their relevance to stock Moira, as
maintained at <https://github.com/dirkwhoffmann/Moira>: the 68000, 68010,
68EC020 and 68020 cores, the disassembler and the debugger. It is meant for
cherry-picking one change at a time. It does not replace the inventory.

How to read it:

- **Row N** is row N of the table *Inventory of local patches* in
  `extern/moira/POM68K_VENDOR.md`. That row lists the files, the reason and
  the gate. The journal sections after the table give the full rationale.
- **Where** names functions in `extern/moira/Moira/`. Every changed line
  there is marked `POM68K`, so `grep -n POM68K` inside a named function
  finds the change.
- **Evidence** repeats the grade recorded at the site (`CLAUDE.md` of
  POM68K: [A] hardware, [B] Motorola manual, [C] ROM, [D] another
  emulator with its revision, [E] unsourced, [F] admitted unknown).
  SingleStepTests/680x0 and WinUAE are both grade D.
- **Gate** is a test registered by this release (`ctest -R <name>`).
  `moira_rules_test (cell)` names a function in
  `tests/moira_rules_test.cpp`. Corpus gates (`sst68000`, `sst68030`, ...)
  carry the `corpus` label and soft-skip without their external data:
  `tests/sst_corpora.lock` and `tests/fetch_sst_68000.sh`.
- **Upstream at ce28239** is the result of reading upstream's code at commit
  `ce28239` (its master when this file was written). The fork is based on
  that commit since 2026-11-20: every upstream commit up to it is in the
  fork, and `patches/moira-vs-upstream.patch` applies to it with `git am`
  (`RELEASE.txt`, README § *Relation to upstream*).

Configuration matters. POM68K builds with `MOIRA_PRECISE_TIMING=true`,
`MOIRA_EMULATE_ADDRESS_ERROR=true` and `MOIRA_MIMIC_MUSASHI=false` (row 1).
Upstream's checked-in `MoiraConfig.h`, which upstream's `Runner/` uses
against Musashi, has the opposite three values. A local clone of vAmiga at
`f51a6bf9` ships the same three values as POM68K. Many fork changes live
only in the non-Musashi branch and are invisible to the Runner. Other
changes alter what the Runner compares (68000/68010 cycles, the ordered list
of memory writes with their FC, registers, SR). A ported fix of that second
kind will show up as a Runner mismatch without being a regression. Each
entry below says which kind it is, where that was checked. The Runner skips
the cycle comparison for the 68020 when `MOIRA_EMULATE_ICACHE` is set
(`compareCycles` in `Runner/Testrunner.cpp`). It also compares writes but
not reads (`Sandbox::replayPoke`).

## A — changes to behaviour stock Moira already has

These are the candidates for cherry-picking. A.1 lists the rows where
upstream at ce28239 still has the old behaviour. A.2 lists rows that touch
upstream code where upstream is already right.

### A.1 Still present upstream at ce28239

**Row 2 — 68000 convergence on SingleStepTests/680x0.** The 68000 has
address-error frames with the wrong idle cycles, stacked PC and register
side effects. It also has DIVU/DIVS overflow and divide-by-zero flags, CHK
flags, ASR past the operand width, ADDQ/SUBQ.l #,An at 8 cycles (the corpus
says 6) and BTST Dn,#imm at 8 (the corpus says 10). LINK A7 pushes the
undecremented A7 outside Musashi mode.
- Models: 68000. BTST Dn,#imm also gains 2 cycles on the 68010 and 68020
  cores (`execBitDxEa`), which goes beyond the 68000 corpus.
- Where: `execAddressError`, `makeFrame`, `readOp`, `readM`, `execAddxEa`,
  `execChk`, `execDivsMoira`, `execDivuMoira`, `divsMoira`, `divuMoira`,
  `execAddqAn`, `execBitDxEa`, `execLink` and the MOVE/MOVEM/CMPM handlers.
- Evidence: [D] SingleStepTests/680x0 68000/v1 @ e0d5ece9. Two items
  follow the corpus against a higher-grade reading: the early exit of every
  DIVS overflow, and ASR clearing C and X (`oracle/README.md`, end of
  § *Rulings where the fork does not follow the oracle*). LINK A7 has [B]
  support (M68000PRM-1992, LINK; ruling D42).
- Gate: `sst68000` (corpus).
- Upstream: still present. Checked items: `execAddressError` still syncs 8
  for every fault; `execLink` subtracts 4 from A7 only under
  `MOIRA_MIMIC_MUSASHI`. The other items were not compared one by one.
- Config/Runner: the address-error part needs
  `MOIRA_EMULATE_ADDRESS_ERROR`. The ADDQ/SUBQ.l #,An and BTST Dn,#imm
  cycles are not gated by any setting. Musashi in `Runner/Musashi/m68kops.c`
  gives 8 for both, so the Runner reports clock mismatches on the 68000 and
  the 68010.

**Row 6 — 68020 integer rulings D11–D17.** On the C68020 core, CHK no
longer refreshes N (upstream's port of WinUAE's table dropped
`SET_NFLG(dst < 0)`). DIVU/DIVS divide-by-zero and overflow flags are
corrected. DIVS.L 32/32 of $80000000 by −1 now leaves its registers alone.
The reserved full-extension I/IS=100 encoding no longer dereferences
memory. User-mode cpSAVE/cpRESTORE shapes take a privilege violation
before Line-F. The odd-PC address-error part (D11) is for the 68030 only.
- Models: 68020, 68EC020 (and 68030/68040).
- Where: `setUndefinedCHK`, `execChk`, `execDivsMoira`, `execDivuMoira`,
  `divsMoira`, `divuMoira`, `execDivlMoira`, `computeEAfull`, `execLineF`.
- Evidence: [D] WinUAE @ Hatari e77819f7. The flags are undefined in the
  manuals, so this is a reading where the manuals are silent.
- Gate: `sst68030` (corpus). The integer vectors run on the shared
  C68020 core.
- Upstream: still present. Checked items: no N refresh in
  `setUndefinedCHK`; `computeEAfull` dereferences for `iis & 0b100`;
  `execLineF` has no privilege test.
- Config/Runner: the Runner compares SR. Musashi's choice for these
  undefined flags was not checked.

**Row 13 — IRQ recognition delayed two instructions after an SR write
that lowers the mask.**
- Models: 68020 and up. The 68000 and 68010 are unchanged.
- Where: `setSR` and `execute` (`Moira.cpp`), member `irqDelay`.
- Evidence: [F]. The depth was chosen empirically, and the guest crash
  that motivated it was later traced to another device (the comment in
  `setSR`). A hardware trace would settle it. It is not proposed as a fix.
- Gate: `moira_rules_test (irqDelayAfterMaskLowering)`.
- Upstream: still present (upstream has no delay).

**Row 15 — exception frames at an odd SP on the 68010/68020.**
`writeStackFrame0000` masked A0 (`& ~1`) on the 010/020 path. A frame
pushed while SP was odd therefore landed one byte low, and RTE then read a
garbage format word. The second half of the row is
`execInterrupt<C68020>` passing `4 * vector` to a writer that scales by 4
again.
- Models: 68010, 68020 (the gate covers 68020/030/040; no cell covers the
  68010).
- Where: `writeStackFrame0000`, `execInterrupt`.
- Evidence: no grade recorded. The argument is the core's own consistency
  (`execRte` reads at the true addresses), and the symptom was a Macintosh
  application freeze.
- Gate: `moira_rules_test (oddStackInterruptFrame)`, `sst68000`.
- Upstream: the A0 masking is still present. The double scaling was fixed
  upstream in `eb9529c`.
- Config/Runner: non-Musashi branch only. With `MOIRA_MIMIC_MUSASHI` the
  frame goes through `push<>` and was never affected.

**Row 38 — `abs(INT32_MIN)` in DIVS.** `setUndefinedDIVS` and
`cyclesDiv<…, DIVS>` called `abs()` on a 32-bit dividend, which is undefined
behaviour for $80000000.
- Models: 68000/68010 (cycle count), 68020 (`DIVS.W` of $80000000).
- Where: `setUndefinedDIVS`, `cyclesDiv` (back-ported from NeoST `74b2f2d`).
- Evidence: C++ language rule. No behaviour change with GCC or Clang here.
- Gate: `moira_rules_test (divsOfIntMin)`.
- Upstream: still present (`std::abs(dividend)` in `setUndefinedDIVS`,
  `abs(dividend)` in `cyclesDiv`).

**Row 40 — 68000 refill after a write to SR or CCR.** MOVE to SR, MOVE to
CCR and ANDI/ORI/EORI to SR and to CCR read PC+2 twice instead of PC then
PC+2. The immediate forms read in data space, which leaves a stale opcode
in IRD.
- Models: 68000.
- Where: `execMoveToSr`, `execMoveToCcr`, `execAndisr`, `execAndiccr`
  (`fullPrefetch`).
- Evidence: [D] NeoST `ce1cc52`, `d8d33bb` (microcode transcription and
  Hatari CE), [D] SingleStepTests transactions.
- Gate: `sst68000` (bus cycles, corpus).
- Upstream: the SR/CCR part is still present (`read<…>(reg.pc + 2)` then
  `prefetch`, in data space for `execAndisr`). The DIVU/DIVS part of the
  row is upstream's own `f90fc50`.
- Config/Runner: total cycles are unchanged, and only reads differ, which
  the Runner does not compare.

**Row 41 — 68000 bus-cycle order and idle placement.** The group-0 frame
is written in the wrong word order, with its idle cycles after the frame.
RTE/RTR pop in the wrong order. JSR pushes before fetching its target. A
trapping CHK does not fetch the next word. MOVEM.l reads its extra word in
the wrong place. Long ADD/SUB/ADDQ/SUBQ/ADDI/SUBI and `MOVEM.l list,-(An)`
store the high word first. ADDX/SUBX.l read high first.
- Models: 68000 only (`C == Core::C68000`).
- Where: `writeStackFrameAEBE`, `execRte`, `execRtr`, `execJsr`, `execChk`,
  `execMovemEaRg`, `execMovemRgEa`, `execAddRgEa`, `execAddiEa`,
  `execAddqEa`, `execAddxEa`, `execBitDxEa`; `read<>` gained `REVERSE`.
- Evidence: [D] SingleStepTests/680x0 v1 `transactions`.
- Gate: `sst68000` (corpus; compares bus order and timing since this row).
- Upstream: still present. Checked: `writeStackFrameAEBE` pushes PC low,
  PC high, SR, IR, …; `execJsr` pushes before reading the target.
- Config/Runner: the low-word-first long stores are not gated by
  `MOIRA_MIMIC_MUSASHI`. The Runner's Musashi writes a long as high word
  then low word (`m68k_write_memory_32` in `Runner/musashi.cpp`) and checks
  write order, so it would report these stores.

**Row 52 (CLR part) — CLR reads its destination on the 68020.** Only the
68000/68008 read before clearing. The rest of the row, the 68030 last-write
marking, is MMU-only.
- Models: 68020, 68EC020.
- Where: `execClr`.
- Evidence: [B] M68000PRM-1992, CLR note; [D] WinUAE gencpu `i_CLR`.
- Gate: `moira_rules_test (clrReadsOnlyOn68000)`, `sst68030_rmw`.
- Upstream: still present (`execClr` shares the 68000 read path with C68020).
- Config/Runner: removes a read only, so the Runner is silent.

**Row 79 — memory bit fields touch only the bytes they span.**
`execBitFieldEa` always read and rewrote a longword (plus a fifth byte),
touching up to three bytes outside the field. It also wrote the first four
bytes before reading the fifth.
- Models: 68020 and up.
- Where: `execBitFieldEa`.
- Evidence: [B] MC68030UM-1989 §11.6.14; [D] WinUAE `get_bitfield` and
  `put_bitfield`; [F] for the three-byte case.
- Gate: `moira_rules_test (bitfieldMemoryAccesses)`, `sst68030`.
- Upstream: still present.
- Config/Runner: not gated. Musashi also writes a longword
  (`m68k_op_bfchg_32_ai`), so expect Runner write mismatches on fields
  narrower than four bytes.

**Row 91 — 68000 STOP.** STOP fetched the next word on the bus, and an
interrupt unmasked by its operand was taken on the wrong cycle.
- Models: 68000 (68010/020 keep upstream's path).
- Where: `execStop`, `execute`, member `stopGraceUntil` (NeoST `2fc53a8`,
  `9f20046`, `993ce5e`).
- Evidence: [B] M68000UM Rev 8 §8.11; [D] fx68k @ 0602ee46, Hatari @
  f0736b24; [F] STOP+8 against STOP+12.
- Gate: `moira_rules_test (stop68000)`.
- Upstream: still present (`readI` in `execStop`).

**Row 92 — 68000 reset timing.** The gap between the two reset prefetch
reads is 2 cycles short.
- Models: 68000.
- Where: `reset` (NeoST `9f20046`).
- Evidence: [D] fx68k @ 0602ee46. The manual gives only the total.
- Gate: `moira_rules_test (reset68000)`.
- Upstream: still present (`SYNC(2)` before the final prefetch).

**Row 95 — PACK/UNPK −(An) bus shape.** PACK reads two bytes around the
extension fetch instead of one word at Ax−2. A faulting access leaves An
mid-update.
- Models: 68020 and up.
- Where: `execPackPd`, `execUnpkPd`.
- Evidence: [D] WinUAE gencpu `i_PACK`/`i_UNPK`; [F] bus cycles.
- Gate: `moira_rules_test (packUnpkFaultRestoresAn)`.
- Upstream: still present.
- Config/Runner: non-Musashi branch only.

**Row 105 — BKPT makes a data read at PC on the 68020.** Upstream stands in
for the breakpoint-acknowledge cycle with a data read. On the 020 that
cycle is in CPU space.
- Models: 68020 and up (the 68010 keeps the read).
- Where: `execException` (BKPT case).
- Evidence: [D] WinUAE; [F] acknowledge timing.
- Gate: `moira_rules_test (bkptMakesNoDataAccess)`.
- Upstream: still present (`readM<C, Mode::DN, Word>(reg.pc)`).

**Row 107 — ANDI/ORI/EORI to SR refill in data space.** On the C68020 core,
the refill after an SR write is a data read instead of a program read, so
it carries the wrong FC.
- Models: 68020 and up.
- Where: `execAndisr`.
- Evidence: [D] WinUAE.
- Gate: `moira_rules_test (srWriteRefillIsAFetch)`.
- Upstream: still present (`read<C, AddrSpace::DATA, Word>(reg.pc + 2)`).

**Row 121 — bit-field width overflow.** `(width - 1)` on an `int` loaded
from a data register holding $80000000 is signed overflow.
- Models: 68020 and up.
- Where: `execBitFieldDn`, `execBitFieldEa`.
- Evidence: C++ language rule. Every result is unchanged.
- Gate: only under UBSan (`jit_program_fuzz_*` built with
  `-fsanitize=undefined`). The release has no sanitizer configuration.
- Upstream: still present.

**Row 127 — level 7 is transition-sensitive.** A held level 7 re-entered
its handler after every instruction.
- Models: 68020 and up. The 68000/68010 keep the level-sensitive reading.
- Where: `setIPL`, `irqEligible`, `execInterrupt`, member `nmiLatch`.
- Evidence: [B] MC68020UM-1992 §6.1.9 (also MC68030UM §8.1.9, MC68040UM
  §8.2.9).
- Gate: `moira_rules_test (level7TransitionSensitive)`,
  `jit_program_fuzz_020_irq7_test`.
- Upstream: still present (`reg.ipl > reg.sr.ipl || reg.ipl == 7`).

**Row 129 — interrupts on the C68020 core cost no cycles.**
- Models: 68020 (the same table is used for 030/040, [F] there).
- Where: `execInterrupt` (26 cycles on the I-stack, 41 on the M-stack).
- Evidence: [B] MC68020UM-1992 §8.2.17.
- Gate: `moira_rules_test (interruptSequenceBilled)`.
- Upstream: still present. Note that upstream's 68020 timing now goes
  through `CycleBudget`/`syncCp` (`426e4a3`), so a port has to choose
  between `sync` and that path.

**Row 133 — 68020 trace exception frame.** The trace exception stacks
format $0. The 68020/68030 stack format $2 carrying the traced
instruction's address.
- Models: 68020, 68EC020 (and 68030).
- Where: `execException` (TRACE case), `execute` (`tracePc040` latch).
- Evidence: [B] MC68020UM-1992 Table 6-5.
- Gate: `moira_rules_test (traceFollowsTheTracedInstruction)`,
  `cpu_vectors030_test`.
- Upstream: still present. Musashi's `m68ki_exception_trace` already uses
  format $2 above the 68010, so this change moves Moira towards Musashi.

**Row 135 — T0, trace on change of flow.** SR.T0 is stored but has no
effect.
- Models: 68020 and up (the 68040 list is separate).
- Where: `traceFlow`, `traceInstructionTrap`, `setSR`, `fullPrefetch`
  (`FLOW` flag), `State::TRACING0`.
- Evidence: [B] MC68020UM-1992 §6.1.7; [D] WinUAE `check_t0_trace`.
- Gate: `moira_rules_test (traceOnChangeOfFlow)`,
  `jit_program_fuzz_030_t0_test`.
- Upstream: still present (T0 is only read for `getSR` and the
  disassembler).

**Row 137 — CAS2 with Dc1 = Dc2 and a failed compare.** The register ends
up holding memory operand 2. The manual says operand 1.
- Models: 68020, 68030 (the 68040 keeps the old order, ruling D44).
- Where: `execCas2`.
- Evidence: [B] M68000PRM-1992, CAS2; [D] WinUAE `i_CAS2`.
- Gate: `moira_rules_test (cas2SameCompareRegister)`.
- Upstream: still present.
- Config/Runner: non-Musashi branch only. Musashi writes Dc1 then Dc2.

**Row 151 — MOVES leaves SFC on the pins after an address error.** The
`<ea>,Rn` direction sets `fcSource` and clears it on the normal path only:
after an address error on the read, the stacking, the vector fetch and the
handler all report SFC. The fork also clears it on a host bus error in both
directions.
- Models: 68010 (`misaligned()` is never true on the 68020 core).
- Where: `execMoves`.
- Evidence: [B] MC68020UM-1992 §2.2. Reproduced on unmodified upstream
  ce28239 on 2026-11-20 (a small host program, 68010, address errors on).
- Gate: `moira_rules_test (movesFcAfterFault010)`.
- Upstream: still present.
- Config/Runner: needs `MOIRA_EMULATE_ADDRESS_ERROR`; invisible to the Runner.

**Row 152 — DBcc count expired: a 68020 program read past the cache.** The
word at pc+2 is read straight from the bus on every pass, cached or not.
- Models: 68020, 68EC020 with `MOIRA_EMULATE_ICACHE`.
- Where: `execDbcc`.
- Evidence: [B] MC68020UM-1992 §8.2.15 (cache case 10(0/0/0)). Reproduced
  on unmodified upstream ce28239 on 2026-11-20. No clock change.
- Gate: `icache020_test`.
- Upstream: still present.
- Config/Runner: the Runner compares writes, not reads; no effect on it.

**Row 153 — DIVS.L $80000000:00000000 / −1.** Upstream's `divlsMoira`
asserts on both overflow forms (the fork had dropped the asserts and kept
the registers, rows 43 and 55), and `divlsMusashi<Long>` divides INT64_MIN
by −1 unguarded: SIGFPE on an x86-64 host.
- Models: 68020 and up.
- Where: `divlsMusashi`, `divlsMoira`, `execDivlMoira`.
- Evidence: [B] M68000PRM-1992, DIVS. Reproduced on unmodified upstream
  ce28239 on 2026-11-20 (assert in a debug build without Musashi; SIGFPE
  with it, debug and release).
- Gate: `moira_upstream_config_test (divslOverflow)`,
  `moira_rules_test (divslOverflowKeepsRegisters)`.
- Upstream: still present.
- Config/Runner: the Musashi half is reachable by the Runner's random
  operands only by chance; the 32/32 Musashi result (V clear) is kept.

**Row 154 — the other raw 68020 program reads.** The refill word after
an SR or CCR write, TRAPV's pc+2 and the vector jump's first fetch are read
from the bus past the cache, as the DBcc word of row 152 was.
- Models: 68020, 68EC020 with `MOIRA_EMULATE_ICACHE`.
- Where: `pomRefillRead` (the fork's name for upstream's explicit read in
  `execMoveToSr`, `execMoveToCcr`, `execAndisr`), `execTrapv`,
  `jumpToVector`; helper `pomProgWord`.
- Evidence: [B] MC68020UM-1992 §8.2.7, §8.2.16, §8.2.17 (cache case, no
  instruction access). Read from upstream's code at ce28239; not run there.
- Gate: `icache020_test (refills)`.
- Upstream: still present.
- Config/Runner: the Runner compares writes, not reads; the fill charge
  moves the clock, which the Runner does not compare under the cache.

One more item sits inside a row filed under C:

- Row 55 drops `assert(S == Word)` from `divlsMoira`; row 153 above covers
  it with the rest of that overflow.

### A.2 Rows touching upstream code where upstream is already right

**Row 86 — 68030 CACR mask $3313.** Taken from upstream `f7844ac`
(`cacrMask`; gate `moira_rules_test (cacrStrobesReadZero)`).

**Row 115 — group-2 format-$2 instruction address.** Upstream's
`execException` already stacks `reg.pc0` for DIVIDE_BY_ZERO, CHK and TRAPV
on C68020. The defect came from the fork's own earlier ruling D12, and this
row undoes it. [B] MC68030UM-1989 Table 8-7. Gate:
`moira_rules_test (group2InstructionAddress)`.

**Row 156 — a 68000-68030 PC-relative operand read is a program
reference.** Upstream's `readM` at ce28239 already reads `isPrgMode` operands
in program space; the fork had moved them to data space to match
SingleStepTests and now returns to upstream on the 68000 to the 68030
([B] M68000PRM-1992 §2.2.11, [B] MC68030UM-1989 §2, ruling D46). On the
68040 the fork keeps data space ([B] MC68040UM-1993 §8.4.6.7), which
upstream, with no 68040 execution, does not reach. Gates: `sst68000` (the FC
pair counted), `moira_rules_test (pcRelativeOperandSpace)`.

**Row 157 — the 020/030 PC-relative read stays an operand cycle.** Fork-only:
upstream has no bus-error frame for the 020/030. The fork's fault builder
told a data cycle from a fetch by the function code, which program space
defeats; `readM` now marks the read, so its frame carries DF and FC 6
([B] MC68030UM-1989 §8.2.1). Gate: `moira_rules_test (pcRelativeOperandCycle)`.

**Row 145 — throwaway frame of an interrupt taken with SR.M set.** Upstream
fixed this first in `eb9529c` (`setMasterMode(false)` before the format-$1
frame). The fork followed. Gate: `moira_rules_test (masterStackInterrupt)`.

## B — new execution upstream does not have

Upstream has no PMMU, FPU or 68040 execution and no bus-error entry point.
These rows build that execution. They can only be taken as a feature, not
cherry-picked as fixes.

| Feature | Rows | Main files | Gates in this release | External dependency |
|---|---|---|---|---|
| 68030 PMMU: PMOVE/PTEST/PFLUSH/PLOAD, table walk, ATC, TT, i-cache overlay | 3, 4, 5, 11, 14, 36, 54, 56, 109, 110, 111, 112 | `MoiraExecMMU_cpp.h`, `MoiraMmu030Walk.h`, `Moira.h`, `MoiraTypes.h` | `sst68030`, `moira_rules_test`, `berr030_test`, `cpu_vectors030_test` | — |
| 68030 fault frames $A/$B, access log, write buffer, RTE continuation | 80, 81, 82, 83, 84, 88, 94, 96, 101, 104, 122, 131, 132, 141, 142 | `MoiraExecMMU_cpp.h`, `MoiraExec_cpp.h`, `MoiraDataflow_cpp.h` | `sst68030`, `sst68030_rmw`, `sst68030_solo`, `moira_rules_test`, `fault_transparency_test`, `berr030_test` | — |
| Faults and restart shared by 030/040: deferred prefetch faults, odd JSR target, locked RMW, stack faults, SR refill, RTE frame read | 87, 97, 98, 99, 103, 117, 138, 139, 140, 143, 144 | `MoiraExec_cpp.h`, `MoiraExecMMU_cpp.h`, `MoiraDataflow_cpp.h`, `Moira.h` | `moira_rules_test`, `fault_transparency_test`, `cpu_vectors030_test`, `cpu_vectors040_test`, `jit_program_fuzz_040_test` | — |
| External bus error entry points (`extBusError`, `extBusError040`, `extBusError000`), NeoST host hooks, software-completed 020 format $B | 9, 10, 19, 21, 37, 42, 77 | `MoiraExecMMU_cpp.h`, `MoiraExceptions_cpp.h`, `MoiraDataflow_cpp.h`, `Moira.cpp`, `Moira.h` | `berr020_test`, `berr030_test`, `moira_rules_test`, `fault_transparency_test`, `cache040_test` | — |
| MC68020 CALLM/RTM | 25, 149 | `MoiraExec_cpp.h`, `Moira.h`, `MoiraTypes.h` | `callm_rtm_test` | — |
| Attached 68881/68882 FPU: execution, timing, FSAVE/FRESTORE, mid-instruction interrupts | 7, 8, 26, 29, 47, 53, 78, 93, 102, 113, 114, 128 | `MoiraExecFPU_cpp.h`, `Moira.h`, `MoiraTypes.h`, `MoiraInit_cpp.h` | `fpu_sanity`, `fpu_context_test`, `softfloat_primitives_test`, `sst68030_fpuraw`, `sst68030_fpu2`, `sst68030_fpuh` | `extern/softfloat` (GPLv2+ / SoftFloat-2a), with a per-instance context |
| 68040 integer core and trace | 17, 43, 100, 106, 108, 134, 136 | `MoiraExec_cpp.h`, `MoiraExceptions_cpp.h`, `Moira.cpp`, `MoiraInit_cpp.h` | `sst68040`, `sst68040_8k`, `moira_rules_test`, `cpu_vectors040_test` | — |
| 68040 MMU: translation, TTR, four-way I/D ATCs with FC2, format $7, MOVEM/MOVEP restart; the 68EC040 as an access control unit (no table search, inert PTEST/PFLUSH, IACR/DACR names) | 18, 60, 61, 62, 63, 72, 73, 74, 75, 126, 155 | `MoiraExecMMU_cpp.h`, `Moira.h`, `Moira.cpp`, `MoiraDataflow_cpp.h` | `sst68040`, `sst68040_8k`, `mmu040_atc_test`, `moira_rules_test`, `jit_program_fuzz_040_mmuprot_cas_test` | — |
| 68040 integrated FPU: FPSP traps, posted exceptions, state frames, no-FPU Line-F, odd-displacement FBcc/FDBcc | 27, 44, 45, 46, 50, 51, 158 | `MoiraExecFPU_cpp.h`, `Moira.h` | `fpu040_test`, `moira_rules_test`, `sst68040_fpu`, `sst68040_fpu2`, `sst68040_fpuh`, `sst68040_fline` | `extern/softfloat` |
| 68040 data-bearing I/D caches (opt-in) | 28, 59, 65, 66, 67, 68, 69, 70, 71, 85 | `MoiraCache040.h`, `MoiraExecMMU_cpp.h`, `Moira.h` | `cache040_test`, `jit_copyback_write_040_test` | — |

Row 19 (`extBusError040`) has no gate of its own in this release. The
function is exercised by `cache040_test`, `fault_transparency_test` and
`mmu040_atc_test`.

## C — POM68K host seams (not proposed for upstream)

These rows serve POM68K as a host: its JIT, its save states, its debugger
and its configuration. They are not proposed for upstream. Defining
`MOIRA_ENABLE_JIT=0` compiles the JIT seam out: no fetch or data windows,
probes or native publications (row 76, target `moira_interp`). The
remaining seams do nothing until a host arms them.

| Group | Rows | Note |
|---|---|---|
| Build configuration | 1, 116 | Row 116 makes the three settings `#ifndef` so a build can use upstream's values; gate `moira_upstream_config_test` |
| JIT seam (`pomJit*`, interpreter fetch window) | 22, 30, 31, 32, 33, 34, 55, 76, 119, 120, 123, 125 | Row 55 also carries the `divlsMoira` assert noted in A |
| Save-state visitor and snapshot determinism | 23, 24, 57, 64, 118, 124, 130 | `pomVisitState` in `MoiraState.h`; row 24 is a no-op outside the 68010 |
| Debugger and host accessors | 12, 20, 35 | Row 35 re-arms `CHECK_CP` on reset. Upstream calls `catchpointMatches` unconditionally, so upstream is not affected |
| Host-time performance | 16, 48, 89, 90 | No architectural effect claimed |
| No environment reads in the core | 39, 49 | Behavioural switches are injected by the host |
| Upstream's structure and 68020 cache as carried by the fork | 146, 147 | Row 146: the fork's non-templated definitions behind `MOIRA_MAIN_TU`. Row 147: the conditions under which `MOIRA_EMULATE_ICACHE`, bus sizing and `syncCp` apply; the cache is on in POM68K's configuration as upstream, restricted to the 68020/EC020, and its fetch charges are corrected: 0 for a hit or a latch word (upstream −2), +2 per bus cycle from a 32-bit port (upstream 0), +6 from a narrower one (upstream +4), so a warm loop lands on the manual's cache case instead of below its best case. That correction is a candidate for upstream (README § *Limits*). An aligned data longword on a 32-bit port stays two word calls (the fork's /BERR path), unlike upstream's single `read32`; the JIT runs a 68020 on the interpreter |
| Musashi's behaviour under `MOIRA_MIMIC_MUSASHI` | 148 | Where a ruling of rows 2, 4, 5, 6, 17, 18, 25, 47 or 66 departs from Musashi, the fork gives Musashi's behaviour under that setting; upstream's unmodified Runner then passes on every model (README § *In upstream's tree*) |
| Building in upstream's tree | 150 | `Moira/CMakeLists.txt` links SoftFloat, whose exports are prefixed so that it links next to Musashi's copy |

## D — records, no code

| Group | Rows | Note |
|---|---|---|
| Evidence documentation | 58 | Comment-only audit of manual citations |

## Upstream's Runner on the fork

Run on 2026-11-20 (Linux x86-64, GCC 15, Release): upstream's master at
`ce28239`, `Moira/` replaced by the fork's through
`patches/moira-vs-upstream.patch`, `softfloat/` beside it, upstream's own
root `CMakeLists.txt`, upstream's settings (README § *In upstream's tree*
has the commands). The unmodified `testRunner` prints PASSED for all
thirteen passes — 68000, 68010, EC020, 68020, EC030, 68030, EC040, LC040
and 68040 CPU, the three MMU passes and the FPU pass — and exits 0, on the
day's seed (2349) and on seeds 1, 7 and 2026.

It did not at first. A census of its reports against the fork as it stood
(three seeds, 15.9 M cases each) found 2.7 % of the CPU cases failing, in
eleven classes, all of one kind: a place where the fork follows other
evidence than Musashi and had not kept Musashi's behaviour for
`MOIRA_MIMIC_MUSASHI`. Row 148 lists the branches that restore it. Two
things found on the way are fixes in their own right: a CALLM or RTM that
took a format error cost zero cycles (row 149), and the fork's SoftFloat
could not link next to Musashi's (row 150).

What a green Runner does and does not show:

- It compares registers, SR, cycles on the 68000 and 68010 (not from the
  68020 up once `MOIRA_EMULATE_ICACHE` is set), the ordered list of memory
  writes, and disassembly against Musashi and binutils. It does not compare
  reads, and it skips ABCD/SBCD/NBCD, the bit-field instructions, RTE from
  the 68020 up and a few others.
- Its MMU and FPU passes compare disassembly only (opcodes from `$F000`
  are not executed). The fork's PMMU, FPU and 68040 execution are judged by
  the corpora and gates of this release, not by the Runner.
- Under `MOIRA_MIMIC_MUSASHI` a 68030 or 68040 that translates nothing
  writes through the plain 68020 path, so the Runner does not exercise the
  bus layers' handling of misaligned accesses.

## What was not checked

- No fix of Section A was built on its own in upstream's tree: the fork as
  a whole was (§ *Upstream's Runner on the fork*). The per-row Runner notes
  in A.1 come from reading `Runner/Testrunner.cpp`, `Runner/Sandbox.cpp`,
  `Runner/musashi.cpp` and the Musashi tables at ce28239, and from the
  census; a row cherry-picked without its `MOIRA_MIMIC_MUSASHI` branch can
  show up as a Runner mismatch where the note says so.
- Rows 2, 6 and 41 each bundle many rules. Upstream was checked on the
  items named in each entry, not on every rule. Musashi's undefined-flag
  choices (rows 2, 6) were not compared.
- The models in A.1 come from the code's `C ==` / `cpuModel` guards. No
  68010-specific test exists for the 68010 half of row 15 or for the 68010
  BTST timing in row 2.
- cputester on real Amigas was not consulted. No row cites a hardware
  measurement ([A]), so a cputester result that contradicts a [D] row
  should overrule it.
- The upstream status for Section B was not checked row by row. Upstream
  ships 68030/68040 decode, disassembly and stubs, but no PMMU, FPU or
  68040 execution, no external-bus-error entry, and no CALLM/RTM bodies.
- The vAmiga configuration statement rests on one local clone (`f51a6bf9`).
  It is not a pinned release.
- `tools/moira_release.py --check` fails when an inventory row is missing
  from this file, so every row is sorted; rows 146 to 150 were sorted on
  the day of the rebase, without the per-row reading of upstream that the
  earlier rows had.
