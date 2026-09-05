# The caller-separated profile, re-taken on the arm that ships

*2026-09-05. Closes the TODO § B.2 item "refaire le profil par appelant sur
`threaded`". Both arms captured from the SAME tree (`ca8b453`) and the same
binary, so the two columns differ only by `POM68K_JIT_BACKEND`.*

## Why it had to be re-taken

The 2026-09-04 caller profile was captured under `POM68K_JIT_BACKEND=x64`.
`X64Backend::caps()` declares `autoFamilies = kGuest68040` only, so a 68030
guest on x86-64 resolves automatically to `threaded`: that profile described a
diagnostic override, not the product. The slice-0 agent said so and predicted
the shape of the difference from the code — the generator's share of every
bucket should fall to zero, and the absolute cost should rise, because
`JitBackendThreaded.cpp` runs *every* instruction through `pomJitExecOne()` →
`mmuExecuteStart`. It was inference, explicitly labelled as such. It is now
measured, and it holds in both directions at once.

## Provenance

`build-profile/jit_bench_lcii` — RelWithDebInfo, **LTO off**
(`profile_census.py`'s header: identical-code folding under LTO reassigns
samples to arbitrary sibling symbols and the report lies),
`-fno-omit-frame-pointer`, `POM68K_NATIVE=ON`. gperftools through the
`LD_PRELOAD` shim, `CPUPROFILE_FREQUENCY=500`, `POM68K_BENCH_FRAMES=2000`,
`POM68K_JIT_BLOCKS=1 POM68K_JIT_HOT=1`. Guest fingerprint
`3de5c5ab62b4eca8` on both arms.

Host 1-minute load 2.12 → 1.87 across the captures: **these are ratios, not
times.** No wall-clock number below, and none derivable.

Sample counts: 9024 (`threaded`), 3402 (x64). The `threaded` arm has ~2.7×
the samples for the same guest work, which is itself the headline — it is
~2.7× slower on this workload, and that is the arm an LC II selects here.

## The two arms

| bucket | x64, % of run | `threaded`, % of run | generator's share, x64 | generator's share, `threaded` |
|---|---:|---:|---:|---:|
| `mmuFetchWord` | 1.50 | **3.32** | 19.6 % | **0 %** |
| `mmuRead<N>` | 5.11 | **8.19** | 76.4 % | **0 %** |
| `mmuWrite<N>` | 1.73 | **3.92** | 52.5 % | **0 %** |
| `V8Memory::read16` | 1.41 | **2.09** | 41.7 % | **0 %** |
| `V8Memory::write16` | 0.21 | **1.71** | 71.4 % | **0 %** |
| `V8Memory::read8` | 0.97 | 0.35 | 69.7 % | **0 %** |
| **sum of the first five** | **9.96** | **19.23** | — | — |

On the shipping arm the memory-translation family is **19.2 % of the run
against 10.0 %** on the override, and the interpreter owns **all** of it. That
is the whole answer to TODO § B.2's second bullet, and it is why slices 4 and
5 paid there (−10 % and −5.5 %) while measuring nothing on x64.

By caller across the whole run:

| caller | x64 | `threaded` |
|---|---:|---:|
| interpreter — instruction dispatch | 10.38 % | **76.67 %** |
| generator — exact access thunk | 19.17 % | 0 % |
| generated — native block body | 19.75 % | 0 % |
| generator — whole-instruction replay | 8.64 % | 0 % |
| devices / peripheral pacing / harness | 26.25 % | 2.24 % |
| unattributed (stack truncated at depth 8) | 13.93 % | 20.77 % |

## Two honesty notes

**The `threaded` "unattributed" 20.77 % is not a hole in the attribution.**
Its top leaves are `jit::Engine::executeUntil` (10.72 %),
`ThreadedBackend::run` (5.76 %) and `Engine::armWindow` (1.91 %) — engine-loop
frames that *are* the dispatch rather than memory buckets whose caller went
missing. Every named memory bucket above attributes to 100 %.

**The x64 column has moved since 2026-09-04, and in the expected direction.**
`mmuFetchWord` was 2.50 % of that run and is 1.50 % of this one; `mmuWrite<N>`
was 2.32 % and is 1.73 %. Slices 4 and 5 landed in between. A profile bucket
visibly shrinking after the change that was supposed to shrink it is a
corroboration, not a discrepancy — but it does mean the two dates' numbers are
not interchangeable, and only this file's two columns should be compared with
each other.

## What it changes about the plan's remaining ideas

Nothing that is still open depends on the old column, but the ranking logic
does: any future B.2-shaped idea must be priced on `threaded` until
`X64Backend::caps().autoFamilies` gains `kGuest68030` (TODO § B.3). Pricing on
the x64 arm underestimates every interpreter-path change by roughly a factor
of two on these buckets, and reports zero for the generator's share of work
the generator does not do on this host.
