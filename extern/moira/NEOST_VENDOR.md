# Moira — vendored copy (NeoST fork)

> **POM68K note (2026-08-12, revised 2026-11-22).** This file is **NeoST's**
> provenance record, kept because POM68K's copy of Moira inherits its four
> patches. NeoST wrote it in French; since 2026-11-22 the sections below are an
> English translation, faithful to the original, which stays in NeoST's own
> repository. It describes NeoST, not this tree, so:
>
> - **All four patches below are still present here** — deferred IPL
>   (`iplPrev`/`iplChangeClock`/`iplDelay4`/`iplDelay2`, `setIplDelay`,
>   `pollIpl`, `POLL_IPL` in `MoiraMacros.h:67`), the STOP level-sensitive
>   IRQ re-check (`Moira.cpp:612` — `if (reg.ipl > reg.sr.ipl || reg.ipl == 7)`
>   in the `STOPPED` branch opened at `:587`), the guarded `reset<C>()` whose
>   double fault ends in `halt()` rather than an escaping C++ exception
>   (`Moira.cpp:284-309`), and the 24-bit watchpoint mask
>   (`MoiraDataflow_cpp.h:547`/`:637`). Beyond the four, NeoST also wrapped
>   three `execException` calls on `execute()`'s own paths in `try` →
>   `processException`, so an odd TRACE/PRIVILEGE vector raises an address error
>   instead of escaping the emulator (`Moira.cpp:527`, `:590`, `:635`) —
>   undocumented below but present. Everything NeoST touched carries `NEOST`
>   markers, never `POM68K` ones — `grep -rn NEOST Moira/` finds them, and the
>   two greps are disjoint.
> - **Deferred IPL is dormant in POM68K.** Nothing in `src/` or `tests/` calls
>   `setIplDelay`, so `iplDelay4 == 0` and `pollIpl()` is the plain
>   assignment on every profile. `NEOST_IPLFETCH` was NeoST's env knob and is
>   not read by anything here; `Moira.h:602-604` (`pomJitSimpleIpl`) depends
>   on the feature staying off, since generated code models `POLL_IPL` as the
>   assignment.
> - **`NEOST_EXC_DIAG`** (presence-only, stderr) survives as a NAME only:
>   since 2026-11-07 the core no longer reads it. Its three sites test
>   `Moira::setPomExceptionDiag(bool)`, and the host reads the variable
>   (`POM68K_VENDOR.md` row 49).
> - Everything else it names — `docs/MOIRA_WINUAE_CONVERGENCE.md`,
>   `tools/make_trace_odd_test.py`, `roms/tos106us.img`, the `Cpu68k::run` STOP
>   guard, the 19/19 Atari etalons, the `trace_odd` gate — is **NeoST-side and
>   does not exist in POM68K**. The rationale is kept because the *why* of each
>   patch is not recoverable from the code; the file paths are not usable.
> - **Its closing instruction does not apply.** POM68K's copy is a fork
>   (`POM68K_VENDOR.md` § *Status*): changes are committed here and documented
>   there, not pushed back to NeoST.

This directory is **no longer a Git submodule**. It is a copy versioned
directly in the NeoST repository ("vendored") of Dirk W. Hoffmann's
cycle-exact 68000 core [Moira](https://github.com/dirkwhoffmann/Moira) (MIT
licence, see `LICENSE`).

## Why vendored

NeoST **modifies** Moira's internal files (see the patch below). With a
submodule those edits are not in the parent repository and are **overwritten
by the first `git submodule update`** — which already happened once (the local
fork was lost, commits `e4da365`/`a1e52ec` could not be found). Vendoring
freezes the code and the patch in NeoST's history, safe from such a clobber.

## What is kept

Only `Moira/` (the sources NeoST compiles), plus upstream's `LICENSE`,
`README.md` and `CMakeLists.txt` (for reference). The full upstream tree —
`Cputester/` (~713 MB of ADF corpus), `Documentation/`, `docs/`, `Runner/`,
`Moira.xcodeproj/` — was **pruned** (NeoST's build compiles `Moira/*` only).

## Local patch: `NEOST_IPLFETCH` (deferred IPL recognition)

A faithful port of WinUAE `ipl_fetch_next` (mechanism B). **Off by default**
(`iplDelay4 == 0` ⇒ `pollIpl()` ≡ `reg.ipl = ipl`, byte-identical to upstream).
Enabled through the `NEOST_IPLFETCH=1` environment variable on the NeoST side.
Details and measurements: `docs/MOIRA_WINUAE_CONVERGENCE.md` (mechanism B).

Files touched against upstream:
- `Moira/MoiraMacros.h` — `POLL_IPL` → `pollIpl()`
- `Moira/Moira.h` — members `iplPrev`/`iplChangeClock`/`iplDelay4`/`iplDelay2`,
  methods `setIplDelay()` + `pollIpl()`
- `Moira/Moira.cpp` — pin history in `setIPL`, the three-case rule in
  `pollIpl()`
- `Moira/Moira.cpp` — **exception guards** (2026-07-03): the TRACE,
  PRIVILEGE-from-STOP and LOOPING paths of `execute()` go through
  `processException` (an odd vector throws `AddressError` there → a 68000
  address error, not an abort); `processException` fixes the `throw df` (a
  pointer never caught) and treats nested AddressError/BusError as a **double
  fault → HALT**. Etalon: `trace_odd` (`tools/make_trace_odd_test.py`).

## Local patch: level-sensitive STOP (2026-07-03)

The 68000 compares IPL with the mask **continuously** during a STOP (the pins
are level-sensitive). Upstream re-tests `checkForIrq()` only on `CHECK_IRQ`,
set on a pin CHANGE — an IRQ raised BEFORE the `stop` (masked by the SR of the
time, unmasked by the stop's operand) was never tested again: the CPU slept
until the next pin change. Fix: in the STOPPED branch of `execute()`
(Moira.cpp), after `POLL_IPL`, re-arm `CHECK_IRQ` if `reg.ipl > reg.sr.ipl`.
Counterpart: a `!irqDeliverable()` guard on the STOP wait skip of NeoST's
`Cpu68k::run` — without it the clock was teleported to the next event and the
already-takeable IRQ left ~350 cycles late. Measured case: Super Hang-On's
"Timer B + stop #$2100 + HBL" raster (a white band at the horizon, three
palette writes per activation instead of two, a drift of one line per
segment). Etalons 19/19 + EL + Cuddly re-validated after the fix.

## Local patch: guarded reset (2026-07-08)

A bus or address error while fetching the reset vectors (SSP/PC at $0-$7,
e.g. a truncated ROM image — `roms/tos106us.img` is 192 KB instead of 256)
escaped the emulator (`terminate called after throwing moira::BusError`),
since `processException` covered only `execute()`'s paths. Faithful to the
68000: a double fault at reset is a **HALT** (cpu_halt(CPU_HALT_DOUBLE_FAULT)
in WinUAE/Hatari). Fix: `Moira::reset<C>()` (Moira.cpp) wraps the
`read16OnReset` calls + prefetch in a try → `halt()`. Etalons 19/19
re-validated.

## Local patch: watchpoints masked to the 24-bit bus (2026-07-11)

NeoST's debugger sets memory watchpoints through `debugger.watchpoints`. Moira
tests the access in `peekM`/`pokeM` (`MoiraDataflow_cpp.h`) with the
**unmasked** EA, while the real access just below uses `addr & addrMask<C>()`
(24 bits). An I/O access in absolute short addressing (`$8001.w` → EA
`$FFFF8001`) therefore never matched a watchpoint set on `$FF8001`. Fix: mask
`addr` with `addrMask<C>()` **before** `watchpointMatches`/`didReachWatchpoint`
(both sites, read and write). Short-circuited outside watchpoints
(`flags & CHECK_WP &&`) → no cost in normal running. Checked: `--watch FF8001`
(through `$8001.w`) and `--watch 10` (plain RAM) both fire; self-tests and
glue-selftest 31/31 intact.

## DIVS: absolute values without undefined behaviour (NeoST 2026-10-20, ported here 2026-11-06)

`cyclesDiv<C68000|C68010, DIVS>` and `setUndefinedDIVS` computed
`abs(dividend)` on an `i32`: for `0x80000000`, `abs(INT32_MIN)` is undefined.
clang and GCC took different branches for `0x80000000 / 0x8000` (NeoST's
`cpu_div_ram` bench: green on macOS, Δ = +136 cycles on Linux). The absolute
values are now computed unsigned; |−2³¹| >> 16 = `0x8000` ≥ `0x8000` →
overflow, as in Hatari's `getDivs68kCycles`.

*(NeoST's text, commit `74b2f2d`. It is the first NeoST fix after the
2026-07-11 copy to be back-ported; it is marked `POM68K` in the source and is
row 38 of `POM68K_VENDOR.md`. The others are listed in
`TODO.md § Moteur › Convergence`.)*

## Prefetch after an SR/CCR write, and DIV internal cycles (NeoST 2026-10-19/20, ported here 2026-11-06)

Three NeoST groups, row 40 of `POM68K_VENDOR.md`, marked `POM68K` in the
source:

- **MOVE to SR** (`execMoveToSr`, NeoST `ce1cc52`): `fullPrefetch<POLL>`
  replaces the dummy read of `pc+2` followed by `prefetch`.
- **ANDI/ORI/EORI #,SR and #,CCR, MOVE to CCR** (`execAndisr`, `execAndiccr`,
  `execMoveToCcr`, NeoST `d8d33bb`): the same fix. The [microcode
  transcription](https://og.kervella.org/m68k/all.txt) gives the tail
  `1cc stiw1` → `321 stiw4` → `1e1 malw3` → `363 b` → `34c mmrw3`: accesses
  pc0+4, **pc0+4**, pc0+6 instead of pc0+4, pc0+6, pc0+6; IRD receives the
  word re-read AFTER the SR/CCR write, in PROGRAM space. Hatari CE does the
  same. Cycle count unchanged.
- **DIVU/DIVS** (`execDivuMoira`, `execDivsMoira`, NeoST `262f63a`, upstream
  `f90fc50`): on the 68000 the internal cycles precede `writeD` and the
  prefetch. POM68K keeps DIVS's early overflow exit (16/18 cycles,
  SingleStepTests): the quotient is computed first.

*(What POM68K adds to NeoST's evidence: the transaction lists of the
SingleStepTests/680x0 corpus show exactly these sequences on every
non-exception vector, and `sst68000` now compares bus cycles. Still to port:
the four hooks — `illegalOpcodeHook`, `willProcessFault`, bus-cycle bounds,
`setBusLock` — and STOP+8; see `TODO.md § Moteur › Convergence`.)*

## Host hooks: illegal opcode, fault, bus lock (NeoST 2026-10-01 and 2026-10-19, ported here 2026-11-06)

Row 42 of `POM68K_VENDOR.md`, marked `POM68K` in the source, inert by default:

- `illegalOpcodeHook(u16)` (NeoST `fdbce2e`): called by `execIllegal` before
  the exception; `true` = the opcode is consumed like a NOP. NeoST serves its
  GEMDOS host calls there (`$0008`-`$000A`).
- `willProcessFault(const StackFrame&)` (NeoST `ce1cc52`): the fault being
  processed, including one that interrupts an exception (double fault → HALT).
- `setBusLock(bool)` (NeoST `ce1cc52`): TAS's /RMC — `true` before the read,
  `false` after the write, and also if either raises an exception.

*(Not ported: the bus-cycle bounds `willAccessBus`/`didAccessBus`, two virtual
calls per access — `TODO.md § Moteur › Convergence`. POM68K adds
`extBusError000(addr, write)`, which raises the group-0 `moira::BusError` with
the recipe of NeoST's `Cpu68k::raiseBusError`: a 68000 host no longer has to
build the frame itself.)*

*(NeoST's original closing line — "any future change to a `Moira/` file is
committed normally in NeoST" — is superseded in this repo: see the POM68K
note at the top.)*

## Upstream origin and the three-way diff (POM68K, 2026-11-10)

The upstream commit both forks start from is
**`1efd69467ca13b27b2fb40febd5cb31dbecdea5f`** of
`github.com/dirkwhoffmann/Moira` ("Added .nojekyll", 2025-09-11). NeoST
recovered it on 2026-08-28 from its own submodule gitlink (NeoST
`6e7ab7a`, the first integration, before any local patch; NeoST's
`NEOST_VENDOR.md` § *Pin d'origine* has the table and the command). This
tree copied NeoST at `47d6c39` (2026-07-11). The diff therefore has three
corners, and each one is reachable:

```sh
git clone --filter=blob:none --no-checkout https://github.com/dirkwhoffmann/Moira moira-upstream
git -C moira-upstream sparse-checkout set --no-cone /Moira/ /LICENSE /README.md
git -C moira-upstream checkout 1efd69467ca13b27b2fb40febd5cb31dbecdea5f
diff -ruN moira-upstream/Moira extern/moira/Moira          # this fork vs upstream
diff -ruN <neost>/extern/moira/Moira extern/moira/Moira    # this fork vs NeoST
```

The sparse checkout leaves out upstream's `Cputester/` (~713 MB).
`tools/moira_release.py --upstream moira-upstream` writes the first diff
into a release as `patches/moira-vs-upstream.patch`, a `git am` commit on
the base named below.

Since 2026-11-20 this fork is based on upstream `master` at
`ce28239b0507ebfe1bf2ad2b9c9252dc2f2cc031` (2026-09-23), eleven commits after
that origin: each was taken in turn, and `POM68K_VENDOR.md` § *Rebase onto
upstream ce28239* has the table. The first diff above is therefore taken
against `ce28239`; `1efd694` remains the commit NeoST and the fork's history
start from.
