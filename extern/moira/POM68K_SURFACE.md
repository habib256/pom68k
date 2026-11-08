# Moira fork — extension surface

This document is the host-side contract of the 68k core in
`extern/moira/Moira/` (POM68K's permanent fork of Dirk W. Hoffmann's Moira).
It says what the core exposes and what a host owes it. It does not describe
Macintosh behaviour except where a seam was tuned on a Macintosh guest; those
cases are listed in § 10. Identifiers are cited as `file` + name. Line numbers
are deliberately absent. "Unverified" marks a statement read from the code
but not proved by execution or by a gate.

---

## 1. Scope and vocabulary

### 1.1 Models

`MoiraTypes.h` `enum class Model`: `M68000`, `M68010`, `M68EC020`, `M68020`,
`M68EC030`, `M68030`, `M68EC040`, `M68LC040`, `M68040`. The per-enumerator
comments still say "Disassembler only" for the 030/040 rows; they are stale.
The 030 and 040 execute (see `POM68K_VENDOR.md` § *Model support in this copy*).

`MoiraTypes.h` `enum class FPUModel`: `NONE` (F-line traps, jump table
identical to stock Moira), `M68881`, `M68882` (external coprocessor through
the 020/030 coprocessor interface), `M68040` (integrated 040 FPU).

### 1.2 Core template parameter

`MoiraTypes.h` `enum class Core`: `C68000` (used by `M68000`), `C68010`
(`M68010`), `C68020` (every other model). Dispatch helpers (`Moira::reset`,
`Moira::processException`, `Moira::execException`, `Moira::execInterrupt`,
`Moira::addrMask`) switch on `cpuModel` and instantiate the template once per
core. Inside `Core::C68020`, the 030 and 040 are distinguished at run time by
`cpuModel` tests, not by a separate core.

| Model | Core | Bus path for data/prog accesses | Instruction start |
|---|---|---|---|
| 68000, 68010 | C68000 / C68010 | `read<>`/`write<>` (`MoiraDataflow_cpp.h`), 24-bit `addrMask` | prefetch queue (`prefetch`, `fullPrefetch`) |
| 68EC020, 68020 | C68020 | `read<>`/`write<>` plain-020 branch (`kPlain020` stamps) | prefetch queue |
| 68EC030 | C68020 | plain-020 branch (the MMU funnel tests `cpuModel == Model::M68030` exactly) | prefetch queue; no i-cache overlay, no window (`pomJitProbeCode` refuses) — unverified by any corpus |
| 68030 | C68020 | `mmuRead`/`mmuWrite` (`MoiraExecMMU_cpp.h`), always, even with TC.E clear | "mode 5": `mmuExecuteStart` |
| 68EC040, 68LC040, 68040 | C68020 | `mmu040Read`/`mmu040Write` (`cpuModel >= Model::M68EC040`) | "mode 5": `mmu040InstrStart` |

`Moira::hasMMU()` answers false for `M68EC040` while every `>= M68EC040`
model takes the 040 translation path; `TODO.md` § *Surface portable* records
the contradiction. No profile or corpus exercises 68EC040.

### 1.3 "Mode 5"

The 030 and 040 follow WinUAE's MMU run loop (`m68k_run_mmu030`,
`m68k_run_mmu040`), called "mode 5" in the sources. There is no prefetch queue
carried between instructions: `prefetch<C,F>()` and `fullPrefetch<C,F,delay>()`
return early for these models (`MoiraDataflow_cpp.h`). At every instruction
start the loop head polls the IPL, resets the per-instruction MMU bookkeeping,
and fetches `ird` at `pc` and the one-word lookahead `irc` at `pc+2` through
translation. Extension words are fetched at consumption time (`readExt<C>`).
Both loop heads return false when the fetch faulted, after the bus-error
exception was processed.

### 1.4 "Host"

The host is the class deriving from `moira::Moira` (POM68K: the CPU
wrappers, through `src/MoiraDebugSeam.h` → `src/MoiraSnapshot.h` →
`src/MoiraCpu.h`'s `pom68k::MoiraCpu<Derived, Memory>`, or directly for
`src/Cpu68k.h`). It implements the bus callbacks, owns the clock policy and
device catch-up, and may arm the optional seams of §§ 3, 7, 9.

---

## 2. Host obligations: the virtual interface

All of these are `protected` in `Moira.h` and virtual when
`MOIRA_VIRTUAL_API == true` (POM68K and NeoST both build that way). With
`MOIRA_VIRTUAL_API == false` the same names become non-virtual declarations
the host must define; `pomDidReachWatchpoint` then has an inline body
forwarding to `didReachWatchpoint`. `readModuleCpuSpace8`,
`writeModuleCpuSpace8`, `writeModuleCpuSpace32` are virtual in both
configurations.

### 2.1 Bus callbacks (mandatory)

| Member | Called | Notes |
|---|---|---|
| `u8 read8(u32) const`, `u16 read16(u32) const` (pure) | every physical read the core issues | address already masked with `addrMask<C>()` on the non-MMU path; on 030/040 it is the translated physical address (`mmuTranslateAccess`, `mmu040Translate`). Also used for 030 table walks (`mmuRead32`) and 040 walks, cache line fills and pushes. |
| `void write8(u32,u8) const`, `write16(u32,u16) const` (pure) | every physical write | same masking. Longs are two `write16` (order reversed when `Flags` `REVERSE` is set, `Moira::write`). |
| `u16 read16OnReset(u32) const` | only from `Moira::reset<C>()`: SSP hi/lo at 0/2, PC hi/lo at 4/6, first `irc` | default forwards to `read16`. Any exception escaping it is caught by `reset<C>()` and turns into `halt()` (NeoST patch, kept). |
| `u16 read16Dasm(u32) const` | only by the disassembler (`MoiraDasm_cpp.h`) | default forwards to `read16`, i.e. the LIVE bus. POM68K overrides it with a side-effect-free peek (`src/MoiraCpu.h` `read16Dasm`); a host with read-sensitive I/O must do the same. |
| `u16 readIrqUserVector(u8 level) const` | `Moira::getIrqVector` when `irqMode == IrqMode::USER` | `irqMode` is a protected member with no setter; the host assigns it (NeoST sets `IrqMode::USER` in its `Cpu68k`). Default `IrqMode::AUTO` (vector 24 + level). |

Timing seen by a bus callback:

- `Core::C68000`/`C68010` with `MOIRA_PRECISE_TIMING == true`: `read<>`/`write<>`
  call `SYNC(2)` before the access and `SYNC(2)` after (long: 2 / callback /
  4 / callback / 2). `clock` is therefore mid-cycle when the callback runs and
  every `sync()` has already been delivered.
- `Core::C68020`: `SYNC(x)` expands to nothing (`MoiraMacros.h` `SYNC`); an
  instruction's cycles are charged in one `sync()` at its end through
  `CYCLES_68020` (`(c) + cp`). During a callback `clock` holds the previous
  instruction's end plus whatever was charged mid-instruction: 030 i-cache
  misses (added to `clock` directly by `pomIcacheFetch`, NOT through `sync`),
  6888x busy quanta (`fpuRunBusy`, through `sync`), 040 cache hit/fill/push
  charges (`pomCache040ReadValue`/`WriteValue`/`Writeback`, through `sync`),
  explicit `SYNC_*`-free charges in FPU/MOVE16 paths.

**Position and order of 68000 bus cycles.** On `Core::C68000` every bus cycle
is four clock cycles and the callback fires two cycles into it (`read<>` /
`write<>`: `SYNC(2)`, callback, `SYNC(2)`). The order, addresses, function
codes and start cycles of the bus cycles of every instruction match the
SingleStepTests/680x0 corpus, exception stacking included
(`POM68K_VENDOR.md` rows 40-41); `tests/sst68000.cpp` compares them and is
the reference a port must keep green. A host that arbitrates the bus per
cycle (video contention, a blitter) can rely on that sequence.

### 2.2 Bus errors raised by the host

The core distinguishes three exception types (`MoiraTypes.h`):

| Thrown | Who may throw | Where it is converted | Frame |
|---|---|---|---|
| `moira::BusError(StackFrame)` via `[[noreturn]] Moira::extBusError000(addr, write)` | the host, from inside `read8/16`/`write8/16`, on `M68000`/`M68010`. The core fills `StackFrame{code, addr, ird, sr, pc, fc, ssw}` with NeoST's `raiseBusError` recipe (status word = IR bits 15-5, R/W, function code). A host may still throw `moira::BusError` with a frame of its own. | `processException<C>` → `execBusError<C>` | 68000: group-0 frame (`writeStackFrameAEBE`); otherwise `writeStackFrame1000` (68010 format $8). Not appropriate for 020+. |
| `MmuBusError` via `[[noreturn]] Moira::extBusError()` | the host, from inside a bus callback, on `M68020`/`M68EC020`/`M68030` | `processException<Core::C68020>` (and the two loop heads' own `catch (MmuBusError&)`) → `execMmuBusError<C>` | format $A/$B with SSW built from the in-flight stamp `mmuAccAddr/mmuAccSsw/mmuAccFc/mmuAccWrite` |
| `MmuBusError` via `[[noreturn]] Moira::extBusError040()` | the host, from inside a bus callback, on `>= M68EC040` | → `execMmu040BusError<C>` | format $7, SSW.ATC clear, from `mmu040AccAddr/AccVal/AccSz/AccWrite/AccData` (+ `mmu040Moves` for FC) |

Rules:

- `extBusError`/`extBusError040` replay the stamp of the access in flight;
  they are meaningful only while the core is inside a callback it issued. The
  stamp is written by `read<>`/`write<>` (plain 020), `mmuTranslateAccess` and
  `mmuFetchWord` (030), `mmu040Read`/`mmu040Write` and `pomJitStampAccess`
  (040). Raised during a 030 table-search descriptor fetch, `extBusError`
  is not a fault of the access: the search ends with MMUSR B|I, the ATC
  entry carries B, and the retried access faults on its logical address;
  a PTEST reports B|I instead of taking an exception (MC68030UM § 9.5.2,
  Table 9-3; `POM68K_VENDOR.md` row 56; gate `moira_rules_test`).
- Called outside `execute()` (e.g. from `read16Dasm` or a host-side peek) the
  C++ exception escapes to the caller.
- On `Core::C68000`/`C68010`, `extBusError` must not be used:
  `processException<C>` handles `MmuBusError` only under
  `if constexpr (C == Core::C68020)`, so that exception would reach the
  trailing `throw exc;` and leave `execute()`. A 68000 board calls
  `extBusError000(addr, write)` (`POM68K_VENDOR.md` row 42; gate
  `moira_rules_test`).
- A fault while stacking a fault is a double fault: `processException` turns
  nested `MmuBusError`/`AddressError`/`BusError`/`DoubleFault` into `halt()`.
- The core requires C++ exceptions (all faults unwind through `throw`).
- On a faulting `MOVES`, `processException` clears `fcSource` before stacking
  (`POM68K_VENDOR.md` row 37); the stacked SSW keeps the alternate FC.

`AddressError` is thrown by the core itself (`misaligned<C,S>`, 68000/68010
only; `MOIRA_EMULATE_ADDRESS_ERROR`). On the 030/040, odd instruction-flow
targets go through `mmuCheckOddPc`/`execAddressError030` (format $B) and
`execAddressError040` (format $2).

### 2.3 Clock

| Member | Contract |
|---|---|
| `virtual void sync(int cycles)` | default `clock += cycles`. The host's override MUST add to `clock` (POM68K: `src/MoiraCpu.h` `MoiraCpu::sync` adds, taps `onSyncCharge`, then `catchUp()` peripherals). It is the only door through which mid-instruction time reaches devices; generated code must also come through it (`pomJitSync`). |
| `getClock`/`setClock` | public; `clock` is `i64` cycles since power-up. |

### 2.4 Interrupt acknowledge (68000 only)

`virtual int iackSyncBefore(u8 level)` / `iackSyncAfter(u8 level)` (default 4/4)
are called only from `execInterrupt<Core::C68000>`, between the PC-low push
and the SR/PC-high pushes, around `getIrqVector`. They return cycles that
become `SYNC(n)`. NeoST overrides them for E-clock/IACK timing; POM68K does
not. C68010 and C68020 interrupt sequences have no IACK hook and no IACK bus
cycle.

### 2.5 State, instruction and exception delegates

| Member | When | Models |
|---|---|---|
| `cpuDidReset()` | end of `reset<C>()` | all |
| `cpuDidHalt()` | `Moira::halt()` (double fault, nested fault) | all |
| `willExecute(const char*, Instr, Mode, Size, u16)` | handler entry (`AVAILABILITY` macro) for instructions matching `MOIRA_WILL_EXECUTE` (`STOP`, `TAS`, `BKPT` in POM68K's `MoiraConfig.h`) | all |
| `didExecute(const char*, Instr, Mode, Size, u16)` | `FINALIZE` for `MOIRA_DID_EXECUTE` (`RESET`) | all |
| `willExecute(M68kException, u16 vector)` / `didExecute(...)` | before/after `execException<C>`, `execBusError<C>`, `execAddressError<C>`, `execMmuBusError<C>`, `execMmu040BusError<C>`, `execAddressError030/040` | all |
| `willInterrupt(u8 level)` | first thing in `execInterrupt<C>` (before any push) | all |
| `didJumpToVector(int nr, u32 addr)` | end of `jumpToVector<C,F>` (on 030/040 before the loop head refetches) | all |
| `didChangeCACR(u32 raw)` | `Moira::setCACR`, reached by `MOVEC` to CACR; receives the RAW value (write-only strobes such as 030 CI/CEI are visible only here; the stored `reg.cacr` is masked by `cacrMask()`) | 020+ |
| `didChangeCAAR(u32)` | `Moira::setCAAR` (`MOVEC` to CAAR) | 020/030 |
| `illegalOpcodeHook(u16 opcode)` → `bool` | `execIllegal<C>`, before the ILLEGAL exception; returning `true` consumes the opcode like a NOP (`prefetch<C, POLL>`). Default `false` | all |
| `willProcessFault(const StackFrame&)` | `processException<C>` for every `AddressError`/`BusError`, including one raised while stacking another (double fault → `halt()`). MMU faults (`MmuBusError`) are not reported | 68000/68010 faults |
| `setBusLock(bool)` | `execTas`: `true` before the read, `false` after the write, also when either throws | all |

No delegate fires for `CINV`/`CPUSH` (`MoiraExec_cpp.h` `execCinv`,
`execCpush`); with the 040 cache model off they are no-ops apart from timing.
No delegate fires for PMOVE/PFLUSH or 040 MMU MOVECs; those bump
`pomJitMmuGen` (§ 9) instead.

### 2.6 Debugger delegates

Evaluated only while `flags` carries `State::CHECK_BP`, `CHECK_WP` or
`CHECK_CP` (set by `Debugger` in `MoiraDebugger.cpp`):

- `didReachSoftstop(u32)`, `didReachBreakpoint(u32)`: end of `execute()`
  slow path, after the instruction retired.
- `pomDidReachWatchpoint(u32 addr, Size S, bool write, bool program)`: before
  the access, at six sites — `read<>`, `write<>` (masked bus address),
  `mmuRead`, `mmuWrite`, `mmu040Read`, `mmu040Write` (LOGICAL address). The
  default forwards to `didReachWatchpoint(addr)`. A watchpoint disables the
  JIT fetch window and data windows (`pomJitFetch`, `pomJitData` test
  `CHECK_WP`).
- `didReachCatchpoint(u8 vector)`: `jumpToVector`, `execMmu040BusError`,
  `execAddressError030/040`.
- `didReachSoftwareTrap(u32)`: `execLineA` when the opcode is a registered
  `debugger.swTraps` entry.

### 2.7 MC68020 CALLM/RTM access control

`virtual bool readModuleCpuSpace8(u32, u8&) const`,
`writeModuleCpuSpace8(u32, u8) const`, `writeModuleCpuSpace32(u32, u32) const`
— default `false`. Called from `execCallm`/`execRtm` (`MoiraExec_cpp.h`)
for type-$01 module descriptors only, at CPU-space addresses
`$10000`-`$1005F` (CAL/AS/IAL/DAL and descriptor-address registers). `false`
means "no access-control hardware" and the core takes the architected format
error. `CALLM`/`RTM` are available on `AV::M68020` only.

---

## 3. Configuration the host injects

| Setter | Default | Effect |
|---|---|---|
| `setModel(Model)` / `setModel(cpu, dasm)` | `M68000` | rebuilds the 65 536-entry jump tables (`createJumpTable`), masks `reg.cacr`, clears `LOOPING`. Call before `reset()`. |
| `setFPUModel(FPUModel)` | `NONE` | rebuilds jump tables, `fpuResetState()`. FPU handlers are `AVAILABILITY(Core::C68020)`: attached to a 68000/68010 model a 6888x is never reached — every $Fxxx stays the Line 1111 trap, vector 11, with the 68000's 6-byte or the 68010's format $0 frame (`moira_rules_test` § Line-F on a 68000/68010, with a 68020 control row that does execute the FMOVE). |
| `setIplDelay(i64 d4, i64 d2)` | 0, 0 (off) | NeoST deferred IPL recognition (WinUAE `ipl_fetch_next` port, § 4). Never set by POM68K. A JIT backend must refuse when on (`pomJitSimpleIpl`). |
| `irqMode` (protected member, no setter) | `IrqMode::AUTO` | `USER` routes through `readIrqUserVector`; `SPURIOUS` → 24; `UNINITIALIZED` → 15. |
| `setMmu040AtcArmed(bool)` | true | false = walk per access (differential mode); also makes `pomJitProbeCode` refuse. |
| `setPomCache040(bool)` | false | arms the data-bearing 040 I/D caches (§ 7); inert below `M68EC040` (`pomCache040Active`). Flushes JIT data mappings on change. |
| `setPomCmStats040(bool)` | false | diagnostic CM-bit histogram per ATC fill, printed at exit. |
| `setPomCache040Timing(PomCache040Timing)` | `{hit=0, lineFill=8, pushLong=2}` | cycles charged through `sync()`; bumps `pomJitCache040Gen`. |
| `pomIcache` (protected member) | `armed=false`, `missPenalty=0` | 030 i-cache timing overlay, below. |
| `debugger` (public member) | empty | breakpoints/watchpoints/catchpoints/soft traps. |
| `setDasm*`, `setDump*` | — | disassembly style only. |

`irqDelay` is internal state, not configuration: `Moira::setSR` sets it to 2
when `cpuModel >= M68020` and the new mask is lower than the old; the
`execute()` slow path decrements it instead of calling `checkForIrq()`.
`reset<C>()` clears it.

### 3.1 `MoiraConfig.h` macros (POM68K values)

| Macro | POM68K | Meaning |
|---|---|---|
| `MOIRA_PRECISE_TIMING` | true | `sync()` around every bus access; 68000/68010 only (§ 4.4) |
| `MOIRA_VIRTUAL_API` | true | virtual host interface |
| `MOIRA_EMULATE_ADDRESS_ERROR` | true | 68000/68010 odd-address `AddressError` |
| `MOIRA_EMULATE_FC` | true | `fcl` maintained by `setFC` |
| `MOIRA_ENABLE_DASM` | true | disassembler tables |
| `MOIRA_BUILD_INSTR_INFO_TABLE` | true | `getInstrInfo` |
| `MOIRA_MIMIC_MUSASHI` | false | accuracy mode |
| `MOIRA_WILL_EXECUTE` | `STOP \|\| TAS \|\| BKPT` | instruction delegate filter |
| `MOIRA_DID_EXECUTE` | `RESET` | instruction delegate filter |

POM68K edits the file in place; NeoST rewrites it at configure time
(NeoST `CMakeLists.txt` forces `PRECISE_TIMING true`, `MIMIC_MUSASHI false`,
`EMULATE_ADDRESS_ERROR true`).

### 3.2 The 68030 i-cache timing overlay (`PomIcache`)

`Moira.h` `struct PomIcache { armed, missPenalty, tag[16], valid[16],
fetches, hits, misses; reset(); clearEntry(caar); }`. Model: 16 lines × 4
longwords, logical, direct-mapped, tag = A[31:8] | supervisor bit 31,
per-longword valid bits, whole-line eviction on tag change (MC68030UM § 6).

- Charged in `pomIcacheFetch<Words>(pc)`, called from `mmuFetchWord` (one
  word) and from `mmuExecuteStart`'s fused path (two words). Both are 68030
  instruction-fetch paths only: on a 040 the overlay can be `armed` and never
  charges (`PomJitLayout::icLive`).
- `fetches` counts whenever armed; nothing else happens unless `reg.cacr`
  bit 0 (EI) is set. A miss does `clock += missPenalty` directly (not
  `sync()`), before the word is translated and read.
- Fetches served from the post-PMOVE pipe (`mmuPipe*`) return before the
  overlay and count nothing.
- CACR strobes: the host's `didChangeCACR(raw)` should call
  `pomInvalidateIcache030(raw)` — CI (bit 3) → `reset()`, else CEI (bit 2) →
  `clearEntry(reg.caar)`. Hard reset should call `pomIcache.reset()`
  (`reset<C>()` does not).
- Arming: set `armed`, `missPenalty`, call `reset()` (POM68K:
  `src/MoiraCpu.h` `armIcacheOverlay`).

### 3.3 Process environment

The core reads no environment variable except `NEOST_EXC_DIAG`, at three
sites, each a function-local `static const bool` evaluated once: `Moira.cpp`
`Moira::setIPL` (`[PIN]` trace), `MoiraExceptions_cpp.h`
`execInterrupt<Core::C68000>` (`[EXC]` trace), `MoiraDataflow_cpp.h`
`jumpToVector` (`[JTV]` trace; the 030/040 early return skips it). The 040
cache switch and CM histogram were moved to `setPomCache040` /
`setPomCmStats040` (`POM68K_VENDOR.md` row 39).

**Since 2026-11-07 the paragraph above is history**: the three sites test a
member set by `setPomExceptionDiag(bool)` (row 49) and the core reads no
environment variable at all. `docs_test` holds it: no `getenv` under
`extern/moira/Moira/`. A host that wants the trace calls the setter; POM68K
does so from `jit::ResolvedConfig::exceptionDiag`, fed by `NEOST_EXC_DIAG`
through the startup snapshot.

---

## 4. Interrupts and time

### 4.1 Pins and polling

- `setIPL(u8)`: on a change, records history (`iplPrev`, `iplChangeClock`,
  `iplChangeClockPrev`), stores `ipl`, sets `State::CHECK_IRQ`. The host must
  call it on every level change of its interrupt resolver.
- `pollIpl()` (= `POLL_IPL`): copies the pin into the polled `reg.ipl`
  (deferred-recognition rules apply when `iplDelay4 != 0`: settled → new
  value; recent → previous value; very recent → `iplDeferred`, applied at
  the next `execute()` slow path). Also feeds the JIT timing probe.
- `checkForIrq()` compares `reg.ipl` with `reg.sr.ipl` (level 7 always
  taken) and clears `CHECK_IRQ` once `reg.ipl == ipl` and nothing is taken.

Recognition points (where `reg.ipl` is refreshed):

| Path | Poll site |
|---|---|
| 68000/68010/plain 020 | `read<>`/`write<>` when the access carries `Flags` `POLL` — the instruction's final prefetch (`prefetch<C,POLL>`); long accesses poll between the halves |
| 68030 | top of `mmuExecuteStart` (every instruction), plus `POLL` sites inside `mmuRead`/`mmuWrite` (and the data-window fast path at the same positions) |
| 68040 | top of `mmu040InstrStart`, plus handler-level `POLL_IPL` in selected instructions (`MoiraExec_cpp.h`) |
| STOP | each `execute()` call while `STOPPED` polls, re-arms `CHECK_IRQ` if level > mask (NeoST level-sensitive fix) and `sync(2)` |
| 6888x long ops | `fpuRunBusy`: every 8-cycle quantum `sync`s, polls, and throws `FpuMidInstruction(reg.ipl)` if an interrupt is acceptable and work remains (§ 6) |
| JIT fetch flavours | `pomJitFetch020<P>`, `pomJitFetch000<C,P>` poll exactly where `read<>` would |

The interrupt is then taken in the `execute()` slow path, after `irqDelay`
and before the next instruction; `execInterrupt<C>` clears `STOPPED` and
`LOOPING`, zeroes `reg.ipl`, raises the mask, stacks (020+: format $0, plus
format $1 throwaway when `reg.sr.m`; format $9 after an FPU mid-instruction
checkpoint on 6888x) and vectors.

### 4.2 STOP

`execStop`: `setSR(operand)`, `flags |= STOPPED`. `execute()` then returns
after `sync(2)` per call until an interrupt is taken, a trace fires, or (user
mode) a privilege exception. NeoST's STOP+8 grace (`stopGraceUntil`) is not in
this fork (§ 12). On 020+, a STOP whose operand lowers the mask also arms
`irqDelay = 2`, consumed by the next two `execute()` calls.

### 4.3 Running

`execute()` runs one instruction (fast path when `flags == 0`);
`execute(i64 cycles)` and `executeUntil(i64 cycle)` loop `execute()` while
`clock < target` and may overshoot by one instruction. `execute()` asserts
`reg.pc0 == reg.pc` at entry and exit; a host that sets PC must keep `pc0`
equal and refill the queue (`setIRD`/`setIRC`) on pre-030 models.

### 4.4 `MOIRA_PRECISE_TIMING` per core

- C68000/C68010: true → `SYNC(x)` = `sync(x)` inside handlers and around every
  bus cycle; `CYCLES_68000/68010` are empty. Devices are caught up at bus
  granularity.
- C68020: `SYNC(x)` is always empty (`MoiraMacros.h`); time arrives through
  `CYCLES_68020` = `sync(c + cp)` at instruction end, plus the mid-instruction
  charges listed in § 2.1. The 020 cycle tables are used for the 030 and
  the 040 as well (approximate; § 10).

### 4.5 Time scaling in wrappers

The core has one clock. POM68K's 030/040 wrappers run the core faster than
machine time and scale back down when catching up peripherals: e.g.
`src/Cpu030.h` `cacheBoost_` (core cycles per machine cycle), `boost_`,
`machineClock()`, `flushTicks()`, with `icacheMiss_` passed to the overlay as
`missPenalty`. The contract a host must keep is generic: every cycle the core
reports through `sync()` (and the direct `clock` additions of the i-cache
overlay and `pomJitBiasClock`) is a core cycle; conversion to device time, and
the deadline at which `sync()` calls `catchUp()`, belong to the host.
Device code must never read host wall time (`CLAUDE.md` invariant).

---

## 5. MMU and bus faults

The host provides physical-bus callbacks only. Translation, ATCs, table walks
(including U/M descriptor write-back through `write16`), transparent
translation and fault frames live in the core. No host-side MMU exists or is
expected.

### 5.1 68030 (`M68030` only)

- Funnel: `read<>`/`write<>` → `mmuRead<C,S,F>`/`mmuWrite<C,S,F>` for every
  access (bus splitting, access log `mmuAd`/`mmuIdx`/`mmuIdxDone`);
  `mmuFetchWord` for every instruction word; `mmuTranslateAccess` per
  sub-access (stamps `mmuAcc*`, returns identity when TC.E clear or FC = 7).
- `mmuExecuteStart<C>`: `POLL_IPL`, resets `mmuState`, `mmuIdx/IdxDone`,
  `mmuAd`, `mmuFixupReg`, `mmuCcrSave`, `mmuLogging`, `mmuRmw`, `mmuOpcodeV`;
  fetches `ird`/`irc`.
- ATC: `MmuAtcEntry mmuAtcArr[MMU_ATC_ENTRIES]` (22), fully associative,
  exact FC match, pseudo-LRU (`mmuAtcTouch`, `mmuAtcMruCount`) with a
  last-hit memo `mmuAtcLast[8][2]`. Flushes: `mmuAtcFlushAll/FlushFc/
  FlushPage/FlushPageFc` (PFLUSH family, PMOVE).
- TT registers `reg.tt0`/`reg.tt1`: `mmuMatchTTAccess` (OK-match only;
  locked RMW uses the RWM rule).
- Faults: `mmuPageFault` captures `mmuSsw`, `mmuFaultAddr`, `mmuWb*`,
  `mmuStageB`, applies pending `(An)±` fixups, throws `MmuBusError`;
  `execMmuBusError<C>` stacks format $A (short) or $B (long) via
  `writeStackFrameShortBusFault`/`writeStackFrameLongBusFault`.
- RTE of $B with SSW DF cleared by the handler arms the one-shot latch
  `mmuRteSubstArmed/Write/Addr/Data/Pc`; `mmuRead`/`mmuWrite` complete the
  re-executed access without a bus cycle (read returns the stacked data
  input buffer; write is skipped). Any new fault voids it.
- Pipe carry: PMOVE to TC/CRP/SRP calls `mmuCapturePipe()` with the old
  mapping; `mmuFetchWord` serves up to 4 linear words (`mmuPipeAddr`,
  `mmuPipe[4]`, `mmuPipeCnt`) and drops them on the first non-linear fetch.
  `pomMmuPipeLive()` exposes it.
- External /BERR: § 2.2.

### 5.2 68040 (`>= M68EC040`)

- `mmu040Read<C,S>(addr, data)`/`mmu040Write<C,S>` with page-boundary
  splitting; `mmu040Translate` (TTR `mmu040MatchTTR`, then ATC, then
  `mmu040Walk` with U/M write-back); `mmu040Fault` throws `MmuBusError`;
  `execMmu040BusError<C>` stacks format $7 with the last-write dichotomy
  (fault on the instruction's last write → next PC, no restore; otherwise
  restart: CCR restored, fixups undone, PC = instruction).
- `mmu040InstrStart<C>`: `POLL_IPL`, resets `mmu040EffAddr`,
  `mmu040AccFirst`, `mmu040AccSplit`, `mmu040LastWrite`, `mmu040Moves`,
  `mmu040Lrmw`, `mmuFixupReg`, `mmu040CcrSave`; fetches `ird`/`irc`.
- ATCs: `Mmu040AtcEntry mmu040AtcI[32]`, `mmu040AtcD[32]`, pseudo-LRU, page
  size from TC.P (4 K / 8 K; the 8 K cell is not fuzzed), global bit honoured
  by `mmu040AtcFlushNonGlobal`. `setMmu040AtcArmed(false)` forces a walk per
  access.
- Registers: `setURP040`, `setSRP040`, `setTC040` (all flush the ATCs),
  `setITT0/ITT1/DTT0/DTT1` (masked `$FFFFE364`, bump `pomJitMmuGen`),
  `setMMUSR040`.
- MOVEM restart latch: `mmu040MovemArmed`, `mmu040MovemEa` (public) survive
  from a faulting MOVEM to its RTE-restarted completion.
- MOVE16: `mmu040Move16[4]` line buffer, `mmu040GetMove16`/`PutMove16`.

### 5.3 Function codes

`fcl` holds FC1:FC0, `fcSource` selects the FC source (0 = `sr.s<<2 | fcl`,
1 = `reg.sfc`, 2 = `reg.dfc`; `Moira::readFC`). `setFC<M>()` always drives
`FC::USER_DATA` for operand accesses, including PC-relative reads (SST rule,
§ 12). `MOVES` sets `fcSource` (030) or `mmu040Moves` (040). A host that
decodes FC (e.g. CPU space, FC = 7) reads `readFC()` from inside its
callback. The core never generates FC = 7 cycles for coprocessor
communication (§ 6.4); FC = 7 reaches the host only through `MOVES` with
SFC/DFC = 7 and through CALLM/RTM's own hooks (§ 2.7, which bypass the bus).

### 5.4 Known model limits

- One-word lookahead: `irc` at `pc+2` is fetched (and may fault or touch a
  page) at instruction start on 030/040 even when the instruction is one word
  long; visible only for an instruction on a page's last word.
- Restart, not continuation: Moira re-executes a faulted instruction after
  RTE; WinUAE continues mid-instruction from the access log. The RTE $B
  substitution latch and the 040 MOVEM latch bridge the observed cases; other
  continuation-dependent sequences are unverified.
- 030 `MOVEM`/FPU operand fixups follow WinUAE's encoding; see
  `POM68K_VENDOR.md` § *68030 MMU bus layer*.

---

## 6. FPU

### 6.1 Arithmetic backend

`extern/softfloat` (`softfloat.c`, `softfloat_decimal.c`, `softfloat_fpsp.c`,
headers), built as the separate static C library `softfloat68k`
(`extern/softfloat/CMakeLists.txt`); `moira` links it PUBLIC
(root `CMakeLists.txt`). `Moira.cpp` includes `softfloat.h` and
`softfloat-specialize.h` inside `extern "C"`. `MoiraTypes.h` `FpuExtended`
mirrors `floatx80` bit for bit so `Moira.h` does not depend on softfloat.

Thread safety: `MoiraExecFPU_cpp.h` declares one `static float_status
moiraFpuFs` shared by every `Moira` instance; every instruction re-arms it
from FPCR (`fpuSetMode`), which makes sharing safe on one thread only.
`softfloat.c` additionally has mutable globals (`floatx80_internal_sign`,
`_exp`, `_sig`, `_exp0`, `_sig0`, `_sig1`, `_precision`, `_mode`). Two CPU
instances executing FPU code on different threads race. That list is the
complete one: the writable data and BSS symbols of the built
`libsoftfloat68k.a` are exactly those eight (`nm`, 2026-11-08 — the
rounding/exception state travels by pointer in `float_status`). The core
library adds two: `moiraFpuFs` (above) and the function-local histogram in
`mmu040AtcFill`, shared by every instance but touched only while
`setPomCmStats040(true)` arms the diagnostic.

### 6.2 6888x vs integrated 040

- `M68881`/`M68882`: full 6888x programmer's model, transcendental set (FPSP
  port), packed decimal, pre-instruction exceptions (format $0) and
  post-instruction exceptions (format $3 with `fpu.ea`, also on the 020/030 —
  ruling D23, `execFpuException`), FSAVE NULL (4 bytes) / IDLE ($1F/$18 on
  68881, $1F/$38 on 68882) / BUSY ($1F/$B4, $1F/$D4); FRESTORE accepts
  these and opaque hardware BUSY frames; invalid frames → format error
  (`execFRestoreFormatError`). On `M68030`, FSAVE skips the eight
  68882-internal longs, as the oracle does.
- `M68040`: native subset only; unimplemented opcodes and denormal/packed
  operands trap to FPSP vectors (`execFpuUnimplemented040`,
  `execFpuDatatype040`) with revision-$41 FSAVE frames (NULL/IDLE 4 bytes,
  UNIMP header `$41/$30`, BUSY header `$41/$60`). FPCR keeps all 16 bits (`setFPCR`).
- No FPU on a 040 model (`NONE`): F2xx → vector 11 format $4
  (`execFpuDisabled040`).

### 6.3 Mid-instruction interrupts (6888x only)

`fpuRunBusy<C>` sets `fpu.busy`, `busyOpcode`, `busyExt`, `busyUntil`,
keeps the completed result in `busyFp[8]`/`busyFpsr`, and runs the operation
in 8-cycle quanta. If an acceptable interrupt is pending it throws
`FpuMidInstruction(level)`; `processException<Core::C68020>` sets
`fpuMidInterruptFrame`, calls `execInterrupt<C>`, which stacks format $9
(`writeStackFrame1001` with `fpu.busyIa`) on models below 040. The next FPU
instruction, FSAVE (BUSY frame) or `fpuCompleteBusy(wait)` completes or
serializes the pending command. The integrated 040 never enters this path.

### 6.4 Coprocessor detection

The core emits no CPU-space (FC = 7) CIR cycles: with an FPU attached, F-line
coprocessor instructions execute internally; with `NONE`, they take Line-F.
A guest that probes the CIR directly (MOVES with SFC = 7 to the coprocessor
CIR window, e.g. `$22000` for coprocessor ID 1) reaches the host's bus
callback with `readFC() == 7`. A host that wants a detectable absent FPU must
raise a bus error there (POM68K: `src/Cpu030.cpp`
`Cpu030::checkCoprocessorBus` calls `extBusError()` when `getFPUModel() ==
FPUModel::NONE`, `readFC() == 7`, `(addr & $FFFFE000) == $22000`;
`src/Pb030Cpu.cpp` does the same). What a host must answer on a present FPU's
CIR read is unverified. `TODO.md` records that this probe is reimplemented per
board for lack of core-emitted CPU-space cycles.

---

## 7. 68040 caches

`MoiraCache040.h` `struct Cache040`: 4 KB, 64 sets × 4 ways × 16-byte lines,
physically indexed and tagged (set = PA[9:4], tag = PA[31:10]), per-longword
dirty bits on the data cache, global 2-bit replacement counter, invalid ways
first. CM modes: `CM_WRITETHROUGH`, `CM_COPYBACK`, `CM_SERIAL_NC`, `CM_NC`.
The struct owns no bus and no clock.

In `Moira` (active only when `setPomCache040(true)` and model `>= M68EC040`):
`pomCache040I`, `pomCache040D`, line fills and writebacks through the host's
`read16`/`write16`, charges from `PomCache040Timing` through `sync()`,
`CINV`/`CPUSH` via `pomCacheOp040` (CPUSH drains dirty longwords before
invalidating), MOVE16 bypass, locked RMW and MOVES resolving dirty data
first. The legacy `PomIcache` overlay is independent of this model.

Host obligations when the model is armed:

- **Alternate bus masters** (DMA, a second CPU) must snoop:
  `pomSnoop040Read(pa, dst, bytes, invalidate)` supplies dirty D-cache bytes
  into `dst` (returns true if any were supplied) and optionally invalidates
  the touched lines; `pomSnoop040Write(pa, src, bytes, sink)` either
  invalidates the D lines (`sink == false` or a 16-byte line write) or merges
  into resident lines and returns true when a dirty hit means memory must be
  inhibited. Only the data cache is snooped. No POM68K board calls these
  today (only `tests/cache040_test.cpp`).
- **Save states**: `pomFlushAtcs()` calls `invalidateAll()` on both caches.
  Dirty copyback lines are discarded, not pushed to memory. **Open defect:**
  a snapshot taken with dirty copyback data loses that data on restore.
- **Debugger views**: `pomCache040Data()`, `pomCache040Inst()` return the
  live `Cache040&`; `pomCache040Armed()`, `pomCache040Active()`,
  `pomCache040CodeMatches(addr, bytes)`, `getPomCache040Timing()`.

---

## 8. Save-state contract

The core contains no serializer. POM68K's visitor is
`src/MoiraSnapshot.h` `MoiraSnapshot::visitCpuCommon(Ar&)`, a thin class
deriving from `Moira` (through `MoiraDebugSeam`) to reach protected state.

Serialized, in order: `clock`; `reg.pc`, `reg.pc0`, `reg.r[16]`, `usp`,
`isp`, `msp`, `reg.ipl`, `vbr`, `sfc`, `dfc`, `cacr`, `caar`; `reg.sr.t1/t0/
s/m/x/n/z/v/c/ipl`; 030 `crp`, `srp`, `tc`, `tt0`, `tt1`, `mmusr`; 040
`urp040`, `srp040`, `tc040`, `itt0`, `itt1`, `dtt0`, `dtt1`, `mmusr040`;
`queue.irc`, `queue.ird`, `irqMode`, `ipl`, `iplPrev`, `iplChangeClock`,
`iplChangeClockPrev`, `iplDelay4`, `iplDelay2`, `iplDeferred`, `irqDelay`;
`trace040Pending`, `tracePc040`, `fcl`, `fcSource`, `exception`,
`loopModeDelay`, `readBuffer`, `writeBuffer`, `flags` minus debugger bits;
`fpu.fp[8]` (high, low), `fpcr`, `fpsr`, `fpiar`, `state`, `expState`,
`expPend`, `fsaveCcr`, `fsaveEo[3]`, `ea`.

Not serialized:

| Field(s) | Status |
|---|---|
| `cp` | deliberate: zeroed at each `C68020` instruction start (`AVAILABILITY`); set to 0 on load |
| `pomIcache` tags/valid/counters, `mmuAtcArr`, `mmu040AtcI/D`, `pomCache040I/D` | deliberate: caches are flushed on load (dirty 040 lines lost — § 7) |
| `fpu.busy`, `busyUntil`, `busyOpcode`, `busyExt`, `busyIa`, `busyFp[8]`, `busyFpsr` | omitted: a snapshot taken between a format-$9 interrupt and the FPU command's completion loses the pending 6888x result |
| `fpu.cmdreg1b`, `cmdreg3b`, `stag`, `dtag`, `wbtm66`, `grs`, `wbte15`, `e1`, `e3`, `t`, `fpiarCu`, `fpt`, `et`, `wbt` | omitted: integrated-040 UNIMP/BUSY FSAVE micro-state |
| `mmuPipeAddr`, `mmuPipe[4]`, `mmuPipeCnt` | omitted: a snapshot inside a PMOVE's pipe shadow resumes without it |
| `mmuRteSubstArmed/Write/Addr/Data/Pc` | omitted: a snapshot between an RTE $B with DF cleared and the retried access re-runs the bus cycle |
| `mmu040MovemArmed`, `mmu040MovemEa` | omitted: a snapshot between a faulted MOVEM and its RTE restart loses the saved EA |
| per-instruction 030/040 MMU bookkeeping (`mmuState`, `mmuAd`, `mmuFixup*`, `mmu040Acc*`, fault capture) | not needed at an instruction boundary (reset by the loop heads) |
| `fpuMidInterruptFrame` | transient inside `processException` |
| models, `mmu040AtcArmed`, `pomCache040On`, `PomCache040Timing`, `pomIcache.armed/missPenalty`, dasm styles | construction/host configuration |
| all `pomJit*` state | derived; rebuilt by the engine |
| `mmu040TtrTab` | derived from ITT0/ITT1 and DTT0/DTT1; self-validating — the first `mmu040MatchTTR` after a load compares the register pair and rebuilds |

Whether each "omitted" row is reachable at a snapshot boundary in practice is
unverified; the fields survive instruction boundaries by construction.

On load the visitor: restores `flags` as `(guestFlags & ~kDebugFlags) |
liveDebug` (`kDebugFlags` = `CHECK_BP | CHECK_WP | CHECK_CP | LOGGING`, so
debugger requests are host state and are neither saved nor overwritten); sets
`cp = 0`; calls `pomIcache.reset()`, `pomJitDisarm()`, `pomFlushAtcs()`.
`pomFlushAtcs()` (protected, `Moira.h`) runs `mmuAtcFlushAll()` and
`mmu040AtcFlushAll()` — both reach `pomJitMapMoved()`, which also empties the
JIT data TLBs — and invalidates both 040 caches. The generation bump makes
the engine re-prove its blocks. RAM, replaced under translated blocks, is
covered on the host side: no POM68K restore calls `jit::Engine::flushAll()`
directly, but every board's `visit()` load branch raises the code guard's
`JitGuard::invalidate()` (all fourteen `*Memory` classes, directly or through
their `mapChanged()`), which the engine treats as `mustFlush` — every block
dropped at its next service — and `visit()` in `src/MoiraSnapshot.h` disarms
the window and flushes both ATCs. A port with a block cache must do the
equivalent on load.

`readBuffer`/`writeBuffer` are 68010 format-$8 state; every store goes
through `pomSetRB<C>`/`pomSetWB<C>`, which compile to nothing off
`Core::C68010` (argument side effects kept), so snapshots do not depend on
which engine ran (`POM68K_VENDOR.md` row 24).

---

## 9. JIT seam (optional layer)

Every member below is public on `Moira` (a direct, non-virtual call from
`src/jit/`) and inert until something arms it. An interpreter-only host can
ignore the whole section; the cost when disarmed is one predictable branch at
each fetch/data site.

### 9.1 Members by purpose

| Group | Members | Caller | Invariant |
|---|---|---|---|
| Fetch window | `PomJitWindow pomJitWindow`, `pomJitArm`, `pomJitDisarm`, `pomJitCovers`, `pomJitPeek`, private `pomJitFetch`, `pomJitFetch020<P>`, `pomJitFetch000<C,P>`, private `pomJitStampAccess`, `pomJitCache040Match` | arm/disarm/covers: `jit::Engine`; fetch: interpreter fetch sites (`mmu040InstrStart`, 040 `readExt`, `mmuFetchWord`, fused `mmuExecuteStart`, plain-020 and 68000 `prefetch`/`fullPrefetch`/`readExt`) | window = host pointer into guest RAM/ROM for `[base, base+len)`, `len >= 4`; valid only while `gen == pomJitMmuGen` and `super == sr.s`; refused under `CHECK_WP`; on an active 040 cache every byte must match resident I-cache data. Guest writes are visible through the pointer (no SMC invalidation needed). `pomJitFetch000` replays `read<>`'s SYNC(2)/FC/POLL/bus-stall/SYNC(2) and bails on odd addresses. |
| Translation generation | `pomJitMmuGen`, `pomJitMapMoved()` | interpreter (ATC flushes, PMOVE TC/SRP/CRP, TTR setters, 040 IC CINV/CPUSH) | any logical→physical change bumps the counter and flushes the data TLBs |
| ATC eviction | `pomJitAtcEvict(page, len, code)` | interpreter (`mmuAtcLookup`, `mmuAtcFill`, `mmu040AtcLookup`, `mmu040AtcFill`) | derived state dies with its ATC entry: code eviction disarms an overlapping window; data eviction kills matching DTLB slices and bumps `pomJitCache040Gen` |
| Probes | `pomJitProbeCode`, `pomJitProbeData` (const, no walk, no throw), private `pomIdentityProbeBound` | `jit::Engine` between instructions | read-only ATC scan; identity for TT/TC.E-off/no-MMU (4 KB bound until TC.PS is programmed); 68000/68010 refuse `logical > $FFFFFF`; 68EC030 refuses; data writes require WP clear and M already set |
| Data TLB | `PomJitDtlbEntry {tag, codeMask, host}`, `PomJitDtlb {kEntries=256, kEmpty, kSliceShift=8}`, `pomJitDtlbR/W`, level-0 `pomJitDataR1/W1`, `pomJitDataTag`, `pomJitDtlbFlush`, `pomJitDtlbFillFn/Ctx`, `pomJitData<N,W>`, `pomJitDataSlow`, `pomJitData030Ok`, `pomJitData030Hits/Refusals` | fill: `jit::Engine::fillDtlbThunk`; lookup: generated code and the interpreter's `mmuRead`/`mmuWrite` (030) and `mmu040Read`/`mmu040Write` (040) | 4 KB logical slices, privilege in tag bit 31; refused under watchpoints, MOVES/alternate FC, locked RMW, page straddle, active 040 cache; `codeMask` bit = 256-byte sub-slice holding translated code (writes there must go through the memory map) |
| Slow data doors | `pomJitReadData`, `pomJitWriteData`, `pomJitReadProg`, `pomJitJsr040` (all `noexcept`) | generated code | perform the real access; a fault becomes `false` with nothing committed (except `pomJitJsr040`, which processes the exception itself) |
| 040 D-cache lines | `PomJitCache040Entry`, `pomJitCache040R/W`, `pomJitCache040Gen`, `pomJitCache040Bump`, `pomJitCache040Tag`, `pomJitCache040Publish`, `pomJitCache040Consumer`, `pomJitCache040NativeReads/Writes` | publish: interpreter (`mmu040Write` when consumer), slow doors; consumer flag: `jit::Engine::setEnabled` | entries validated by logical tag, generation, ATC memo, valid bit and physical tag |
| Execution | `pomJitExecOne()`, `pomJitIdle()`, `pomJitSuper()`, `pomJitIrd()` | `jit::Engine` | caller must have `pomJitIdle()` (`flags == 0`); runs exactly the interpreter's instruction-start contract; false = did not retire normally |
| Timing | `PomJitTiming`, `pomJitBeginTiming`, `pomJitEndTiming`, `pomJitTimingProbe`, `pomJitIcachePeekPenalty`, `pomJitBiasClock` | `jit::Engine` tracer, backends | probe counts IPL polls and their position relative to 040 data accesses and 030 fetch words; `pomJitBiasClock` adds to `clock` without `sync()` |
| Clock/IPL helpers | `pomJitSync(int)` (= `sync`), `pomJitSimpleIpl()` | generated code, backends | generated cycles must go through `sync()`; refuse to compile when `iplDelay4 != 0` |
| Bus stall | `pomJitSetBusStall(fn, ctx)`, `pomJitBusStallFn/Ctx` | set by a cycle-exact wrapper (POM68K `src/Cpu68k.cpp`); called by `pomJitFetch000` | reproduces a per-access charge the host applies inside `read16()` |
| Layout | `PomJitLayout pomJitLayout() const` | native backends | byte offsets from the object of `reg.d/a`, `pc`, `pc0`, SR flag fields, `reg.ipl`, `ipl`, `clock`, `flags`, `ird`, `irc`, DTLB bases, 040 cache tables/epoch/ATC memo, `mmu040MovemArmed`, `reg.cacr`, `PomIcache` arrays/counters/penalty, plus `cache040Live`, `icLive`, `is030`, `mmuRmw`. Computed from a live object; generated code writes raw offsets, so the layout is per build. |
| Pipe | `pomMmuPipeLive()` | `jit::Engine` | refuse to arm the window while the post-PMOVE pipe is live |
| 030 log counters | `pomMmuBumpIdx`, `pomMmuBumpIdxDone`, `pomMmuIdxCeiling` | interpreter | saturating, because generated 030 code never passes `mmuExecuteStart` |

### 9.2 What an interpreter-only host gets

Since 2026-11-07 (`POM68K_VENDOR.md` row 55) the INTERPRETER arms the fetch
window itself when no engine does: `pomSetInterpWindow(on, codeSpanFn, ctx)`
gives the core a host-pointer provider for physical pages, and the 68030 and
68040 loop heads call `pomInterpArm` on a miss — the decisions of
`Engine::armWindow`, without its block bookkeeping. `jit::Engine` switches
it on while it is disabled and off while it runs; a bare host calls the
setter once with its own span function. The fused 030 `ird`/`irc` fetch
(row 33) is a window hit (`pomJitFetch(reg.pc, 4)`), so it benefits too.
Before that date the window was armed only by `jit::Engine`: The 030
interpreter data window (row 34, `pomJitData030Ok`) needs
`pomJitDtlbFillFn`, which only `jit::Engine`'s constructor binds (when
`config_.dataWindow`; default on for the 68030 family), and unbinds in its
destructor. In POM68K the interpreter arm still benefits from the data window
because an `Engine` object exists even when disabled. **A host that runs bare
`Moira::execute()` without constructing a `jit::Engine` gets the fetch
window and the fused fetch by calling `pomSetInterpWindow`, and still no
data window** (its fill function is the engine's). `TODO.md` § *Surface
portable* lists separating the seam from the interpreter hot path at compile
time as open work.

### 9.3 Machine boundary: `jit::MemoryHooks` and `jit::CodeGuard`

`src/jit/JitEngine.h` `struct MemoryHooks` (plain function pointers + `self`):

| Hook | Contract |
|---|---|
| `codeSpan(self, phys, len&)` | host pointer to readable bytes at physical `phys`, `len` = contiguous valid bytes; nullptr for anything that is not plain RAM/ROM (I/O, VRAM, read side effects, a boot overlay still up) |
| `dataSpan(self, phys, len&, write)` | same, for the data TLB; with `write` also refuse ROM, write side effects, debug write-watch |
| `aliasCodeMask(self, physSlice, pageMap, pages)` | optional: extra 256-byte write-mask bits for other bus addresses reaching the same bytes |
| `setGuard(self, CodeGuard*)` | attach/detach the write guard (nullptr while the engine is disabled) |
| `ramBytes(self)` | physical RAM size, for the guard's map |

`src/jit/JitGuard.h` `struct CodeGuard` (dependency-free, the only JIT header
a memory map includes): one byte per 256-byte physical slice (`kShift = 8`),
each byte a mask of 32-byte sub-slices (`kSubShift = 5`). Obligations:

- **Every store into guest RAM that does not come from the CPU must call
  `guard->note(phys, bytes)`** when a guard is attached. CPU stores through
  the host's `write8/16` must call it too (POM68K maps call it in their write
  paths). Otherwise translated blocks keep stale code.
- **Every change of the address map that writes no byte** (overlay flip, ROM
  reload, bank switch) must call `guard->invalidate()` **and** flush the CPU's
  data TLB directly with `pomJitDtlbFlush()` (POM68K `Q605Memory::jitMapChanged`),
  because the interpreter's data window bypasses the guard.

For an Atari ST host this means: blitter writes, DMA sound (if it writes —
unverified), ACSI/floppy DMA into RAM (FDC), any debugger or host poke
(GEMDOS host-disk transfers, snapshot loads, cartridge/TOS reload), and any
second master all call `note()`; MMU/GLUE configuration changes that remap
RAM or ROM call `invalidate()` + `pomJitDtlbFlush()`. With the 040 cache model
armed, alternate masters must also snoop (§ 7). The fetch window itself needs
no notification (it reads the live buffer).

`jit::Engine` also depends on `jit::ResolvedConfig` (`src/jit/JitConfig.h`),
which includes POM68K's `StartupSnapshot.h`; reusing `src/jit` outside POM68K
requires replacing that include (open in `TODO.md`).

---

## 10. Generic vs Macintosh-tuned

| Seam / behaviour | Class | Evidence (`POM68K_VENDOR.md`) |
|---|---|---|
| 030/040 MMU, ATCs, $A/$B/$7 frames, last-write dichotomy | generic (WinUAE-arbitrated) | rows 3-5, 18; `sst68030`, `sst68040` |
| Prefetch-pipe carry (4 words after PMOVE TC/CRP/SRP) | Mac-tuned approximation of the real 030 pipe; motivated by the LC II ROM `pmove tc; nop; bne; jmp (An)` idiom | row 11, § *Prefetch-pipe carry* |
| `POLL_IPL` at mode-5 instruction start | generic necessity of the mode-5 model; found with the LC II ROM TimeDBRA loop | row 11 |
| RTE $B DF-cleared one-shot latch | generic 030 protocol, implemented as restart+substitution; pinned only by Mac OS slot-probe recovery (`berr030_test`) | row 10 |
| `irqDelay = 2` after a mask-lowering SR write (020+) | Mac-tuned: depth 2 is empirical (SimCity 2000 VBL re-entry); PRM reading is 1 | row 13, § *IRQ-recognition delay* |
| i-cache overlay geometry | generic MC68030UM § 6 | row 14 |
| `missPenalty` value (4) and wrapper `cacheBoost_` (4) | Mac-tuned throughput calibration on the LC II | row 14, `src/Cpu030.h` comment |
| 020 cycle tables reused for 030/040 | approximation, not calibrated per model | `src/Cpu030.h` comment |
| External /BERR on plain 020 with `mmuAcc*` stamping | generic; found with the Mac LC ROM 32-bit probe | row 21 |
| FC = 7 CIR probe | board-side, Mac (V8, PowerBook) | row 37, § *MOVES bus faults* |
| 68000 SST rules (AE idle 0/2, uniform PC−2 frames, early DIVS overflow exit, DIV N/Z preserved, ADDQ/SUBQ.l An 6 cycles, BTST Dn,#imm 10, ASR C/X clear past width, FC = data for PC-relative reads) | generic 68000 but SingleStepTests-graded (grade D); two contradict higher-grade readings | row 2, § *SingleStepTests convergence* |
| 24-bit pc refusal in `pomJitProbeCode` (68000/68010) | generic (bus width); "Mac code never leaves 24 bits" is only the cost argument | § *JIT seam* point 4 |
| DTLB 4 KB slices (`>> 12`), `kSliceShift = 8`, 4 KB identity bound | generic host choices; 030 pages of 256 B–32 KB are handled by slice loops | rows 22, 31 |
| Data window default on for 030, off for 040 | measured on Mac OS workloads | row 34 |
| `setIplDelay`, `iackSyncBefore/After`, STOP level-sensitive | NeoST/Atari-tuned (Hatari/WinUAE); off/default in POM68K except the STOP fix | NEOST_VENDOR.md |
| Odd-SP interrupt frames (no A0 masking 010/020, single vector scale) | generic; found with Lode Runner | row 15 |
| MOVES releases FC on fault | generic | row 37 |
| FSAVE 030 skipping eight 68882 longs | oracle-specific (WinUAE 68030-MMU build) | § *MC68882 FPU execution* |

---

## 11. What a port must bring along

Minimal file set:

- `extern/moira/Moira/` — all 25 files (`Moira.cpp` and `MoiraDebugger.cpp`
  are the only translation units; the rest are headers, including the
  `_cpp.h` inclusions, `MoiraCache040.h`, `MoiraConfig.h`) and
  `extern/moira/Moira/LICENSE`.
- `extern/softfloat/` — `softfloat.c`, `softfloat_decimal.c`,
  `softfloat_fpsp.c`, `softfloat.h`, `softfloat-macros.h`,
  `softfloat-specialize.h`, `softfloat_fpsp_tables.h`, `VENDOR.md`.
- Optional: `src/jit/` (engine + `threaded` backend + native backends), which
  today also needs `jit::ResolvedConfig`'s POM68K startup dependency replaced.
- Optional: a CPU state visitor modelled on `src/MoiraSnapshot.h`
  (GPLv3, POM68K code) — the core has none.

Build: C++20 (`CMAKE_CXX_STANDARD 20`, designated initializers in
`Moira.cpp`); C for softfloat; C++ exceptions and RTTI enabled
(`dynamic_cast` in `processException`); `MOIRA_HOT_INLINE` is defined in
`Moira.h` (`always_inline` on GCC/Clang); under Emscripten
`-fwasm-exceptions` for compile and link (root `CMakeLists.txt`); `moira` is
built with `-Wno-unused-parameter -Wno-unused-variable` and included as
`SYSTEM`.

Licences: Moira MIT (`extern/moira/Moira/LICENSE`, Dirk W. Hoffmann; notice
must be kept). softfloat: SoftFloat-2a + BSD (QEMU parts) + GPLv2-or-later
(`extern/softfloat/VENDOR.md`), linked as a separate library. POM68K code
(`src/`, including `src/jit/` and `src/MoiraSnapshot.h`) is GPLv3. A binary
linking softfloat is subject to GPL obligations.

Mac-free gates (bare `moira` + softfloat, flat RAM, no ROM):

| Gate | Registered in | Proves |
|---|---|---|
| `sst68000` | `cmake/Pom68kComponentGates.cmake` | SingleStepTests 68000 corpus (user-fetched; soft-skips when absent) |
| `sst68030` | same | oracle-generated 030 corpus (local; soft-skips) |
| `sst68040` | same | oracle-generated LC040 corpus (local; soft-skips) |
| `fpu_sanity` | `cmake/Pom68kCpuGates.cmake` | 6888x smoke, format $9 + BUSY resume, detached F-line |
| `fpu040_test` | same | integrated 040 FPU |
| `cache040_test` | same | 040 cache geometry, policy, copyback, CINV/CPUSH, snooping, timing |
| `callm_rtm_test` | same | CALLM/RTM |
| `moira_rules_test` | same | hand-pinned rules (DIVS of `$80000000` on six models) |
| `berr030_test` | `cmake/Pom68kMachineGates.cmake` (links `moira` only) | external /BERR, RTE $A/$B, DF-cleared latch |
| `jit_asset_free_lockstep_test` | `cmake/Pom68kJitGates.cmake` (links `pom68k_core` for the engine; flat-RAM 68040 program) | JIT vs interpreter boundaries, restart/last-write frames, guard indexing |

The three SST corpora are not in the repository; a green run proves only
asset detection unless the corpus was present.

---

## 12. Known divergences from NeoST's copy of Moira

POM68K's copy was taken from NeoST at `47d6c39` (lineage `0b96cab` →
`47d6c39`, `POM68K_VENDOR.md` provenance). Both keep NeoST's earlier patches:
`NEOST_IPLFETCH` (`setIplDelay`/`pollIpl`), level-sensitive STOP, guarded
reset, 24-bit-masked watchpoints, `iackSyncBefore/After`, `NEOST_EXC_DIAG`.
`diff -rq` shows 13 differing files plus POM68K's `MoiraCache040.h`.

NeoST's nine post-copy patch groups, and where each stands here:

| NeoST group | Here |
|---|---|
| DIVS unsigned absolute values (`74b2f2d`) | ported, row 38 |
| MOVE to SR `fullPrefetch` (`ce1cc52`); #,SR / #,CCR / MOVE to CCR double reload (`d8d33bb`); DIV internal cycles before the prefetch (`262f63a`) | ported, row 40 |
| `illegalOpcodeHook` (`fdbce2e`), `willProcessFault`, `setBusLock` (`ce1cc52`) | ported, row 42 |
| `willAccessBus(addr, write, size)` / `didAccessBus()` around each bus cycle, with the long access split `SYNC(2)` + hook + `SYNC(2)` (`ce1cc52`) | not ported: two virtual calls per bus cycle. A port needs a compile-time switch and must add the hooks to `pomJitFetch000`, which bypasses `read<>` |
| STOP+8: `stopGraceUntil`, quantised wake-up, STOP with no bus cycle (`993ce5e`, `2fc53a8`) | not ported: no corpus vector covers STOP, the final NeoST code charges the STOP itself 0 cycles, and the wake rounding would reach 020/030/040 STOPs unless guarded |

Added here and absent in NeoST: the 68000 bus-cycle order of row 41 (group-0
frame stacking order and idle placement, RTE/RTR unstack order, JSR fetch
before push, CHK's prefetch, MOVEM.l extra read and predecrement write order,
long ADD/SUB write-back and ADDX/SUBX read order, BTST Dn,#imm idle
placement), and `extBusError000`.

Changed in POM68K, absent in NeoST (68000-visible, `POM68K_VENDOR.md` row 2):
address-error idle cycles (2 for data, 4 for instruction flow, all before
the frame), uniform
`PC − 2` data-fault frames (`makeFrame`, `STD_AE_FRAME = 0`), `(An)+`
post-increment before a faulting read, early DIVS overflow exit with N/Z
preserved, ADDQ/SUBQ `.l` to An = 6 cycles, BTST Dn,#imm = 10 cycles, ASR past
operand width clears C/X (68000/68010 only), LINK A7 pushes the decremented
SP, FC = data for PC-relative operand reads (`readM`, `setFC<M>`),
divide-by-zero stacks the DIV's own address. Plus everything above 68010
(rows 3-39), the odd-SP frame fix (row 15) which also touches 010 frames, and
`MoiraConfig.h` edited in place. The line-level diff (≈1 700 lines in each of
`Moira.h` and `MoiraExec_cpp.h`) was not audited beyond these named patches.
