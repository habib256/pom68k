# Moira — the POM68K fork: 68000–68040 core and JIT

This archive is a source release of the fork of **Moira**, Dirk W. Hoffmann's
68k core (<https://github.com/dirkwhoffmann/Moira>, MIT), maintained inside
**POM68K**, a 68k Macintosh emulator. It is cut so the core can be read,
built and tested **without the emulator around it**: no Macintosh, no ROM, no
disk image is needed for anything below.

It is not a pull request. The fork changed too much of upstream's design to
merge back as one piece (`extern/moira/POM68K_VENDOR.md` § *Status*). It is
offered as a reference that can be read and cherry-picked: every local change
is marked `POM68K` in the source, inventoried with its reason and the gate
that would catch its loss, and backed by a test that runs in this archive.

`RELEASE.txt` names the POM68K commit this archive was cut from and the
upstream commit the fork started from. `MANIFEST.sha256` hashes every file.
When the archive carries `patches/moira-vs-upstream.patch`, that file is the
complete diff of `extern/moira/Moira/` against upstream `Moira/` at that
starting commit.

## Contents

| Path | What it is | License |
|---|---|---|
| `extern/moira/Moira/` | the core: upstream Moira plus the fork's changes | MIT notice, see `LICENSES.md` |
| `extern/moira/POM68K_VENDOR.md` | the fork's design record: one inventory row per patch group, then the journal of rulings | — |
| `extern/moira/POM68K_SURFACE.md` | the host contract: what the core exposes and what a host owes it | — |
| `extern/moira/NEOST_VENDOR.md` | the NeoST patches the fork inherited (NeoST is the Atari ST emulator the copy came through) | — |
| `extern/softfloat/` | 80-bit SoftFloat (QEMU SoftFloat-2a, WinUAE/Previous 68k extensions) behind the FPU | GPLv2+ / SoftFloat-2a |
| `src/jit/` | the JIT: engine, IR, `threaded` backend, x86-64 and AArch64 generators; `POM68K_JIT.md` is its design | GPLv3 |
| `src/*.h` | six header-only adapters the JIT gates include (configuration snapshot, save-state archive) | GPLv3 |
| `tests/`, `tools/` | the gates and the corpus tools | GPLv3 |
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
`corpus` replays the large differential corpora, which live outside the
archive (`tests/sst_corpora.lock` names each one by hash and by recipe;
`tests/fetch_sst_68000.sh` fetches SingleStepTests). Point
`-DMOIRA_POM68K_SST_ROOT=<dir>` at them; an absent corpus soft-skips and
proves nothing.

## What the fork adds

Grouped from the inventory (`POM68K_VENDOR.md` § *Inventory of local
patches*, rows 1–86), which is the authority:

- **68000/68010.** Converged on SingleStepTests/680x0 (1 000 058 vectors,
  bus-cycle order and timing included): address-error machinery, DIV/CHK/ASR
  rules, group-0 frame order; three NeoST prefetch fixes and three host hooks
  (`illegalOpcodeHook`, `willProcessFault`, `setBusLock`); an external bus
  error entry point.
- **68020.** CALLM/RTM, external /BERR with software-completed format-$B
  frames.
- **68030.** The PMMU made real: PMOVE/PTEST/PFLUSH/PLOAD, table walks, an
  ATC, format-$A/$B bus-fault frames, the prefetch pipe across a translation
  switch, an i-cache timing overlay.
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

## Upstream since the starting commit

Upstream has moved on since the fork's starting commit. Its later changes are
read, and taken one at a time with their own gate, never merged wholesale:

| Upstream | Change | In this fork |
|---|---|---|
| `f90fc50` | DIVU/DIVS prefetch timing | yes, through NeoST (row 40) |
| `426e4a3` | DoubleFault rethrow | the defect was fixed independently (NeoST); the 68020 cycle budget and `didLogInstruction` are not taken |
| `f7844ac` | 68030 CACR mask $3313 | yes (row 86, `moira_rules_test`) |
| `a4c273b` | 68020 instruction cache from vAmiga | not taken yet: it changes 68020 cycle counts, which the fork's Macintosh II boot gates pin; it needs its own evaluation |
| `20e6015`, `eb9529c`, `4146111`, `e05882a`, `832052a`, `9ca9ca0` | memory footprint, `isize` in `Guards`, MSVC `constexpr`, `version()`/`build()`, cleanups | not taken; no behaviour change expected |

## Limits, stated plainly

- The interpreter is the accuracy reference; the JIT is held to it by
  lockstep. In this archive the JIT gates use flat RAM. The JIT has run real
  software only inside POM68K's Macintosh boards, and nowhere else yet.
- 68030/68040 cycle counts are the 68020 tables scaled by the host; they are
  not a calibrated 030/040 timing model.
- Open rulings with the oracle (D25–D29 and others) are listed by name in
  the `.known` files; each listed vector fails the gate if it starts to pass.
  Several need a capture from real silicon to be settled.
- The 68EC040 runs the full 68040 paging path; it has no corpus.
- `POM68K_SURFACE.md` marks every host-contract statement not proved by
  execution as "unverified".

## Licenses

See `LICENSES.md`.
