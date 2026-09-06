# Speedometer 4.02 family validation — 2026-09-06

## Why this pass exists

The first repaired census selected only Performance Rating / CPU. That is a
useful phase, but it is not Speedometer 4.02 as a workload. The application
also exposes Benchmark Mix, direct FPU benchmarks and Color QuickDraw. This
pass makes those families individually selectable so later profiles can keep
CPU, floating-point and drawing costs attributable.

## Asset evidence from USB `TEST`

The mounted backup contains both the historical StuffIt distribution and an
Infinite Mac ZIP copy:

| file | SHA-256 |
|---|---|
| `/Volumes/TEST/pom68K/AppleShare/Speedo402.sit` | `93220699e763d68dddb1e872e8d2e707ab4c5a3ae99f11fb79ed5f8a156fc06c` |
| `/Volumes/TEST/sauvegarde-20260906/refs/infinite-mac/Library/Utilities/Speedometer.zip` | `6c3efc0b182366fbfbd1c4d42f44b965ec37b6090febd3381378ae98c32d594e` |

The ZIP preserves the data and resource forks of `Speedometer 4.02`. Its
resource strings name the workload rather than leaving it to screen-coordinate
guesswork:

- Benchmark Mix: Whetstones, Dhrystones, Towers of Hanoi, Quicksort, Bubble
  Sort, Queens, Puzzle, Permutations, integer Matrix Multiply and Sieve;
- FPU Benchmarks: FPU Whetstone, FPU Matrix Multiply and FPU FFT, described as
  using the FPU directly;
- Color QuickDraw: monochrome, 2-, 4-, 8- and 16-bit tests.

The guest Hardware Information window identifies the configured coprocessor
as an MC68882. The census continues to boot the locked `GISTPERSO-boot.vhd`
identity `73bf9d2e5db2c2f538d43933fb38ed1880ab2f2111cee5c7fec7c7468f0f7a5b`.

## Harness protocol

`POM68K_SPEEDO_MODE=cpu|mix|fpu|graphics` selects one family. `cpu` retains
the historical Performance Rating path with Graphics, Disk and Math disabled.
`mix` and `fpu` run all rows selected by their dialogs. `graphics` explicitly
enables the four rows that Speedometer leaves unchecked by default, so it runs
all five depths rather than silently measuring monochrome alone.

Every family emits a census boundary immediately before Run and immediately
after the completion alert. CPU keeps its two-panel structural detector. The
other result windows have different geometry, so they share an FNV-1a hash of
the stable monochrome mask covering the completion alert's icon and opaque
text. This is intentionally stronger than black density: the first graphics
probe crossed the old region with a QuickDraw diagonal and falsely reported
completion at frame 60.

## Paired A64 / `threaded` results

These wall times validate completion; they are not a controlled speed claim.
The guest frame, fingerprint, screen and SCSI columns are the correctness
observables.

| family | engine | frames | host wall | result CPU fp | result screen | SCSI delta | final CPU fp | final screen |
|---|---|---:|---:|---|---|---:|---|---|
| CPU | A64 | 270 | 0.288898 s | `3f71466c4a59cc92` | `7ff1fce65e502e83` | +560 | `e171e5e403d30748` | `2496cd0688566441` |
| CPU | threaded | 270 | 0.906212 s | `3f71466c4a59cc92` | `7ff1fce65e502e83` | +560 | `e171e5e403d30748` | `2496cd0688566441` |
| FPU | A64 | 450 | 0.855796 s | `7e5b156c7e7efdf1` | `e203493e22f88cf3` | +556 | `294d8982ca4c20f1` | `3cfeb28831043d23` |
| FPU | threaded | 450 | 1.060654 s | `7e5b156c7e7efdf1` | `e203493e22f88cf3` | +556 | `294d8982ca4c20f1` | `3cfeb28831043d23` |
| Mix | A64 | 1590 | 1.817091 s | `71ee4dacad498a9b` | `227b973e7c332711` | +556 | `0053d3570e0b471b` | `5508b6e9afbd6d91` |
| Mix | threaded | 1590 | 5.055262 s | `71ee4dacad498a9b` | `227b973e7c332711` | +556 | `0053d3570e0b471b` | `5508b6e9afbd6d91` |
| Graphics, 5 depths | A64 | 2520 | 3.696823 s | `6988c7da30132770` | `c5595ea4d99a8381` | +635 | `db177ec6cea609b5` | `f873ba738b751df3` |
| Graphics, 5 depths | threaded | 2520 | 8.289958 s | `6988c7da30132770` | `c5595ea4d99a8381` | +635 | `db177ec6cea609b5` | `f873ba738b751df3` |

The visible five-depth QuickDraw result is non-zero on every row: 6.133,
6.683, 6.979, 7.906 and 10.022 seconds, average ratio 1.242. The direct-FPU
result likewise contains all three rows (Whetstone 3374.593, Matrix Multiply
1.850 and FFT 0.971; average ratio 0.442). Thus neither successful detector is
merely observing an opened or empty dialog.

## Phase census and one rejected QuickDraw optimization

The isolated FPU phase retires 2,894,615 instructions. Only 84.7% are native:
438,964 instructions (15.2%) are `UNSAFE`, led by the F-line opcodes `F22E`,
`F200`, `F232` and `F230`. Its ordinary unsupported + runtime-access fallback
table is only 15,885 entries. That makes direct FPU execution the useful
temporal question; optimizing a small memory-fallback row from this phase
would address the wrong denominator.

The all-depth QuickDraw phase is almost the inverse: 28,896,782 instructions,
99.7% native, but 479,254 unsupported + 2,189,087 runtime fallbacks. Five
register-count shifts (`E1A9`, `E1AA`, `E1AC`, `E1AD`, `E2AD`) account for
1,879,617 runtime guard replays. An experiment added them to the existing
bounded 0..31 shift-version cache and extended both the asset-free 68040 and
68030 matrices from 4 × 32 to 9 × 32 cases. Both oracles passed, every target
row fell to zero, and total fallbacks fell 2,668,341 → 787,156 (**−70.5%**),
with all guest observables exact.

That candidate was nevertheless removed. A same-session binary ABBA gave
OFF 3.629683 / 3.674648 s (median 3.652166 s) against ON 3.714827 / 3.717069 s
(median 3.715948 s): **ON was 1.75% slower**. The extra version selection,
single-instruction block boundaries and cache residency cost more than the
replay count they removed. The retained policy therefore still names only
the original four measured multi-version opcodes. The negative evidence is
`speedometer_graphics_qd_versions_abba.log`; the before/experimental census
pair is `speedometer_graphics_all_a64_instrumented.log` and
`speedometer_graphics_all_a64_qd_versions_instrumented.log`.

## Decision

The expanded suite is correctness evidence and a workload selector. It does
not promote an opcode. The existing whole-route temporal samples are still
boot-dominated; the next measurement step is to repeat a selected family in
the guest or attach the sampler at its phase boundary. `Run All Tests` remains
outside this harness because combining the families would destroy the
attribution this work just established.

Raw successful logs are the `speedometer_*_a64.log`,
`speedometer_*_threaded.log`, `speedometer_graphics_all_*.log` and
`speedometer_cpu_*_after_suite.log` files beside this report. The retained
`*_probe.log` files are failed detector-development probes, not passing
evidence.
