# STATUS — the gate registry, generated

<!-- GENERATED FILE — do not edit by hand.
     `tools/status_md.py` rewrites it from the files CMake writes at
     configure time (pom68k_gates.tsv, pom68k_gates_absent.tsv,
     pom68k_gate_manifest.tsv); `docs_test` fails when this file and the
     configured registry disagree. -->

The default registry has more than one size: the native-backend locksteps
register only on the host they exercise. Its union below is derivable
identically on every host (the absent roster is added back); each *Registered
on* section is owned by the host it names and regenerated there — the same
division of labour as `gate_resource_budgets.tsv`. `PRODUCT_LLE` is a distinct,
opt-in configuration: its section records both the shared gates it qualifies
and the gates it alone registers. Recorded runs name their configuration and
carry `tools/gate_execution_census.py`'s executed/soft-skipped pair: quote the
pair, never the green total alone — a soft-skipped gate exited 0 and proved
nothing about the behaviour it names.

## Union across hosts — 267 gates

| `ctest -L` | selects |
|---|---|
| `etalon` | 143 |
| `etalon-core` | 12 |
| `gui` | 1 |
| `jit` | 44 |
| `jit-fast` | 8 |
| `m030` | 62 |
| `m040` | 61 |
| `smoke` | 9 |
| `unit` | 123 |

`-L` is a regex over each label: `jit` also selects `jit-fast`, `etalon`
also selects `etalon-core`. The asset/host/scope/tier dimensions and the
scheduling slots are per-host manifest facts and live in the sections below.

## Registered on aarch64

262 gates registered; 5 union gates cannot register here: `jit_lockstep_030_x64_alignment_test`, `jit_lockstep_030_x64_experimental_test`, `jit_lockstep_030_x64_packed_ccr_test`, `jit_lockstep_x64_fine_test`, `jit_lockstep_x64_test`.

| dimension | value | gates |
|---|---|---|
| assets | none | 93 |
| assets | optional | 15 |
| assets | required | 154 |
| host | a64 | 4 |
| host | any | 252 |
| host | native | 6 |
| scope | component | 95 |
| scope | engine | 21 |
| scope | profile | 143 |
| scope | repository | 3 |
| tier | daily | 93 |
| tier | full | 157 |
| tier | platform | 12 |
| slots_src | assumed | 146 |
| slots_src | measured | 116 |

Scheduling cost if every gate ran at once: 489 slots of 256 MiB (`slots_src` says which rows are measured — an `assumed` gate is scheduled as one slot because nobody has measured it here).

## Registered on x86_64

261 gates registered; 4 union gates cannot register here: `jit_lockstep_030_a64_alignment_test`, `jit_lockstep_030_a64_experimental_test`, `jit_lockstep_a64_coarse_test`, `jit_store_guard_a64_test`.

| dimension | value | gates |
|---|---|---|
| assets | none | 91 |
| assets | optional | 15 |
| assets | required | 155 |
| host | any | 250 |
| host | native | 6 |
| host | x64 | 5 |
| scope | component | 94 |
| scope | engine | 22 |
| scope | profile | 142 |
| scope | repository | 3 |
| tier | daily | 91 |
| tier | full | 158 |
| tier | platform | 12 |
| slots_src | assumed | 148 |
| slots_src | measured | 113 |

Scheduling cost if every gate ran at once: 620 slots of 256 MiB (`slots_src` says which rows are measured — an `assumed` gate is scheduled as one slot because nobody has measured it here).

## PRODUCT_LLE on aarch64

Configuration union across hosts: 273 gates.

| `ctest -L` | selects |
|---|---|
| `a64` | 21 |
| `a64-oracle` | 18 |
| `etalon` | 142 |
| `etalon-core` | 12 |
| `gui` | 1 |
| `jit` | 44 |
| `jit-fast` | 8 |
| `lle` | 18 |
| `m030` | 62 |
| `m040` | 66 |
| `product` | 18 |
| `smoke` | 9 |
| `unit` | 130 |

268 gates registered; 5 union gates cannot register here: `jit_lockstep_030_x64_alignment_test`, `jit_lockstep_030_x64_experimental_test`, `jit_lockstep_030_x64_packed_ccr_test`, `jit_lockstep_x64_fine_test`, `jit_lockstep_x64_test`.

| dimension | value | gates |
|---|---|---|
| assets | none | 100 |
| assets | optional | 15 |
| assets | required | 153 |
| host | a64 | 12 |
| host | any | 250 |
| host | native | 6 |
| scope | component | 102 |
| scope | engine | 21 |
| scope | profile | 142 |
| scope | repository | 3 |
| tier | daily | 100 |
| tier | full | 156 |
| tier | platform | 12 |
| config | all | 260 |
| config | product-lle | 8 |
| slots_src | assumed | 152 |
| slots_src | measured | 116 |

Scheduling cost if every gate ran at once: 495 slots of 256 MiB (`slots_src` says which rows are measured — an `assumed` gate is scheduled as one slot because nobody has measured it here).

## Recorded runs

Appended by `tools/status_md.py --record-run [--log FILE] [--note TEXT]`.
Copy `LastTest.log` the moment a run ends: every ctest invocation
overwrites it, including a one-gate `ctest -R`.

| start | host | configuration | in log | executed | soft-skipped | failed | note |
|---|---|---|---|---|---|---|---|
| Aug 29 23:58 +04 | x86_64 | default | 235 | 232 | 1 | 2 | first FULL registry run on an x86-64 host carrying the assets; ctest -j64, 3134 s wall; the two reds land on dirty/drifted reference volumes (check_volume_state.py) |
| Aug 30 01:24 +04 | x86_64 | default | 235 | 231 | 1 | 3 | stability repeat, ctest -j64, 3271 s wall; same two fixture reds, plus iivx_persist_etalon Timeout at 1800 s after passing run 1 at 1795.94 s — a gate sitting ON its bound here |
| Sep 01 22:49 +04 | x86_64 | default | 236 | 235 | 1 | 0 | first ALL-GREEN full registry run on the x86-64 proof host: 236/236 in 3313 s, ctest -j64; census 235 executed / 1 expected soft-skip (jit_store_guard_a64); clean hdv/ref fixtures, iivx TIMEOUT 2700 |
| Sep 01 23:45 +04 | x86_64 | default | 236 | 235 | 1 | 0 | consecutive ALL-GREEN repeat on the same tree: 236/236 in 3316 s, ctest -j64, census identical (235/1/0) — milestone-1 exit criterion met for x86-64 |
| Sep 06 21:03 +04 | aarch64 | default | 56 | 56 | 0 | 0 | AArch64-native 68030 requalification: ctest -L m030 serial, 56/56 in 2970.62 s; census 56 executed / 0 soft-skipped / 0 failed after assets.lock strict 38/38. jit_store_guard_a64_test also passed separately, 1/1 in 0.89 s. |
| Sep 09 23:30 +04 | aarch64 | default | 92 | 92 | 0 | 0 | AppleShare persistence: full rebuild, asset-none outside sandbox |
| Sep 09 23:31 +04 | aarch64 | default | 1 | 1 | 0 | 0 | AppleShare: real Mac OS 8.1 two-fork transfers before and after reconnect |
| Sep 10 07:10 +04 | aarch64 | default | 92 | 92 | 0 | 0 | AppleShare outage session fixes: full rebuild, asset-none, 92 executed, zero skips |
| Sep 10 07:41 +04 | aarch64 | default | 1 | 1 | 0 | 0 | Data-fork outage: real Mac OS 8.1 reconnects on a fresh SLS and retries both forks, 196.44 s, executed |
| Sep 10 07:52 +04 | aarch64 | default | 92 | 92 | 0 | 0 | Full rebuild after AFP listener rotation and socket-scoped ATP cache: asset-none 92 executed, zero skips |
| Sep 10 07:52 +04 | aarch64 | default | 3 | 3 | 0 | 0 | Real Mac OS 8.1: clean reconnect plus data/resource outages, exact two-fork retries and Finder partial-copy cleanup; 3 executed, zero skips, 546.48 s |
| Sep 10 08:01 +04 | aarch64 | default | 92 | 92 | 0 | 0 | Final AppleShare outage regression: 92 asset-none gates executed, zero skips, 9.29 s |
| Sep 10 17:29 +04 | aarch64 | default | 92 | 92 | 0 | 0 | Ethernet independent of AppleTalk: full rebuild, 92 asset-none executed, zero skips |
| Sep 10 17:32 +04 | aarch64 | default | 1 | 1 | 0 | 0 | Ethernet separation: real Mac OS 8.1 AppleShare clean reconnect and exact two-fork copies unchanged; 141.04 s, executed |
| Sep 10 21:09 +04 | aarch64 | default | 1 | 1 | 0 | 0 | Real DaynaPORT SCSI/Link driver: Dayna's installer, EtherTalk over the card, MacTCP 192.168.151.2 and an answered injected echo, 156.92 s, executed |
| Sep 10 21:17 +04 | aarch64 | default | 92 | 92 | 0 | 0 | Asset-free tier after the real-driver gate landed: 92 executed, zero skips |
| Sep 10 22:25 +04 | aarch64 | default | 1 | 1 | 0 | 0 | Wire latency on the Ethernet segment: MacTCP Ping reports 5/5 success over the real DaynaPORT driver, 154.10 s, executed |
| Sep 10 23:58 +04 | aarch64 | default | 93 | 93 | 0 | 0 | EtherTalk bridge: 93 asset-none gates executed, zero skips |
| Sep 11 00:02 +04 | aarch64 | default | 1 | 1 | 0 | 0 | Real DaynaPORT driver end to end: install, EtherTalk on the card, the guest joins net 2 and its Chooser lists POM68K, MacTCP 5/5 pings |
