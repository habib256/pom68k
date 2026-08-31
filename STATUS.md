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

## Union across hosts — 239 gates

| `ctest -L` | selects |
|---|---|
| `etalon` | 124 |
| `etalon-core` | 12 |
| `gui` | 1 |
| `jit` | 42 |
| `jit-fast` | 7 |
| `m030` | 56 |
| `m040` | 54 |
| `smoke` | 9 |
| `unit` | 114 |

`-L` is a regex over each label: `jit` also selects `jit-fast`, `etalon`
also selects `etalon-core`. The asset/host/scope/tier dimensions and the
scheduling slots are per-host manifest facts and live in the sections below.

## Registered on aarch64

236 gates registered; 3 union gates cannot register here: `jit_lockstep_030_x64_alignment_test`, `jit_lockstep_030_x64_experimental_test`, `jit_lockstep_030_x64_packed_ccr_test`.

| dimension | value | gates |
|---|---|---|
| assets | none | 88 |
| assets | optional | 12 |
| assets | required | 136 |
| host | a64 | 4 |
| host | any | 224 |
| host | native | 6 |
| host | x64 | 2 |
| scope | component | 88 |
| scope | engine | 22 |
| scope | profile | 124 |
| scope | repository | 2 |
| tier | daily | 88 |
| tier | full | 136 |
| tier | platform | 12 |
| slots_src | assumed | 120 |
| slots_src | measured | 116 |

Scheduling cost if every gate ran at once: 463 slots of 256 MiB (`slots_src` says which rows are measured — an `assumed` gate is scheduled as one slot because nobody has measured it here).

## Registered on x86_64

236 gates registered; 3 union gates cannot register here: `jit_lockstep_030_a64_alignment_test`, `jit_lockstep_030_a64_experimental_test`, `jit_lockstep_a64_coarse_test`.

| dimension | value | gates |
|---|---|---|
| assets | none | 88 |
| assets | optional | 12 |
| assets | required | 136 |
| host | a64 | 1 |
| host | any | 224 |
| host | native | 6 |
| host | x64 | 5 |
| scope | component | 88 |
| scope | engine | 22 |
| scope | profile | 124 |
| scope | repository | 2 |
| tier | daily | 88 |
| tier | full | 136 |
| tier | platform | 12 |
| slots_src | assumed | 120 |
| slots_src | measured | 116 |

Scheduling cost if every gate ran at once: 463 slots of 256 MiB (`slots_src` says which rows are measured — an `assumed` gate is scheduled as one slot because nobody has measured it here).

## Recorded runs

Appended by `tools/status_md.py --record-run [--log FILE] [--note TEXT]`.
Copy `LastTest.log` the moment a run ends: every ctest invocation
overwrites it, including a one-gate `ctest -R`.

| start | host | in log | executed | soft-skipped | failed | note |
|---|---|---|---|---|---|---|
| Aug 29 23:58 +04 | x86_64 | 235 | 232 | 1 | 2 | first FULL registry run on an x86-64 host carrying the assets; ctest -j64, 3134 s wall; the two reds land on dirty/drifted reference volumes (check_volume_state.py) |
| Aug 30 01:24 +04 | x86_64 | 235 | 231 | 1 | 3 | stability repeat, ctest -j64, 3271 s wall; same two fixture reds, plus iivx_persist_etalon Timeout at 1800 s after passing run 1 at 1795.94 s — a gate sitting ON its bound here |
