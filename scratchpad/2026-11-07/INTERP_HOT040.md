# 68040 interpreter hot path — measurement, 2026-11-07

Follow-up of `scratchpad/2026-11-06/INTERP_PROFILE.md`, which ordered the
work: two thirds of the 68040 interpreter was address translation, spread
over three out-of-line calls per access.

**Conditions.** `build-profile` (RelWithDebInfo, no LTO, `-march=native`),
`jit_bench` (Quadra 605), `POM68K_CPU_ENGINE=interp`,
`POM68K_BENCH_FRAMES=1500` = 625 000 500 machine cycles, pinned to one core
(`taskset -c 14`), interleaved ABBA. A serial gate tier was running on
another core. Baseline = the tree at `4de050d`.

| binary | wall, s (each run) | mean | vs baseline |
|---|---|---|---|
| baseline | 32.37 32.05 32.07 32.20 | 32.17 | — |
| step 1: TTR table, `mmu040TranslateHot`, bus doors | 25.42 25.17 25.60 25.62 | 25.45 | −20.9 % |
| step 1 (second series) | 26.17 25.34 | 25.76 | |
| step 2: + `mmu040FetchWord`, `mmu040MatchTTR` inlined | 22.08 21.85 | 21.97 | **−31.7 %** (×1.46) |

Fingerprint `a2036281a5c2ac55` on every run of every binary — the value the
2026-11-06 profile recorded. The interpreter crosses real time on this host
(×0.78 → ×1.14).

**JIT mode is unchanged**, as it should be (the fetch window and the native
code bypass these paths): 3 000 frames, baseline 8.86 9.25 9.25 9.38,
patched 9.25 9.14 9.12 9.18, fingerprint `b9e4eacc7fc3933c` on all eight.

**Where the time is now** (`q605_interp_hot.census.txt`, 22.0 s of samples):

| share | bucket | was |
|---|---|---|
| 30.6 % | MMU and cache helpers | 63.2 % |
| 29.5 % | instruction handlers and the run loop | 11.9 % |
| 14.4 % | engine and wrapper glue | 8.7 % |
| 12.3 % | the board's memory map | 9.5 % |
| 11.9 % | peripherals | 6.0 % |

`mmu040InstrStart` (18.1 %) and `readExt` (9.3 %) now carry the inlined
fetches; what is left in them is mostly the virtual `read16` into
`Q605Memory::read16` (8.1 %). The next step is therefore not in the
translation any more but in the bus call: the fetch window, which only the
JIT engine arms today (`TODO.md § Moteur › Interprète`).

**Why it is the same machine.** Nothing was cached that was not already
cached: the "micro-TLB" the profile asked for existed as the last-hit probe
of `mmu040AtcLookup`. The work was to stop paying three calls to reach it.
`mmu040TranslateHot` makes the same decisions in the same order and touches
the ATC's replacement state only when the access is known to succeed there;
every other outcome re-enters `mmu040Translate` from an untouched state.
The TTR table is rebuilt from the reference test itself.
