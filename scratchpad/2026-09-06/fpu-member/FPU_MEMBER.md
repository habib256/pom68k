# The FPU general window becomes a block member — 2026-09-06/07

Tree: `7a6f730` plus this change. Host: Apple M4, arm64 Darwin 25.6.0.
Measurement binary: `build/` Release, `POM68K_LTO=OFF`, `POM68K_NATIVE=OFF`,
`POM68K_FAST_LINK=ON` (the same directory every 2026-09-06 Speedometer
figure came from).

## What changed

`classify()` returns `Kind::Fpu` for `$F200-$F23F` (coprocessor 1, type 000:
FMOVE/FMOVEM/FMOVECR and every arithmetic form). The block builder keeps the
instruction as a member; neither native generator emits it, so it reaches the
same cold exact fallback (`pom68kA64Step` / `pom68kJitStep`) every
unsupported opcode uses, and `FlagMayTrap` makes the continuation compare PC
before entry i+1. Every other F-line form stays `Unsafe`.
`POM68K_JIT_FPU_MEMBER=0` restores the boundary (attribution arm).

The tracer also keeps a discontinuity prefix: a trace whose i-th member
transferred control caches instructions 0..i-1 and ends before the offender
instead of discarding the whole trace. It never fires on the Speedometer
routes below (instrumented count 0); the asset-free 68LC040 regime is where
it matters.

## Conformance

| gate | result |
|---|---|
| `jit_backend_test` (a64 build and forced-x64 build) | OK, F-line sub-window pins |
| `jit_backend_parity_test` (both builds) | 0 divergence groups |
| `jit_asset_free_lockstep_test` | PASSED: straight / enabled FDIV-by-zero (vector 50 from inside the block) / detached-FPU 68LC040 (format $4), both knob arms |
| 13 asset-free JIT gates (`jit_copyback_*`, `jit_store_guard_a64`, `cache040`, `fpu_sanity`, `fpu040`, …) | 13/13 |
| `jit_lockstep_030_test`, `_no_data_window`, `_blocks`, `jit_lockstep_test`, `_blocks`, `_noaccess` | 6/6 |
| Speedometer FPU family, A64 member / A64 boundary / threaded / interpreter | 450 frames, fp `7e5b156c7e7efdf1` → `294d8982ca4c20f1`, screen `e203493e22f88cf3` → `3cfeb28831043d23`, SCSI +556 on all four |
| Speedometer CPU / Mix / Graphics, A64 member | 270 / 1590 / 2520 frames, fingerprints identical to the 2026-09-06 (seventh) table |

## Same-binary ABBA (process-level, counterbalanced A B B A)

`abba.py` runs the two arms alternately and refuses to summarise if any run's
frame count or fingerprints drift. Idle host, nothing else running.

| family | arm | n | median | min | max | delta |
|---|---|---:|---:|---:|---:|---:|
| FPU | boundary (`POM68K_JIT_FPU_MEMBER=0`) | 6 | 0.868176 s | 0.859720 | 0.872743 | |
| FPU | member (default) | 6 | 0.767134 s | 0.763082 | 0.769764 | **−11.64 %** |
| Mix | boundary | 4 | 1.856594 s | 1.856024 | 1.856989 | |
| Mix | member | 4 | 1.814250 s | 1.804207 | 1.823631 | **−2.28 %** |
| CPU | boundary | 6 | 0.300508 s | 0.289473 | 0.301640 | |
| CPU | member | 6 | 0.299346 s | 0.291355 | 0.301569 | −0.39 % (noise) |

The FPU spread is 1.5 % within arms against an 11.6 % delta; Mix is 1.1 %
within arms against 2.3 %. The CPU family has no F-line instructions and is
the null.

## Phase-attached profile (build-profile, 1 ms, 1 s from the first progress line)

| bucket (% on-CPU) | boundary (779 samples) | member (776 samples) |
|---|---:|---:|
| generated bodies | 24.13 % | 25.26 % |
| engine runtime/windows | **21.18 %** | **14.18 %** |
| interpreter (fallback, FPU handlers included) | 19.51 % | 22.29 % |
| MMU/cache | 13.74 % | 15.08 % |
| LLE/peripherals | 13.48 % | 15.72 % |

Self symbols: `jit::Engine::executeUntil` 8.86 % → 2.84 %,
`jit::Engine::dispatchBlockKey` 2.31 % → below the top 45. The seven points
the engine loop loses are the per-FPU-instruction dispatch this change removes;
the FPU handlers themselves (`execFGen`, `fpuRunBusy` 4–5 %, `fpuArithmetic`)
cost the same on both arms. `fpu_phase_{member,boundary}.profile.txt`.

## The cross-binary comparison that was NOT valid

A "before" binary built from `7a6f730` in a fresh worktree measured the FPU
phase at 0.779 s — faster than the new binary's boundary arm (0.868 s) although
both execute the identical engine work (instrumented dispatch/miss/block
counters byte-identical). Graphics, which has no F-line traffic, was +4.79 %
slower on the new binary too, and the whole route 37.9 s against 39.5–40.0 s.
The cause was the build, not the change: a fresh configure defaults to
`POM68K_LTO=ON` and `POM68K_NATIVE=ON` (`-flto=thin -mcpu=native`), while
`build/` has both OFF. Only same-binary arms are compared above; the
rebuilt-in-place null check (original vs patched, both LTO+native, Graphics)
is in `abba_graphics_orig_patched_sametree.log`.

Side observation, not a claim: on this host the LTO+native build is about
4–5 % faster on the Graphics phase and the whole route than the OFF/OFF
measurement binary. TODO § C.4 "Activer LTO dans les artefacts" owns that.

## Files

`abba_fpu_knob.log`, `abba_mix_knob.log`, `abba_cpu_knob.log` (the decision
evidence); `abba_fpu_binary.log`, `abba_graphics_before_member0.log`,
`route_wall.log`, `env_presence_probe.log` (the invalid cross-binary series
and the probes that exposed it); `instr_before.log` /
`instr_after_member0.log` (identical engine counters);
`speedometer_*_{a64_member,a64_boundary,threaded,interp}.log` (identity);
`locksteps.log`, `asset_free_jit_gates.log`; `fpu_phase_*.profile.txt`
(phase-attached 1 ms samples, `phase_sample.py`).
