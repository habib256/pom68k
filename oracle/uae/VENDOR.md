# oracle/uae — vendored WinUAE 68030+MMU core (O1, primary oracle)

Provenance: extracted from **Hatari** (`https://github.com/hatari/hatari`),
commit **`e77819f73a30010aebb86bb47d5c8e54c3a68f8b`** (2026-07-06), directory
`src/cpu/` — Hatari's copy of the **WinUAE** CPU core (Toni Wilen, GPLv2+),
which it keeps in sync with upstream WinUAE. `cpummu030.c` is the 68030 MMU
reference implementation originally written by Andreas Grabher for
**Previous** (it boots NeXTSTEP with the MMU on) and merged into WinUAE.

Why Hatari and not Previous or raw WinUAE: the only reachable Previous git
mirror (`probonopd/previous`) is a 2017 snapshot (pre- many mmu030 fixes;
official SourceForge SVN not reachable from this environment), and raw WinUAE
is Windows-entangled. Hatari is current, Unix-buildable, carries the same
`newcpu.c`/`cpummu030.c` lineage with the post-2017 MMU fixes, and its
`WINUAE_FOR_HATARI` conditioning already cuts the Amiga chipset out of the
core — the smallest shim surface available.

## Layout

```
upstream/    vendored src/cpu files (unmodified except the patches below)
generated/   output of build68k + gencpu (vendored generated code, see below)
includes/    endianswap.h, vs-fix.h  (from hatari src/includes, unmodified)
shim/        POM68K replacement headers + stubs for the Hatari glue
glue.c       oracle_api.h implementation (set_state / step / get_state)
smoke.c      CTest gate: integer step, TRAP via VBR, MMU translation,
             PTEST/MMUSR, MMU fault frame ($A) + vector 2
oracle_uae.map  linker version script: only the 5 oracle_* symbols exported
```

### upstream/ (from hatari `src/cpu`, same paths)

`newcpu.c/.h newcpu_common.c cpummu.c/.h cpummu030.c/.h mmu_common.h
cpu_prefetch.h readcpu.c/.h options_cpu.h events.c/.h custom.c/.h maccess.h
compat.h sysconfig.h sysdeps.h debug.h debugmem.h savestate.h memory.h
fpp.c/.h fpp_native.c fpp_softfloat.c disasm.c/.h writelog.c
softfloat/* machdep/* uae/*` plus the generator provenance:
`build68k.c gencpu.c table68k`.

### generated/ (vendored gencpu output)

`cpudefs.c cpustbl.c cputbl.h cpuemu_{0,11,13,20,21,22,23,24,31,32,33,34,35,40,50}.c`
produced by running upstream's own generators at the pinned commit:

```
cc -Isrc/cpu -o build68k build68k.c writelog.c
./build68k < table68k > cpudefs.c
cc -Isrc/cpu -I. -o gencpu cpudefs.c gencpu.c readcpu.c
./gencpu
```

The oracle executes only `cpuemu_32.c` (`op_smalltbl_32`, "Previous 68030
MMU": 68030 + mmu030, not cycle-exact, not prefetch-compatible — mode 5 of
`cputbls[][]` in `newcpu.c`), but `newcpu.c` references every table and many
accessors from the other units unconditionally, so the full Hatari set is
vendored and linked (`--gc-sections` trims the .so).

## Local patches (all marked `POM68K:` in the code)

1. `upstream/sysconfig.h` — `#define DEBUGGER` wrapped in
   `#ifndef POM68K_ORACLE`: the oracle build drops the WinUAE debugger
   (avoids vendoring the 9k-line `debug.c` and its dependency tree; the few
   remaining references — `get_*_debug()` used by `disasm.c` — are routed to
   the flat buffer in `shim/stubs.c`).
2. `upstream/newcpu.c`, `m68k_run_mmu030()` — outer loop condition
   `while(!halt)` → `while(!halt && !bQuitProgram)`: after an exception is
   processed in the CATCH block, control would otherwise fetch and execute
   the handler's first instruction before any spcflags check; the oracle
   must stop a step at the exception boundary.
3. `upstream/newcpu.c`, `m68k_run_mmu030()` CATCH block —
   `bQuitProgram = true;` after `Exception(prb)`: marks the step complete
   (nothing else signals it: `add_approximate_exception_cycles()` is a no-op
   for 68030, so no `M68000_AddCycles()` fires during exception processing).
4. `upstream/newcpu.c`, `m68k_run_mmu030()` tail — `cpu_halt(halt)` →
   `if (halt) cpu_halt(halt)`: with patch 2 the loop now exits normally with
   `halt == 0`; under `WINUAE_FOR_HATARI`, `cpu_halt()` ends in
   `Dialog_HaltDlg()`, which the shim uses as the *double-fault* detector
   (`oracle_step()` then returns a negative value), so it must not fire on a
   normal single-step exit.
5. `upstream/newcpu.c`, `m68k_run_mmu030()` — `struct flag_struct f` →
   `volatile`: f (the pre-instruction CCR capture restored by the CATCH
   block on non-last-write MMU faults) is written between setjmp and
   longjmp; without volatile its post-longjmp value is the *register*
   content at setjmp time (C11 7.13.2.1) — always 0 in this build, so
   every $B fault frame stacked CCR = 0 instead of the captured CCR.
   O4 slice 3 arbitration D9 (`../fuzz/disputes/NOTES.md`).
6. `glue.c oracle_set_state()` — zeroes the instruction-restart globals
   that leak into $A/$B bus-fault frames (mmu030_ad[], idx/idx_done,
   mmu030_state[], data buffer, disp/fmovem stores, stage-B address,
   regs.wb2/wb3*, mmu_fault_addr/ssw, prefetch020*/pipeline): upstream
   persists them across instructions, which made fault-frame internals
   depend on fuzz-run history; a single-step oracle must be
   deterministic from the initial state (D9).
7. `upstream/fpp.c` — added `pom_fpu_clear_internal()` right after
   `reset_fsave_data()` (O5): the static `fsave_data` block (FSAVE
   idle-frame internals) persists across instructions upstream and is only
   reset on exception paths; `glue.c oracle_set_state()` calls the hook so
   FSAVE output is deterministic from the initial state (same rationale
   as patch 6 / D9).

No other vendored file is modified.

## Shim strategy

The vendored `.c` files are compiled as-is with `-Ishim` first on the include
path. Every Hatari header they pull (`main.h m68000.h cycInt.h cycles.h
mfp.h dsp.h log.h stMemory.h blitter.h scc.h vdi.h cart.h dialog.h debugui.h
debugcpu.h video.h bios.h xbios.h tos.h options.h reset.h profile.h
symbols.h hatari-glue.h config.h gemdos.h natfeats.h traps.h`) is replaced
by a tiny header in `shim/` with inline no-ops or extern declarations;
`shim/stubs.c` provides the inert globals/functions (no MFP/DSP/blitter, no
interrupts: `intlev()` returns -1, savestate primitives never called) and
`shim/memglue.c` replaces Hatari's `memory.c`: `memory_get_* / memory_put_*`
(the single funnel for *all* physical accesses, incl. `x_phys_get_*` MMU
table walks via `mmu_common.h`) operate on the host buffer big-endian,
address AND (size-1).

## Single-step mechanism (glue.c)

`oracle_step()` drives the official entry `m68k_go(1)`:
- `SPCFLAG_BRK` is armed; after one instruction `do_specialties()` returns 1
  and the run function exits;
- the shim `M68000_AddCycles()` (called by the run loop right after every
  instruction, and only while one executes) calls `pom_request_break()`,
  which sets `bQuitProgram` (breaks the `m68k_go` loop) and re-arms
  `SPCFLAG_BRK` (reset paths clear spcflags);
- exceptions end the step through patches 2–3; a double fault returns < 0
  via the `Dialog_HaltDlg()` flag.

`oracle_init()` performs the one-time boot through `m68k_go()`
(`quit_program = UAE_RESET` runs the static `build_cpufunctbl()` /
`set_x_funcs()` / `m68k_reset2()`) with the memory shim frozen (reads 0,
writes dropped) so the host buffer is never touched; the single harmless
instruction executed is `ORI.B #0,D0` at PC 0.

`oracle_set_state()` restores the MMU with the upstream sequence
(`mmu030_reset(-1)`, registers, `mmu030_flush_atc_all()`,
`restore_mmu030_finish()`, `mmu030_decode_tc(tc, false)`), pre-syncs
`regs.s/m` before `MakeFromSR()` so the already-correct A7 is not swapped,
and leaves the pipeline empty (mode 5 prefetches via `x_prefetch(0)` at each
step).

## Known limitations

- **Cycle counts are functional estimates** (allowed by `oracle_api.h`):
  the mmu030 handlers return WinUAE's 68030-relative scale (≈4× the nominal
  cycle count, e.g. MOVEQ = 16). SST030 `length` is advisory.
- **FPU enabled since O5**: `fpu_model = 68882`, `fpu_mode = 1` — the
  SOFTFLOAT backend (`fpp_softfloat.c`, selected in `fpp.c fpu_reset()`
  when `fpu_mode > 0`) for host-independent determinism. `oracle_set_state`
  restores FP0-7/FPCR/FPSR/FPIAR via `fpp_to_exten_fmovem` /
  `fpp_set_fp{cr,sr,iar}` (fpcr masked 0xfff0, fpsr 0x0ffffff8 — fpp.c
  `get_features()`), forces the idle FPU state (`regs.fpu_state = 1`) and
  clears every FPU-internal latch (exp/unimp pendings, fsave_data via
  patch 7) so steps are deterministic.
- No interrupts (`intlev()` = -1), no tracing/debugger, savestates stubbed.
- `memory_clear()` is a no-op by design (the harness owns the buffer).
- CCR of a write-faulting instruction is already updated in the stacked SR
  of the $A/$B frame (matches 68030 pending-write semantics; the smoke test
  asserts only the upper SR byte).

## Build

```
cmake -S oracle/uae -B build-oracle-uae -DCMAKE_BUILD_TYPE=Release
cmake --build build-oracle-uae -j8      # → liboracle_uae.so + smoke
ctest --test-dir build-oracle-uae       # gate: oracle_uae_smoke
```

Licenses: WinUAE core GPLv2+ (headers kept intact); POM68K additions GPLv3.
