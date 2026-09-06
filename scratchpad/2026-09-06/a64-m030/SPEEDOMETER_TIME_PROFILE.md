# Speedometer 4.02 — corrected AArch64 time profile

Date: 2026-09-06 (+04)  
Tree: `4c29ef8` plus the Finder-scope repair in
`tests/lcii_speedometer_census.cpp`  
Host: Apple M4, 10 logical CPUs, 24 GiB, arm64 Darwin 25.6.0  
Build: `build-profile`, RelWithDebInfo, `POM68K_FAST_LINK=ON`,
`-Wl,-no_deduplicate`  
Binary SHA-256:
`011c3c68d68492b8d0072575fe42eb9e04b167ba79e03a70d79c92f29608c81a`

## Protocol

`tools/profile_census_macos.py` launched the complete corrected
`lcii_speedometer_census` route and sampled it at 1 ms with macOS `sample`.
The parser normalizes over on-CPU samples and refuses an ld64
`<deduplicated_symbol>` share above 0.5%; all three captures passed that
guard. These are three independent process launches against the locked,
read-only `hdv/ref/GISTPERSO-boot.vhd` identity.

Every run reached the Finder after 1,751 SCSI commands, completed the real
Speedometer Performance Rating / CPU test at frame 270, and ended unharmed
after 2,311 SCSI commands. Workload identity was byte-exact across the three
runs:

- result shape `0.810/0.150/0.764`;
- CPU fingerprint `3f71466c4a59cc92` and result screen
  `7ff1fce65e502e83`;
- final fingerprint `e171e5e403d30748` and final screen
  `2496cd0688566441`;
- screen delta 47.1%, SCSI delta +560, `halted=0`.

## Results

| on-CPU bucket | run 1 | run 2 | run 3 | median | span |
|---|---:|---:|---:|---:|---:|
| generated bodies | 39.19% | 39.57% | 38.96% | **39.19%** | 0.61 pp |
| MMU/cache | 16.14% | 15.86% | 16.18% | **16.14%** | 0.32 pp |
| engine runtime/windows | 15.45% | 15.33% | 16.23% | **15.45%** | 0.90 pp |
| LLE/peripherals | 15.11% | 14.76% | 14.38% | **14.76%** | 0.73 pp |
| memory map/thunks | 6.30% | 6.60% | 6.29% | **6.30%** | 0.31 pp |
| interpreter fallback | 5.54% | 5.62% | 5.64% | **5.62%** | 0.10 pp |
| host/harness | 1.86% | 1.83% | 1.86% | **1.86%** | 0.03 pp |
| compilation | 0.25% | 0.24% | 0.29% | **0.25%** | 0.05 pp |
| other | 0.16% | 0.19% | 0.17% | **0.17%** | 0.03 pp |

The captures contain 33,449 / 33,512 / 33,322 on-CPU samples. Their median
is 33,449 and their total-work spread is 0.57%. The embedded CPU-test wall
times are 0.273603 / 0.275013 / 0.277063 s; they prove deterministic
completion, but that short slice is under 1% of the complete sampled route.

The top non-generated leaves are stable as well: `M68hc05::run` is
7.71–8.10%, `mmuRead<word>` 5.05–5.27%, `Engine::executeUntil` 3.79–4.07%,
`mmuFetchWord` 3.43–3.75%, and `V8Memory::read16` 2.90–3.10%.

## Decision

This replaces the invalid 2026-09-02 whole-route profile that later proved
to be running Prince of Persia's Read Me. It is a reproducible temporal
profile of the corrected boot, launch, complete CPU test and exit route.

It does **not** justify an opcode promotion. Every whole-instruction fallback
combined is only 5.54–5.64% of the route; `C029`, `08D1`, and the variable
peripheral reads are proper subsets of that ceiling, and this statistical
profile does not identify their individual time. More importantly, a sample
count cannot supply the missing peripheral access-phase contract that made
the earlier `C029` experiment change 270 frames into 450. They remain in
Moira.

If an opcode-level Speedometer promotion is reconsidered, first isolate or
amplify the 0.27 s CPU phase and sample that phase independently. The current
time profile instead points at the same broad costs as the other application
profiles: MMU/cache, engine runtime and the LLE/peripheral pump; compilation
at 0.24–0.29% is not the next lever.

The decoded reports are the three `speedometer_a64_full_{1,2,3}.profile.txt`
files beside this note, and each capture's `.sample.out` file carries the
workload identities above. The raw 2.4 MB `.sample` captures follow the
2026-09-03 convention and stay on the profiling host, out of the repository.
