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

## Model support in this copy (`MoiraTypes.h`)

- 68000 / 68010 — cycle-exact execution ✓ (Mac Plus phase)
- 68EC020 / 68020 — functional execution ✓
- 68030 / 68040 — **disassembler only**: the 68030 execution core (incl. MMU)
  is the POM68K LC II phase, to be built with AI + differential testing
  against two oracles (WinUAE, MAME) — see `TODO.md § LC II`.

Do **not** re-sync from upstream blindly: diff against this file and
`NEOST_VENDOR.md` first.
