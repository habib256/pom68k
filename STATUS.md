# STATUS — the gate registry, generated

<!-- GENERATED FILE — do not edit by hand.
     `tools/status_md.py` rewrites it from the files CMake writes at
     configure time (pom68k_gates.tsv, pom68k_gates_absent.tsv,
     pom68k_gate_manifest.tsv); `docs_test` fails when this file and the
     configured registry disagree. -->

The registry has more than one size: the native-backend locksteps register
only on the host they exercise. The union below is derivable identically on
every host (the absent roster is added back); each *Registered on* section
is owned by the host it names and regenerated there — the same division of
labour as `gate_resource_budgets.tsv`. Recorded runs carry
`tools/gate_execution_census.py`'s executed/soft-skipped pair: quote the
pair, never the green total alone — a soft-skipped gate exited 0 and proved
nothing about the behaviour it names.

## Union across hosts — 240 gates

| `ctest -L` | selects |
|---|---|
| `etalon` | 124 |
| `etalon-core` | 12 |
| `gui` | 1 |
| `jit` | 43 |
| `jit-fast` | 8 |
| `m030` | 56 |
| `m040` | 54 |
| `smoke` | 9 |
| `unit` | 115 |

`-L` is a regex over each label: `jit` also selects `jit-fast`, `etalon`
also selects `etalon-core`. The asset/host/scope/tier dimensions and the
scheduling slots are per-host manifest facts and live in the sections below.

## Registered on aarch64

235 gates registered; 5 union gates cannot register here: `jit_lockstep_030_x64_alignment_test`, `jit_lockstep_030_x64_experimental_test`, `jit_lockstep_030_x64_packed_ccr_test`, `jit_lockstep_x64_fine_test`, `jit_lockstep_x64_test`.

| dimension | value | gates |
|---|---|---|
| assets | none | 86 |
| assets | optional | 15 |
| assets | required | 134 |
| host | a64 | 4 |
| host | any | 225 |
| host | native | 6 |
| scope | component | 88 |
| scope | engine | 20 |
| scope | profile | 124 |
| scope | repository | 3 |
| tier | daily | 86 |
| tier | full | 137 |
| tier | platform | 12 |
| slots_src | assumed | 119 |
| slots_src | measured | 116 |

Scheduling cost if every gate ran at once: 462 slots of 256 MiB (`slots_src` says which rows are measured — an `assumed` gate is scheduled as one slot because nobody has measured it here).

## Registered on x86_64

236 gates registered; 4 union gates cannot register here: `jit_lockstep_030_a64_alignment_test`, `jit_lockstep_030_a64_experimental_test`, `jit_lockstep_a64_coarse_test`, `jit_store_guard_a64_test`.

| dimension | value | gates |
|---|---|---|
| assets | none | 85 |
| assets | optional | 15 |
| assets | required | 136 |
| host | any | 225 |
| host | native | 6 |
| host | x64 | 5 |
| scope | component | 88 |
| scope | engine | 21 |
| scope | profile | 124 |
| scope | repository | 3 |
| tier | daily | 85 |
| tier | full | 139 |
| tier | platform | 12 |
| slots_src | assumed | 123 |
| slots_src | measured | 113 |

Scheduling cost if every gate ran at once: 595 slots of 256 MiB (`slots_src` says which rows are measured — an `assumed` gate is scheduled as one slot because nobody has measured it here).

## Recorded runs

Appended by `tools/status_md.py --record-run [--log FILE] [--note TEXT]`.
Copy `LastTest.log` the moment a run ends: every ctest invocation
overwrites it, including a one-gate `ctest -R`.

| start | host | in log | executed | soft-skipped | failed | note |
|---|---|---|---|---|---|---|
| Aug 29 23:58 +04 | x86_64 | 235 | 232 | 1 | 2 | first FULL registry run on an x86-64 host carrying the assets; ctest -j64, 3134 s wall; the two reds land on dirty/drifted reference volumes (check_volume_state.py) |
| Aug 30 01:24 +04 | x86_64 | 235 | 231 | 1 | 3 | stability repeat, ctest -j64, 3271 s wall; same two fixture reds, plus iivx_persist_etalon Timeout at 1800 s after passing run 1 at 1795.94 s — a gate sitting ON its bound here |
| Sep 01 22:49 +04 | x86_64 | 236 | 235 | 1 | 0 | first ALL-GREEN full registry run on the x86-64 proof host: 236/236 in 3313 s, ctest -j64; census 235 executed / 1 expected soft-skip (jit_store_guard_a64); clean hdv/ref fixtures, iivx TIMEOUT 2700 |
| Sep 01 23:45 +04 | x86_64 | 236 | 235 | 1 | 0 | consecutive ALL-GREEN repeat on the same tree: 236/236 in 3316 s, ctest -j64, census identical (235/1/0) — milestone-1 exit criterion met for x86-64 |
