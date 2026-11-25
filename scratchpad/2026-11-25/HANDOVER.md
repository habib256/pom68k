# Handover x86-64 → M4, 2026-11-25

State of `main` when this host stopped, and what to do next. Everything
below is committed and pushed; nothing lives only on the x86-64 host.

## Where the core stands

- `extern/moira/Moira` = Dirk Hoffmann's `master` @ `bf98107f` + 15 queue
  patches (`extern/moira/patches/`, `queue.tsv`, `moira_queue_test`).
- Dirk merged #46-#49 on 2026-11-25 (MOVE to SR latch refill, FBcc/FDBcc
  odd target, FRESTORE format check, 68040 FSAVE unimplemented frame).
  Nothing of ours is open upstream.
- The `MoiraCompat.h` triage is done: the fork's inert members and the five
  knobs (`POM68K_040_DCACHE`, `POM68K_040_CM_STATS`, `POM68K_ICACHE_MISS`,
  `POM68K_MMU040_WALK`, `NEOST_EXC_DIAG`) are gone (DEV.md § 5 Retired).
  The debugger translates through Dirk's `MMU::peek()`
  (`MoiraDebugSeam::debugTranslate()`).
- Save-state format v40 (the 68040 FSAVE frame as twelve longwords).
- JIT still compiled out (`POM68K_JIT=OFF`).

## Gates run on x86-64 (interpreter)

- Machine tier `m040|m030|etalon` (320 tests) on `6e7f5179` (before the
  triage and the rebase): 311 + the 9 timeouts rerun three at a time, all
  green. Tight budgets on the interpreter: `q605_dayna_driver_etalon`
  1 889 s / 2 400, `q605_cdinstall_etalon` 2 097 s / 3 600.
- After the triage and the rebase: see the commit message of the push
  (asset-none, SST, CPU vectors, save-state, boot/agent/beyond etalons).
- **Not run on the final tree: the full machine tier.** First job on the M4.

## Next, in order

1. `git pull` on the M4, configure and build (`-j4`, bounded), run
   `ctest -L asset-none`, then the full machine tier
   (`ctest -L 'm040|m030|etalon'`, one ctest per build dir, log to a file).
   This is the AArch64 leg of phase 1 (TODO § Objectif principal) and the
   stability check of Dirk's core with the 15-patch queue. A timeout is not
   a failure until it is rerun alone.
2. If the tier is green on both hosts: start phase 2, the JIT reintroduction,
   from the plan in `JIT_PLAN.md` (this folder) — the owner approves the plan
   before any code moves.
3. Open, no action needed now:
   - MC68030 DT=1 root limit with FCL: the manual contradicts itself
     (§ 9.7.1 vs Figure 9-28); core keeps Dirk's reading, cells listed [F]
     in `tests/known/moira_rules_test.txt`, TODO item holds the measurement.
     The fix, its unit test and a PR draft are in `mmu030_root_limit_fcl/`
     (apply with `git am` on a clone of Dirk's Moira). No PR without a
     measurement on a real 68030.
   - `fpsp_harness/`: the FPSP run behind PR #49 (needs the private ROM).
   - To read in MC68040UM before any action: when the host raises /BERR
     after translation, `readBus`/`writeBus` have already replaced `addr`
     by the physical address, so the 68040 frame's FA (queue patch 12, and
     upstream before it) is physical, not logical. Code fact from the
     2026-11-25 review of `docs/MOIRA_CONVERGENCE.md`; not ruled.
   - `moira_rules_test`'s 48 `ptestLongDescriptorLimits` cells fail on this
     core because their PTEST extension word has FC field %00101, which
     Dirk refuses as illegal (`MoiraExecMMU_cpp.h:136`): the fixture, not
     the limits, needs fixing before those cells test anything.
   - Possible candidate (read MC68881UM 6.1 first): a 6888x FMOVE out to
     memory reports its exception mid-instruction (format $9); Dirk defers
     it to a pre-instruction format $0 frame.

## Rules the owner set on 2026-11-25

- No reference to Claude, Claude Code or Anthropic in commits (no
  `Co-Authored-By` trailer) or in anything public (PRs, comments).
- Nothing goes to Dirk (PR, comment, discussion #36) unless it is sure and
  the owner has approved the exact text; an uncertain fix is not proposed.
