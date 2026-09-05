final rebuild rc=0 — make : on quitte le répertoire « /home/gistarcade/src/pom68k/.claude/worktrees/agent-af30bba2444f67915/build »
=== ctest -L asset-none, POM68K_DATA_WINDOW=0 ===
rc=8

The following tests FAILED:
	 40 - docs_test (Failed)
Errors while running CTest
=== ctest -L asset-none, POM68K_DATA_WINDOW=1 ===
rc=8

The following tests FAILED:
	 40 - docs_test (Failed)
Errors while running CTest
=== post-rebuild identity re-check (150 frames, all three engines) ===
interp     knob=0  fp=0685879ca2d506fd | icache: 57358133 fetches, 57350541 hits, 3425 misses (99.99% hit) | data window: 0 hits, 0 refusals  [POM68K_DATA_WINDOW off]
interp     knob=1  fp=0685879ca2d506fd | icache: 57358133 fetches, 57350541 hits, 3425 misses (99.99% hit) | data window: 18712149 hits, 3287857 refusals
interp     HEAD    fp=0685879ca2d506fd | icache: 57358133 fetches, 57350541 hits, 3425 misses (99.99% hit) |
threaded   knob=0  fp=0685879ca2d506fd | icache: 57358133 fetches, 57350541 hits, 3425 misses (99.99% hit) | data window: 0 hits, 0 refusals  [POM68K_DATA_WINDOW off]
threaded   knob=1  fp=0685879ca2d506fd | icache: 57358133 fetches, 57350541 hits, 3425 misses (99.99% hit) | data window: 18712149 hits, 3287857 refusals
threaded   HEAD    fp=0685879ca2d506fd | icache: 57358133 fetches, 57350541 hits, 3425 misses (99.99% hit) |
x64        knob=0  fp=0685879ca2d506fd | icache: 57358133 fetches, 57350541 hits, 3425 misses (99.99% hit) | data window: 0 hits, 0 refusals  [POM68K_DATA_WINDOW off]
x64        knob=1  fp=0685879ca2d506fd | icache: 57358133 fetches, 57350541 hits, 3425 misses (99.99% hit) | data window: 11938 hits, 3287858 refusals
x64        HEAD    fp=0685879ca2d506fd | icache: 57358133 fetches, 57350541 hits, 3425 misses (99.99% hit) |

=== docs_test repair (2026-09-05) ===
Both ctest legs above failed ONLY on `docs_test` (test #40, rc=8); every other
gate in the tier passed in both legs. Cause: `docs_test` § 7 recomputes four
headline numbers that `extern/moira/POM68K_VENDOR.md` asserts about itself,
and this slice moved three of them:

  distinct pom* identifiers   89 (57 pomJit*)  ->  92 (60 pomJit*)
  POM68K-marked lines         398              ->  406
  patch groups in inventory   32               ->  33
  source files with a marker  13 of 25         ->  unchanged (still passing)

Recomputed with the test's own algorithm and written into the table; the
"re-measured" date line moved to 2026-09-04 and names patch 33.

  ctest --test-dir <build> -R '^docs_test$' --output-on-failure
  rc=0 — 1/1 Test #40: docs_test .......... Passed

NOTE for integration: the slice-4 agent also added a `POM68K_VENDOR.md`
patch row 33. One of the two rows is renumbered to 34 at merge, and these
three numbers must be recomputed once more on the merged tree — the identifier
and marked-line counts are additive across both slices.
