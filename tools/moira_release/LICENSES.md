# Licenses in this archive

Everything the fork wrote is MIT. The only third-party code is the
80-bit SoftFloat behind the FPU, which keeps its authors' licenses.

| Path | License | Holder |
|---|---|---|
| `extern/moira/Moira/` | MIT — `extern/moira/Moira/LICENSE`, unchanged from upstream | Dirk W. Hoffmann (Moira); the fork's changes, marked `POM68K` and `NEOST`, are by VERHILLE Arnaud under the same MIT terms (`LICENSE-MIT`) |
| `src/jit/`, `src/*.h`, `tests/`, `tools/`, `CMakeLists.txt` | MIT — `LICENSE-MIT` | VERHILLE Arnaud |
| `extern/softfloat/` | SoftFloat-2a, BSD (QEMU parts) and GPL-2.0-or-later, per file header (`extern/softfloat/VENDOR.md`); GPL text in `LICENSE-GPL-3.0` | John R. Hauser; the QEMU project; Andreas Grabher (Previous); the WinUAE authors |
| documents (`*.md`) | as their directory | — |

## Third-party work cited, not included

The fork's code contains no code from another emulator. It cites other
emulators as evidence, with a grade and a revision (`[D] … @ revision`):
WinUAE through Hatari `e77819f7` (the differential oracle), Musashi, MAME,
SingleStepTests/680x0. A citation records how that emulator behaves where it
helped settle a ruling; it is not a copy of its code.

The `tests/cpu_vectors/` corpus and the external corpora named in
`tests/sst_corpora.lock` are outputs of the WinUAE CPU core (via Hatari
`e77819f7`) and of SingleStepTests/680x0; they contain no Apple data.

## What a program linking these targets accepts

- **`moira`, `moira_interp` and `moira_jit` are MIT code** that links
  `softfloat68k`: the FPU (68881/68882 and the 68040's) does its arithmetic
  in SoftFloat. A binary containing `softfloat68k` carries the
  GPL-2.0-or-later obligations of its SoftFloat files. A host that wants an
  MIT-only binary replaces `extern/softfloat` with an arithmetic library
  under a compatible license; the interface it must provide is
  `extern/softfloat/softfloat.h` as used by `MoiraExecFPU_cpp.h`.
- The gates and tools are MIT and are not linked into any library.
