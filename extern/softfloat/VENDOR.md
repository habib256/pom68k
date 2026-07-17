# extern/softfloat — POM68K vendored copy (O5, 68882 FPU)

## What this is

The 80-bit extended-precision softfloat implementation used by Moira's
MC68882 FPU execution core (`extern/moira/Moira/MoiraExecFPU_cpp.h`):

| File | Content |
|---|---|
| `softfloat.c/.h`, `softfloat-macros.h`, `softfloat-specialize.h` | John R. Hauser's SoftFloat-2a (via QEMU), with the WinUAE/Previous `SOFTFLOAT_68K` extensions: `floatx80_move/abs/neg/getexp/getman/scale/rem/mod/sglmul/sgldiv/cmp/tst`, int8/int16 conversions, denormal/unnormal handling, and the internal overflow/underflow operand capture (`getFloatInternal*`) that feeds 6888x exceptional-operand frames |
| `softfloat_fpsp.c`, `softfloat_fpsp_tables.h` | the FPSP transcendental set (`floatx80_sin/cos/tan/atan/etox/logn/…/sincos`), a softfloat port of Motorola's 68040FPSP done for the Previous emulator |
| `softfloat_decimal.c` | packed-decimal (P format) ↔ floatx80 conversions (`floatdecimal_to_floatx80`, `floatx80_to_floatdecimal`) |

## Provenance

Copied 2026-07-15 from **`oracle/uae/upstream/softfloat/`** (this repo's
WinUAE-core oracle vendor tree, itself vendored from Hatari commit
`e77819f7`'s WinUAE CPU core; the softfloat code entered WinUAE from the
**Previous** (NeXT) emulator, by Andreas Grabher, derived from QEMU's
SoftFloat-2a). The `oracle/` copy is test-time only and must never be
included at build time — this `extern/` copy is the one Moira links.

**Rationale**: this is the *same softfloat family the primary oracle
(WinUAE) executes*. Project policy says the oracle wins disputes, so using
its exact arithmetic gives numerical convergence with the differential
corpus *by construction* (identical rounding, NaN propagation, denormal
and transcendental behaviour).

## License

SoftFloat-2a + BSD (QEMU parts) + **GPL-v2-or-later** (see the headers in
each file). GPLv2+ is compatible with POM68K's GPLv3. `extern/moira/`
itself stays MIT and only **links** against this library (separate static
lib target `softfloat68k`).

## Local changes (all tagged `POM68K:` in the sources)

1. `softfloat.c` — include path flattened: `"softfloat/softfloat.h"` →
   `"softfloat.h"` (no subdirectory in this vendor copy).
2. `softfloat_decimal.c` — `"sysconfig.h"`/`"sysdeps.h"` includes dropped
   (only used for `write_log`/`_T` when `DECIMAL_LOG=1`, which is off) and
   `"softfloat/softfloat-specialize.h"` flattened.
3. `CMakeLists.txt` + this file added.

## Trim list

Everything from the upstream `softfloat/` directory is kept — all seven
files are needed: the FPU uses floatx80 ops + status/rounding plumbing
(`softfloat.c`), the transcendentals (`softfloat_fpsp.c` + tables), and
packed decimal (`softfloat_decimal.c`). What was *not* copied is the rest
of the WinUAE FPU layer (`fpp.c`, `fpp_softfloat.c`, `fpp_native.c`):
their 6888x semantics are re-implemented inside Moira
(`MoiraExecFPU_cpp.h`) with `fpp.c` line citations, and the native-FPU
backend is not used at all.
