# TODO § B.2 — Attacking Moira translation and the memory thunks on the 68030

*Read-only plan produced 2026-09-04 by an Opus planning agent. Evidence base: `CHANGELOG.md` 2026-09-02 (sixth), 2026-08-29 (seventh)/(late night)/(later), `docs/JIT_BRINGUP.md` § C.2-C.4quinquies, `extern/moira/POM68K_VENDOR.md` rows 16/22/30/31/32, `docs/MEASURING.md`.*

## 0. What the code actually says (the model the slices act on)

The profile's three named symbols have exactly one structure behind them.

**The 68030 interpreted instruction.** `Moira::pomJitExecOne()` (`extern/moira/Moira/Moira.cpp:327`) routes an 030 through `mmuExecuteStart<C68020>()` (`MoiraExecMMU_cpp.h:512`), which calls `mmuFetchWord(pc)` and `mmuFetchWord(pc+2)` **separately** (`:536-537`). Each call pays, in order: the post-PMOVE pipe test (`:412-417`), the i-cache overlay with its `clock += missPenalty` (`:424-439`), the in-flight access stamp (`:443`), then `pomJitFetch` (`Moira.h:944`). Operands then go through `mmuRead`/`mmuWrite` (`:994` / `:1101`), which have **no data-window fast path at all** — every sub-access calls `mmuTranslateAccess` (`:888`) and then the machine map.

**The 68040 does both of these things better, in the same file.** `mmu040InstrStart` fetches *both* words with one `pomJitFetch(reg.pc, 4, p)` and one stamp (`:1927-1938`), and `mmu040Read`/`mmu040Write` consult `pomJitData<N,W>` before the ATC chain (`:3018`, `:3104`). Neither is wired on the 030.

**Generated 030 code.** `Emitter::memProbe` (`JitBackendX64.cpp:1653`) inlines the DTLB lookup; a hit is a direct host load/store. A miss goes to the exact thunk (`memLoad`, `:1848`) — `pom68kJitRead` → `pomJitReadData` (`MoiraExecMMU_cpp.h:2325`) → the **full interpreter `mmuRead`**. For **stores**, `proofOptions` (`JitBackendX64.cpp:67`) sets `exactWrites = thunks >= 2`, and `X64Backend::caps()` clamps `maxAccessThunk030 = 1` (`:5149`), so `memStore` (`:1940`) sends every DTLB miss to `runtimeStub` — **the whole instruction replays interpreted**. On a64, `exactWrites` is on but scoped by `restartableWriteRequired = L.is030` (`JitBackendA64.cpp:39-54`), which by `JitIr.h:806-813` covers only `MOVE Dn/#imm → <mem>` — so `MOVE.L (A0),(A2)+`, the QuickDraw shape, replays fully on **both** backends.

**Where the DTLB refuses on an LC II.** `Engine::fillDtlb` (`JitEngine.cpp:1112`) asks `V8Memory::dataSpan` (`V8Memory.cpp:597`), which returns RAM below `$A00000` and ROM for reads — **and nothing else**. The VRAM aperture `$F40000-$FC0000` is refused, cached as a tagged-null entry, and served forever through the runtime stub. `Q605Memory::dataSpan` (`Q605Memory.cpp:634-649`) does the opposite, with the reason written in the comment.

`ATC lookup cost is already solved`: `mmuAtcLookup` has the O(1) pseudo-LRU and the last-hit probe (`:756`, `:787-797`) — vendor patch 16. Do not re-open it.

---

## Slice 0 — Price the buckets before touching anything (enabling, not a gain)

**Target.** No production code. `tests/lcii_simcity_census.cpp`, `tests/lcii_speedometer_census.cpp`, `tests/jit_bench_lcii.cpp`, and the census printer `JitEngine.cpp:353-430`.

**Mechanism.** Run both application censuses at their fixed phase budgets and read three tables the tree already prints:
* `[jit] runtime fallback causes` — `fill/tag MMU` vs `non-plain/MMIO` vs `codeMask` vs `cross-page`, **in accesses**, not in remembered fills. This is the number 2026-08-29 (seventh) did not have: it reported `probe=65 772 / codepage=24 795 / notram=24 807` *fills*, which cannot apportion the 1.64 M replays of `$24D0`.
* `[jit] dtlb refusals: probe/pagelen/codepage/notram/cache040` (`JitEngine.cpp:227`).
* `native / block fallback / window+interp / tracing` shares plus `icache: N fetches` (`jit_bench_lcii.cpp:340-345`). `pomIcache.fetches` **is** the count of `mmuFetchWord` calls that reached the overlay, so `fetches / retired` prices slice 4 directly.

One instrument gap must be closed inside this slice, and it is six lines: the per-address table at `JitEngine.cpp:388-430` is built only for `RuntimeCodeMask`, `RuntimeCrossPage`, `RuntimeOther`. Add `RuntimeNonPlain` to that reason list and to the a64 recording site (`JitBackendA64.cpp:4907-4919`) so the non-plain **addresses** are named. Second gap, structural and worth writing down: only `JitBackendA64.cpp` implements the runtime-reason census — `JitBackendX64.cpp` never fills `slowRuntimeReasonHisto`. This is `docs/MEASURING.md` § 3's own trap ("an instrument wired on one backend reports success on the backend that is not looking"); the census legs therefore run on the **a64** host, and the x64 leg uses the coarser counters.

**Conformance contract.** None — nothing executes differently. The only requirement is `docs/MEASURING.md` § 4.1: re-run `POM68K_BENCH_NULL=1` on the host of the day before quoting anything. `performance_budgets.tsv:67-68` records **10 permille (x86_64) / 11 permille (aarch64)**; the 2026-09-02 night driver measured 1.1 % on the LC II leg and printed `HOST NOISIER THAN POLICY`. Settle the floor first or every number below is unquotable.

**Ceiling.** 0 % by itself. It is the go/no-go for slices 1, 4 and 5 and the re-pricing rule for 2 and 3.

**Risk.** That the census phases drift (the `(INVALID: nothing launched)` guard in `lcii_simcity_census.cpp:163` is the tripwire). Speedometer's honesty note from 2026-09-02 (sixth) stands: its cpu-test leg reported `done=0`.

---

## Slice 1 — `V8Memory::dataSpan` publishes the VRAM aperture (rank 1)

**Target.** `src/V8Memory.cpp:597` (`dataSpan`), with a stated guard added at `src/V8Memory.cpp:622` (`jitAliasCodeMask`) and the stale comment at `src/V8Memory.h:127-135` corrected.

**Mechanism.** Add one region to `dataSpan`, exactly mirroring `Q605Memory::dataSpan:645-649`:

```
if (phys >= 0xF40000 && phys < 0xFC0000) { len = 0xFC0000 - phys; return vram_.data() + (phys - 0xF40000); }
```

placed after the RAM and ROM branches and before the final `return nullptr`. `codeSpan` stays unchanged and keeps refusing VRAM — no block is ever translated from there, so `pageMap_`/`codePage_` never cover it and no `codeMask` is owed. Effect: on an LC II / Color Classic / Mac TV, every QuickDraw screen store and read becomes an inline DTLB hit in generated code instead of, respectively, a whole interpreted instruction (x64 mode 1, and both backends for the `mem→mem` MOVE family) or an exact thunk into `mmuWrite`/`mmuRead` + `V8Memory::write16`/`read16`.

Two mechanical details that must be in the same change:
* `jitAliasCodeMask` will now be called with a VRAM `physSlice`. It is correct today **by accident**: `ramIndex($F40000)` returns `0x140000` (a valid-looking RAM index, because `ramIndex`'s `addr >= 0x800000` branch is unbounded above), and only the two `bus < 0xA00000` bounds in the alias arms stop a false mask bit. Add an explicit `if (physSlice >= 0xA00000) return 0;` so the safety is stated, not inherited.
* The Classic II (Eagle) scans out of **main RAM** at `$1F9A80` (`V8Memory.h:eagleFrame`), which `dataSpan` already exposes. So `classic2`/`cclassic2` gates are unaffected by construction, and `lcii`/`cclassic`/`mactv` are the ones that move. Say so in the entry.

**Invalidation / conformance contract.** The VRAM window is decoded at `V8Memory.cpp:485` **before** `if (cpu_) cpu_->flushTicks();`, and its `read8`/`write8`/`read16`/`write16` paths contain no `stall()`, no `viaSync()`, no device state and no dirty tracking — the video pulls `vram_` at raster time (`V8Video.h:93-144`). A host-pointer store is therefore byte-identical to `vram_[i] = v` and moves **no** peripheral flush, **no** IPL poll and **no** bus-time boundary: the IR's poll placement and the instruction's access positions are untouched, only the callee of an access that was already at that position changes. The 68030 has no modelled data cache, so cache-inhibit on the descriptor is irrelevant. Invalidation is inherited whole: `pomJitMapMoved` on PMOVE/PFLUSH, `pomJitAtcEvict` per page and per space, `V8Memory::jitMapChanged()` on the overlay flip and RAM reconfiguration, privilege in DTLB tag bit 31. A 2-byte access at `$FBFFFF` cannot straddle out of the aperture: `memProbe` refuses `off > 4096 - n` and `$FC0000` is 4 KB-aligned.

**The instrument gap this slice creates, and how it is closed.** `jit_lockstep_030_test`'s `ramDiff` walks `peek8(0..bytes)` (`tests/jit_lockstep_030_test.cpp:122-127`) — with `POM68K_JIT_LOCKSTEP_FULL_RAM_AT` it reaches `$00A00000` and **never touches VRAM**. Shipping this slice without adding a VRAM leg means the framebuffer is the one thing the oracle cannot see. So the slice includes a `vramDiff` over `V8Memory::vram()` (`V8Memory.h:302`, already public) in the same comparison, and the A/B additionally requires `screen=%016llx` from `lcii_simcity_census.cpp:164-166` to be identical across interp / threaded / native at the same guest budget. Proven by: `jit_lockstep_030_test` + `_blocks_test` + the host-native `_experimental` / `_alignment` / `_packed_ccr` gates with fresh seeds, `ctest -L m030`, `lcii_boot_etalon`, `lcii_sys7_boot_etalon` and the `lcii_beyond_etalon` scenarios (these compare the screen against references and are the ones that would catch a wrong framebuffer).

**Ceiling.** `V8Memory::read16/write16` is 5.7 % on SimCity, and each refused screen **store** on x64 today also drags a full `mmuExecuteStart` (2 × `mmuFetchWord`) + `mmuWrite` + `mmuTranslateAccess` behind it. Honest upper bound over the whole boot→load run: **≈ 8 %**; over the app-load phase alone, plausibly more. Well above the 1 % floor. Confidence **high** — the Q605 comment is a measured precedent on the same mechanism ("made the code generator SLOWER than the interpreter for the whole Finder phase"), and this is the concrete instance of the open design question 2026-08-29 (seventh) left: *"a direct-write path for refused-but-plain destinations with the guard observed inline"* — here the guard is not owed at all.

**Risks.** (a) The framebuffer stops passing through `observeWrite` (`V8Memory.cpp:657`, `:807`); a64 emits the observer on direct DTLB stores (`JitBackendA64.cpp:5086`), x64 does not — so `POM68K_JIT_LOCKSTEP_WRITE_TRACE_AT` loses VRAM coverage on x64. Pre-existing for RAM, newly true for VRAM; state it. (b) A future V8 revision that puts a register inside `$F40000-$FC0000` would silently become plain memory — pin the bound with a comment citing `read8:485`. (c) The 2026-08-29 mode-2 storm teaches the specific failure mode to watch for: a *coarse* trip against an *exact* eviction produced an engine loop retiring nothing. This slice cannot reproduce it — VRAM carries no guard slice and no eviction — but the same run must still show `Miss::GuardReplay` unchanged, because a change there would mean the codeMask surface moved.

**A/B protocol.** `jit_bench_lcii`, `POM68K_BENCH_COMPARE=5`, ABBA, one warm-up pair discarded, at **both** budgets (`POM68K_BENCH_FRAMES=2000` and `6000`, R3), on a quiet host (the harness refuses over ¼ of the hardware threads), preceded by `POM68K_BENCH_NULL=1`. Arms: same binary, slice on/off behind a temporary compile switch or two builds under `tools/check_binaries_fresh.py`. Fingerprint identical within each budget **and** the `icache: fetches/hits/misses` triple identical. Then `lcii_simcity_census` for the phase attribution (`app-load` phase, `wall=`, `fp=`, `screen=`).

---

## Slice 2 — one address decode and one guard note per `V8Memory` word access (rank 2)

**Target.** `src/V8Memory.cpp:761` (`read16`) and `:804` (`write16`), against `ramIndex` at `src/V8Memory.h:408-420`.

**Mechanism.** Both word paths call `ramIndex(addr)` **and** `ramIndex(addr+1)` — two full piecewise bank decodes for a pair that is contiguous in every case except a bank seam. Compute `i0` once and derive `i1 = i0 + 1` when the pair provably stays inside the same piece (`(addr & 0x1FFFFF) != 0x1FFFFF` for the fixed alias arm; `addr + 1 < mbLoc_ + mbSize_` / `simmLoc_ + simmPhys_` for the other two), falling back to the second `ramIndex` otherwise. Same for the guard: `write16` calls `jitGuard_->note()` twice on the LC II (the bus view and the 2 MB alias view, `V8Memory.cpp:812-822`) — the alias address is a pure function of `addr` and `i0`, already computed; the second `note()` can be hoisted out of the branch chain and skipped outright when `pageMap` is null or when neither slice is marked.

**Invalidation / conformance contract.** There is none to write: the change returns **the same values from the same array indices** and calls the same device functions in the same order, entirely inside one `read16`/`write16` invocation. Nothing crosses an IPL poll or a `flushTicks()` boundary because the RAM branch reaches neither. Proof is by fingerprint: `ctest -L m030` plus `lcii_boot_etalon`, `cclassic_boot_etalon`, `cclassic2_boot_etalon`, `mactv` and `classic2` legs, and identical `fp=` on `jit_bench_lcii` at both budgets on all three engines. The `sst68030` fuzz gate is the second net.

**Ceiling.** `read16 + write16` is 5.7 %; the duplicated decode plus the duplicated `note()` is on the order of a third of those bodies → **≈ 2 %** measured *before* slice 1. **This competes with slice 1 for the same samples**: once VRAM traffic leaves this path the residual is ~1-1.5 %, i.e. brushing the floor. Either measure it first, or accept that its number after slice 1 will be small and quote it at the pre-slice-1 budget with that stated.

**Risks.** Low. The only real one is the bank-seam case: the LC II's `mbLoc_`/`simmLoc_`/`simmPhys_` geometry is reconfigured by `applyRamConfig` at boot, so the derivation must read the current fields, never cache them. `jitMapChanged()` already exists for the map-move side and is untouched.

**A/B protocol.** As slice 1, with a third arm on a second V8 model (Classic II — its framebuffer is in RAM, so this slice is the *only* one that moves it) to show the gain is not LC II-specific.

---

## Slice 3 — retire the `maxAccessThunk030 = 1` clamp on x64 (rank 3)

**Target.** `src/jit/backends/JitBackendX64.cpp:5149`, resolved by `JitEngine.cpp:99-102`.

**Mechanism.** Set `c.maxAccessThunk030 = 2`. `proofOptions` then turns `exactWrites` on (`:74`), and `memStore`'s DTLB miss takes `pom68kJitWrite` — one access — instead of `runtimeStub`, which today replays the entire instruction through `mmuExecuteStart` + handler. Note deliberately **not** in scope: setting `restartableWriteRequired` on x64 as a64 does. The comment at `JitBackendX64.cpp:74-81` records that doing so globally *diverged the LC II lockstep at an 8192-cycle boundary* (peripheral-phase class, 2026-08-18) while regressing reach; x64 scopes it inside `emitMove`. Leave that alone.

**Invalidation / conformance contract.** Mode 2 is already mechanised and repaired: 2026-08-29 (late night) ran the storm to ground (coarse 32-byte trip vs byte-exact eviction vs a guard door that exits without retiring) and fixed it with one interpreted step counted as `Miss::GuardReplay`; the bench went from a >600 s wedge to 1.25 s at `threaded`'s exact fingerprint `8b4045727f565816`, and the 120k lockstep at explicit mode 2 with both § C.4nonies admissions was 120 000 steps identical. The peripheral-phase alignment is what makes a store thunk conformant at all: `pom68kJitWrite` brackets the access with `pomJitIcachePeekPenalty` / `pomJitBiasClock` (`JitBackendX64.cpp:139-148`, `Moira.h:757-782`), and `jit_lockstep_030_x64_alignment_test` is the gate that pins it. So the contract is: `ctest -L m030` green, all four x64 030 lockstep gates green **with fresh seeds** (vary `POM68K_JIT_LOCKSTEP_BUDGET` off 8192 and `POM68K_JIT_LOCKSTEP_FINE_AT` off 110000, plus a 260480 real-frame cadence run — the 6000-frame a64 gate exists precisely because the 8192 cadence once hid a native two-memory MOVE corruption), `POM68K_JIT_LOCKSTEP_ICTRACE=1` on one long run, and `Miss::GuardReplay` reported non-zero-and-bounded rather than growing without bound.

**Ceiling.** 2026-08-29 (late night) says mode 2 was **+3 %** over mode 1 — *one unpaired run*, and the entry itself refuses to move the default on it. After slice 1 removes the VRAM store misses, the residual is I/O stores, code-slice stores and page straddles: expect **1-2 %**. Re-price after slice 1; if it lands under the floor at both budgets, close the item as "measured, not worth the default" rather than leaving it open.

**Risks.** The re-run hazard the 2026-08-29 entry names is unchanged by this slice and must be repeated in the new entry verbatim: the guard door's documented contract re-runs a store whose first attempt already landed — idempotent for RAM stores of the same value, and an RMW re-run reads its own first attempt's value. That hazard predates mode 2 and the 120k lockstep has never met it, but a default change is when it becomes shipped behaviour.

**A/B protocol.** This one is *easy to do right*: the knob is `POM68K_JIT_ACCESS_THUNK` and it is explicit-wins (`JitEngine.cpp:99`), so both arms are **the same binary** — `POM68K_JIT_ACCESS_THUNK=1` against `=2`, `POM68K_BENCH_COMPARE=5`, ABBA, 2000 and 6000 frames, on x86-64, fingerprint identical in both arms. That is exactly the comparison TODO § B.3 asks for ("comparer les modes d'accès 1 et 2 dans le même binaire"), so this slice pays for itself twice.

---

## Slice 4 — fuse the 68030 `ird`/`irc` fetch, the way the 040 already does (rank 4, gated)

**Target.** `extern/moira/Moira/MoiraExecMMU_cpp.h:536-537` inside `mmuExecuteStart`, modelled on `:1927-1938` inside `mmu040InstrStart`.

**Mechanism.** Take the 040's shape verbatim. When (i) `mmuPipeCnt == 0`, (ii) `!(flags & State::CHECK_WP)` and (iii) `pomJitFetch(reg.pc, 4, p)` succeeds, serve both words from the window with **one** pipe test, **one** in-flight stamp (at `reg.pc + 2`, exactly what the second `mmuFetchWord` would have left — the 040 calls this `pomJitStampAccess(reg.pc + 2)`), and the i-cache overlay executed for the two words in order through a shared line probe. The shared probe is not new arithmetic: it is the "sequential walk with a one-line local override" already proved in `pomJitIcachePeekPenalty` (`Moira.h:757-777`) — an instruction's words are consecutive, so an earlier word's install can only be hit by a later word on the same line, and lines advance monotonically. Anything else falls back to the existing two `mmuFetchWord` calls, byte for byte.

**Invalidation / conformance contract.** The invariant is the three `PomIcache` counters plus the cache *content* (tags and valid bits), because `jit_lockstep_030_test` compares both (`:540-548`) and a counter agreement with parted content is exactly how the 2026-08-19 retained-cache divergence presented. The fused path must produce identical `fetches`, `hits`, `misses` and identical `tag[]`/`valid[]` for every `(pc, cacr, sr.s)`. The `clock += missPenalty` of the *first* word is charged before the second — legal only because, on the fused path, **no bus access happens between them**: the window serves both, `SYNC` expands to nothing on `Core::C68020` (`MoiraMacros.h`), nothing reads `clock` in between, and no `POLL_IPL` sits between the two fetches (`mmuExecuteStart`'s `POLL_IPL` is at `:519`, before both). If the window does not cover 4 bytes, one word may reach `read16` — which can clear the ROM overlay or call `flushTicks()` on a device — and that is precisely why the fused path is refused in that case. Vendor patch 32 is preserved by construction: the `mmuPipeCnt == 0` test is the first condition, so a fetch in a PMOVE's shadow keeps returning before the counters exactly as `pomMmuPipeLive()` promises the engine (`Moira.h:497-507`, `JitEngine.cpp:672-679`). Proof: `POM68K_JIT_LOCKSTEP_ICTRACE=1` over a full 120k run printing nothing (the trace only prints when a counter delta changes), plus `jit_lockstep_030_test` / `_blocks_test` / host-native gates with fresh seeds, plus `ctest -L m030`, plus identical `icache:` triples on `jit_bench_lcii` across all three engines. `POM68K_VENDOR.md` gains a row.

**Ceiling.** The saving is one call, one pipe test, one 4-store stamp and one line-array load per interpreted instruction — roughly 30 % of `mmuFetchWord`'s 6.1 % self time → **≈ 1.8 %** at today's interpreted-instruction rate. **Explicitly gated**: after slices 1 and 3 the interpreted-instruction count falls, and this may drop under the 1 % floor. Slice 0's `icache fetches / retired` ratio, re-measured after slices 1-3, is the go/no-go. Do not open it if that ratio has collapsed.

**Risks.** It touches the vendored fork on the one path whose counters are lockstep-verified, i.e. the highest-blast-radius file in the plan for the smallest of the four real gains. The 2026-08-29 (night) ±2 divergence — two fetches, two hits, zero misses, zero cycles, found only because ICTRACE gave it a step number — is the exact failure shape to expect if the shared line probe is wrong.

---

## Slice 5 — give the 68030 interpreter the data window (rank 5, opt-in first, expect to have to refuse it)

**Target.** `extern/moira/Moira/MoiraExecMMU_cpp.h:994` (`mmuRead`) and `:1101` (`mmuWrite`), consuming `Moira::pomJitData<N,W>` (`Moira.h:690`) and `pomJitDataSlow` (`Moira.cpp:920`), with the 030 refusal set written to match `pomJitProbeData`'s 030 branch (`:2105-2155`).

**Mechanism.** After the watchpoint hook, the `mmuLogging` entry bookkeeping and the `mmuRteSubst` check, and **only for naturally aligned Byte / Word / Long**, try `pomJitData<N,W>(addr)`; on a hit read/write the host bytes and finish the call's existing logging tail (`mmuAd[mmuIdxDone]`, `pomMmuBumpIdxDone`) unchanged. Everything else — unaligned and page-straddling forms, which are the only paths that touch `mmuState[1]` and `mmuDataBuffer` — keeps the long path verbatim. Additional 030-specific refusals on top of `pomJitData`'s own (`CHECK_WP`, page straddle): `fcSource != 0` (MOVES alternate space — the same rule `pomJitReadData:2329` already applies) and `mmuRmw` (a locked RMW read probes the ATC as a write, `mmuTranslateAccess:907`).

**Invalidation / conformance contract.** This is the slice that can move an access relative to an IPL poll, and therefore the one that is **forbidden without lockstep proof**. `mmuRead`'s `if (F & POLL) POLL_IPL;` sits at three *different* positions depending on size: before the access for Byte (`:1029`), after it for Word (`:1048`), and **between the two halves** of an aligned Long (`:1058`). The fast path must execute the same `POLL_IPL` at the same position for each size, or interrupt recognition moves. Likewise the in-flight stamp is deliberately *not* written on a hit — the same argument as the 040 (`:3011-3015`): a hit is plain guest memory behind a resident, permitted translation, no fault is possible, and the next slow access stamps its own before anything can read it. Exactness is inherited from J3b: `pomJitAtcEvict` (`Moira.h:375-401`) already kills the derived slices per page and per space on both 030 eviction sites (`mmuAtcLookup:795` and `:811`, `mmuAtcFill:836`), so a hit implies the interpreter's own ATC would have hit and no descriptor U-bit write is skipped. Ship it **behind `POM68K_DATA_WINDOW=1`** (`JitEngine.cpp:128-131`) exactly as the 040 side is, because that is also the only way to close `docs/JIT_BRINGUP.md` § C.2's standing complaint that the knob is a dead path on the 030 ("identical fingerprints and identical *zero* fills — a dead path, not a passing test"). Proof: `ctest -L m030` and all 030 lockstep gates run **twice**, knob off and knob on, with fresh seeds; ICTRACE for the fetch side; `POM68K_JIT_LOCKSTEP_FULL_RAM_AT` for the store side; identical fingerprints on interp / threaded / native at both bench budgets.

**Ceiling.** `mmuRead<2>+<4>` is 8.1 %, but after slices 1 and 3 the residual `mmuRead`/`mmuWrite` traffic is mostly genuine I/O (VIA/SCC/SCSI polls) that the DTLB **must** refuse. What is left is interpreted instructions' RAM operands — the tracer's first pass, `Kind::Unsafe` forms, `Miss::GuardReplay`, and the arm-backoff windows. Honest ceiling **2-4 %**, and the *most likely* outcome is a measured refusal.

**Risks and the standing negative precedent.** The interpreter data window was measured a **net loss** on the 68040 and made opt-in for that reason (2026-07-28 "The honest cost", `POM68K_VENDOR.md` § J3 point 11): once J3b capped it at ATC residency, the eviction/refill churn under Mac OS VM exceeded what the surviving hits saved (73 s versus 42 s). Two things weaken that precedent on the 030 without cancelling it — the 030 ATC has 22 entries against the 040's 32, but its page size on an LC II is ≥ 4 KB and is neither 4 nor 8 KB (`JitEngine.cpp:1170-1177`, `POM68K_VENDOR.md` row 31), so per-entry coverage is far larger; and the 030's `mmuRead` is also the **exact-thunk target from generated code**, where a hit replaces a full C++ chain rather than a hot MRU probe — which is the exact distinction the 040 entry drew when it kept the backend's inline TLB and dropped the interpreter's. That is an argument for measuring, not for expecting a win. The second measured trap applies unchanged: consulting the 256-entry table directly from the interpreter cost ~10 %, so the two-level shape (`pomJitDataR1/W1` first) is not optional.

**A/B protocol.** Same binary, `POM68K_DATA_WINDOW=0` vs `=1`, `POM68K_BENCH_COMPARE=5`, ABBA, 2000 and 6000 frames, plus `lcii_simcity_census` phase attribution, plus a third arm on the interpreter alone (`POM68K_CPU_ENGINE=interp`) because the window serves both engines and the 040 lesson is that the interpreter arm is where it lost.

---

## Ranking and sequencing

| rank | slice | ceiling (whole run) | confidence | effort | note |
|---|---|---|---|---|---|
| — | 0 · price the buckets | 0 % (enabler) | — | XS | gates 1, 4, 5; re-prices 2, 3 |
| 1 | V8 `dataSpan` publishes VRAM | ≈ 8 % | high | S | helps the shipping a64 default too |
| 2 | one decode / one guard note in `read16`/`write16` | ≈ 2 % (pre-1) | high | XS | zero architectural surface |
| 3 | x64 `maxAccessThunk030 = 2` | 3 % pre-1 → 1-2 % post | medium | S code / L proof | also closes TODO § B.3 bullet 1 |
| 4 | fused 030 `ird`/`irc` fetch | ≈ 1.8 %, at risk | medium | M | vendored fork, ICTRACE-gated |
| 5 | 030 interpreter data window | 2-4 % | low | L | standing negative 040 precedent |

Sequencing: **0 → 2 → 1 → 3 → (re-price) → 4 → 5**. Slice 2 goes before slice 1 only because the two compete for the same samples and slice 2's signal is largest while VRAM traffic still flows through `write16`; it costs nothing to reorder if the census says otherwise. One lever, one proof, one commit (`docs/MEASURING.md` § 4.2) — the 2026-08-18 confusion came from chaining three changes ahead of the first one's proof.

## Explicitly flagged: already tried, already refused, or below the floor

* **Re-tuning `POM68K_JIT_ARM_BACKOFF`** — refused. `docs/MEASURING.md` § 2: a single-run sweep found a tidy 2.9 % minimum at 2, ABBA reversed the sign, the default went back to 32. Do not re-open the *number*; a structural change (backing off only on permanent `ArmFail::NotRam`, not on transient `ArmFail::Probe`) is a different item and needs slice 0's arm-refusal breakdown first.
* **Lowering `POM68K_JIT_MIN_NATIVE`** — refused. `JIT_BRINGUP` § C.4ter: `MIN_NATIVE=0` is 37 % slower with 2.2× the residency; 50 upward is one flat plateau. Native residency is a symptom, not the objective function.
* **Removing the CACR/SMC flush on the V8** — already **done**, conformantly, via `V8Memory.h kJitStoreInventoryComplete` and `Cpu030::didChangeCACR`. The −21.8 % is banked. (Sonora still owes its audit — TODO § B.4, not B.2.)
* **Re-optimising the 030 ATC lookup** — done. Vendor patch 16: O(1) pseudo-LRU + last-hit probe + `MOIRA_HOT_INLINE`, when two 22-entry scans were 38 % of LC II machine time (`MoiraExecMMU_cpp.h:756`, `:787-797`).
* **Porting a64's `(An)+` probe-first order to x64** — dead on measurement, 2026-08-29 (seventh): `$24D0`'s 1.64 M replays are identical at mode 1 and mode 2 (1 644 266 vs 1 644 272); the thunk path is not in the story.
* **Skipping the `mmuLogging` bookkeeping inside `pomJitReadData`/`pomJitWriteData`** — below the floor. A handful of stores per access, on a path vendor patch 30 already had to make overflow-safe for exactly this reason. Not worth opening.
* **De-duplicating the RMW translation** — mostly already done where it mattered: `Emitter::memRmwLoad`/`memRmwStore` (`JitBackendX64.cpp:2033`/`:2054`) probe **once** with the writable translation and keep the host pointer across flag generation. The residue is the interpreter's read-then-write pair, two O(1) ATC last-hit probes since patch 16 — ceiling well under 1 %. Not worth opening.
* **Skipping the in-flight access stamp on window-served fetches** (`MoiraExecMMU_cpp.h:443`) — four stores, under the floor, and it would weaken the `extBusError()` replay contract for nothing. Not worth opening on its own; slice 4 folds the *duplicate* stamp away as a side effect, which is the only defensible version.
* **Anything that moves an access relative to an IPL poll or a bus-time boundary** — only slice 5 does this (the size-dependent `POLL_IPL` sites at `MoiraExecMMU_cpp.h:1029`/`:1048`/`:1058`), and it is written as forbidden without the 030 lockstep at two cadences with fresh seeds. Slices 1, 2 and 3 are chosen partly *because* they do not: the VRAM aperture is decoded before `flushTicks()`, the `read16` fast path is entirely inside one call, and the mode-2 store thunk keeps the access exactly where the IR already put it with the `pomJitBiasClock` bracket that `jit_lockstep_030_x64_alignment_test` pins.

## Two documentation defects found on the way (cheap, and they caused this)

* `src/V8Memory.h:131-134` still says *"the 030 has no data window today, but the engine's `fillDtlb` probes through it and simply never fills (`pomJitProbeData` is 040-only), so it stays cheap and honest."* `pomJitProbeData` grew its 030 branch on **2026-08-10** (`MoiraExecMMU_cpp.h:2105`). `V8Memory::dataSpan` was written **2026-07-28**, under the belief that comment records. The VRAM omission is an artefact of a stale premise, not a decision — which is the whole argument for slice 1.
* `src/jit/POM68K_JIT.md` § 8 describes the interpreter data window without saying it is unreachable on the 68030; `docs/JIT_BRINGUP.md` § C.2 does say so. One of them should point at the other.
