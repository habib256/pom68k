# Slice 3's mechanism, named — `POM68K_JIT_ACCESS_THUNK` 1 vs 2 by CAUSE

Same binary, 6000 frames, `POM68K_JIT_HISTO=1 POM68K_JIT_VERBOSE=1`,
`fp=cfb184b6faddabec` in both arms. Counters only; no wall-clock claim.
Raw: `histo_6000_thunk1.log`, `histo_6000_thunk2.log`.

The coordinator's A/B measured `block fallback` 6 082 194 → 5 558 149
(−524 045) and `backend declined` 751 327 → 919 539 (+168 212) without being
able to say which refusal moved. The x64 cause census now says it.

## Runtime fallbacks by cause

| cause | mode 1 | mode 2 | delta |
|---|---:|---:|---:|
| fill/tag MMU | 38 484 | 34 014 | −4 470 |
| **non-plain/MMIO** | **2 503 605** | **1 858 373** | **−645 232** |
| codeMask | 3 333 114 | 3 251 345 | −81 769 |
| cross-page | 40 561 | 39 405 | −1 156 |
| other guard | 1 | 1 | 0 |
| **attributed** | **5 915 765** | **5 183 138** | **−732 627** |
| static "unsupported" | 1 845 905 | 1 996 290 | +150 385 |

Both arms print `attributed N / N runtime  exact`.

**The whole gain is one cause: `non-plain/MMIO`, −645 232 (−25.8 %).** And
the per-address table names it exactly — the opcodes that leave the non-plain
plane between the two arms are

| opcode | form | mode 1 | mode 2 |
|---|---|---:|---:|
| `177C` | `MOVE.B #imm,d16(An)` → `$50F10010/20/30` | 275 031 | gone |
| `1747` | `MOVE.B D7,d16(An)` → `$50F10010/20/30/40/70` | 185 902 | gone |
| `117C` | `MOVE.B #imm,d16(An)` → `$50F27A03/7C13` | 46 716 | gone |
| `1084` | `MOVE.B D4,(An)` → `$50F10010` | 28 858 | gone |
| `1740` | `MOVE.B D0,d16(An)` → `$50F10010` | 19 225 | gone |

Every one is a **byte STORE into a device register** — `$50F10000+` is the
53C80 SCSI register file, `$50F26/$50F27` the VIA space (`V8Memory.cpp:502`,
`:724`). That is precisely what `exactWrites = thunks >= 2` buys: a store that
the DTLB refuses stops replaying the whole instruction through
`mmuExecuteStart` and takes `pom68kJitWrite` — one access — instead.

What does NOT move is just as informative: `24D0 R $50F06060` is
1 642 326 in mode 1 and 1 642 333 in mode 2. It is a `MOVE.L (A0),(A2)+`
whose source is the **SCSI pseudo-DMA window** (`V8Memory.cpp:502-503`,
`scsiDma_()`), i.e. a two-access instruction, which has no exact-thunk form on
either backend. This is the same invariance the 2026-08-29 (seventh) entry
recorded as "1 644 266 vs 1 644 272 — the thunk path is not in the story";
the census now supplies the reason instead of the observation.

## The cost side: what the +168 212 `backend declined` are

`compile:` line, same runs:

| | mode 1 | mode 2 |
|---|---|---|
| attempts | 23 852 | 23 782 |
| rejected | 58 | 77 |
| …`context/IR` | 16 | 14 |
| …**`coverage`** | **42** | **63** |

The +19 rejected blocks are **+21 `CompileReject::Coverage`**
(`JitBackendX64.cpp:5517`), less 2 context/IR. With `exactWrites` on,
`memoryProofPlan` marks more stores `exactRequired`; an instruction this
emitter cannot serve inline stops counting toward the block's native
coverage, the block falls under the acceptance threshold, and the **whole
block** is declined and runs through the window instead
(`JitEngine.cpp:1477`, `Miss::Rejected`).

So mode 2's ledger on this workload is: −645 232 whole-instruction replays
of device stores, bought with +168 212 instructions in 19 blocks that stop
being compiled at all. Both halves are counters, not seconds — the wall-clock
question stays with the orchestrator's ABBA.

`guard replay` is 47 231 → 47 039: bounded, not the 2026-08-29 storm.
