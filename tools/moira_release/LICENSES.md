# Licenses in this archive

Three licenses meet here. Which one governs a file depends on its directory,
and what a program that links these libraries must honour depends on which
libraries it links.

| Path | License | Holder |
|---|---|---|
| `extern/moira/Moira/` | MIT — `extern/moira/Moira/LICENSE`, unchanged from upstream | Dirk W. Hoffmann (Moira); the fork's changes, marked `POM68K` and `NEOST`, are by VERHILLE Arnaud and kept under the same MIT terms |
| `extern/softfloat/` | SoftFloat-2a, BSD (QEMU parts) and GPL-2.0-or-later, per file header (`extern/softfloat/VENDOR.md`) | John R. Hauser; QEMU; Andreas Grabher (Previous); WinUAE |
| `src/jit/`, `src/*.h`, `tests/`, `tools/`, `CMakeLists.txt` | GPL-3.0 — `LICENSE-GPL-3.0` | VERHILLE Arnaud (POM68K) |
| documents (`*.md`) | as their directory | — |

## What a program linking these targets accepts

- **`moira` and `moira_interp` link `softfloat68k`.** The fork's FPU
  (68881/68882 and the 68040's) does its arithmetic in SoftFloat, so the core
  as shipped here cannot be linked without it. The Moira sources stay MIT;
  a binary that contains them together with `softfloat68k` carries the
  GPL-2.0-or-later obligations of the SoftFloat files. A host that wants an
  MIT-only binary must replace `extern/softfloat` with an arithmetic library
  under a compatible license; the interface it must provide is
  `extern/softfloat/softfloat.h` as used by `MoiraExecFPU_cpp.h`.
- **`moira_jit` is GPL-3.0.** A program that links it is a GPL-3.0 work.
- The gates and tools are GPL-3.0 and are not linked into any library.

The `tests/cpu_vectors/` corpus and the external corpora named in
`tests/sst_corpora.lock` are outputs of the WinUAE CPU core (via Hatari
`e77819f7`) and of SingleStepTests/680x0; they contain no Apple data.
