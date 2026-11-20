# Moira — the POM68K fork: 68000–68040 core and JIT

This archive is a source release of the fork of **Moira**, Dirk W. Hoffmann's
68k core (<https://github.com/dirkwhoffmann/Moira>, MIT), maintained inside
**POM68K**, a 68k Macintosh emulator. It is cut so the core can be read,
built and tested **without the emulator around it**: no Macintosh, no ROM, no
disk image is needed for anything below.

The fork is based on upstream's master (`RELEASE.txt` names the commit):
every upstream commit up to it is in the fork, and
`patches/moira-vs-upstream.patch` is the whole difference as one `git am`
commit on that master. With the fork dropped into upstream's tree, upstream's
own `testRunner` passes unmodified (§ *In upstream's tree*).

It is still not a pull request. The difference is about 15 000 lines, most
of it execution upstream does not have (68030 PMMU, 6888x and 68040 FPU, the
68040) and seams for POM68K's JIT and save states. It is offered to be read
and cherry-picked: every local change is marked `POM68K` in the source,
inventoried with its reason and the gate that would catch its loss, and
backed by a test that runs in this archive.

**Start with `UPSTREAM_GUIDE.md`** if the question is what stock Moira could
take: it lists the changes to the 68000–68020 cores one by one, each with
its evidence, the function it sits in and the gate that holds it.

`RELEASE.txt` names the POM68K commit this archive was cut from and the
upstream commit the fork is based on. `MANIFEST.sha256` hashes every file.

## Contents

| Path | What it is | License |
|---|---|---|
| `extern/moira/Moira/` | the core: upstream Moira plus the fork's changes | MIT notice, see `LICENSES.md` |
| `UPSTREAM_GUIDE.md` | every patch group sorted by what it means for stock Moira: defects in code upstream already has (with their status at upstream's master), new execution, host seams | — |
| `extern/moira/POM68K_VENDOR.md` | the fork's design record: one inventory row per patch group, then the journal of rulings | — |
| `extern/moira/POM68K_SURFACE.md` | the host contract: what the core exposes and what a host owes it | — |
| `extern/moira/NEOST_VENDOR.md` | the NeoST patches the fork inherited (NeoST is the Atari ST emulator the copy came through) | — |
| `extern/softfloat/` | 80-bit SoftFloat (QEMU SoftFloat-2a, WinUAE/Previous 68k extensions) behind the FPU | GPLv2+ / SoftFloat-2a |
| `src/jit/` | the JIT: engine, IR, `threaded` backend, x86-64 and AArch64 generators; `POM68K_JIT.md` is its design | MIT |
| `src/*.h` | six header-only adapters the JIT gates include (configuration snapshot, save-state archive) | MIT |
| `tests/`, `tools/` | the gates and the corpus tools | MIT |
| `tests/jit_program_fuzz.cpp`, `cmake/Pom68kJitFuzzGates.cmake` | the program fuzzer, interpreter against translator, and its gate registry (seeds that once failed are pinned there with what they found) | MIT |
| `tests/cpu_vectors/` | a small differential corpus generated against WinUAE, owned by the repository and pinned by hash | — |
| `oracle/README.md` | the rulings: each place the fork follows or overrules the WinUAE oracle, and why | — |
| `docs/CACHE_040.md` | the 68040 cache model | — |

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build -L 'unit|jit' --output-on-failure
```

CMake 3.16 and a C++20 compiler (GCC, Clang, MSVC). `auto` adds the native
generator of the host: AArch64 everywhere, x86-64 outside Windows (it follows
the System V ABI); `-DMOIRA_POM68K_JIT_BACKENDS=threaded` builds without one,
and the three gates that prove generated host code are then not registered.
Targets:

- `moira` — the core with the acceleration seam the JIT drives;
- `moira_interp` — the same sources with `MOIRA_ENABLE_JIT=0`: no fetch or
  data windows, no probes. This is the library an interpreter-only host
  embeds; `moira_interp_test` proves the two builds agree on state, cycles,
  bus traffic and memory;
- `moira_jit` — the JIT on top of `moira`.

Labels: `unit` and `jit` need nothing outside the archive and must run; a
gate in them that prints `SKIP` is a failure of the release, not a pass.
On POSIX hosts the `jit` label includes the program fuzzer
(`jit_program_fuzz_*`, about forty gates over fixed seed ranges, every
model from the 68000 to the 68040): random multi-instruction programs run
by the interpreter and by the translator, compared register, clock and bus
after every slice, through blocks, self-modifying stores, timed interrupts,
MMU faults, traces and save states. A failing seed replays alone with
`jit_program_fuzz <same options> --first S --seeds 1 -v`.
`corpus` replays the large differential corpora, which live outside the
archive (`tests/sst_corpora.lock` names each one by hash and by recipe;
`tests/fetch_sst_68000.sh` fetches SingleStepTests). Point
`-DMOIRA_POM68K_SST_ROOT=<dir>` at them; an absent corpus soft-skips and
proves nothing.
The 68030/68040 runners compare unlisted RAM writes too (`--strict-ram`)
and use the same scoped or named oracle rules as the parent repository.

## What the fork adds

Grouped from the inventory (`POM68K_VENDOR.md` § *Inventory of local
patches*, one numbered row per patch group), which is the authority:

- **68000/68010.** Converged on SingleStepTests/680x0 (1 000 058 vectors,
  bus-cycle order and timing included): address-error machinery, DIV/CHK/ASR
  rules, group-0 frame order; three NeoST prefetch fixes and three host hooks
  (`illegalOpcodeHook`, `willProcessFault`, `setBusLock`); an external bus
  error entry point.
- **68020.** CALLM/RTM, external /BERR with software-completed format-$B
  frames; the trace exception with T0 (trace on change of flow) and the
  format-$2 trace frame; a transition-sensitive level 7; the interrupt
  acknowledge sequence billed.
- **68030.** The PMMU made real: PMOVE/PTEST/PFLUSH/PLOAD, table walks, an
  ATC, format-$A/$B bus-fault frames, the prefetch pipe across a translation
  switch, an i-cache timing overlay. RTE of a format-$B frame continues the
  faulted instruction from the frame's access log, with no trace or
  interrupt in between; `fault_transparency_test` faults every bus cycle of
  every covered instruction, returns, and requires the state an unfaulted
  run leaves.
- **6888x.** FPU execution backed by SoftFloat, with a per-instance
  arithmetic context, timing tables, FSAVE/FRESTORE, mid-instruction
  interrupts (format $9, BUSY).
- **68040.** Integer core, MMU with four-way set-associative I/D ATCs,
  format-$7 faults, the integrated FPU with FPSP traps and state frames, and
  an optional data-bearing I/D cache model (copyback, snooping, CPUSH/CINV).
- **Seams.** The JIT seam (fetch window, data TLB, probes; compiled out by
  `MOIRA_ENABLE_JIT=0`), an instruction-boundary state visitor for save
  states, a debugger access-stop delegate. The core reads no environment
  variable.

Disputed behaviour is decided by evidence grade, recorded at the head of the
comment (`[B]` manual, `[C]` ROM, `[D]` another emulator with its revision,
`[F]` unknown with what would settle it). A manual outranks the oracle; the
known exceptions are listed in `oracle/README.md` § *Rulings taken against a
manual*.

## Relation to upstream

The fork started from upstream `1efd694` (2025-09-11) and was rebased on
2026-11-20 onto upstream's master of that day, `ce28239`. Each upstream
commit in between was taken in turn, with the fork's tiers run at each
stage (`extern/moira/POM68K_VENDOR.md` § *Rebase onto upstream ce28239*):

| Upstream | Change | In this fork |
|---|---|---|
| `4146111`, `20e6015`, `9ca9ca0`, `832052a`, `e05882a` | MSVC `constexpr`, one translation unit per core, de-templated disassembler, narrower tables, `version()`/`build()` | as upstream; the fork's non-templated definitions sit behind `MOIRA_MAIN_TU` (row 146) |
| `a4c273b`, `426e4a3` | 68020 instruction cache, latch, bus sizing, cycle budget, `didLogInstruction` | as upstream when `MOIRA_EMULATE_ICACHE` is true, on the 68020/EC020; **false by default here** (row 147, and *Limits* below) |
| `eb9529c` | interrupt taken with M set, `isize` in `Guards` | as upstream (row 145) |
| `f7844ac`, `f90fc50` | 68030 CACR mask, DIVU/DIVS prefetch timing | as upstream (rows 86, 40) |

Three things the fork keeps where upstream changed the same code: its own
halt on a double fault (upstream: `throw *df`), its own JSR (target-fetch
faults and T0 trace on the 68030/68040), and `read32`/`write32` with
defaults made of two 16-bit calls, so that a host implementing only the
8- and 16-bit accessors keeps compiling.

## In upstream's tree

```sh
git clone https://github.com/dirkwhoffmann/Moira.git && cd Moira
git checkout -b pom68k <the upstream commit of RELEASE.txt>   # or stay on master
git am /path/to/release/patches/moira-vs-upstream.patch        # Moira/ becomes the fork's
# (an archive cut without the patch: rm -rf Moira && cp -R /path/to/release/extern/moira/Moira Moira)
cp -R /path/to/release/extern/softfloat softfloat              # beside Moira/
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS="-DMOIRA_PRECISE_TIMING=false -DMOIRA_EMULATE_ADDRESS_ERROR=false -DMOIRA_MIMIC_MUSASHI=true -DMOIRA_EMULATE_ICACHE=true -DMOIRA_ENABLE_JIT=0"
cmake --build build --target testRunner --parallel
./build/testRunner
```

Nothing else in upstream's tree is edited. `Moira/CMakeLists.txt` builds
`softfloat/` (or the directory named by `-DMOIRA_SOFTFLOAT_DIR=`, or an
existing `softfloat68k` target) and links it; SoftFloat's exports carry the
prefix `pom68k_sf_`, so the library links next to Musashi's own copy.

The five definitions select upstream's configuration, because the fork's
`MoiraConfig.h` carries POM68K's, each under `#ifndef`:

| Definition | Fork default | Upstream's Runner |
|---|---|---|
| `MOIRA_PRECISE_TIMING` | `true` | `false` |
| `MOIRA_EMULATE_ADDRESS_ERROR` | `true` | `false` |
| `MOIRA_MIMIC_MUSASHI` | `false` | `true` |
| `MOIRA_EMULATE_ICACHE` | `false` | `true` |
| `MOIRA_ENABLE_JIT` (fork only) | `1` | `0` |

They must reach every translation unit that includes `Moira.h`, the
Runner's too, which `CMAKE_CXX_FLAGS` does; editing `Moira/MoiraConfig.h`
works as well. Checked on 2026-11-20 on Linux x86-64 with GCC 15: all
thirteen Runner passes PASSED, exit 0. `UPSTREAM_GUIDE.md` § *Upstream's
Runner on the fork* says what that does and does not show. The Xcode
project was not updated or built: it does not list SoftFloat's three C
files, the fork's added headers or the definitions.

## Limits, stated plainly

- The interpreter is the accuracy reference; the JIT is held to it by
  lockstep. In this archive the JIT gates use flat RAM. The JIT has run real
  software only inside POM68K's Macintosh boards, and nowhere else yet.
- 68030/68040 cycle counts are the 68020 tables scaled by the host; they are
  not a calibrated 030/040 timing model.
- Upstream's 68020 instruction cache is in the fork and switched off in
  POM68K's configuration. This core's 68020 tables are the manuals'
  cache-case column, so subtracting 2 per hit counts the cache twice, and
  the budget constants (`maxStalled` 4, `minAccum` -454) are not derived
  from a source. Turning it on for the Macintosh II and LC needs a timing
  model in which a hit is free and a miss costs the difference between the
  two columns. `MOIRA_EMULATE_ICACHE` also refuses `MOIRA_ENABLE_JIT`: the
  JIT's fetch window would bypass the cache. `icache020_test` holds the
  cache's behaviour as upstream wrote it.
- Open rulings with the oracle (D25–D29 and others) are listed by name in
  the `.known` files; each listed vector fails the gate if it starts to pass.
  Several need a capture from real silicon to be settled.
- The 68EC040 runs the full 68040 paging path; it has no corpus.
- `POM68K_SURFACE.md` marks every host-contract statement not proved by
  execution as "unverified".

## Licenses

See `LICENSES.md`.
