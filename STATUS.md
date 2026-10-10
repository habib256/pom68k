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

## Union across hosts — 399 gates

| `ctest -L` | selects |
|---|---|
| `etalon` | 243 |
| `etalon-core` | 12 |
| `gui` | 3 |
| `jit` | 45 |
| `jit-fast` | 8 |
| `m030` | 87 |
| `m040` | 92 |
| `smoke` | 9 |
| `unit` | 153 |

`-L` is a regex over each label: `jit` also selects `jit-fast`, `etalon`
also selects `etalon-core`. The asset/host/scope/tier dimensions and the
scheduling slots are per-host manifest facts and live in the sections below.

## Registered on aarch64

394 gates registered; 5 union gates cannot register here: `jit_lockstep_030_x64_alignment_test`, `jit_lockstep_030_x64_experimental_test`, `jit_lockstep_030_x64_packed_ccr_test`, `jit_lockstep_x64_fine_test`, `jit_lockstep_x64_test`.

| dimension | value | gates |
|---|---|---|
| assets | none | 124 |
| assets | optional | 16 |
| assets | required | 254 |
| host | a64 | 4 |
| host | any | 384 |
| host | native | 6 |
| scope | component | 127 |
| scope | engine | 21 |
| scope | profile | 243 |
| scope | repository | 3 |
| tier | daily | 124 |
| tier | full | 258 |
| tier | platform | 12 |
| slots_src | assumed | 274 |
| slots_src | measured | 120 |

Scheduling cost if every gate ran at once: 629 slots of 256 MiB (`slots_src` says which rows are measured — an `assumed` gate is scheduled as one slot because nobody has measured it here).

## Registered on x86_64

395 gates registered; 4 union gates cannot register here: `jit_lockstep_030_a64_alignment_test`, `jit_lockstep_030_a64_experimental_test`, `jit_lockstep_a64_coarse_test`, `jit_store_guard_a64_test`.

| dimension | value | gates |
|---|---|---|
| assets | none | 123 |
| assets | optional | 16 |
| assets | required | 256 |
| host | any | 384 |
| host | native | 6 |
| host | x64 | 5 |
| scope | component | 127 |
| scope | engine | 22 |
| scope | profile | 243 |
| scope | repository | 3 |
| tier | daily | 123 |
| tier | full | 260 |
| tier | platform | 12 |
| slots_src | assumed | 282 |
| slots_src | measured | 113 |

Scheduling cost if every gate ran at once: 754 slots of 256 MiB (`slots_src` says which rows are measured — an `assumed` gate is scheduled as one slot because nobody has measured it here).

## PRODUCT_LLE on aarch64

Configuration union across hosts: 287 gates.

| `ctest -L` | selects |
|---|---|
| `a64` | 21 |
| `a64-oracle` | 18 |
| `etalon` | 154 |
| `etalon-core` | 12 |
| `gui` | 1 |
| `jit` | 44 |
| `jit-fast` | 8 |
| `lle` | 18 |
| `m030` | 68 |
| `m040` | 70 |
| `product` | 18 |
| `smoke` | 9 |
| `unit` | 132 |

282 gates registered; 5 union gates cannot register here: `jit_lockstep_030_x64_alignment_test`, `jit_lockstep_030_x64_experimental_test`, `jit_lockstep_030_x64_packed_ccr_test`, `jit_lockstep_x64_fine_test`, `jit_lockstep_x64_test`.

| dimension | value | gates |
|---|---|---|
| assets | none | 102 |
| assets | optional | 15 |
| assets | required | 165 |
| host | a64 | 12 |
| host | any | 264 |
| host | native | 6 |
| scope | component | 104 |
| scope | engine | 21 |
| scope | profile | 154 |
| scope | repository | 3 |
| tier | daily | 102 |
| tier | full | 168 |
| tier | platform | 12 |
| config | all | 274 |
| config | product-lle | 8 |
| slots_src | assumed | 166 |
| slots_src | measured | 116 |

Scheduling cost if every gate ran at once: 509 slots of 256 MiB (`slots_src` says which rows are measured — an `assumed` gate is scheduled as one slot because nobody has measured it here).

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
| Sep 11 00:51 +04 | aarch64 | default | 1 | 1 | 0 | 0 | AppleShare over EtherTalk: the guest mounts the volume on the card and its new folder lands on the host |
| Sep 11 07:09 +04 | aarch64 | default | 2 | 2 | 0 | 0 | EtherTalk vs LocalTalk transfer rate: the same two-fork duplicate, 2.20 s of guest time on the card against 165-241 s on the SCC |
| Sep 11 13:56 +04 | x86_64 | default | 3 | 3 | 0 | 0 | x86-64 leg of the DaynaPort handoff: ethertalk_test and daynaport_test asset-free, docs_test against the x86_64 section regenerated from a real configure (263 registered, no diff to the hand refresh) |
| Sep 11 13:57 +04 | x86_64 | default | 2 | 2 | 0 | 0 | x86-64 leg of the DaynaPort handoff: Dayna's own driver end to end (install, EtherTalk on the card, MacTCP, AppleShare mounted and written over the card) in 528.52 s, its transfer 41984 bytes in 2.20 s of guest time as on AArch64; AFP live over LocalTalk 240.68 s identical, the second copy 171.67 s against AArch64's 165.17 |
| Sep 11 21:00 +04 | x86_64 | default | 92 | 91 | 0 | 1 | LocalTalk FCS-residue fix (Scc8530 lossless readiness) and the traced AFP gate: full rebuild, asset-none tier |
| Sep 11 21:12 +04 | x86_64 | default | 92 | 92 | 0 | 0 | asset-none rerun after regenerating CHANGELOG_INDEX.md: the previous row's one red was docs_test counting 489 of 490 dated entries, the new CHANGELOG entry not yet indexed |
| Sep 11 21:22 +04 | x86_64 | default | 6 | 6 | 0 | 0 | LocalTalk FCS-residue fix: the six network gates, outage variants closing every alert the guest has in front; every completed copy retransmit-free (4.15-4.65 s of guest time), EtherTalk 2.20 s unchanged |
| Sep 12 00:34 +04 | x86_64 | default | 263 | 261 | 1 | 1 | Phase 0 safety net on main 598428f after the 2026-09-11 SCC fix: full registry, ctest -j64, 2607 s wall; one red, lcii_floppy_etalon (no Cmd-N folder in the host file), deterministic alone and under the interpreter, so not contention and not the engine; lcii_floppy144_etalon soft-skips for its missing 1.44 MB image |
| Sep 12 02:21 +04 | x86_64 | default | 277 | 275 | 1 | 1 | full registry after the DaynaPort port to every SCSI platform; ctest -j64, a memcheck run co-resident on one core |
| Sep 12 10:14 +04 | x86_64 | default | 5 | 5 | 0 | 0 | the five asset-required save-state etalons after the v15 DaynaPort chunk: real ROMs, all executed, no skip — the format bump changes no existing result |
| Sep 12 12:38 +04 | x86_64 | default | 277 | 275 | 1 | 1 | full registry after serialising the three AFP timing gates (RUN_SERIAL): the data-outage variant is green in situ again, 0 client retransmissions on every copy; wall 3394 s against 2770 s, the cost of no longer overlapping them |
| Sep 12 16:08 +04 | x86_64 | default | 277 | 275 | 1 | 1 | full registry with the AFP server date pinned (AfpServer::setFixedDate) and RUN_SERIAL reverted: the three AFP gates pass while heavily contended, 0 retransmissions; wall 2752 s against 3394 s serialised and 2770 s before |
| Sep 13 01:18 +04 | x86_64 | default | 94 | 94 | 0 | 0 | asset-none after correcting four comments that denied the v15 DaynaPort chunk: 94 executed, 0 soft-skipped, 0 failed, 10.86 s, ctest -j16; comment-and-doc-only change, no gate changes result |
| Sep 13 11:33 +04 | x86_64 | default | 94 | 94 | 0 | 0 | asset-none after the 400K PWM spindle fix (Mac 128K/512K reach the Finder): 94 executed, 0 soft-skipped, 0 failed, 10.76 s, ctest -j16 |
| Sep 13 11:34 +04 | x86_64 | default | 28 | 26 | 1 | 1 | compact-family blast radius on real ROMs after the PWM fix: 26 executed, 1 soft-skipped (lcii_floppy144_etalon, no 1.44 MB image), 1 failed (lcii_floppy_etalon, red since 2026-09-12 with the same signature and structurally unreachable from this change: V8Memory has neither pwmPush nor hasPwmSpindle); 312 s, ctest -j8 |
| Sep 13 12:47 +04 | x86_64 | default | 94 | 94 | 0 | 0 | asset-none after registering mac128k_boot_etalon and mac512k_boot_etalon: registry regenerated to 279 here / 283 union, both new gates execute in 7.40 s apiece |
| Sep 13 15:30 +04 | x86_64 | default | 94 | 94 | 0 | 0 | asset-none after the DaynaPort GUI status line and cable toggle (S3+S4), rebuilt: EtherLink.cpp is in pom68k_core so every binary was stale |
| Sep 13 18:05 +04 | aarch64 | default | 95 | 95 | 0 | 0 | asset-none on the M4 (aarch64), DaynaPort staged card + NetworkWindow |
| Sep 13 18:29 +04 | aarch64 | default | 96 | 96 | 0 | 0 | asset-none on the M4 (aarch64), editable network configuration + atalk_hub_test |
| Sep 14 18:54 +04 | aarch64 | default | 18 | 16 | 2 | 0 | the sixteen DaynaPort gates on the M4 (aarch64): daynaport_test, both controller tests, q605_dayna_driver_etalon and the twelve dayna_boot etalons all EXECUTED (16/18; the two 64 K boot etalons soft-skip here for want of disks35/System 1.1.dsk) |
| Sep 14 19:27 +04 | aarch64 | default | 1 | 1 | 0 | 0 | q605_dayna_driver_etalon with its new cable phase on the M4: 231 s, EXECUTED (cable out: 8 guest ICMP requests, 0 frames back; in: 6 frames back) |
| Sep 14 21:48 +04 | aarch64 | default | 32 | 32 | 0 | 0 | the 32 <profile>_agent_boot_etalon variants on the M4 (aarch64): the agent installed by the host, launched by the Finder, mounting a live disk — every 68030/68040 board, the Mac II and SE/30 on System 7.0, the Duo |
| Sep 15 08:08 +04 | aarch64 | default | 4 | 4 | 0 | 0 | the four compact agent variants (Plus, SE, SE FDHD, Classic) on System 7.0 HD.dsk, M4: all EXECUTED, the 68000 Plus polls and mounts |
| Sep 15 08:14 +04 | aarch64 | default | 2 | 2 | 0 | 0 | iix/iicx base boot gates after the real Toby ROM install, M4 |
| Sep 16 11:19 +04 | aarch64 | default | 332 | 329 | 3 | 0 | first ALL-GREEN full registry run on the AArch64 proof host (Apple M4): 332/332 in 3945.89 s wall, ctest -j6, at 41912d1, after assets.lock strict 44/44. Census CORRECTED the same day: sst68000/sst68030/sst68040 had no corpus here and printed "soft skip" without the literal SKIP, so the tool counted them executed — 329 executed / 3 soft-skipped / 0 failed (CHANGELOG 2026-09-16 (eighth)); the corpus was then fetched from the TEST drive and the three gates executed, see the next row |
| Sep 16 14:50 +04 | aarch64 | default | 3 | 3 | 0 | 0 | the three SST corpus gates on the M4 after the corpus arrived from the TEST drive (pom68k-prive-20260906/depot-ignore/tests/data): sst68000 1 000 058/1 000 058 vectors across 124 files, sst68030 and sst68040 green; completes the 2026-09-16 11:19 run's census to every registered gate executed on AArch64 |
| Sep 16 21:31 +04 | aarch64 | default | 14 | 14 | 0 | 0 | the day's attributions on the M4: finder_boot_matrix macii × 7.5.5 six runs PASS (real and synthetic Toby); q605_afp_live_etalon seven runs PASS under POM68K_AFP_DATE (pinned, moving ×3, host, host±3600); q605_dayna_driver_etalon red at 400 s under the -j8 network sweep (node joined EtherTalk, 0 AppleShare sessions), green alone in 234 s |
| Oct 08 17:49 +04 | aarch64 | default | 15 | 15 | 0 | 0 | DART stored/RLE/LZH, physical tags, states and Plus System 3.3 internal/external DART boot |
| Oct 08 17:52 +04 | aarch64 | default | 15 | 15 | 0 | 0 | Final DART validation: RLE length 20960 is words, all 15 checks executed, Plus internal/external Finder |
| Oct 08 18:20 +04 | aarch64 | default | 26 | 26 | 0 | 0 | 512Ke/II FDHD hardware: 26 selected gates; original ROM/media Finder and input; GLUE sibling boots, physical MFM index and MMIO write, deterministic snapshots |
| Oct 08 18:23 +04 | aarch64 | default | 5 | 5 | 0 | 0 | Pinned-screen verification after new 512Ke/II FDHD keys: three original-ROM boots plus documentation and file-size budgets; 5/5 executed |
| Oct 08 18:52 +04 | aarch64 | default | 37 | 37 | 0 | 0 | Native MOOF: 37/37 gates executed; bit/flux codec, complete 160-track state, native export boot, Plus both bays, FDHD HD MOOF/ADB, existing floppy/profile and all snapshot families; zero soft skips |
| Oct 08 19:00 +04 | aarch64 | default | 13 | 13 | 0 | 0 | 13/13 after malformed-turn/spindle arithmetic bounds: native codec/full-media state, all snapshot families, five actual MOOF consumers, docs and budgets; zero skips |
| Oct 08 19:04 +04 | aarch64 | default | 4 | 4 | 0 | 0 | 4/4 final exporter provenance/META regression, production re-export real Plus boot, documentation and file-size budgets; zero skips |
| Oct 08 19:14 +04 | aarch64 | default | 20 | 20 | 0 | 0 | Weak read-amplifier output: 20 executed gates, zero skips; native GCR/MFM boots, IWM/SWIM and five state families. |
| Oct 08 19:16 +04 | aarch64 | default | 4 | 4 | 0 | 0 | Final weak-channel cases: stopped motor, exact GCR/HD MFM spacing, fresh IWM replay/export isolation; 4 executed, zero skips. |
| Oct 08 19:42 +04 | aarch64 | default | 2 | 0 | 0 | 2 | Oids gate discovery: two engines reject the warp animation at the playable-scene assertion; frame budget corrected after screenshot review. |
| Oct 08 19:49 +04 | aarch64 | default | 2 | 0 | 0 | 2 | Oids counterfactual discovery: both engines correctly reject Up as thrust; actual capture uses Option. Fresh state replay already exact. |
| Oct 08 19:50 +04 | aarch64 | default | 2 | 2 | 0 | 0 | Original Oids bit/flux capture: interpreter and JIT execute Finder launch, galaxy loading, Option thrust versus equal-time neutral, and exact fresh-machine replay; zero skips. |
| Oct 08 19:54 +04 | aarch64 | default | 6 | 5 | 0 | 1 | Engine audit: interpreter passed; threaded JIT leg rejected by an overly narrow compiled-block assertion. Its actual fast path is code-window execution; gate now checks that path too. |
| Oct 08 19:56 +04 | aarch64 | default | 6 | 6 | 0 | 0 | Final Oids application/physical-media/docs/budget gates: 6 executed, zero skips; real interpreter/threaded-window execution, flight counterfactual, exact fresh-state replay. |
| Oct 08 21:09 +04 | aarch64 | default | 22 | 22 | 0 | 0 | IWM cell serializer v26: 22 regression gates, actual Plus/II FDHD MOOF boot and original Oids interp/threaded flight+fresh restore; zero skips |
| Oct 08 21:10 +04 | aarch64 | default | 4 | 4 | 0 | 0 | IWM v26 final head/unchanged-select/reset exact arcs; native partial-byte replay, docs and size budgets: 4 passed, zero skips |
| Oct 08 21:15 +04 | aarch64 | default | 7 | 5 | 0 | 2 | Address-mask initial regression: protocol passes; MacIP socket sandbox failure and Dayna guest navigation failure before gateway attachment |
| Oct 08 21:17 +04 | aarch64 | default | 1 | 0 | 0 | 1 | Dayna driver diagnostic repeat: fails opening Network after AppleTalk restart, before Ethernet gateway attachment |
| Oct 08 21:17 +04 | aarch64 | default | 8 | 8 | 0 | 0 | RFC 950 Ethernet wire service: 8 unit/bus/network/docs/budget gates passed outside socket sandbox; zero skips |
| Oct 08 21:20 +04 | aarch64 | default | 1 | 0 | 0 | 1 | Dayna navigation diagnostic: preserved failure before gateway attachment; screenshot identifies missed disk selection |
| Oct 08 21:26 +04 | aarch64 | default | 9 | 9 | 0 | 0 | RFC 950 mask service final: 9 gates passed outside socket sandbox; actual Q605 Dayna install/EtherTalk/AFP/MacTCP regression after settled ADB volume gesture; zero skips |
| Oct 08 21:33 +04 | aarch64 | default | 2 | 2 | 0 | 0 | Final changelog anchor/date-index correction: documentation and file-size budgets passed; no ceiling raised |
| Oct 08 22:14 +04 | aarch64 | default | 114 | 114 | 0 | 0 | RARP wire service: full rebuild, 114 asset-none gates executed, zero skips; ROM profile manifest and Oids diagnostic declaration repaired. |
| Oct 08 22:14 +04 | aarch64 | default | 1 | 1 | 0 | 0 | Real Dayna installer/MacTCP Server without guest IP entry: RARP assignment, five successful pings, incoming echo, cable recovery and EtherTalk AFP; 1 executed, zero skips. |
| Oct 08 22:19 +04 | aarch64 | default | 1 | 1 | 0 | 0 | Final real Dayna manual-configuration regression: executed in 280.43 s, no skip; Server mode measured separately at 245.31 s and 535152 KiB peak RSS. |
| Oct 08 22:28 +04 | aarch64 | default | 114 | 114 | 0 | 0 | Final RARP regression after persistent MAC reservations and measured gate budget: 114 executed, zero skips; full rebuild, docs and file-size budgets passed. |
| Oct 08 22:59 +04 | aarch64 | default | 1 | 1 | 0 | 0 | RARP guest regression after NetProbe/control-panel navigation changes; executed, no skips |
| Oct 08 22:54 +04 | aarch64 | default | 114 | 114 | 0 | 0 | Snow RARP/NetProbe increment: all 114 asset-none gates executed outside socket sandbox |
| Oct 08 23:04 +04 | aarch64 | default | 1 | 1 | 0 | 0 | Real Dayna/MacTCP manual-address regression after NetProbe/control-panel navigation changes; executed, no skips |
| Oct 08 23:09 +04 | aarch64 | default | 3 | 3 | 0 | 0 | Real MacTCP DNS-to-TCP after RARP: network gate plus docs/size; 3 executed, no skips |
| Oct 08 23:25 +04 | aarch64 | default | 3 | 1 | 0 | 2 | PCAP real MacTCP guest passed: 741 frames, zero observation loss; docs citation drift and size checks failed in same run, corrections tracked separately |
| Oct 08 23:32 +04 | aarch64 | default | 115 | 115 | 0 | 0 | PCAP increment: all asset-none gates execute outside socket sandbox after citation/size fixes; no skips |
